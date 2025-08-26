static const float PI = 3.1415926;

Texture2D equirectTexture : register(t0);
RWTexture2DArray<float4> cubeTexture : register(u0);

SamplerState defaultSampler : register(s0);

float3 getSamplingVector(uint3 threadID)
{
    uint width, height, depth;
    cubeTexture.GetDimensions(width, height, depth);

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

[numthreads(32, 32, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    float3 N = getSamplingVector(threadID);
    
    float2 texCoord = float2(atan2(N.z, N.x) / (2 * PI), acos(N.y) / PI);
    
    float4 color = equirectTexture.SampleLevel(defaultSampler, texCoord, 0);
    cubeTexture[threadID] = color;
}