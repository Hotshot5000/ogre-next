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
#include "OgreVulkanProgram.h"

#include "OgreLogManager.h"
#include "OgreProfiler.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanGpuProgramManager.h"
#include "OgreVulkanMappings.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreVulkanUtils.h"
#include "SPIRV-Reflect/spirv_reflect.h"

#include "SPIRV/Logger.h"

// Inclusion of SPIRV headers triggers lots of C++11 errors we don't care
namespace glslang
{
    struct SpvOptions
    {
        SpvOptions() :
            generateDebugInfo( false ),
            disableOptimizer( true ),
            optimizeSize( false ),
            disassemble( false ),
            validate( false )
        {
        }
        bool generateDebugInfo;
        bool disableOptimizer;
        bool optimizeSize;
        bool disassemble;
        bool validate;
    };

    void GetSpirvVersion( std::string & );
    int GetSpirvGeneratorVersion();
    void GlslangToSpv( const glslang::TIntermediate &intermediate, std::vector<unsigned int> &spirv,
                       SpvOptions *options = 0 );
    void GlslangToSpv( const glslang::TIntermediate &intermediate, std::vector<unsigned int> &spirv,
                       spv::SpvBuildLogger *logger, SpvOptions *options = 0 );
    void OutputSpvBin( const std::vector<unsigned int> &spirv, const char *baseName );
    void OutputSpvHex( const std::vector<unsigned int> &spirv, const char *baseName,
                       const char *varName );

}  // namespace glslang

namespace Ogre
{
    //-----------------------------------------------------------------------
    VulkanProgram::CmdPreprocessorDefines VulkanProgram::msCmdPreprocessorDefines;
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    VulkanProgram::VulkanProgram( ResourceManager *creator, const String &name, ResourceHandle handle,
                                  const String &group, bool isManual, ManualResourceLoader *loader,
                                  VulkanDevice *device ) :
        HighLevelGpuProgram( creator, name, handle, group, isManual, loader ),
        mDevice( device ),
        mShaderModule( 0 ),
        mCompiled( false ),
        mConstantsBytesToWrite( 0 )
    {
        if( createParamDictionary( "VulkanProgram" ) )
        {
            setupBaseParamDictionary();
            ParamDictionary *dict = getParamDictionary();

            dict->addParameter(
                ParameterDef( "preprocessor_defines", "Preprocessor defines use to compile the program.",
                              PT_STRING ),
                &msCmdPreprocessorDefines );
        }

        // Manually assign language now since we use it immediately
        mSyntaxCode = "glsl-vulkan";
    }
    //---------------------------------------------------------------------------
    VulkanProgram::~VulkanProgram()
    {
        // Have to call this here reather than in Resource destructor
        // since calling virtual methods in base destructors causes crash
        if( isLoaded() )
        {
            unload();
        }
        else
        {
            unloadHighLevel();
        }
    }
    //-----------------------------------------------------------------------
    EShLanguage VulkanProgram::getEshLanguage( void ) const
    {
        switch( mType )
        {
        // clang-format off
        case GPT_VERTEX_PROGRAM:    return EShLangVertex;
        case GPT_FRAGMENT_PROGRAM:  return EShLangFragment;
        case GPT_GEOMETRY_PROGRAM:  return EShLangGeometry;
        case GPT_HULL_PROGRAM:      return EShLangTessControl;
        case GPT_DOMAIN_PROGRAM:    return EShLangTessEvaluation;
        case GPT_COMPUTE_PROGRAM:   return EShLangCompute;
            // clang-format on
        }

        return EShLangFragment;
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::initGlslResources( TBuiltInResource &resources )
    {
        resources.maxLights = 32;
        resources.maxClipPlanes = 6;
        resources.maxTextureUnits = 32;
        resources.maxTextureCoords = 32;
        resources.maxVertexAttribs = 64;
        resources.maxVertexUniformComponents = 4096;
        resources.maxVaryingFloats = 64;
        resources.maxVertexTextureImageUnits = 32;
        resources.maxCombinedTextureImageUnits = 80;
        resources.maxTextureImageUnits = 32;
        resources.maxFragmentUniformComponents = 4096;
        resources.maxDrawBuffers = 32;
        resources.maxVertexUniformVectors = 128;
        resources.maxVaryingVectors = 8;
        resources.maxFragmentUniformVectors = 16;
        resources.maxVertexOutputVectors = 16;
        resources.maxFragmentInputVectors = 15;
        resources.minProgramTexelOffset = -8;
        resources.maxProgramTexelOffset = 7;
        resources.maxClipDistances = 8;
        resources.maxComputeWorkGroupCountX = 65535;
        resources.maxComputeWorkGroupCountY = 65535;
        resources.maxComputeWorkGroupCountZ = 65535;
        resources.maxComputeWorkGroupSizeX = 1024;
        resources.maxComputeWorkGroupSizeY = 1024;
        resources.maxComputeWorkGroupSizeZ = 64;
        resources.maxComputeUniformComponents = 1024;
        resources.maxComputeTextureImageUnits = 16;
        resources.maxComputeImageUniforms = 8;
        resources.maxComputeAtomicCounters = 8;
        resources.maxComputeAtomicCounterBuffers = 1;
        resources.maxVaryingComponents = 60;
        resources.maxVertexOutputComponents = 64;
        resources.maxGeometryInputComponents = 64;
        resources.maxGeometryOutputComponents = 128;
        resources.maxFragmentInputComponents = 128;
        resources.maxImageUnits = 8;
        resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
        resources.maxCombinedShaderOutputResources = 8;
        resources.maxImageSamples = 0;
        resources.maxVertexImageUniforms = 0;
        resources.maxTessControlImageUniforms = 0;
        resources.maxTessEvaluationImageUniforms = 0;
        resources.maxGeometryImageUniforms = 0;
        resources.maxFragmentImageUniforms = 8;
        resources.maxCombinedImageUniforms = 8;
        resources.maxGeometryTextureImageUnits = 16;
        resources.maxGeometryOutputVertices = 256;
        resources.maxGeometryTotalOutputComponents = 1024;
        resources.maxGeometryUniformComponents = 1024;
        resources.maxGeometryVaryingComponents = 64;
        resources.maxTessControlInputComponents = 128;
        resources.maxTessControlOutputComponents = 128;
        resources.maxTessControlTextureImageUnits = 16;
        resources.maxTessControlUniformComponents = 1024;
        resources.maxTessControlTotalOutputComponents = 4096;
        resources.maxTessEvaluationInputComponents = 128;
        resources.maxTessEvaluationOutputComponents = 128;
        resources.maxTessEvaluationTextureImageUnits = 16;
        resources.maxTessEvaluationUniformComponents = 1024;
        resources.maxTessPatchComponents = 120;
        resources.maxPatchVertices = 32;
        resources.maxTessGenLevel = 64;
        resources.maxViewports = 16;
        resources.maxVertexAtomicCounters = 0;
        resources.maxTessControlAtomicCounters = 0;
        resources.maxTessEvaluationAtomicCounters = 0;
        resources.maxGeometryAtomicCounters = 0;
        resources.maxFragmentAtomicCounters = 8;
        resources.maxCombinedAtomicCounters = 8;
        resources.maxAtomicCounterBindings = 1;
        resources.maxVertexAtomicCounterBuffers = 0;
        resources.maxTessControlAtomicCounterBuffers = 0;
        resources.maxTessEvaluationAtomicCounterBuffers = 0;
        resources.maxGeometryAtomicCounterBuffers = 0;
        resources.maxFragmentAtomicCounterBuffers = 1;
        resources.maxCombinedAtomicCounterBuffers = 1;
        resources.maxAtomicCounterBufferSize = 16384;
        resources.maxTransformFeedbackBuffers = 4;
        resources.maxTransformFeedbackInterleavedComponents = 64;
        resources.maxCullDistances = 8;
        resources.maxCombinedClipAndCullDistances = 8;
        resources.maxSamples = 4;
        resources.maxMeshOutputVerticesNV = 256;
        resources.maxMeshOutputPrimitivesNV = 512;
        resources.maxMeshWorkGroupSizeX_NV = 32;
        resources.maxMeshWorkGroupSizeY_NV = 1;
        resources.maxMeshWorkGroupSizeZ_NV = 1;
        resources.maxTaskWorkGroupSizeX_NV = 32;
        resources.maxTaskWorkGroupSizeY_NV = 1;
        resources.maxTaskWorkGroupSizeZ_NV = 1;
        resources.maxMeshViewCountNV = 4;
        resources.limits.nonInductiveForLoops = 1;
        resources.limits.whileLoops = 1;
        resources.limits.doWhileLoops = 1;
        resources.limits.generalUniformIndexing = 1;
        resources.limits.generalAttributeMatrixVectorIndexing = 1;
        resources.limits.generalVaryingIndexing = 1;
        resources.limits.generalSamplerIndexing = 1;
        resources.limits.generalVariableIndexing = 1;
        resources.limits.generalConstantMatrixVectorIndexing = 1;
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::loadFromSource( void ) { compile( true ); }
    //-----------------------------------------------------------------------
    bool VulkanProgram::compile( const bool checkErrors )
    {
        mCompiled = false;
        mCompileError = false;

        const EShLanguage stage = getEshLanguage();
        glslang::TShader shader( stage );

        TBuiltInResource resources;
        memset( &resources, 0, sizeof( resources ) );
        initGlslResources( resources );

        // Enable SPIR-V and Vulkan rules when parsing GLSL
        EShMessages messages = ( EShMessages )( EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules );

        const char *sourceCString = mSource.c_str();
        shader.setStrings( &sourceCString, 1 );

        if( !shader.parse( &resources, 450, false, messages ) )
        {
            LogManager::getSingleton().logMessage( "Vulkan GLSL compiler error in " + mName + ":\n" +
                                                   shader.getInfoLog() + "\nDEBUG LOG:\n" +
                                                   shader.getInfoDebugLog() );
            mCompileError = true;
        }

        // Add shader to new program object.
        glslang::TProgram program;
        if( !mCompileError )
        {
            program.addShader( &shader );

            // Link program.
            if( !program.link( messages ) )
            {
                LogManager::getSingleton().logMessage( "Vulkan GLSL linker error in " + mName + ":\n" +
                                                       program.getInfoLog() + "\nDEBUG LOG:\n" +
                                                       program.getInfoDebugLog() );
                mCompileError = true;
            }
        }

        glslang::TIntermediate *intermediate = 0;
        if( !mCompileError )
        {
            // Save any info log that was generated.
            if( shader.getInfoLog() )
            {
                LogManager::getSingleton().logMessage(
                    "Vulkan GLSL shader messages " + mName + ":\n" + shader.getInfoLog(), LML_TRIVIAL );
            }
            if( program.getInfoLog() )
            {
                LogManager::getSingleton().logMessage(
                    "Vulkan GLSL linker messages " + mName + ":\n" + program.getInfoLog(), LML_TRIVIAL );
            }

            intermediate = program.getIntermediate( stage );

            // Translate to SPIRV.
            if( !intermediate )
            {
                LogManager::getSingleton().logMessage( "Vulkan GLSL failed to get intermediate code " +
                                                       mName );
                mCompileError = true;
            }
        }

        mSpirv.clear();

        if( !mCompileError )
        {
            spv::SpvBuildLogger logger;
            glslang::GlslangToSpv( *intermediate, mSpirv, &logger );

            LogManager::getSingleton().logMessage(
                "Vulkan GLSL to SPIRV " + mName + ":\n" + logger.getAllMessages(), LML_TRIVIAL );
        }

        mCompiled = !mCompileError;

        if( !mCompileError )
            LogManager::getSingleton().logMessage( "Shader " + mName + " compiled successfully." );

        if( !mCompiled && checkErrors )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         ( ( mType == GPT_VERTEX_PROGRAM ) ? "Vertex Program " : "Fragment Program " ) +
                             mName + " failed to compile. See compile log above for details.",
                         "VulkanProgram::compile" );
        }

        if( mCompiled && !mSpirv.empty() )
        {
            VkShaderModuleCreateInfo moduleCi;
            makeVkStruct( moduleCi, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO );
            moduleCi.codeSize = mSpirv.size() * sizeof( uint32 );
            moduleCi.pCode = &mSpirv[0];
            VkResult result = vkCreateShaderModule( mDevice->mDevice, &moduleCi, 0, &mShaderModule );
            checkVkResult( result, "vkCreateShaderModule" );
        }

        return mCompiled;
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::createLowLevelImpl( void )
    {
        mAssemblerProgram = GpuProgramPtr( this, SPFM_NONE );
        if( !mCompiled )
            compile( true );
    }
    //---------------------------------------------------------------------------
    void VulkanProgram::unloadImpl()
    {
        // We didn't create mAssemblerProgram through a manager, so override this
        // implementation so that we don't try to remove it from one. Since getCreator()
        // is used, it might target a different matching handle!
        mAssemblerProgram.setNull();

        unloadHighLevel();
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::unloadHighLevelImpl( void )
    {
        // Release everything
        mCompiled = false;

        mSpirv.clear();
        if( mShaderModule )
        {
            vkDestroyShaderModule( mDevice->mDevice, mShaderModule, 0 );
            mShaderModule = 0;
        }
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::populateParameterNames( GpuProgramParametersSharedPtr params )
    {
        getConstantDefinitions();
        params->_setNamedConstants( mConstantDefs );
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::buildConstantDefinitions( void ) const
    {
        OgreProfileExhaustive( "VulkanProgram::buildConstantDefinitions" );

        // if( !mBuildParametersFromReflection )
        //     return;

        if( mCompileError )
            return;

        if( mSpirv.empty() )
            return;

        SpvReflectShaderModule module;
        memset( &module, 0, sizeof( module ) );
        SpvReflectResult result =
            spvReflectCreateShaderModule( mSpirv.size() * sizeof( uint32 ), &mSpirv[0], &module );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectCreateShaderModule failed on shader " + mName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        uint32 numDescSets = 0;
        result = spvReflectEnumerateDescriptorSets( &module, &numDescSets, 0 );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectEnumerateDescriptorSets failed on shader " + mName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        FastArray<SpvReflectDescriptorSet *> sets;
        sets.resize( numDescSets );
        result = spvReflectEnumerateDescriptorSets( &module, &numDescSets, sets.begin() );
        if( result != SPV_REFLECT_RESULT_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "spvReflectEnumerateDescriptorSets failed on shader " + mName +
                             " error code: " + getSpirvReflectError( result ),
                         "VulkanDescriptors::generateDescriptorSet" );
        }

        // const_cast to get around the fact that buildConstantDefinitions() is const.
        VulkanProgram *vp = const_cast<VulkanProgram *>( this );

        size_t numSets = 0u;
        FastArray<SpvReflectDescriptorSet *>::const_iterator itor = sets.begin();
        FastArray<SpvReflectDescriptorSet *>::const_iterator endt = sets.end();

        while( itor != endt )
        {
            const SpvReflectDescriptorSet &reflSet = **itor;
            const size_t numUsedBindings = reflSet.binding_count;

            size_t numBindings = 0;
            for( size_t i = 0; i < numUsedBindings; ++i )
                numBindings = std::max<size_t>( reflSet.bindings[i]->binding + 1u, numBindings );

            // VulkanConstantDefinitionBindingParam prevBindingParam;
            // prevBindingParam.offset = 0;
            // prevBindingParam.size = 0;
            size_t prevSize = 0;

            for( size_t bindingPos = 0; bindingPos < numUsedBindings; ++bindingPos )
            {
                const SpvReflectDescriptorBinding &reflBinding = *( reflSet.bindings[bindingPos] );

                if( reflBinding.binding != OGRE_VULKAN_PARAMETER_SLOT )
                    continue;

                const VkDescriptorType type =
                    static_cast<VkDescriptorType>( reflBinding.descriptor_type );
                if( type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER )
                {
                    for( uint32 memberPos = 0; memberPos < reflBinding.block.member_count; ++memberPos )
                    {
                        const SpvReflectBlockVariable &blockVariable =
                            reflBinding.block.members[memberPos];
                        GpuConstantType constantType =
                            VulkanMappings::get( blockVariable.type_description->op );
                        if( constantType == GCT_MATRIX_4X4 )
                        {
                            const uint32_t rowCount = blockVariable.numeric.matrix.row_count;
                            const uint32_t columnCount = blockVariable.numeric.matrix.column_count;

                            if( rowCount == 2 && columnCount == 2 )
                                constantType = GCT_MATRIX_2X2;
                            else if( rowCount == 2 && columnCount == 3 )
                                constantType = GCT_MATRIX_2X3;
                            else if( rowCount == 2 && columnCount == 4 )
                                constantType = GCT_MATRIX_2X4;
                            else if( rowCount == 3 && columnCount == 2 )
                                constantType = GCT_MATRIX_3X2;
                            else if( rowCount == 3 && columnCount == 3 )
                                constantType = GCT_MATRIX_3X3;
                            else if( rowCount == 3 && columnCount == 4 )
                                constantType = GCT_MATRIX_3X4;
                            else if( rowCount == 4 && columnCount == 2 )
                                constantType = GCT_MATRIX_4X2;
                            else if( rowCount == 4 && columnCount == 3 )
                                constantType = GCT_MATRIX_4X3;
                            else if( rowCount == 4 && columnCount == 4 )
                                constantType = GCT_MATRIX_4X4;
                        }
                        else if( blockVariable.type_description->type_flags &
                                 SPV_REFLECT_TYPE_FLAG_VECTOR )
                        {
                            const uint32 componentCount = blockVariable.numeric.vector.component_count;
                            if( blockVariable.type_description->type_flags &
                                SPV_REFLECT_TYPE_FLAG_FLOAT )
                            {
                                switch( componentCount )
                                {
                                case 1:
                                    constantType = GCT_FLOAT1;
                                    break;
                                case 2:
                                    constantType = GCT_FLOAT2;
                                    break;
                                case 3:
                                    constantType = GCT_FLOAT3;
                                    break;
                                case 4:
                                    constantType = GCT_FLOAT4;
                                    break;
                                default:
                                    OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                                                 "invalid component count for float vector",
                                                 "VulkanProgram::buildConstantDefinitions" );
                                }
                            }
                            else if( blockVariable.type_description->type_flags &
                                     SPV_REFLECT_TYPE_FLAG_INT )
                            {
                                switch( componentCount )
                                {
                                case 1:
                                    constantType = GCT_INT1;
                                    break;
                                case 2:
                                    constantType = GCT_INT2;
                                    break;
                                case 3:
                                    constantType = GCT_INT3;
                                    break;
                                case 4:
                                    constantType = GCT_INT4;
                                    break;
                                default:
                                    OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                                                 "invalid component count for int vector",
                                                 "VulkanProgram::buildConstantDefinitions" );
                                }
                            }
                        }
                        else if( blockVariable.type_description->type_flags &
                                 SPV_REFLECT_TYPE_FLAG_STRUCT )
                        {
                            continue;
                        }

                        GpuConstantDefinition def;
                        def.constType = constantType;
                        def.logicalIndex = prevSize;  // blockVariable.offset;
                        // def.physicalIndex = blockVariable.offset;
                        if( blockVariable.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_ARRAY )
                        {
                            def.elementSize = blockVariable.array.stride / sizeof( float );
                            def.arraySize = blockVariable.array.dims_count;
                        }
                        else
                        {
                            def.elementSize =
                                GpuConstantDefinition::getElementSize( def.constType, false );
                            def.arraySize = 1;
                        }
                        def.variability = GPV_GLOBAL;

                        if( def.isFloat() )
                        {
                            def.physicalIndex = mFloatLogicalToPhysical->bufferSize;
                            OGRE_LOCK_MUTEX( mFloatLogicalToPhysical->mutex );
                            mFloatLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                                def.logicalIndex,
                                GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                    GPV_GLOBAL ) ) );
                            mFloatLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                            mConstantDefs->floatBufferSize = mFloatLogicalToPhysical->bufferSize;
                        }
                        else if( def.isUnsignedInt() )
                        {
                            def.physicalIndex = mUIntLogicalToPhysical->bufferSize;
                            OGRE_LOCK_MUTEX( mUIntLogicalToPhysical->mutex );
                            mUIntLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                                def.logicalIndex,
                                GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                    GPV_GLOBAL ) ) );
                            mUIntLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                            mConstantDefs->uintBufferSize = mUIntLogicalToPhysical->bufferSize;
                        }
                        else
                        {
                            def.physicalIndex = mIntLogicalToPhysical->bufferSize;
                            OGRE_LOCK_MUTEX( mIntLogicalToPhysical->mutex );
                            mIntLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                                def.logicalIndex,
                                GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                    GPV_GLOBAL ) ) );
                            mIntLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                            mConstantDefs->intBufferSize = mIntLogicalToPhysical->bufferSize;
                        }

                        String varName = blockVariable.name;
                        if( blockVariable.array.dims_count )
                            vp->mConstantDefs->generateConstantDefinitionArrayEntries( varName, def );

                        mConstantDefs->map.insert(
                            GpuConstantDefinitionMap::value_type( varName, def ) );
                        vp->mConstantDefsSorted.push_back( def );

                        vp->mConstantsBytesToWrite = std::max<uint32>(
                            vp->mConstantsBytesToWrite,
                            def.logicalIndex + def.arraySize * def.elementSize * sizeof( float ) );
                    }

                    VulkanConstantDefinitionBindingParam bindingParam;
                    bindingParam.offset = reflBinding.block.offset;
                    bindingParam.size = reflBinding.block.size;
                    if( vp->mConstantDefsBindingParams.find( reflBinding.binding ) ==
                        vp->getConstantDefsBindingParams().end() )
                    {
                        prevSize += alignMemory(
                            reflBinding.block.size,
                            mDevice->mDeviceProperties.limits.minUniformBufferOffsetAlignment );
                    }

                    vp->mConstantDefsBindingParams.insert(
                        unordered_map<uint32, VulkanConstantDefinitionBindingParam>::type::value_type(
                            reflBinding.binding, bindingParam ) );

                    // prevBindingParam.offset = bindingParam.offset;
                    // prevBindingParam.size = bindingParam.size;
                }
                else
                {
                    GpuConstantDefinition def;
                    if( type == VK_DESCRIPTOR_TYPE_SAMPLER )
                    {
                        def.constType = GCT_SAMPLER2D;
                    }
                    else if( type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER )
                    {
                        def.constType = GCT_SAMPLER1D;
                    }
                    else if( type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER )
                    {
                        def.constType = GCT_SAMPLER2D;
                    }
                    def.arraySize = 1;
                    def.logicalIndex = prevSize;
                    // if( prevBindingParam.size != 0 )
                    // {
                    //     def.logicalIndex = alignMemory( prevBindingParam.size,
                    //     mDevice->mDeviceProperties.limits.minUniformBufferOffsetAlignment );
                    //     prevBindingParam.offset = 0;
                    //     prevBindingParam.size = 0;
                    // }
                    // def.physicalIndex = 0;
                    def.elementSize = 1;
                    GpuNamedConstants &defs = *mConstantDefs.get();
                    if( def.isFloat() )
                    {
                        def.physicalIndex = mFloatLogicalToPhysical->bufferSize;
                        OGRE_LOCK_MUTEX( mFloatLogicalToPhysical->mutex );
                        mFloatLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                            def.logicalIndex,
                            GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                GPV_GLOBAL ) ) );
                        mFloatLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                        mConstantDefs->floatBufferSize = mFloatLogicalToPhysical->bufferSize;
                    }
                    else if( def.isUnsignedInt() )
                    {
                        def.physicalIndex = mUIntLogicalToPhysical->bufferSize;
                        OGRE_LOCK_MUTEX( mUIntLogicalToPhysical->mutex );
                        mUIntLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                            def.logicalIndex,
                            GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                GPV_GLOBAL ) ) );
                        mUIntLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                        mConstantDefs->uintBufferSize = mUIntLogicalToPhysical->bufferSize;
                    }
                    else
                    {
                        def.physicalIndex = mIntLogicalToPhysical->bufferSize;
                        OGRE_LOCK_MUTEX( mIntLogicalToPhysical->mutex );
                        mIntLogicalToPhysical->map.insert( GpuLogicalIndexUseMap::value_type(
                            def.logicalIndex,
                            GpuLogicalIndexUse( def.physicalIndex, def.arraySize * def.elementSize,
                                                GPV_GLOBAL ) ) );
                        mIntLogicalToPhysical->bufferSize += def.arraySize * def.elementSize;
                        mConstantDefs->intBufferSize = mIntLogicalToPhysical->bufferSize;
                    }
                    // if( def.isFloat() )
                    // {
                    //     def.physicalIndex = defs.floatBufferSize;
                    //     defs.floatBufferSize += def.arraySize * def.elementSize;
                    // }
                    // else if( def.isDouble() )
                    // {
                    //     def.physicalIndex = defs.doubleBufferSize;
                    //     defs.doubleBufferSize += def.arraySize * def.elementSize;
                    // }
                    // else if( def.isInt() || def.isSampler() )
                    // {
                    //     def.physicalIndex = defs.intBufferSize;
                    //     defs.intBufferSize += def.arraySize * def.elementSize;
                    // }
                    // else if( def.isUnsignedInt() || def.isBool() )
                    // {
                    //     def.physicalIndex = defs.uintBufferSize;
                    //     defs.uintBufferSize += def.arraySize * def.elementSize;
                    // }
                    String varName( reflBinding.name );
                    if( varName.empty() )
                        varName = reflBinding.type_description->type_name;
                    if( reflBinding.array.dims_count > 0 )
                        vp->mConstantDefs->generateConstantDefinitionArrayEntries( varName, def );
                    mConstantDefs->map.insert( GpuConstantDefinitionMap::value_type( varName, def ) );

                    vp->mConstantDefsSorted.push_back( def );

                    vp->mConstantsBytesToWrite = std::max<uint32>(
                        vp->mConstantsBytesToWrite,
                        def.physicalIndex + def.arraySize * def.elementSize * sizeof( float ) );

                    VulkanConstantDefinitionBindingParam bindingParam;
                    bindingParam.offset = def.logicalIndex;
                    bindingParam.size = def.arraySize * def.elementSize;
                    prevSize += bindingParam.size;

                    vp->mConstantDefsBindingParams.insert(
                        unordered_map<uint32, VulkanConstantDefinitionBindingParam>::type::value_type(
                            reflBinding.binding, bindingParam ) );
                }
            }

            ++itor;
        }

        spvReflectDestroyShaderModule( &module );
    }
    //-----------------------------------------------------------------------
    static VkShaderStageFlagBits get( GpuProgramType programType )
    {
        switch( programType )
        {
        // clang-format off
        case GPT_VERTEX_PROGRAM:    return VK_SHADER_STAGE_VERTEX_BIT;
        case GPT_FRAGMENT_PROGRAM:  return VK_SHADER_STAGE_FRAGMENT_BIT;
        case GPT_GEOMETRY_PROGRAM:  return VK_SHADER_STAGE_GEOMETRY_BIT;
        case GPT_HULL_PROGRAM:      return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case GPT_DOMAIN_PROGRAM:    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case GPT_COMPUTE_PROGRAM:   return VK_SHADER_STAGE_COMPUTE_BIT;
            // clang-format on
        }
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    void VulkanProgram::fillPipelineShaderStageCi( VkPipelineShaderStageCreateInfo &pssCi )
    {
        makeVkStruct( pssCi, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO );
        pssCi.stage = get( mType );
        pssCi.module = mShaderModule;
        pssCi.pName = "main";
    }
    //-----------------------------------------------------------------------
    uint32 VulkanProgram::getBufferRequiredSize( void ) const { return mConstantsBytesToWrite; }
    //-----------------------------------------------------------------------
    void VulkanProgram::updateBuffers( const GpuProgramParametersSharedPtr &params,
                                       uint8 *RESTRICT_ALIAS dstData )
    {
        vector<GpuConstantDefinition>::type::const_iterator itor = mConstantDefsSorted.begin();
        vector<GpuConstantDefinition>::type::const_iterator endt = mConstantDefsSorted.end();

        while( itor != endt )
        {
            const GpuConstantDefinition &def = *itor;

            void *RESTRICT_ALIAS src;
            if( def.isFloat() )
                src = (void *)&( *( params->getFloatConstantList().begin() + def.physicalIndex ) );
            else if( def.isUnsignedInt() )
                src = (void *)&( *( params->getUnsignedIntConstantList().begin() + def.physicalIndex ) );
            else
                src = (void *)&( *( params->getIntConstantList().begin() + def.physicalIndex ) );

            memcpy( &dstData[def.logicalIndex], src, def.elementSize * def.arraySize * sizeof( float ) );

            ++itor;
        }
    }
    //---------------------------------------------------------------------
    inline bool VulkanProgram::getPassSurfaceAndLightStates( void ) const
    {
        // Scenemanager should pass on light & material state to the rendersystem
        return true;
    }
    //---------------------------------------------------------------------
    inline bool VulkanProgram::getPassTransformStates( void ) const
    {
        // Scenemanager should pass on transform state to the rendersystem
        return true;
    }
    //---------------------------------------------------------------------
    inline bool VulkanProgram::getPassFogStates( void ) const
    {
        // Scenemanager should pass on fog state to the rendersystem
        return true;
    }
    //-----------------------------------------------------------------------
    String VulkanProgram::CmdPreprocessorDefines::doGet( const void *target ) const
    {
        return static_cast<const VulkanProgram *>( target )->getPreprocessorDefines();
    }
    //-----------------------------------------------------------------------
    void VulkanProgram::CmdPreprocessorDefines::doSet( void *target, const String &val )
    {
        static_cast<VulkanProgram *>( target )->setPreprocessorDefines( val );
    }
    //-----------------------------------------------------------------------
    const String &VulkanProgram::getLanguage( void ) const
    {
        static const String language = "glsl";
        return language;
    }
    //-----------------------------------------------------------------------
    GpuProgramParametersSharedPtr VulkanProgram::createParameters( void )
    {
        GpuProgramParametersSharedPtr params = HighLevelGpuProgram::createParameters();
        params->setTransposeMatrices( true );
        return params;
    }
    //-----------------------------------------------------------------------
}  // namespace Ogre
