static const float PI = 3.1415926;
static const float3 Fdielectric = 0.04;

static const uint NumLights = 1;

cbuffer TransformCB : register(b0)
{
    float4x4 viewProj;
    float4x4 skyboxProj;
};

cbuffer ShadingCB : register(b0)
{
    struct
    {
        float3 position;
        float3 radiance;
    } lights[NumLights];
    
    float3 cameraPos;
};

struct VertexInput
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float2 texcoord  : TEXCOORD;
};

struct VertexOutput
{
    float3 posWorld : POSITION;
    float4 posClip  : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float3x3 tangentBasis : TBASIS;
};

TextureCube irradianceTexture : register(t0);
TextureCube prefilterTexture  : register(t1);
Texture2D   brdfTexture       : register(t2);
Texture2D   albedoTexture     : register(t3);
Texture2D   normalTexture     : register(t4);
Texture2D   metalnessTexture  : register(t5);
Texture2D   roughnessTexture  : register(t6);

SamplerState defaultSampler : register(s0);
SamplerState brdfSampler    : register(s1);

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

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

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

uint MaxTextureLevels()
{
    uint width, height, levels;
    prefilterTexture.GetDimensions(0, width, height, levels);
    return levels;
}

VertexOutput main_vs(VertexInput vin)
{
    VertexOutput output;
    output.posWorld = vin.position;
    output.posClip = mul(viewProj, float4(vin.position, 1.0));
    output.texcoord = float2(vin.texcoord.x, 1.0 - vin.texcoord.y);
    output.tangentBasis = float3x3(vin.tangent, vin.bitangent, vin.normal);
    return output;
}

float4 main_ps(VertexOutput pin) : SV_Target
{
    float3 albedo = albedoTexture.Sample(defaultSampler, pin.texcoord).rgb;
    float metalness = metalnessTexture.Sample(defaultSampler, pin.texcoord).r;
    float roughness = roughnessTexture.Sample(defaultSampler, pin.texcoord).r;
    
    float3 N = normalize(mul(2.0 * normalTexture.Sample(defaultSampler, pin.texcoord).rgb - 1.0, pin.tangentBasis));
    float3 V = normalize(cameraPos - pin.posWorld);
    float3 R = reflect(-V, N);
    
    float3 F0 = lerp(Fdielectric, albedo, metalness);
    
    float3 Lo = 0.0;
    for (uint i = 0; i < NumLights; i++)
    {
        float3 L = normalize(lights[i].position - pin.posWorld);
        float3 H = normalize(V + L);
        
        float distance = length(L);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = lights[i].radiance * attenuation;

        float  D = DistributionGGX(N, H, roughness);
        float  G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator = D * G * F;
        float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;

        float3 kD = lerp(float3(1.0, 1.0, 1.0) - F, float3(0.0, 0.0, 0.0), metalness);

        float NdotL = max(dot(N, L), 0.0);

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    float3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    float3 kD = lerp(float3(1.0, 1.0, 1.0) - F, float3(0.0, 0.0, 0.0), metalness);

    float3 irradiance = irradianceTexture.Sample(defaultSampler, N).rgb;
    float3 diffuse = irradiance * albedo;

    float3 prefilteredColor = prefilterTexture.SampleLevel(defaultSampler, R, roughness * MaxTextureLevels()).rgb;
    float2 brdf = brdfTexture.Sample(brdfSampler, float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    float3 ambient = kD * diffuse + specular;

    return float4(ambient + Lo, 1.0);
}