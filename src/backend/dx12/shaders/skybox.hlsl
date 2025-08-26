TextureCube envTexture : register(t0);
SamplerState defaultSampler : register(s0);

cbuffer TransformCB : register(b0)
{
    float4x4 viewPorj;
    float4x4 skyboxProj;
};

struct VertexOutput
{
    float3 posWorld : POSITIONT;
    float4 posClip : SV_POSITION;
};

VertexOutput main_vs(float3 position : POSITION)
{
    VertexOutput output;
    output.posWorld = position;
    output.posClip = mul(skyboxProj, float4(position, 1.0));
    return output;
}

float4 main_ps(VertexOutput pin) : SV_Target
{
    float3 sampleVector = normalize(pin.posWorld);
    return envTexture.SampleLevel(defaultSampler, sampleVector, 0);
}