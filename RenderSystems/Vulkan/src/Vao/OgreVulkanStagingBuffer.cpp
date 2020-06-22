/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "Vao/OgreVulkanStagingBuffer.h"

#include "Vao/OgreVulkanBufferInterface.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreStringConverter.h"

#include "OgreVulkanHardwareBufferCommon.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanQueue.h"
#include "OgreVulkanUtils.h"

namespace Ogre
{
    VulkanStagingBuffer::VulkanStagingBuffer( size_t internalBufferStart, size_t sizeBytes,
                                              VaoManager *vaoManager, bool uploadOnly,
                                              VkDeviceMemory deviceMemory, VkBuffer vboName,
                                              VulkanDynamicBuffer *dynamicBuffer ) :
        StagingBuffer( internalBufferStart, sizeBytes, vaoManager, uploadOnly ),
        mMappedPtr( 0 ),
        mVulkanDataPtr( 0 ),
        mDeviceMemory( deviceMemory ),
        mVboName( vboName ),
        mFenceThreshold( sizeBytes / 4 ),
        mUnfencedBytes( 0 )
        // mBufferInterface(buffer_interface)
    {
        // mVulkanDataPtr =
        //     reinterpret_cast<uint8 *>( OGRE_MALLOC_SIMD( sizeBytes, MEMCATEGORY_RENDERSYS ) );
    }
    //-----------------------------------------------------------------------------------
    VulkanStagingBuffer::~VulkanStagingBuffer()
    {
        // OGRE_FREE_SIMD( mVulkanDataPtr, MEMCATEGORY_RENDERSYS );
        // mVulkanDataPtr = 0;
        
        if( !mFences.empty() )
            wait( mFences.back().fenceName );

        deleteFences( mFences.begin(), mFences.end() );
    }

    void VulkanStagingBuffer::addFence( size_t from, size_t to, bool forceFence )
    {
        assert( to <= mSizeBytes );

        VulkanFence unfencedHazard( from, to );

        mUnfencedHazards.push_back( unfencedHazard );

        assert( from <= to );

        mUnfencedBytes += to - from;

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        if( mUnfencedBytes >= mFenceThreshold || forceFence )
        {
            VulkanFenceVec::const_iterator itor = mUnfencedHazards.begin();
            VulkanFenceVec::const_iterator end = mUnfencedHazards.end();

            size_t startRange = itor->start;
            size_t endRange = itor->end;

            while( itor != end )
            {
                if( endRange <= itor->end )
                {
                    // Keep growing (merging) the fences into one fence
                    endRange = itor->end;
                }
                else
                {
                    // We wrapped back to 0. Can't keep merging. Make a fence.
                    VulkanFence fence( startRange, endRange );
                    VkFenceCreateInfo fenceCi;
                    makeVkStruct( fenceCi, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO );
                    VkResult result = vkCreateFence( device->mDevice, &fenceCi, 0, &fence.fenceName );
                    checkVkResult( result, "VulkanStagingBuffer::addFence" );

                    // __block dispatch_semaphore_t blockSemaphore = fence.fenceName;
                    // [mDevice->mCurrentCommandBuffer
                    //     addCompletedHandler:^( id<MTLCommandBuffer> buffer ) {
                    //       dispatch_semaphore_signal( blockSemaphore );
                    //     }];
                    mFences.push_back( fence );

                    startRange = itor->start;
                    endRange = itor->end;
                }

                ++itor;
            }

            // Make the last fence.
            VulkanFence fence( startRange, endRange );
            VkFenceCreateInfo fenceCi;
            makeVkStruct( fenceCi, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO );
            VkResult result = vkCreateFence( device->mDevice, &fenceCi, 0, &fence.fenceName );
            checkVkResult( result, "VulkanStagingBuffer::addFence" );
            // __block dispatch_semaphore_t blockSemaphore = fence.fenceName;
            // [mDevice->mCurrentCommandBuffer addCompletedHandler:^( id<MTLCommandBuffer> buffer ) {
            //   dispatch_semaphore_signal( blockSemaphore );
            // }];

            // Flush the device for accuracy in the fences.
            device->commitAndNextCommandBuffer( false );
            mFences.push_back( fence );

            mUnfencedHazards.clear();
            mUnfencedBytes = 0;
        }
    }

    void VulkanStagingBuffer::deleteFences( VulkanFenceVec::iterator itor, VulkanFenceVec::iterator end )
    {
        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();
        while( itor != end )
        {
            vkDestroyFence( device->mDevice, itor->fenceName, 0 );
            itor->fenceName = 0;
            ++itor;
        }
    }

    void VulkanStagingBuffer::wait( VkFence syncObj )
    {
        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkResult result = vkWaitForFences( device->mDevice, 1, &syncObj, true,
                                           UINT64_MAX );  // You can't wait forever in Vulkan?!?
        checkVkResult( result, "VulkanStagingBuffer::wait" );
    }

    void VulkanStagingBuffer::waitIfNeeded()
    {
        assert( mUploadOnly );

        size_t mappingStart = mMappingStart;
        size_t sizeBytes = mMappingCount;

        if( mappingStart + sizeBytes > mSizeBytes )
        {
            if( !mUnfencedHazards.empty() )
            {
                // mUnfencedHazards will be cleared in addFence
                addFence( mUnfencedHazards.front().start, mSizeBytes - 1, true );
            }

            // Wraps around the ring buffer. Sadly we can't do advanced virtual memory
            // manipulation to keep the virtual space contiguous, so we'll have to reset to 0
            mappingStart = 0;
        }

        VulkanFence regionToMap( mappingStart, mappingStart + sizeBytes );

        VulkanFenceVec::iterator itor = mFences.begin();
        VulkanFenceVec::iterator end = mFences.end();

        VulkanFenceVec::iterator lastWaitableFence = end;

        while( itor != end )
        {
            if( regionToMap.overlaps( *itor ) )
                lastWaitableFence = itor;

            ++itor;
        }

        if( lastWaitableFence != end )
        {
            wait( lastWaitableFence->fenceName );
            deleteFences( mFences.begin(), lastWaitableFence + 1 );
            mFences.erase( mFences.begin(), lastWaitableFence + 1 );
        }

        mMappingStart = mappingStart;
    }

    //-----------------------------------------------------------------------------------
    void *VulkanStagingBuffer::mapImpl( size_t sizeBytes )
    {
        assert( mUploadOnly );

        mMappingStart = 0;
        

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        mMappingCount = alignMemory( sizeBytes, device->mDeviceProperties.limits.nonCoherentAtomSize );

        VkResult result = vkMapMemory( device->mDevice, mDeviceMemory, mInternalBufferStart + mMappingStart,
                         mMappingCount, 0, &mMappedPtr );
        checkVkResult( result, "vkMapMemory" );

        VkMappedMemoryRange memRange;
        makeVkStruct( memRange, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE );
        memRange.memory = mDeviceMemory;
        memRange.offset = mInternalBufferStart + mMappingStart;
        memRange.size = mMappingCount;
        result = vkInvalidateMappedMemoryRanges( device->mDevice, 1, &memRange );
        checkVkResult( result, "vkInvalidateMappedMemoryRanges" );

        // mMappedPtr =
        //     mBufferInterface->map( mInternalBufferStart + mMappingStart, sizeBytes, MappingState::MS_UNMAPPED );

        //mMappedPtr = mVulkanDataPtr + mInternalBufferStart + mMappingStart;

        return mMappedPtr;
    }
    //-----------------------------------------------------------------------------------
    void VulkanStagingBuffer::unmapImpl( const Destination *destinations, size_t numDestinations )
    {
        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager*>(mVaoManager);
        VulkanDevice *device = vaoManager->getDevice();

        VkCommandBuffer cmdBuffer = device->mGraphicsQueue.mCurrentCmdBuffer;

        //vkBindBufferMemory( device->mDevice, mVboName, mDeviceMemory, 0 );

        VkMappedMemoryRange memRange;
        makeVkStruct( memRange, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE );
        memRange.memory = mDeviceMemory;
        memRange.offset = mInternalBufferStart + mMappingStart;
        memRange.size = mMappingCount;
        VkResult result = vkFlushMappedMemoryRanges( device->mDevice, 1, &memRange );
        checkVkResult( result, "VkMappedMemoryRange" );

        vkUnmapMemory( device->mDevice, mDeviceMemory );

        VkBufferMemoryBarrier buffer_memory_barrier;
        makeVkStruct( buffer_memory_barrier, VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER );
        buffer_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        buffer_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        buffer_memory_barrier.buffer = mVboName;
        buffer_memory_barrier.offset = mInternalBufferStart + mMappingStart;
        buffer_memory_barrier.size = mMappingCount;
        buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT;
        VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

        // vkCmdPipelineBarrier( device->mGraphicsQueue.mCurrentCmdBuffer, src_stage_mask, dst_stage_mask,
        //                       0, 0, nullptr, 1,
        //                       &buffer_memory_barrier, 0, nullptr );

        mMappedPtr = 0;

        for( size_t i = 0; i < numDestinations; ++i )
        {
            const Destination &dst = destinations[i];

            VulkanBufferInterface *bufferInterface =
                static_cast<VulkanBufferInterface *>( dst.destination->getBufferInterface() );

            assert( dst.destination->getBufferType() == BT_DEFAULT );

            size_t dstOffset = dst.dstOffset + dst.destination->_getInternalBufferStart() *
                                                   dst.destination->getBytesPerElement();

            VkBufferCopy region;
            region.srcOffset = mInternalBufferStart + mMappingStart + dst.srcOffset;
            region.dstOffset = dstOffset;
            region.size = dst.length;
            vkCmdCopyBuffer( device->mGraphicsQueue.mCurrentCmdBuffer, mVboName,
                             bufferInterface->getVboName(), 1u, &region );

            buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            buffer_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;


            src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

            buffer_memory_barrier.buffer = bufferInterface->getVboName();

            vkCmdPipelineBarrier( device->mGraphicsQueue.mCurrentCmdBuffer, src_stage_mask,
                                  dst_stage_mask, 0, 0, nullptr, 1, &buffer_memory_barrier, 0, nullptr );

            // device->mGraphicsQueue.commitAndNextCommandBuffer( false );

            // uint8 *dstPtr = bufferInterface->getVulkanDataPtr();

            // mBufferInterface->copyTo( bufferInterface, dstOffset,
            //                           mInternalBufferStart + mMappingStart + dst.srcOffset, dst.length );

            // memcpy( dstPtr + dstOffset,
            //         mVulkanDataPtr + mInternalBufferStart + mMappingStart + dst.srcOffset, dst.length );
        }
    }
    //-----------------------------------------------------------------------------------
    StagingStallType VulkanStagingBuffer::uploadWillStall( size_t sizeBytes )
    {
        assert( mUploadOnly );
        return STALL_NONE;
    }
    //-----------------------------------------------------------------------------------
    //
    //  DOWNLOADS
    //
    //-----------------------------------------------------------------------------------
    size_t VulkanStagingBuffer::_asyncDownload( BufferPacked *source, size_t srcOffset,
                                                size_t srcLength )
    {
        size_t freeRegionOffset = getFreeDownloadRegion( srcLength );
        size_t errorCode = -1;  // dodge comile error about signed/unsigned compare

        if( freeRegionOffset == errorCode )
        {
            OGRE_EXCEPT(
                Exception::ERR_INVALIDPARAMS,
                "Cannot download the request amount of " + StringConverter::toString( srcLength ) +
                    " bytes to this staging buffer. "
                    "Try another one (we're full of requests that haven't been read by CPU yet)",
                "VulkanStagingBuffer::_asyncDownload" );
        }

        assert( !mUploadOnly );
        assert( dynamic_cast<VulkanBufferInterface *>( source->getBufferInterface() ) );
        assert( ( srcOffset + srcLength ) <= source->getTotalSizeBytes() );

        VulkanBufferInterface *bufferInterface =
            static_cast<VulkanBufferInterface *>( source->getBufferInterface() );

        // uint8 *srcPtr = bufferInterface->getVulkanDataPtr();

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkBufferCopy region;
        region.srcOffset = source->_getFinalBufferStart() * source->getBytesPerElement() + srcOffset;
        region.dstOffset = mInternalBufferStart + freeRegionOffset;
        region.size = srcLength;
        vkCmdCopyBuffer( device->mGraphicsQueue.mCurrentCmdBuffer, mVboName,
                         bufferInterface->getVboName(), 1u, &region );

        // bufferInterface->copyTo( mBufferInterface, mInternalBufferStart + freeRegionOffset,
        //     source->_getFinalBufferStart() * source->getBytesPerElement() + srcOffset,
        //     srcLength );


        // memcpy( mVulkanDataPtr + mInternalBufferStart + freeRegionOffset,
        //         srcPtr + source->_getFinalBufferStart() * source->getBytesPerElement() + srcOffset,
        //         srcLength );

        return freeRegionOffset;
    }

    void VulkanStagingBuffer::_unmapToV1( v1::VulkanHardwareBufferCommon *hwBuffer, size_t lockStart,
        size_t lockSize )
    {
        assert( mUploadOnly );

        if( mMappingState != MS_MAPPED )
        {
            // This stuff would normally be done in StagingBuffer::unmap
            OGRE_EXCEPT( Exception::ERR_INVALID_STATE, "Unmapping an unmapped buffer!",
                         "VulkanStagingBuffer::unmap" );
        }

        mMappedPtr = 0;

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkBufferCopy region;
        region.srcOffset = mInternalBufferStart + mMappingStart;
        region.dstOffset = lockStart;
        region.size = alignToNextMultiple( lockSize, 4u );
        vkCmdCopyBuffer( device->mGraphicsQueue.mCurrentCmdBuffer, mVboName,
                         hwBuffer->getBufferNameForGpuWrite(), 1u, &region );

        // __unsafe_unretained id<MTLBlitCommandEncoder> blitEncoder = mDevice->getBlitEncoder();
        // [blitEncoder copyFromBuffer:mVboName
        //                sourceOffset:mInternalBufferStart + mMappingStart
        //                    toBuffer:hwBuffer->getBufferNameForGpuWrite()
        //           destinationOffset:lockStart
        //                        size:alignToNextMultiple( lockSize, 4u )];

        if( mUploadOnly )
        {
            // Add fence to this region (or at least, track the hazard).
            // addFence( mMappingStart, mMappingStart + mMappingCount - 1, false );
        }

        // This stuff would normally be done in StagingBuffer::unmap
        mMappingState = MS_UNMAPPED;
        mMappingStart += mMappingCount;

        if( mMappingStart >= mSizeBytes )
            mMappingStart = 0;
    }

    unsigned long long VulkanStagingBuffer::_asyncDownloadV1( v1::VulkanHardwareBufferCommon *source,
        size_t srcOffset, size_t srcLength )
    {
        // Vulkan has alignment restrictions of 4 bytes for offset and size in copyFromBuffer
        size_t freeRegionOffset = getFreeDownloadRegion( srcLength );

        if( freeRegionOffset == ( size_t )( -1 ) )
        {
            OGRE_EXCEPT(
                Exception::ERR_INVALIDPARAMS,
                "Cannot download the request amount of " + StringConverter::toString( srcLength ) +
                    " bytes to this staging buffer. "
                    "Try another one (we're full of requests that haven't been read by CPU yet)",
                "VulkanStagingBuffer::_asyncDownload" );
        }

        assert( !mUploadOnly );
        assert( ( srcOffset + srcLength ) <= source->getSizeBytes() );

        size_t extraOffset = 0;
        if( srcOffset & 0x03 )
        {
            // Not multiple of 4. Backtrack to make it multiple of 4, then add this value
            // to the return value so it gets correctly mapped in _mapForRead.
            extraOffset = srcOffset & 0x03;
            srcOffset -= extraOffset;
        }

        size_t srcOffsetStart = 0;

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkBuffer srcBuffer = source->getBufferName( srcOffsetStart );

        VkBufferCopy region;
        region.srcOffset = srcOffset + srcOffsetStart;
        region.dstOffset = mInternalBufferStart + freeRegionOffset;
        region.size = alignToNextMultiple( srcLength, 4u );
        vkCmdCopyBuffer( device->mGraphicsQueue.mCurrentCmdBuffer, srcBuffer, mVboName, 1u, &region );
        // __unsafe_unretained id<MTLBuffer> srcBuffer = source->getBufferName( srcOffsetStart );
        //
        // __unsafe_unretained id<MTLBlitCommandEncoder> blitEncoder = mDevice->getBlitEncoder();
        // [blitEncoder copyFromBuffer:srcBuffer
        //                sourceOffset:srcOffset + srcOffsetStart
        //                    toBuffer:mVboName
        //           destinationOffset:mInternalBufferStart + freeRegionOffset
        //                        size:alignToNextMultiple( srcLength, 4u )];
#if OGRE_PLATFORM != OGRE_PLATFORM_APPLE_IOS
        //[blitEncoder synchronizeResource:mVboName];
#endif

        return freeRegionOffset + extraOffset;
    }

    void VulkanStagingBuffer::_notifyDeviceStalled()
    {
        deleteFences( mFences.begin(), mFences.end() );
        mFences.clear();
        mUnfencedHazards.clear();
        mUnfencedBytes = 0;
    }

    //-----------------------------------------------------------------------------------
    const void *VulkanStagingBuffer::_mapForReadImpl( size_t offset, size_t sizeBytes )
    {
        assert( !mUploadOnly );

        mMappingStart = offset;
        mMappingCount = sizeBytes;

        mMappedPtr = mVulkanDataPtr + mInternalBufferStart + mMappingStart;

        // Put the mapped region back to our records as "available" for subsequent _asyncDownload
        _cancelDownload( offset, sizeBytes );

        return mMappedPtr;
    }
}  // namespace Ogre
