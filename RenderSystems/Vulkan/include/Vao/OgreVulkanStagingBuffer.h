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

#ifndef _Ogre_VulkanStagingBuffer_H_
#define _Ogre_VulkanStagingBuffer_H_

#include "OgreVulkanPrerequisites.h"

#include "Vao/OgreStagingBuffer.h"

namespace Ogre
{
    /** A staging buffer is a buffer that resides on the GPU and be written to/from both CPU & GPU
        However the access in both cases is limited. GPUs can only copy (i.e. memcpy) to another
        real buffer (can't be used directly as i.e. texture or vertex buffer) and CPUs can only
        map it.
        In other words, a staging buffer is an intermediate buffer to transfer data between
        CPU & GPU
    */
    class _OgreVulkanExport VulkanStagingBuffer : public StagingBuffer
    {
    protected:
        void *mMappedPtr;

        uint8 *mVulkanDataPtr;
        // VulkanBufferInterface * mBufferInterface;
        VkDeviceMemory mDeviceMemory;
        VkBuffer mVboName;

        virtual void *mapImpl( size_t sizeBytes );
        virtual void unmapImpl( const Destination *destinations, size_t numDestinations );
        virtual const void *_mapForReadImpl( size_t offset, size_t sizeBytes );

    public:
        VulkanStagingBuffer( size_t internalBufferStart, size_t sizeBytes, VaoManager *vaoManager,
                             bool uploadOnly, VkDeviceMemory deviceMemory, VkBuffer vboName,
                             VulkanDynamicBuffer *dynamicBuffer );        

        virtual ~VulkanStagingBuffer();

        virtual StagingStallType uploadWillStall( size_t sizeBytes );

        virtual size_t _asyncDownload( BufferPacked *source, size_t srcOffset, size_t srcLength );
        void _unmapToV1( v1::VulkanHardwareBufferCommon * hwBuffer, size_t lockStart, size_t lockSize );
        unsigned long long _asyncDownloadV1( v1::VulkanHardwareBufferCommon *source, size_t srcOffset,
                                             size_t srcLength );
    };
}  // namespace Ogre

#endif