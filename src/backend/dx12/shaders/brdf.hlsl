static const float PI = 3.1415926;
static const float TwoPI = 2 * PI;
static const uint NumSamples = 1024;

RWTexture2D<float2> LUT : register(u0);

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

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

[numthreads(32, 32, 1)]
void main(uint2 threadID : SV_DispatchThreadID)
{
    float width, height;
    LUT.GetDimensions(width, height);
    
    float NdotV = threadID.x / width;
    float roughness = threadID.y / height;
    
    NdotV = max(NdotV, 0.00001);
    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float3 N = float3(0.0, 0.0, 1.0);
    
    float A = 0.0;
    float B = 0.0;
    
    for (uint i = 0; i < NumSamples; i++)
    {
        float2 uv = Hammersley(i, NumSamples);
        float3 H = ImportanceSampleGGX(uv, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    LUT[threadID] = float2(A, B) / NumSamples;
}