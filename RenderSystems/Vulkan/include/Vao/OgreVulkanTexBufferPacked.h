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

#ifndef _Ogre_VulkanTexBufferPacked_H_
#define _Ogre_VulkanTexBufferPacked_H_

#include <vulkan/vulkan.h>

#include "OgreVulkanPrerequisites.h"

#include "Vao/OgreTexBufferPacked.h"

namespace Ogre
{
    class VulkanBufferInterface;

    class _OgreVulkanExport VulkanTexBufferPacked : public TexBufferPacked
    {
    public:
        VulkanTexBufferPacked( size_t internalBufStartBytes, size_t numElements, uint32 bytesPerElement,
                               uint32 numElementsPadding, BufferType bufferType, void *initialData,
                               bool keepAsShadow, VaoManager *vaoManager,
                               VulkanBufferInterface *bufferInterface, PixelFormatGpu pf );
        ~VulkanTexBufferPacked();

        virtual void bindBufferVS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 );
        virtual void bindBufferPS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 );
        virtual void bindBufferGS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 ) {}
        virtual void bindBufferDS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 ) {}
        virtual void bindBufferHS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 ) {}
        virtual void bindBufferCS( uint16 slot, size_t offset = 0, size_t sizeBytes = 0 );

        void bindBufferForDescriptor( VkBuffer *buffers, VkDeviceSize *offsets,
                                      size_t offset );

        // Used to check if it makes sense to update VkWriteDescriptorSet with this buffer info.
        bool isDirty() const { return mDirty; }

        void resetDirty() { mDirty = false; }


        VkBufferView getBufferView() const
        {
            return mBufferView;
        }

        uint16 getCurrentBinding() const
        {
            return mCurrentBinding;
        }

    private:
        void bindBuffer( uint16 slot, size_t offset, size_t sizeBytes );

        VkBufferView mBufferView;
        size_t mPrevSizeBytes;
        size_t mPrevOffset;
        uint16 mCurrentBinding;
        bool mDirty;
    };
}  // namespace Ogre

#endif
