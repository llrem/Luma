static const float gamma = 2.2;
static const float exposure = 1.0;
static const float pureWhite = 1.0;

Texture2D inputTexture : register(t0);
SamplerState defaultSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

struct Vertex
{
    float3 xyz;
    float2 uv;
};

static const Vertex vertices[6] =
{
    { float3(-1.0,  1.0,  0.0), float2(0.0, 0.0) },
    { float3(-1.0, -1.0,  0.0), float2(0.0, 1.0) },
    { float3( 1.0,  1.0,  0.0), float2(1.0, 0.0) },
    { float3( 1.0,  1.0,  0.0), float2(1.0, 0.0) },
    { float3(-1.0, -1.0,  0.0), float2(0.0, 1.0) },
    { float3( 1.0, -1.0,  0.0), float2(1.0, 1.0) }
};
    
VertexOutput main_vs(uint VertexID : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(vertices[VertexID].xyz, 1.0);
    output.texcoord = vertices[VertexID].uv;
    return output;
}

float4 main_ps(VertexOutput pin) : SV_Target
{
    float3 color = inputTexture.Sample(defaultSampler, pin.texcoord).rgb * exposure;
    
    // Reinhard tonemapping
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);
    
    float3 mappedColor = (mappedLuminance / luminance) * color;
    
    // gamma correction
    return float4(pow(mappedColor, 1.0 / gamma), 1.0);
}