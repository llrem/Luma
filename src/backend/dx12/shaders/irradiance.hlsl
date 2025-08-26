static const float PI = 3.1415926;
static const float TwoPI = 2 * PI;
static const uint NumSamples = 64 * 1024;

TextureCube envTexture : register(t0);
RWTexture2DArray<float4> irradianceTexture : register(u0);

SamplerState defaultSampler : register(s0);


float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 SampleHemisphere(float2 uv)
{
    float p = sqrt(max(0.0, 1.0 - uv.x * uv.x));
    return float3(cos(TwoPI * uv.y) * p, sin(TwoPI * uv.y) * p, uv.x);
}

float3 GetSamplingVector(uint3 threadID)
{
    uint width, height, depth;
    irradianceTexture.GetDimensions(width, height, depth);

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
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    return float3x3(T, B, N);
}

[numthreads(32, 32, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    float3 N = GetSamplingVector(threadID);
    float3x3 TBN = GetTBN(N);

    float3 irradiance = 0.0;
    for (uint i = 0; i < NumSamples; i++)
    {
        float2 uv = Hammersley(i, NumSamples);        
        float3 L = mul(SampleHemisphere(uv), TBN);
        
        float NdotL = max(0.0, dot(N, L));
        irradiance += 2.0 * envTexture.SampleLevel(defaultSampler, L, 0).rgb * NdotL;
    }
    irradiance /= float(NumSamples);
    
    irradianceTexture[threadID] = float4(irradiance, 1.0);
}