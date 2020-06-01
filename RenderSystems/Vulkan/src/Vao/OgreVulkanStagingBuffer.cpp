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
        mVboName( vboName )
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

        vkCmdPipelineBarrier( device->mGraphicsQueue.mCurrentCmdBuffer, src_stage_mask, dst_stage_mask,
                              0, 0, nullptr, 1,
                              &buffer_memory_barrier, 0, nullptr );

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
            vkCmdCopyBuffer( device->mGraphicsQueue.mCurrentCmdBuffer,
                             bufferInterface->getVboName(),
                             mVboName, 1u, &region );

            buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            buffer_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

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
