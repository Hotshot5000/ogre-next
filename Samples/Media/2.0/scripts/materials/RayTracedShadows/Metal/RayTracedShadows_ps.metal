#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
	float2 uv0;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	shadowTexture	[[texture(0)]],
	texture2d<float>	renderedScene	[[texture(1)]],
	sampler				samplerState0	[[sampler(0)]],
	sampler				samplerState1	[[sampler(1)]]
)
{
    float4 finalColor;
    float4 shadow = shadowTexture.sample( samplerState0, inPs.uv0 );
    if( shadow.y == 0.0 )
        finalColor = renderedScene.sample( samplerState1, inPs.uv0 );
    else
        finalColor = float4( 0, 0, 0, 1 );

	return finalColor;
}
