static const float PI = 3.1415926;
static const float TwoPI = 2 * PI;
static const uint NumSamples = 1024;

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);

SamplerState defaultSampler : register(s0);

cbuffer RootConstants : register(b0)
{
    float roughness;
};

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint NumSamples)
{
    return float2(float(i) / float(NumSamples), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 uv, float roughness)
{
    float alpha = roughness * roughness;

    float cosTheta = sqrt((1.0 - uv.y) / (1.0 + (alpha * alpha - 1.0) * uv.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = TwoPI * uv.x;

    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float NdfGGX(float NdotH, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;

    float denom = (NdotH * NdotH) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * denom * denom);
}

float3 GetSamplingVector(uint3 threadID)
{
    uint width, height, depth;
    outputTexture.GetDimensions(width, height, depth);

    float2 st = threadID.xy / float2(width, height);
    float2 uv = float2(st.x, 1 - st.y) * 2.0 - 1.0;
    
    float3 dir;
    switch (threadID.z)
    {
        case 0: dir = float3( 1.0,   uv.y, -uv.x); break;  // +X
        case 1: dir = float3(-1.0,   uv.y,  uv.x); break;  // -X
        case 2: dir = float3( uv.x,  1.0,  -uv.y); break;  // +Y
        case 3: dir = float3( uv.x, -1.0,   uv.y); break;  // -Y
        case 4: dir = float3( uv.x,  uv.y,   1.0); break;  // +Z
        case 5: dir = float3(-uv.x,  uv.y,  -1.0); break;  // -Z
    }
    return normalize(dir);
}

float3x3 GetTBN(float3 N)
{
    float3 P = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    
    float3 T = normalize(cross(P, N));
    float3 B = normalize(cross(N, T));
    
    return float3x3(T, B, N);
}

[numthreads(32, 32, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint width, height, depth;
    outputTexture.GetDimensions(width, height, depth);
    if (threadID.x >= width || threadID.y >= height)
        return;
    
    inputTexture.GetDimensions(0, width, height, depth);
    float wt = 4.0 * PI / (6 * width * height);
    
    float3 N = GetSamplingVector(threadID);
    float3 V = N;
    float3x3 TBN = GetTBN(N);
    
    float3 color = 0.0;
    float weight = 0.0;
    
    for (uint i = 0; i < NumSamples; i++)
    {
        float2 uv = Hammersley(i, NumSamples);
        float3 H = mul(ImportanceSampleGGX(uv, roughness), TBN);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            float NdotH = max(dot(N, H), 0.0);
            float pdf = NdfGGX(NdotH, roughness) * 0.25;

            float ws = 1.0 / (NumSamples * pdf);
            float mipLevel = max(0.5 * log2(ws / wt) + 1.0, 0.0);

            color += inputTexture.SampleLevel(defaultSampler, L, mipLevel).rgb * NdotL;
            weight += NdotL;
        }
    }
    color /= weight;
    outputTexture[threadID] = float4(color, 1.0);
}