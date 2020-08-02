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

#include "Vao/OgreVulkanConstBufferPacked.h"

#include "OgreVulkanRenderSystem.h"
#include "Vao/OgreVulkanBufferInterface.h"

namespace Ogre
{
    VulkanConstBufferPacked::VulkanConstBufferPacked(
        size_t internalBufferStartBytes, size_t numElements, uint32 bytesPerElement,
        uint32 numElementsPadding, BufferType bufferType, void *initialData, bool keepAsShadow,
        VulkanRenderSystem *renderSystem, VaoManager *vaoManager, BufferInterface *bufferInterface ) :
        ConstBufferPacked( internalBufferStartBytes, numElements, bytesPerElement, numElementsPadding,
                           bufferType, initialData, keepAsShadow, vaoManager, bufferInterface ),
        mRenderSystem( renderSystem )
    {
    }
    //-------------------------------------------------------------------------
    VulkanConstBufferPacked::~VulkanConstBufferPacked() {}
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferVS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferPS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferGS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferHS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferDS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBufferCS( uint16 slot ) { bindBuffer( slot, 0 ); }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindBuffer( uint16 slot, uint32 offsetBytes )
    {
        OGRE_ASSERT_HIGH( dynamic_cast<VulkanBufferInterface *>( mBufferInterface ) );
        VulkanBufferInterface *bufferInterface =
            static_cast<VulkanBufferInterface *>( mBufferInterface );

        VkDescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = bufferInterface->getVboName();
        bufferInfo.offset = mFinalBufferStart * mBytesPerElement + offsetBytes;
        bufferInfo.range = mNumElements * mBytesPerElement;
        mRenderSystem->_setConstBuffer( slot, bufferInfo );
    }
    //-------------------------------------------------------------------------
    void VulkanConstBufferPacked::bindAsParamBuffer( GpuProgramType shaderStage, size_t offsetBytes )
    {
        OGRE_ASSERT_HIGH( dynamic_cast<VulkanBufferInterface *>( mBufferInterface ) );
        VulkanBufferInterface *bufferInterface =
            static_cast<VulkanBufferInterface *>( mBufferInterface );

        VkDescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = bufferInterface->getVboName();
        bufferInfo.offset = mFinalBufferStart * mBytesPerElement + offsetBytes;
        bufferInfo.range = mNumElements * mBytesPerElement;
        mRenderSystem->_setParamBuffer( shaderStage, bufferInfo );
    }
}  // namespace Ogre
