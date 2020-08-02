/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-present Torus Knot Software Ltd

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

#include "OgreVulkanRootLayout.h"

#include "OgreVulkanDescriptorPool.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanGlobalBindingTable.h"
#include "OgreVulkanGpuProgramManager.h"
#include "OgreVulkanUtils.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreException.h"
#include "OgreLwString.h"
#include "OgreStringConverter.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include "vulkan/vulkan_core.h"

#define TODO_limit_NUM_BIND_TEXTURES  // and co.

namespace Ogre
{
    static const char *c_rootLayoutVarNames[VulkanDescBindingTypes::NumDescBindingTypes] = {
        "has_params",     //
        "const_buffers",  //
        "tex_buffers",    //
        "textures",       //
        "samplers",       //
        "uav_textures",   //
        "uav_buffers",    //
    };
    static const char c_bufferTypes[] = "PBTtsuU";
    static const uint8 c_allGraphicStagesMask = ( 1u << VertexShader ) | ( 1u << PixelShader ) |
                                                ( 1u << GeometryShader ) | ( 1u << HullShader ) |
                                                ( 1u << DomainShader );
    //-------------------------------------------------------------------------
    uint32 toVkDescriptorType( VulkanDescBindingTypes::VulkanDescBindingTypes type )
    {
        switch( type )
        {
            // clang-format off
        case VulkanDescBindingTypes::ParamBuffer:       return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case VulkanDescBindingTypes::ConstBuffer:       return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case VulkanDescBindingTypes::TexBuffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case VulkanDescBindingTypes::Texture:           return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case VulkanDescBindingTypes::Sampler:           return VK_DESCRIPTOR_TYPE_SAMPLER;
        case VulkanDescBindingTypes::UavTexture:        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case VulkanDescBindingTypes::UavBuffer:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case VulkanDescBindingTypes::NumDescBindingTypes:   return VK_DESCRIPTOR_TYPE_MAX_ENUM;
            // clang-format on
        }
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
    //-------------------------------------------------------------------------
    VulkanDescBindingRange::VulkanDescBindingRange() : start( 0u ), end( 0u ) {}
    //-------------------------------------------------------------------------
    VulkanRootLayout::VulkanRootLayout( VulkanGpuProgramManager *programManager ) :
        mCompute( false ),
        mParamsBuffStages( 0u ),
        mRootLayout( 0 ),
        mProgramManager( programManager )
    {
    }
    //-------------------------------------------------------------------------
    VulkanRootLayout::~VulkanRootLayout()
    {
        if( mRootLayout )
        {
            vkDestroyPipelineLayout( mProgramManager->getDevice()->mDevice, mRootLayout, 0 );
            mRootLayout = 0;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRootLayout::validate( const String &filename ) const
    {
        // Check start <= end
        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                if( !mDescBindingRanges[i][j].isValid() )
                {
                    char tmpBuffer[512];
                    LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

                    tmpStr.a( "Invalid descriptor start <= end must be true. Set ", (uint32)i,
                              " specifies ", c_rootLayoutVarNames[j], " in range [",
                              mDescBindingRanges[i][j].start );
                    tmpStr.a( ", ", mDescBindingRanges[i][j].end, ")" );

                    OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                 "Error at file " + filename + ":\n" + tmpStr.c_str(),
                                 "VulkanRootLayout::validate" );
                }
            }

            // Ensure texture and texture buffer ranges don't overlap for compatibility with
            // other APIs (we support it in Vulkan, but explicitly forbid it)
            {
                const VulkanDescBindingRange &bufferRange =
                    mDescBindingRanges[i][VulkanDescBindingTypes::TexBuffer];
                if( bufferRange.isInUse() )
                {
                    for( size_t j = i; j < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++j )
                    {
                        const VulkanDescBindingRange &texRange =
                            mDescBindingRanges[j][VulkanDescBindingTypes::Texture];

                        if( texRange.isInUse() )
                        {
                            if( !( bufferRange.end <= texRange.start ||
                                   bufferRange.start >= texRange.end ) )
                            {
                                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                             "Error at file " + filename + ":\n" +
                                                 "TexBuffer and Texture slots cannot overlap for "
                                                 "compatibility with other APIs",
                                             "VulkanRootLayout::validate" );
                            }
                        }
                    }
                }
            }
            {
                const VulkanDescBindingRange &bufferRange =
                    mDescBindingRanges[i][VulkanDescBindingTypes::UavBuffer];
                if( bufferRange.isInUse() )
                {
                    for( size_t j = i; j < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++j )
                    {
                        const VulkanDescBindingRange &texRange =
                            mDescBindingRanges[j][VulkanDescBindingTypes::UavTexture];

                        if( texRange.isInUse() )
                        {
                            if( !( bufferRange.end <= texRange.start ||
                                   bufferRange.start >= texRange.end ) )
                            {
                                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                             "Error at file " + filename + ":\n" +
                                                 "UavBuffer and UavTexture slots cannot overlap for "
                                                 "compatibility with other APIs",
                                             "VulkanRootLayout::validate" );
                            }
                        }
                    }
                }
            }
        }

        // Check range[set = 0] does not overlap with range[set = 1] and comes before set = 1
        // TODO: Do we really need this restriction? Even so, this restriction keeps things sane?
        for( size_t i = 0u; i < VulkanDescBindingTypes::NumDescBindingTypes; ++i )
        {
            for( size_t j = 0u; j < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS - 1u; ++j )
            {
                if( mDescBindingRanges[j][i].end > mDescBindingRanges[j + 1][i].start &&
                    mDescBindingRanges[j + 1][i].isInUse() )
                {
                    char tmpBuffer[1024];
                    LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

                    tmpStr.a( "Descriptors cannot overlap across sets. However set ", (uint32)j,
                              " specifies ", c_rootLayoutVarNames[i], " in range [",
                              mDescBindingRanges[j][i].start );
                    tmpStr.a( ", ", (uint32)mDescBindingRanges[j][i].end, ") but set ",
                              ( uint32 )( j + 1u ), " is in range [",
                              mDescBindingRanges[j + 1][i].start );
                    tmpStr.a( ", ", mDescBindingRanges[j + 1][i].end, ")" );

                    OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                 "Error at file " + filename + ":\n" + tmpStr.c_str(),
                                 "VulkanRootLayout::validate" );
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRootLayout::parseSet( const rapidjson::Value &jsonValue, const size_t setIdx,
                                     const String &filename )
    {
        rapidjson::Value::ConstMemberIterator itor;

        for( size_t i = 0u; i < VulkanDescBindingTypes::NumDescBindingTypes; ++i )
        {
            itor = jsonValue.FindMember( c_rootLayoutVarNames[i] );

            if( i == VulkanDescBindingTypes::ParamBuffer )
            {
                if( itor != jsonValue.MemberEnd() && itor->value.IsArray() )
                {
                    if( mParamsBuffStages != 0u )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "All 'has_params' declarations must live in the same set",
                                     "VulkanRootLayout::parseSet" );
                    }

                    const size_t numStages = itor->value.Size();
                    for( size_t j = 0u; j < numStages; ++j )
                    {
                        if( itor->value[j].IsString() )
                        {
                            if( !mCompute )
                            {
                                const char *stageAcronyms[NumShaderTypes] = { "vs", "ps", "gs", "hs",
                                                                              "ds" };
                                for( size_t k = 0u; k < NumShaderTypes; ++k )
                                {
                                    if( strncmp( itor->value[j].GetString(), stageAcronyms[k], 2u ) ==
                                        0 )
                                    {
                                        mParamsBuffStages |= 1u << k;
                                        ++mDescBindingRanges[setIdx][i].end;
                                    }
                                }

                                if( strncmp( itor->value[j].GetString(), "all", 3u ) == 0 )
                                {
                                    mParamsBuffStages = c_allGraphicStagesMask;
                                    mDescBindingRanges[setIdx][i].end = NumShaderTypes;
                                }
                            }
                            else
                            {
                                if( strncmp( itor->value[j].GetString(), "cs", 2u ) == 0 ||
                                    strncmp( itor->value[j].GetString(), "all", 3u ) == 0 )
                                {
                                    mParamsBuffStages = 1u << GPT_COMPUTE_PROGRAM;
                                    mDescBindingRanges[setIdx][i].end = 1u;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                if( itor != jsonValue.MemberEnd() && itor->value.IsArray() && itor->value.Size() == 2u &&
                    itor->value[0].IsUint() && itor->value[1].IsUint() )
                {
                    if( itor->value[0].GetUint() > 65535 || itor->value[1].GetUint() > 65535 )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "Root Layout descriptors must be in range [0; 65535]",
                                     "VulkanRootLayout::parseSet" );
                    }

                    TODO_limit_NUM_BIND_TEXTURES;  // Only for dynamic sets

                    mDescBindingRanges[setIdx][i].start =
                        static_cast<uint16>( itor->value[0].GetUint() );
                    mDescBindingRanges[setIdx][i].end = static_cast<uint16>( itor->value[1].GetUint() );

                    if( mDescBindingRanges[setIdx][i].start > mDescBindingRanges[setIdx][i].end )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "Root Layout descriptors must satisfy start < end",
                                     "VulkanRootLayout::parseSet" );
                    }
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRootLayout::parseRootLayout( const char *rootLayout, const bool bCompute,
                                            const String &filename )
    {
        OGRE_ASSERT_LOW( !mRootLayout && "Cannot call parseRootLayout after createVulkanHandles" );

        mCompute = bCompute;
        mParamsBuffStages = 0u;

        rapidjson::Document d;
        d.Parse( rootLayout );

        if( d.HasParseError() )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "Invalid JSON string in file " + filename + " at line " +
                             StringConverter::toString( d.GetErrorOffset() ) +
                             " Reason: " + rapidjson::GetParseError_En( d.GetParseError() ),
                         "VulkanRootLayout::parseRootLayout" );
        }

        char tmpBuffer[16];
        LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
                mDescBindingRanges[i][j] = VulkanDescBindingRange();

            tmpStr.clear();
            tmpStr.a( static_cast<uint32_t>( i ) );

            rapidjson::Value::ConstMemberIterator itor;

            itor = d.FindMember( tmpStr.c_str() );
            if( itor != d.MemberEnd() && itor->value.IsObject() )
                parseSet( itor->value, i, filename );
        }

        validate( filename );
    }
    //-------------------------------------------------------------------------
    void VulkanRootLayout::generateRootLayoutMacros( uint32 shaderStage, String &inOutString ) const
    {
        String macroStr;
        macroStr.swap( inOutString );

        char tmpBuffer[256];
        LwString textStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

        textStr.a( "#define ogre_" );
        const size_t prefixSize0 = textStr.size();

        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            uint32 bindingIdx = 0u;
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                textStr.resize( prefixSize0 );
                textStr.aChar( c_bufferTypes[j] );
                const size_t prefixSize1 = textStr.size();

                if( j == VulkanDescBindingTypes::ParamBuffer )
                {
                    // ParamBuffer is special because it's always ogre_P0, but we still
                    // must still account all the other stages' binding indices while counting
                    if( mDescBindingRanges[i][j].isInUse() )
                    {
                        if( mParamsBuffStages & ( 1u << shaderStage ) )
                        {
                            textStr.resize( prefixSize1 );  // #define ogre_P
                            uint32 numStagesWithParams = 0u;
                            if( mCompute )
                                numStagesWithParams = 1u;
                            else
                            {
                                for( size_t k = 0u; k < shaderStage; ++k )
                                {
                                    if( mParamsBuffStages & ( 1u << k ) )
                                        ++numStagesWithParams;
                                }
                            }

                            // #define ogre_P0 set = 1, binding = 6
                            textStr.a( "0", " set = ", (uint32)i, ", binding = ", numStagesWithParams,
                                       "\n" );
                            macroStr += textStr.c_str();
                        }

                        const size_t numSlots = mDescBindingRanges[i][j].getNumUsedSlots();
                        bindingIdx += numSlots;
                    }
                }
                else
                {
                    uint32 emulatedSlot = mDescBindingRanges[i][j].start;
                    const size_t numSlots = mDescBindingRanges[i][j].getNumUsedSlots();
                    for( size_t k = 0u; k < numSlots; ++k )
                    {
                        textStr.resize( prefixSize1 );  // #define ogre_B
                        // #define ogre_B3 set = 1, binding = 6
                        textStr.a( emulatedSlot, " set = ", (uint32)i, ", binding = ", bindingIdx,
                                   "\n" );
                        ++bindingIdx;
                        ++emulatedSlot;

                        macroStr += textStr.c_str();
                    }
                }
            }
        }
        macroStr.swap( inOutString );
    }
    //-------------------------------------------------------------------------
    size_t VulkanRootLayout::calculateNumUsedSets( void ) const
    {
        size_t numSets = 0u;
        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                if( mDescBindingRanges[i][j].isInUse() )
                    numSets = i + 1u;
            }
        }
        return numSets;
    }
    //-------------------------------------------------------------------------
    size_t VulkanRootLayout::calculateNumBindings( const size_t setIdx ) const
    {
        size_t numBindings = 0u;
        for( size_t i = 0u; i < VulkanDescBindingTypes::NumDescBindingTypes; ++i )
            numBindings += mDescBindingRanges[setIdx][i].getNumUsedSlots();

        return numBindings;
    }
    //-------------------------------------------------------------------------
    VkPipelineLayout VulkanRootLayout::createVulkanHandles( void )
    {
        if( mRootLayout )
            return mRootLayout;

        FastArray<FastArray<VkDescriptorSetLayoutBinding> > rootLayoutDesc;

        const size_t numSets = calculateNumUsedSets();
        rootLayoutDesc.resize( numSets );
        mSets.resize( numSets );
        mPools.resize( numSets );

        for( size_t i = 0u; i < numSets; ++i )
        {
            rootLayoutDesc[i].resize( calculateNumBindings( i ) );

            size_t bindingIdx = 0u;

            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                const size_t numSlots = mDescBindingRanges[i][j].getNumUsedSlots();
                for( size_t k = 0u; k < numSlots; ++k )
                {
                    rootLayoutDesc[i][bindingIdx].binding = static_cast<uint32_t>( bindingIdx );
                    rootLayoutDesc[i][bindingIdx].descriptorType =
                        static_cast<VkDescriptorType>( toVkDescriptorType(
                            static_cast<VulkanDescBindingTypes::VulkanDescBindingTypes>( j ) ) );
                    rootLayoutDesc[i][bindingIdx].descriptorCount = 1u;
                    rootLayoutDesc[i][bindingIdx].stageFlags =
                        mCompute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

                    if( !mCompute && j == VulkanDescBindingTypes::ParamBuffer &&
                        ( mParamsBuffStages & c_allGraphicStagesMask ) != c_allGraphicStagesMask )
                    {
                        if( mParamsBuffStages & ( 1u << VertexShader ) )
                            rootLayoutDesc[i][bindingIdx].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
                        if( mParamsBuffStages & ( 1u << PixelShader ) )
                            rootLayoutDesc[i][bindingIdx].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
                        if( mParamsBuffStages & ( 1u << GeometryShader ) )
                            rootLayoutDesc[i][bindingIdx].stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
                        if( mParamsBuffStages & ( 1u << HullShader ) )
                        {
                            rootLayoutDesc[i][bindingIdx].stageFlags |=
                                VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                        }
                        if( mParamsBuffStages & ( 1u << DomainShader ) )
                        {
                            rootLayoutDesc[i][bindingIdx].stageFlags |=
                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                        }
                    }
                    rootLayoutDesc[i][bindingIdx].pImmutableSamplers = 0;

                    ++bindingIdx;
                }
            }

            mSets[i] = mProgramManager->getCachedSet( rootLayoutDesc[i] );
        }

        VulkanDevice *device = mProgramManager->getDevice();

        VkPipelineLayoutCreateInfo pipelineLayoutCi;
        makeVkStruct( pipelineLayoutCi, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO );
        pipelineLayoutCi.setLayoutCount = static_cast<uint32>( numSets );
        pipelineLayoutCi.pSetLayouts = mSets.begin();

        VkResult result = vkCreatePipelineLayout( device->mDevice, &pipelineLayoutCi, 0, &mRootLayout );
        checkVkResult( result, "vkCreatePipelineLayout" );

        return mRootLayout;
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindCommon( VkWriteDescriptorSet &writeDescSet,
                                              size_t &numWriteDescSets, uint32 &currBinding,
                                              VkDescriptorSet descSet,
                                              const VulkanDescBindingRange &bindRanges )
    {
        makeVkStruct( writeDescSet, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET );
        writeDescSet.dstSet = descSet;
        writeDescSet.dstBinding = currBinding;
        writeDescSet.dstArrayElement = 0u;
        writeDescSet.descriptorCount = static_cast<uint32_t>( bindRanges.getNumUsedSlots() );
        currBinding += bindRanges.getNumUsedSlots();
        ++numWriteDescSets;
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindParamsBuffer( VkWriteDescriptorSet *writeDescSets,
                                                    size_t &numWriteDescSets, uint32 &currBinding,
                                                    VkDescriptorSet descSet,
                                                    const VulkanDescBindingRange *descBindingRanges,
                                                    const VulkanGlobalBindingTable &table )
    {
        const VulkanDescBindingRange &bindRanges =
            descBindingRanges[VulkanDescBindingTypes::ParamBuffer];

        if( !bindRanges.isInUse() )
            return;

        if( mCompute )
        {
            VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];
            bindCommon( writeDescSet, numWriteDescSets, currBinding, descSet, bindRanges );
            writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescSet.pBufferInfo = &table.paramsBuffer[GPT_COMPUTE_PROGRAM];
        }
        else
        {
            for( size_t i = 0u; i < NumShaderTypes; ++i )
            {
                if( mParamsBuffStages & ( 1u << i ) )
                {
                    VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];

                    makeVkStruct( writeDescSet, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET );
                    writeDescSet.dstSet = descSet;
                    writeDescSet.dstBinding = currBinding;
                    writeDescSet.dstArrayElement = 0u;
                    writeDescSet.descriptorCount = 1u;
                    ++currBinding;
                    ++numWriteDescSets;

                    writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    writeDescSet.pBufferInfo = &table.paramsBuffer[i];
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindConstBuffers( VkWriteDescriptorSet *writeDescSets,
                                                    size_t &numWriteDescSets, uint32 &currBinding,
                                                    VkDescriptorSet descSet,
                                                    const VulkanDescBindingRange *descBindingRanges,
                                                    const VulkanGlobalBindingTable &table )
    {
        const VulkanDescBindingRange &bindRanges =
            descBindingRanges[VulkanDescBindingTypes::ConstBuffer];

        if( !bindRanges.isInUse() )
            return;

        VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];
        bindCommon( writeDescSet, numWriteDescSets, currBinding, descSet, bindRanges );
        writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescSet.pBufferInfo = &table.constBuffers[bindRanges.start];
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindTexBuffers( VkWriteDescriptorSet *writeDescSets,
                                                  size_t &numWriteDescSets, uint32 &currBinding,
                                                  VkDescriptorSet descSet,
                                                  const VulkanDescBindingRange *descBindingRanges,
                                                  const VulkanGlobalBindingTable &table )
    {
        const VulkanDescBindingRange &bindRanges = descBindingRanges[VulkanDescBindingTypes::TexBuffer];

        if( !bindRanges.isInUse() )
            return;

        VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];
        bindCommon( writeDescSet, numWriteDescSets, currBinding, descSet, bindRanges );
        writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writeDescSet.pTexelBufferView = &table.texBuffers[bindRanges.start];
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindTextures( VkWriteDescriptorSet *writeDescSets,
                                                size_t &numWriteDescSets, uint32 &currBinding,
                                                VkDescriptorSet descSet,
                                                const VulkanDescBindingRange *descBindingRanges,
                                                const VulkanGlobalBindingTable &table )
    {
        const VulkanDescBindingRange &bindRanges = descBindingRanges[VulkanDescBindingTypes::Texture];

        if( !bindRanges.isInUse() )
            return;

        VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];
        bindCommon( writeDescSet, numWriteDescSets, currBinding, descSet, bindRanges );
        writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writeDescSet.pImageInfo = &table.textures[bindRanges.start];
    }
    //-------------------------------------------------------------------------
    inline void VulkanRootLayout::bindSamplers( VkWriteDescriptorSet *writeDescSets,
                                                size_t &numWriteDescSets, uint32 &currBinding,
                                                VkDescriptorSet descSet,
                                                const VulkanDescBindingRange *descBindingRanges,
                                                const VulkanGlobalBindingTable &table )
    {
        const VulkanDescBindingRange &bindRanges = descBindingRanges[VulkanDescBindingTypes::Texture];

        if( !bindRanges.isInUse() )
            return;

        VkWriteDescriptorSet &writeDescSet = writeDescSets[numWriteDescSets];
        bindCommon( writeDescSet, numWriteDescSets, currBinding, descSet, bindRanges );
        writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writeDescSet.pImageInfo = &table.samplers[bindRanges.start];
    }
    //-------------------------------------------------------------------------
    void VulkanRootLayout::bind( VulkanDevice *device, VulkanVaoManager *vaoManager,
                                 const VulkanGlobalBindingTable &table )
    {
        VkDescriptorSet descSets[OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS];

        const size_t numSets = mSets.size();

        for( size_t i = 0u; i < numSets; ++i )
        {
            if( !mPools[i] || !mPools[i]->isAvailableInCurrentFrame() )
                mPools[i] = vaoManager->getDescriptorPool( this, i, mSets[i] );

            VkDescriptorSet descSet = mPools[i]->allocate( device, mSets[i] );

            const VulkanDescBindingRange *descBindingRanges = mDescBindingRanges[i];

            size_t numWriteDescSets = 0u;
            uint32 currBinding = 0u;
            // ParamBuffer consumes up to 1 per stage (not just 1)
            VkWriteDescriptorSet writeDescSets[VulkanDescBindingTypes::Sampler + NumShaderTypes];

            // Note: We must bind in the same order as VulkanDescBindingTypes
            bindParamsBuffer( writeDescSets, numWriteDescSets, currBinding, descSet, descBindingRanges,
                              table );
            bindConstBuffers( writeDescSets, numWriteDescSets, currBinding, descSet, descBindingRanges,
                              table );
            bindTexBuffers( writeDescSets, numWriteDescSets, currBinding, descSet, descBindingRanges,
                            table );
            bindTextures( writeDescSets, numWriteDescSets, currBinding, descSet, descBindingRanges,
                          table );
            bindSamplers( writeDescSets, numWriteDescSets, currBinding, descSet, descBindingRanges,
                          table );

            vkUpdateDescriptorSets( device->mDevice, static_cast<uint32_t>( numWriteDescSets ),
                                    writeDescSets, 0u, 0 );
            descSets[i] = descSet;
        }

        vkCmdBindDescriptorSets(
            device->mGraphicsQueue.mCurrentCmdBuffer,
            mCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, mRootLayout, 0u,
            static_cast<uint32_t>( mSets.size() ), descSets, 0u, 0 );
    }
    //-------------------------------------------------------------------------
    bool VulkanRootLayout::findParamsBuffer( uint32 shaderStage, size_t &outSetIdx,
                                             size_t &outBindingIdx ) const
    {
        if( !( mParamsBuffStages & ( 1u << shaderStage ) ) )
            return false;  // There is no param buffer in this stage

        size_t numStagesWithParams = 0u;
        if( mCompute )
        {
            numStagesWithParams = 1u;
        }
        else
        {
            for( size_t i = 0u; i < shaderStage; ++i )
            {
                if( mParamsBuffStages & ( 1u << i ) )
                    ++numStagesWithParams;
            }
        }

        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            if( mDescBindingRanges[i][VulkanDescBindingTypes::ParamBuffer].isInUse() )
            {
                outSetIdx = i;
                outBindingIdx = numStagesWithParams;
                return true;
            }
        }

        OGRE_ASSERT_LOW( false && "This path should not be reached" );

        return false;
    }
    //-------------------------------------------------------------------------
    VulkanRootLayout *VulkanRootLayout::findBest( VulkanRootLayout *a, VulkanRootLayout *b )
    {
        if( !b )
            return a;
        if( !a )
            return b;
        if( a == b )
            return a;

        VulkanRootLayout *best = 0;

        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            VulkanRootLayout *other = 0;

            bool bDiverged = false;
            size_t numSlotsA = 0u;
            size_t numSlotsB = 0u;
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                numSlotsA += a->mDescBindingRanges[i][j].getNumUsedSlots();
                numSlotsB += b->mDescBindingRanges[i][j].getNumUsedSlots();

                if( !bDiverged )
                {
                    if( numSlotsA != numSlotsB )
                    {
                        VulkanRootLayout *newBest = 0;
                        if( numSlotsA > numSlotsB )
                            newBest = a;
                        else
                            newBest = b;

                        if( best != newBest )
                        {
                            // This is the first divergence within this set idx
                            // However a previous set diverged; and the 'best' one
                            // is not this time's winner.
                            //
                            // a and b are incompatible
                            return 0;
                        }

                        bDiverged = true;
                    }
                }
                else
                {
                    // a and b were already on a path to being incompatible
                    if( other->mDescBindingRanges[i][j].isInUse() )
                        return 0;  // a and b are incompatible
                }
            }
        }

        if( !best )
        {
            // If we're here then a and b are equivalent? We should not arrive here due to
            // VulkanGpuProgramManager::getRootLayout always returning the same layout.
            // Anyway, pick any just in case
            best = a;
        }

        return best;
    }
    //-------------------------------------------------------------------------
    bool VulkanRootLayout::operator<( const VulkanRootLayout &other ) const
    {
        if( this->mCompute != other.mCompute )
            return this->mCompute < other.mCompute;
        if( this->mParamsBuffStages != other.mParamsBuffStages )
            return this->mParamsBuffStages < other.mParamsBuffStages;

        for( size_t i = 0u; i < OGRE_VULKAN_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < VulkanDescBindingTypes::NumDescBindingTypes; ++j )
            {
                if( this->mDescBindingRanges[i][j].start != other.mDescBindingRanges[i][j].start )
                    return this->mDescBindingRanges[i][j].start < other.mDescBindingRanges[i][j].start;
                if( this->mDescBindingRanges[i][j].end != other.mDescBindingRanges[i][j].end )
                    return this->mDescBindingRanges[i][j].end < other.mDescBindingRanges[i][j].end;
            }
        }

        // If we're here then a and b are equals, thus a < b returns false
        return false;
    }
}  // namespace Ogre
