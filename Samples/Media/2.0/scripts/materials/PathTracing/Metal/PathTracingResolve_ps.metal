#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
    float2 uv0;
};

static float3 tonemapReinhard( float3 color )
{
    color *= 0.125f;
    color = color / ( color + float3( 1.0f ) );
    return saturate( color );
}

fragment float4 main_metal
(
    PS_INPUT inPs [[stage_in]],
    texture2d<float> radianceTexture [[texture(0)]],
    sampler samplerState0 [[sampler(0)]]
)
{
    float3 color = max( radianceTexture.sample( samplerState0, inPs.uv0 ).xyz, float3( 0.0f ) );
    color = tonemapReinhard( color );
    color = pow( color, float3( 1.0f / 2.2f ) );
    return float4( color, 1.0f );
}
