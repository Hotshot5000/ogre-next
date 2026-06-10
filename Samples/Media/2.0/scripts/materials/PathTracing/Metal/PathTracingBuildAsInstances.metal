#include <metal_stdlib>
using namespace metal;

struct PathTracerAsInstanceInput
{
    uint accelerationStructureIndex;
    uint padding0;
    uint padding1;
    uint padding2;
    float4 transformRow0;
    float4 transformRow1;
    float4 transformRow2;
    float4 transformRow3;
};

struct PathTracerPackedFloat4x3
{
    packed_float3 column0;
    packed_float3 column1;
    packed_float3 column2;
    packed_float3 column3;
};

struct PathTracerIndirectInstanceDescriptor
{
    PathTracerPackedFloat4x3 transformationMatrix;
    uint options;
    uint mask;
    uint intersectionFunctionTableOffset;
    uint userID;
    ulong accelerationStructureID;
};

kernel void pathtracer_write_indirect_as_instances(
    constant PathTracerAsInstanceInput *inputs [[buffer(0)]],
    constant ulong *accelerationStructureIds [[buffer(1)]],
    device PathTracerIndirectInstanceDescriptor *descriptors [[buffer(2)]],
    device atomic_uint *instanceCount [[buffer(3)]],
    constant uint &numInstances [[buffer(4)]],
    uint tid [[thread_position_in_grid]] )
{
    if( tid == 0 )
        atomic_store_explicit( instanceCount, numInstances, memory_order_relaxed );

    if( tid >= numInstances )
        return;

    constant PathTracerAsInstanceInput &src = inputs[tid];
    device PathTracerIndirectInstanceDescriptor &dst = descriptors[tid];

    dst.accelerationStructureID = accelerationStructureIds[src.accelerationStructureIndex];
    dst.userID = tid;
    dst.options = 4u; // MTLAccelerationStructureInstanceOptionOpaque
    dst.mask = 1u;
    dst.intersectionFunctionTableOffset = 0u;

    dst.transformationMatrix.column0 = packed_float3( src.transformRow0.x,
                                                      src.transformRow1.x,
                                                      src.transformRow2.x );
    dst.transformationMatrix.column1 = packed_float3( src.transformRow0.y,
                                                      src.transformRow1.y,
                                                      src.transformRow2.y );
    dst.transformationMatrix.column2 = packed_float3( src.transformRow0.z,
                                                      src.transformRow1.z,
                                                      src.transformRow2.z );
    dst.transformationMatrix.column3 = packed_float3( src.transformRow0.w,
                                                      src.transformRow1.w,
                                                      src.transformRow2.w );
}
