/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org/

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

#include "OgreVulkanDiscardBufferManager.h"



#include "OgreStringConverter.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanUtils.h"

Ogre::VulkanDiscardBufferManager::VulkanDiscardBufferManager( VulkanDevice *device,
                                                              VaoManager *vaoManager ) :
    mBuffer( 0 ),
    mDevice( device ),
    mVaoManager( vaoManager )
{
    const size_t defaultCapacity = 4 * 1024 * 1024;

    VulkanVaoManager *vaoMagr = static_cast<VulkanVaoManager *>( vaoManager );
    const uint32 *memoryTypeIndex = vaoMagr->getBestVkMemoryTypeIndex();

    VkBufferCreateInfo bufferCi;
    makeVkStruct( bufferCi, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO );
    bufferCi.size = defaultCapacity;
    bufferCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    VkResult result = vkCreateBuffer( mDevice->mDevice, &bufferCi, 0, &mBuffer );
    checkVkResult( result, "vkCreateBuffer" );

    vkGetBufferMemoryRequirements( mDevice->mDevice, mBuffer, &mMemRequirements );

    VkMemoryAllocateInfo memAllocInfo;
    makeVkStruct( memAllocInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO );
    memAllocInfo.allocationSize = mMemRequirements.size;
    memAllocInfo.memoryTypeIndex = memoryTypeIndex[VulkanVaoManager::CPU_ACCESSIBLE_PERSISTENT_COHERENT];

    result = vkAllocateMemory( mDevice->mDevice, &memAllocInfo, NULL, &mDeviceMemory );
    checkVkResult( result, "vkAllocateMemory" );

    // TODO use one large buffer and multiple offsets
    VkDeviceSize offset = 0;
    offset = alignMemory( offset, mMemRequirements.alignment );

    result = vkBindBufferMemory( mDevice->mDevice, mBuffer, mDeviceMemory, offset );
    checkVkResult( result, "vkBindBufferMemory" );

    mFreeBlocks.push_back( VulkanVaoManager::Block( 0, defaultCapacity ) );
}

Ogre::VulkanDiscardBufferManager::~VulkanDiscardBufferManager()
{
    VulkanDiscardBufferVec::const_iterator itor = mDiscardBuffers.begin();
    VulkanDiscardBufferVec::const_iterator end = mDiscardBuffers.end();

    while( itor != end )
        OGRE_DELETE *itor++;
    mDiscardBuffers.clear();

    vkDestroyBuffer( mDevice->mDevice, mBuffer, 0 );
    vkFreeMemory( mDevice->mDevice, mDeviceMemory, 0 );
    mDeviceMemory = 0;
    mBuffer = 0;
}

void Ogre::VulkanDiscardBufferManager::growToFit( size_t extraBytes,
                                                  VulkanDiscardBuffer *forDiscardBuffer )
{
    assert( !( extraBytes & 0x04 ) && "extraBytes must be multiple of 4!" );

    const size_t oldCapacity = mMemRequirements.size;
    const size_t newCapacity =
        std::max( oldCapacity + extraBytes, oldCapacity + ( oldCapacity >> 1u ) + 1u );

    VulkanVaoManager *vaoMagr = static_cast<VulkanVaoManager *>( mVaoManager );
    const uint32 *memoryTypeIndex = vaoMagr->getBestVkMemoryTypeIndex();

    VkBuffer oldBuffer = mBuffer;
    VkDeviceMemory oldMemory = mDeviceMemory;

    VkBufferCreateInfo bufferCi;
    makeVkStruct( bufferCi, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO );
    bufferCi.size = newCapacity;
    bufferCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    VkResult result = vkCreateBuffer( mDevice->mDevice, &bufferCi, 0, &mBuffer );
    checkVkResult( result, "vkCreateBuffer" );

    vkGetBufferMemoryRequirements( mDevice->mDevice, mBuffer, &mMemRequirements );

    VkMemoryAllocateInfo memAllocInfo;
    makeVkStruct( memAllocInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO );
    memAllocInfo.allocationSize = mMemRequirements.size;
    memAllocInfo.memoryTypeIndex = memoryTypeIndex[VulkanVaoManager::CPU_ACCESSIBLE_PERSISTENT_COHERENT];

    result = vkAllocateMemory( mDevice->mDevice, &memAllocInfo, NULL, &mDeviceMemory );
    checkVkResult( result, "vkAllocateMemory" );

    // TODO use one large buffer and multiple offsets
    VkDeviceSize offset = 0;
    offset = alignMemory( offset, mMemRequirements.alignment );

    result = vkBindBufferMemory( mDevice->mDevice, mBuffer, mDeviceMemory, offset );
    checkVkResult( result, "vkBindBufferMemory" );

    {
        // Update our buffers so they point to the new buffer, copy their blocks in use from old
        // MTLBuffer to new one, and tag all of them as in use by GPU (due to the copyFromBuffer);
        // except 'forDiscardBuffer' which we were told this data won't be used.

        const uint32 currentFrame = mVaoManager->getFrameCount();
        VulkanDiscardBufferVec::iterator itor = mDiscardBuffers.begin();
        VulkanDiscardBufferVec::iterator end = mDiscardBuffers.end();

        while( itor != end )
        {
            if( *itor != forDiscardBuffer )
            {
                ( *itor )->mBuffer = mBuffer;

                VkBufferCopy region;
                region.srcOffset = ( *itor )->getBlockStart();
                region.dstOffset = ( *itor )->getBlockStart();
                region.size = alignToNextMultiple( ( *itor )->getBlockSize(), 4u );
                vkCmdCopyBuffer( mDevice->mGraphicsQueue.mCurrentCmdBuffer, oldBuffer, mBuffer, 1u,
                                 &region );
                [blitEncoder copyFromBuffer:oldBuffer
                               sourceOffset:( *itor )->getBlockStart()
                                   toBuffer:mBuffer
                          destinationOffset:( *itor )->getBlockStart()
                                       size:( *itor )->getBlockSize()];
                ( *itor )->mLastFrameUsed = currentFrame;
            }
            else
            {
                ( *itor )->mLastFrameUsed = currentFrame - mVaoManager->getDynamicBufferMultiplier();
            }

            ++itor;
        }
    }

    LogManager::getSingleton().logMessage(
        "PERFORMANCE WARNING: MetalDiscardBufferManager::growToFit must stall."
        "Consider increasing the default discard capacity to at least " +
        StringConverter::toString( newCapacity ) + " bytes" );

    // According to Metal docs, it's undefined behavior if both CPU & GPU
    // write to the same resource even if the regions don't overlap.
    mDevice->stall();

    mFreeBlocks.push_back( VulkanVaoManager::Block( oldCapacity, newCapacity - oldCapacity ) );

    {
        // All "unsafe" blocks are no longer unsafe, since we're using a new buffer.
        UnsafeBlockVec::const_iterator itor = mUnsafeBlocks.begin();
        UnsafeBlockVec::const_iterator end = mUnsafeBlocks.end();

        while( itor != end )
        {
            mFreeBlocks.push_back( *itor );
            VulkanVaoManager::mergeContiguousBlocks( mFreeBlocks.end() - 1, mFreeBlocks );
            ++itor;
        }

        mUnsafeBlocks.clear();
    }
}

void Ogre::VulkanDiscardBufferManager::updateUnsafeBlocks()
{
}

void Ogre::VulkanDiscardBufferManager::_notifyDeviceStalled()
{
}

void Ogre::VulkanDiscardBufferManager::_getBlock( VulkanDiscardBuffer *discardBuffer )
{
}

Ogre::VulkanDiscardBuffer * Ogre::VulkanDiscardBufferManager::createDiscardBuffer( size_t bufferSize,
    uint16 alignment )
{
}

void Ogre::VulkanDiscardBufferManager::destroyDiscardBuffer( VulkanDiscardBuffer *discardBuffer )
{
}

Ogre::VulkanDiscardBuffer::VulkanDiscardBuffer( size_t bufferSize, uint16 alignment,
    VaoManager *vaoManager, VulkanDiscardBufferManager *owner )
{
}

void * Ogre::VulkanDiscardBuffer::map( bool noOverwrite )
{
}

void Ogre::VulkanDiscardBuffer::unmap()
{
}

VkBuffer Ogre::VulkanDiscardBuffer::getBufferName( size_t &outOffset )
{
}
