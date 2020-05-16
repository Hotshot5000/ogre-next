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
#include "OgreVulkanDescriptors.h"

#include "OgreVulkanGpuProgramManager.h"
#include "OgreVulkanProgram.h"
#include "OgreVulkanUtils.h"

#include "OgreException.h"
#include "OgreLwString.h"
#include "OgreVulkanMappings.h"

#include "SPIRV-Reflect/spirv_reflect.h"

namespace Ogre
{
    
    //-------------------------------------------------------------------------
    bool VulkanDescriptors::areBindingsCompatible( const VkDescriptorSetLayoutBinding &a,
                                                   const VkDescriptorSetLayoutBinding &b )
    {
        // OGRE_ASSERT_HIGH( a.binding == b.binding && "Comparing bindings from different slots!" );
        // if( a.binding != b.binding )
        //     return false;

        return a.descriptorCount == 0u || b.descriptorCount == 0 ||
               ( a.descriptorType == b.descriptorType &&  //
                 a.descriptorCount == b.descriptorCount &&
                 a.pImmutableSamplers == b.pImmutableSamplers );
    }
    //-------------------------------------------------------------------------
    bool VulkanDescriptors::canMergeDescriptorSets( const DescriptorSetLayoutBindingArray &a,
                                                    const DescriptorSetLayoutBindingArray &b )
    {
        const size_t minSize = std::min( a.size(), b.size() );

        bool retVal = true;

        for( size_t i = 0u; i < minSize; ++i )
        {
            if( a[i].empty() || b[i].empty() )
                continue;

            const size_t minBindings = std::min( a[i].size(), b[i].size() );
            for( size_t j = 0u; j < minBindings; ++j )
            {
                if( !areBindingsCompatible( a[i][j], b[i][j] ) )
                {
                    char tmpBuffer[256];
                    LwString logMsg( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );
                    logMsg.a( "Descriptor set ", (uint32)i, " binding ", (uint32)j,
                              " of two shader stages (e.g. vertex and pixel shader?) are not "
                              "compatible. These shaders cannot be used together" );
                    LogManager::getSingleton().logMessage( logMsg.c_str() );
                    retVal = false;
                }
            }
        }

        return retVal;
    }
    //-------------------------------------------------------------------------
    void VulkanDescriptors::mergeDescriptorSets( DescriptorSetLayoutBindingArray &a, const String &shaderB,
                                                 const DescriptorSetLayoutBindingArray &b )
    {
        if( !canMergeDescriptorSets( a, b ) )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "Descriptor Sets from shader '" + shaderB +
                             "' cannot be merged because they contradict what's already been defined by "
                             "the previous shader stages. See Ogre.log for more details",
                         "VulkanDescriptors::mergeDescriptorSets" );
        }

        const size_t minSize = std::min( a.size(), b.size() );

        for( size_t i = 0u; i < minSize; ++i )
        {
            if( a[i].empty() )
            {
                // a doesn't use bindings at set i. We can copy all of them straight from b
                a[i] = b[i];
            }
            else if( !b[i].empty() )
            {
                const size_t minBindings = std::min( a[i].size(), b[i].size() );
                for( size_t j = 0u; j < minBindings; ++j )
                {
                    // OGRE_ASSERT_HIGH( a[i][j].binding == j );
                    // OGRE_ASSERT_HIGH( b[i][j].binding == j );
                    if( a[i][j].binding == j && b[i][j].binding == j )
                        a[i][j].stageFlags |= b[i][j].stageFlags;
                }

                a[i].appendPOD( b[i].begin() + minBindings, b[i].end() );
            }
            // else
            //{
            //    b[i].empty() == true;
            //    There's nothing to merge from b
            //}
        }

        a.append( b.begin() + minSize, b.end() );
    }
    //-------------------------------------------------------------------------
    void VulkanDescriptors::generateDescriptorSets( const String &shaderName,
                                                    const std::vector<uint32> &spirv,
                                                    DescriptorSetLayoutBindingArray &outputSets )
    {
        if( spirv.empty() )
            return;

        SpvReflectShaderModule module;
        memset( &module, 0, sizeof( module ) );
        SpvReflectResult result =
            spvReflectCreateShaderModule( spirv.size() * sizeof( uint32 ), &spirv[0], &module );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectCreateShaderModule failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        uint32 numDescSets = 0;
        result = spvReflectEnumerateDescriptorSets( &module, &numDescSets, 0 );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectEnumerateDescriptorSets failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        FastArray<SpvReflectDescriptorSet *> sets;
        sets.resize( numDescSets );
        result = spvReflectEnumerateDescriptorSets( &module, &numDescSets, sets.begin() );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectEnumerateDescriptorSets failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        size_t numSets = 0u;
        FastArray<SpvReflectDescriptorSet *>::const_iterator itor = sets.begin();
        FastArray<SpvReflectDescriptorSet *>::const_iterator endt = sets.end();

        while( itor != endt )
        {
            numSets = std::max<size_t>( ( *itor )->set + 1u, numSets );
            ++itor;
        }

        outputSets.resize( numSets );

        itor = sets.begin();

        while( itor != endt )
        {
            const SpvReflectDescriptorSet &reflSet = **itor;

            FastArray<VkDescriptorSetLayoutBinding> &bindings = outputSets[reflSet.set];
            const size_t numUsedBindings = reflSet.binding_count;

            size_t numBindings = 0;
            for( size_t i = 0; i < numUsedBindings; ++i )
                numBindings = std::max<size_t>( reflSet.bindings[i]->binding + 1u, numBindings );

            bindings.resize( numBindings );
            memset( bindings.begin(), 0, sizeof( VkDescriptorSetLayoutBinding ) * bindings.size() );

            for( size_t i = 0; i < numUsedBindings; ++i )
            {
                const SpvReflectDescriptorBinding &reflBinding = *( reflSet.bindings[i] );
                // VkDescriptorSetLayoutBinding descSetLayoutBinding;
                // memset( &descSetLayoutBinding, 0, sizeof( descSetLayoutBinding ) );

                const size_t bindingIdx = reflBinding.binding;

                bindings[bindingIdx].binding = reflBinding.binding;
                bindings[bindingIdx].descriptorType =
                    static_cast<VkDescriptorType>( reflBinding.descriptor_type );
                bindings[bindingIdx].descriptorCount = 1u;
                for( uint32_t i_dim = 0; i_dim < reflBinding.array.dims_count; ++i_dim )
                    bindings[bindingIdx].descriptorCount *= reflBinding.array.dims[i_dim];
                bindings[bindingIdx].stageFlags =
                    static_cast<VkShaderStageFlagBits>( module.shader_stage );
            }

            ++itor;
        }

        spvReflectDestroyShaderModule( &module );
    }
    //-------------------------------------------------------------------------
    void VulkanDescriptors::generateAndMergeDescriptorSets( VulkanProgram *shader,
                                                            DescriptorSetLayoutBindingArray &outputSets )
    {
        DescriptorSetLayoutBindingArray sets;
        generateDescriptorSets( shader->getName(), shader->getSpirv(), sets );
        if( outputSets.empty() )
            outputSets.swap( sets );
        else
            mergeDescriptorSets( outputSets, shader->getName(), sets );
    }
    //-------------------------------------------------------------------------
    void VulkanDescriptors::optimizeDescriptorSets( DescriptorSetLayoutBindingArray &sets )
    {
        const size_t numSets = sets.size();
        for( size_t i = numSets; i--; )
        {
            const size_t numBindings = sets[i].size();
            for( size_t j = numBindings; j--; )
            {
                if( sets[i][j].descriptorCount == 0 )
                    sets[i].erase( sets[i].begin() + j );
            }
        }
    }
    //-------------------------------------------------------------------------
    VkPipelineLayout VulkanDescriptors::generateVkDescriptorSets(
        const DescriptorSetLayoutBindingArray &bindingSets, DescriptorSetLayoutArray &sets )
    {

        VulkanGpuProgramManager *vulkanProgramManager =
            static_cast<VulkanGpuProgramManager *>( VulkanGpuProgramManager::getSingletonPtr() );

        sets.reserve( bindingSets.size() );

        DescriptorSetLayoutBindingArray::const_iterator itor = bindingSets.begin();
        DescriptorSetLayoutBindingArray::const_iterator endt = bindingSets.end();

        while( itor != endt )
        {
            sets.push_back( vulkanProgramManager->getCachedSet( *itor ) );
            ++itor;
        }

        VkPipelineLayout retVal = vulkanProgramManager->getCachedSets( sets );
        return retVal;
    }

    void VulkanDescriptors::generateVertexInputBindings(
        VulkanProgram *shader, HlmsPso *newPso, VkVertexInputBindingDescription &binding_description,
        std::vector<VkVertexInputAttributeDescription> &attribute_descriptions )
    {
        const std::vector<uint32> &spirv = shader->getSpirv();
        if( spirv.empty() )
            return;

        const String &shaderName = shader->getName();

        SpvReflectShaderModule module;
        memset( &module, 0, sizeof( module ) );
        SpvReflectResult result =
            spvReflectCreateShaderModule( spirv.size() * sizeof( uint32 ), &spirv[0], &module );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectCreateShaderModule failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateVertexInputBindings" );
        }

        uint32_t count = 0;
        result = spvReflectEnumerateInputVariables( &module, &count, NULL );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectCreateShaderModule failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateVertexInputBindings" );
        }

        std::vector<SpvReflectInterfaceVariable *> inputVars( count );
        result = spvReflectEnumerateInputVariables( &module, &count, inputVars.data() );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectCreateShaderModule failed on shader " + shaderName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateVertexInputBindings" );
        }

        
        binding_description.binding = 0;
        binding_description.stride = 0;  // computed below
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        attribute_descriptions.resize(
            inputVars.size(), VkVertexInputAttributeDescription{} );

        for( size_t i_var = 0; i_var < inputVars.size(); ++i_var )
        {
            const SpvReflectInterfaceVariable &refl_var = *( inputVars[i_var] );
            VkVertexInputAttributeDescription &attr_desc = attribute_descriptions[i_var];
            attr_desc.location = refl_var.location;
            attr_desc.binding = binding_description.binding;
            attr_desc.format = static_cast<VkFormat>( refl_var.format );
            attr_desc.offset = 0;  // final offset computed below after sorting.
        }
        // Sort attributes by location
        std::sort(
            std::begin( attribute_descriptions ), std::end( attribute_descriptions ),
            []( const VkVertexInputAttributeDescription &a,
                const VkVertexInputAttributeDescription &b ) { return a.location < b.location; } );
        // Compute final offsets of each attribute, and total vertex stride.
        for( auto &attribute : attribute_descriptions )
        {
            uint32_t format_size = VulkanMappings::getFormatSize( attribute.format );
            attribute.offset = binding_description.stride;
            binding_description.stride += format_size;
        }

        spvReflectDestroyShaderModule( &module );
    }

    //-------------------------------------------------------------------------
}  // namespace Ogre
