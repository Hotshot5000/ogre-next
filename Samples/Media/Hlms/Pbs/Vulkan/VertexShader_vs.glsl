@insertpiece( SetCrossPlatformSettings )
@insertpiece( SetCompatibilityLayer )

out gl_PerVertex
{
	vec4 gl_Position;
@property( hlms_global_clip_planes )
	float gl_ClipDistance[@value(hlms_global_clip_planes)];
@end
};

layout(std140) uniform;

@insertpiece( DefaultHeaderVS )
@insertpiece( custom_vs_uniformDeclaration )

layout( location = 0 )in vec4 vertex;

@property( hlms_normal )layout(location = 1)in vec3 normal;@end
@property( hlms_qtangent )layout(location = 1)in vec4 qtangent;@end

@property( normal_map && !hlms_qtangent )
	in vec3 tangent;
	@property( hlms_binormal )in vec3 binormal;@end
@end

@property( hlms_skeleton )
	in uvec4 blendIndices;
	in vec4 blendWeights;
@end

@foreach( hlms_uv_count, n )
	layout(location = 7)in vec@value( hlms_uv_count@n ) uv@n;@end

@property( GL_ARB_base_instance )
	layout( location = 15 )in uint drawId;
@end

@insertpiece( custom_vs_attributes )

@property( !hlms_shadowcaster || !hlms_shadow_uses_depth_texture || alpha_test || exponential_shadow_maps )
	layout(location = 0)out block
	{
		@insertpiece( VStoPS_block )
	} outVs;
@end

// START UNIFORM GL DECLARATION
layout(binding = OGRE_VULKAN_TEX_SLOT_START+0) uniform samplerBuffer worldMatBuf;
@property( !GL_ARB_base_instance )uniform uint baseInstance;@end
// END UNIFORM GL DECLARATION

void main()
{
@property( !GL_ARB_base_instance )
    uint drawId = baseInstance + uint( gl_InstanceID );
@end
    @insertpiece( custom_vs_preExecution )
	@insertpiece( DefaultBodyVS )
	@insertpiece( custom_vs_posExecution )
}