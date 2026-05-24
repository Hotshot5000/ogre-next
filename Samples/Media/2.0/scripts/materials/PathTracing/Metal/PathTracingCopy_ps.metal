#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
    float2 uv0;
};

fragment float4 main_metal
(
    PS_INPUT inPs [[stage_in]],
    texture2d<float> sceneTexture [[texture(0)]],
    sampler samplerState0 [[sampler(0)]]
)
{
    return sceneTexture.sample( samplerState0, inPs.uv0 );
}
