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

#include "OgreStableHeaders.h"

#include "PathTracing/OgrePathTracer.h"
#include "RTShadows/OgreRTShadowsMeshCache.h"

#include "Compositor/OgreCompositorNode.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Vao/OgreAsyncTicket.h"
#include "OgreHlmsCompute.h"
#include "OgreHlmsComputeJob.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreItem.h"
#include "OgreLight.h"
#include "OgreLogManager.h"
#include "OgreMesh2.h"
#include "OgreRenderSystem.h"
#include "OgrePixelFormatGpuUtils.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreSceneManager.h"
#include "OgreSubItem.h"
#include "OgreSubMesh2.h"
#include "OgreTextureGpu.h"
#include "Vao/OgreConstBufferPacked.h"
#include "Vao/OgreIndexBufferPacked.h"
#include "Vao/OgreVertexArrayObject.h"
#include "Vao/OgreReadOnlyBufferPacked.h"
#include "Vao/OgreVaoManager.h"

namespace Ogre
{
    namespace
    {
        struct PathTracerFrameGpu
        {
            float invProjectionMat[16];
            float invViewMat[16];
            float invViewProjMat[16];
            float cameraCorner[16];
            float cameraPos[4];
            float cameraRight[4];
            float cameraUp[4];
            float cameraFront[4];
            float skyZenith[4];
            float skyHorizon[4];
            float projectionParams[2];
            float width;
            float height;
            uint32 sampleIndex;
            uint32 maxBounces;
            uint32 numLights;
            uint32 flags;
        };

        struct PathTracerLightGpu
        {
            float position[4];
            float diffuse[4];
            float specular[4];
            float attenuation[4];
            float spotDirection[4];
            float spotParams[4];
        };

        struct PathTracerMaterialGpu
        {
            float baseColour_roughness[4];
            float fresnel_transparency[4];
            float emissive_flags[4];
            float diffuseTextureIdx_slice_hasTexture[4];
            float diffuseUvOffsetScale[4];
            float roughnessTextureIdx_slice_hasTexture[4];
            float normalTextureIdx_slice_hasTexture[4];
            float emissiveTextureIdx_slice_hasTexture[4];
            float reflectionTextureIdx_slice_hasTexture[4];
        };

        const size_t MaxPathTracerMaterialTextures = 8u;
        const size_t MaxPathTracerReflectionTextures = 4u;
        const size_t PathTracerDiffuseTextureSlotStart = 10u;
        const size_t PathTracerRoughnessTextureSlotStart =
            PathTracerDiffuseTextureSlotStart + MaxPathTracerMaterialTextures;
        const size_t PathTracerNormalTextureSlotStart =
            PathTracerRoughnessTextureSlotStart + MaxPathTracerMaterialTextures;
        const size_t PathTracerEmissiveTextureSlotStart =
            PathTracerNormalTextureSlotStart + MaxPathTracerMaterialTextures;
        const size_t PathTracerReflectionTextureSlotStart =
            PathTracerEmissiveTextureSlotStart + MaxPathTracerMaterialTextures;

        struct PathTracerGeometryGpu
        {
            float material_subMesh[4];
        };

        struct PathTracerTriangleGpu
        {
            float uv0_uv1[4];
            float uv2_normalX_normalY[4];
            float normalZ_flags[4];
            float tangent[4];
            float bitangent[4];
        };

        void copyMatrix( float *dst, const Matrix4 &src )
        {
            memcpy( dst, &src, sizeof( float ) * 16u );
        }

        bool matricesDiffer( const Matrix4 &a, const Matrix4 &b )
        {
            const Real epsilon = Real( 1e-6 );
            for( size_t row = 0u; row < 4u; ++row )
            {
                for( size_t col = 0u; col < 4u; ++col )
                {
                    if( Math::Abs( a[row][col] - b[row][col] ) > epsilon )
                        return true;
                }
            }
            return false;
        }

        float readFloatComponent( const char *data, VertexElementType type, size_t componentIdx )
        {
            switch( type )
            {
            case VET_FLOAT1:
            case VET_FLOAT2:
            case VET_FLOAT3:
            case VET_FLOAT4:
                return reinterpret_cast<const float *>( data )[componentIdx];
            case VET_HALF2:
            case VET_HALF4:
                return Bitwise::halfToFloat( reinterpret_cast<const uint16 *>( data )[componentIdx] );
            case VET_SHORT2_SNORM:
            case VET_SHORT4_SNORM:
                return std::max( -1.0f, reinterpret_cast<const int16 *>( data )[componentIdx] / 32767.0f );
            case VET_UBYTE4_NORM:
                return reinterpret_cast<const uint8 *>( data )[componentIdx] / 255.0f;
            case VET_BYTE4_SNORM:
                return std::max( -1.0f, reinterpret_cast<const int8 *>( data )[componentIdx] / 127.0f );
            default:
                return 0.0f;
            }
        }

        void readFloat2At( const VertexArrayObject::ReadRequests &request, size_t vertexIdx, float *dst )
        {
            const char *data = request.data + vertexIdx * request.vertexBuffer->getBytesPerElement();
            dst[0] = readFloatComponent( data, request.type, 0u );
            dst[1] = readFloatComponent( data, request.type, 1u );
        }

        Vector3 readFloat3At( const VertexArrayObject::ReadRequests &request, size_t vertexIdx )
        {
            const char *data = request.data + vertexIdx * request.vertexBuffer->getBytesPerElement();
            return Vector3( readFloatComponent( data, request.type, 0u ),
                            readFloatComponent( data, request.type, 1u ),
                            readFloatComponent( data, request.type, 2u ) );
        }

        Vector3 readNormalAt( const VertexArrayObject::ReadRequests &request, size_t vertexIdx )
        {
            Vector3 normal = readFloat3At( request, vertexIdx );
            normal.normalise();
            return normal;
        }

        int addTextureToList( TextureGpu *texture, FastArray<TextureGpu *> &textures, size_t maxTextures )
        {
            if( !texture )
                return -1;

            FastArray<TextureGpu *>::const_iterator texIt =
                std::find( textures.begin(), textures.end(), texture );
            if( texIt == textures.end() && textures.size() < maxTextures )
            {
                textures.push_back( texture );
                texIt = textures.end() - 1;
            }

            if( texIt == textures.end() )
                return -1;

            return static_cast<int>( texIt - textures.begin() );
        }

        uint32 readIndexAt( const uint8 *indexData, IndexBufferPacked *indexBuffer, size_t indexIdx )
        {
            if( indexBuffer->getIndexType() == IndexBufferPacked::IT_16BIT )
                return reinterpret_cast<const uint16 *>( indexData )[indexIdx];
            return reinterpret_cast<const uint32 *>( indexData )[indexIdx];
        }

        void addLight( PathTracerLightGpu *dst, Light *light )
        {
            const ColourValue diffuseColour = light->getDiffuseColour() * light->getPowerScale();
            const ColourValue specularColour = light->getSpecularColour() * light->getPowerScale();
            for( size_t i = 0u; i < 3u; ++i )
            {
                dst->diffuse[i] = static_cast<float>( diffuseColour[i] );
                dst->specular[i] = static_cast<float>( specularColour[i] );
            }

            if( light->getType() == Light::LT_DIRECTIONAL )
            {
                const Vector3 lightDirection = -light->getDerivedDirectionUpdated();
                dst->position[0] = static_cast<float>( lightDirection.x );
                dst->position[1] = static_cast<float>( lightDirection.y );
                dst->position[2] = static_cast<float>( lightDirection.z );
                dst->position[3] = 0.0f;
            }
            else
            {
                const Vector3 lightPosition = light->getParentNode()->_getDerivedPositionUpdated();
                dst->position[0] = static_cast<float>( lightPosition.x );
                dst->position[1] = static_cast<float>( lightPosition.y );
                dst->position[2] = static_cast<float>( lightPosition.z );
                dst->position[3] = 1.0f;
            }

            dst->attenuation[0] = static_cast<float>( light->getAttenuationRange() );
            dst->attenuation[1] = static_cast<float>( light->getAttenuationLinear() );
            dst->attenuation[2] = static_cast<float>( light->getAttenuationQuadric() );

            const Vector3 spotDirection = light->getDerivedDirectionUpdated();
            dst->spotDirection[0] = static_cast<float>( spotDirection.x );
            dst->spotDirection[1] = static_cast<float>( spotDirection.y );
            dst->spotDirection[2] = static_cast<float>( spotDirection.z );
            dst->spotDirection[3] = static_cast<float>( light->getLightProfileIdx() );

            const Real cosInner = Math::Cos( light->getSpotlightInnerAngle() * 0.5f );
            const Real cosOuter = Math::Cos( light->getSpotlightOuterAngle() * 0.5f );
            const Real invCosDiff = 1.0f / std::max<Real>( cosInner - cosOuter, 1e-4f );
            dst->spotParams[0] = static_cast<float>( invCosDiff );
            dst->spotParams[1] = static_cast<float>( cosOuter );
            dst->spotParams[2] = static_cast<float>( light->getSpotlightFalloff() );
            dst->spotParams[3] = static_cast<float>( light->getType() );
        }
    }

    PathTracer::PathTracer( TextureGpu *renderWindow, RenderSystem *renderSystem,
                            HlmsManager *hlmsManager, Camera *camera,
                            CompositorWorkspace *workspace ) :
        mRenderWindow( renderWindow ),
        mRenderSystem( renderSystem ),
        mVaoManager( renderSystem ? renderSystem->getVaoManager() : 0 ),
        mHlmsManager( hlmsManager ),
        mCamera( camera ),
        mWorkspace( workspace ),
        mMeshCache( 0 ),
        mTraceJob( 0 ),
        mAccumulationTexture( 0 ),
        mRadianceTexture( 0 ),
        mFrameConstBuffer( 0 ),
        mLightsConstBuffer( 0 ),
        mMaterialBuffer( 0 ),
        mGeometryBuffer( 0 ),
        mTriangleBuffer( 0 ),
        mSampleCount( 0u ),
        mHasLastCameraState( false ),
        mEnabled( false ),
        mInitialized( false )
    {
        initResources();
    }
    //-------------------------------------------------------------------------
    PathTracer::~PathTracer()
    {
        destroyResources();
    }
    //-------------------------------------------------------------------------
    void PathTracer::initResources()
    {
        if( !mRenderSystem || !mVaoManager || !mHlmsManager || !mWorkspace )
            return;

        const bool rayTracingSupported =
            mRenderSystem->getCapabilities()->hasCapability( RSC_RAY_TRACING );
        if( !rayTracingSupported )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: active RenderSystem does not support ray tracing acceleration structures." );
            return;
        }

        HlmsCompute *hlmsCompute = mHlmsManager->getComputeHlms();
        mTraceJob = hlmsCompute->findComputeJobNoThrow( "PathTracing/TraceJob" );
        if( !mTraceJob )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: could not find PathTracing/TraceJob." );
            return;
        }
        mTraceJob->setNumSamplerUnits( 16u );
        mTraceJob->setNumThreadGroupsBasedOn( HlmsComputeJob::ThreadGroupsBasedOnNothing, 0u, 1u,
                                              1u, 1u );

        CompositorNode *pathTracerNode = mWorkspace->findNode( "PathTracerRenderingNode" );
        if( !pathTracerNode )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: PathTracerRenderingNode is missing from the workspace." );
            return;
        }

        mAccumulationTexture = pathTracerNode->getDefinedTexture( "accumulationTexture" );
        mRadianceTexture = pathTracerNode->getDefinedTexture( "radianceTexture" );
        if( !mAccumulationTexture || !mRadianceTexture )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: compositor UAV textures are missing." );
            return;
        }

        mFrameConstBuffer = mVaoManager->createConstBuffer( sizeof( PathTracerFrameGpu ),
                                                            BT_DYNAMIC_PERSISTENT, 0, false );
        mLightsConstBuffer = mVaoManager->createConstBuffer( sizeof( PathTracerLightGpu ) * 16u,
                                                             BT_DYNAMIC_PERSISTENT, 0, false );

        mMeshCache = new RTShadowsMeshCache();
        mMeshCache->setEnabled( true );
        mInitialized = true;
    }
    //-------------------------------------------------------------------------
    void PathTracer::destroyResources()
    {
        if( mFrameConstBuffer )
        {
            if( mFrameConstBuffer->getMappingState() != MS_UNMAPPED )
                mFrameConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mFrameConstBuffer );
            mFrameConstBuffer = 0;
        }

        if( mLightsConstBuffer )
        {
            if( mLightsConstBuffer->getMappingState() != MS_UNMAPPED )
                mLightsConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mLightsConstBuffer );
            mLightsConstBuffer = 0;
        }

        if( mMaterialBuffer )
        {
            if( mMaterialBuffer->getMappingState() != MS_UNMAPPED )
                mMaterialBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyReadOnlyBuffer( mMaterialBuffer );
            mMaterialBuffer = 0;
        }

        if( mGeometryBuffer )
        {
            if( mGeometryBuffer->getMappingState() != MS_UNMAPPED )
                mGeometryBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyReadOnlyBuffer( mGeometryBuffer );
            mGeometryBuffer = 0;
        }

        if( mTriangleBuffer )
        {
            if( mTriangleBuffer->getMappingState() != MS_UNMAPPED )
                mTriangleBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyReadOnlyBuffer( mTriangleBuffer );
            mTriangleBuffer = 0;
        }

        delete mMeshCache;
        mMeshCache = 0;
    }
    //-------------------------------------------------------------------------
    void PathTracer::setEnabled( bool enabled )
    {
        enabled = enabled && mInitialized;
        if( mEnabled == enabled )
            return;

        mEnabled = enabled;
        mScene.setEnabled( enabled );
        resetAccumulation();
    }
    //-------------------------------------------------------------------------
    void PathTracer::resetAccumulation()
    {
        mSampleCount = 0u;
    }
    //-------------------------------------------------------------------------
    void PathTracer::updateAccelerationStructure()
    {
        if( !mMeshCache )
            return;

        if( mScene.needsBlasRebuild() || mScene.needsTlasRebuild() )
        {
            mMeshCache->removeAllItems();

            const PathTracerScene::ItemArray &items = mScene.getItems();
            PathTracerScene::ItemArray::const_iterator itor = items.begin();
            PathTracerScene::ItemArray::const_iterator end = items.end();
            while( itor != end )
            {
                Item *item = *itor;
                mMeshCache->addMeshToCache( item->getMesh(), item );
                ++itor;
            }
        }

        mMeshCache->updateAS();
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadFrameConstants( uint32 numLights )
    {
        PathTracerFrameGpu *frame = reinterpret_cast<PathTracerFrameGpu *>(
            mFrameConstBuffer->map( 0, mFrameConstBuffer->getNumElements() ) );
        memset( frame, 0, sizeof( PathTracerFrameGpu ) );

        const Matrix4 projMat = mCamera->getProjectionMatrix();
        const Matrix4 viewMat = mCamera->getViewMatrix( true );
        const Matrix4 viewProj = mCamera->getProjectionMatrixWithRSDepth() * viewMat;
        const Matrix4 invProjMat = projMat.inverse();
        const Matrix4 invViewMat = viewMat.inverse();
        const Matrix4 invViewProj = viewProj.inverse();

        copyMatrix( frame->invProjectionMat, invProjMat );
        copyMatrix( frame->invViewMat, invViewMat );
        copyMatrix( frame->invViewProjMat, invViewProj );

        const Vector3 cameraPos = mCamera->getDerivedPosition();
        const Vector3 *corners = mCamera->getWorldSpaceCorners();
        const Vector3 cameraDirs[4] = { corners[5] - cameraPos, corners[6] - cameraPos,
                                        corners[4] - cameraPos, corners[7] - cameraPos };

        for( size_t i = 0u; i < 4u; ++i )
        {
            frame->cameraCorner[i * 4u + 0u] = cameraDirs[i].x;
            frame->cameraCorner[i * 4u + 1u] = cameraDirs[i].y;
            frame->cameraCorner[i * 4u + 2u] = cameraDirs[i].z;
            frame->cameraCorner[i * 4u + 3u] = 1.0f;
        }

        frame->cameraPos[0] = cameraPos.x;
        frame->cameraPos[1] = cameraPos.y;
        frame->cameraPos[2] = cameraPos.z;
        frame->cameraPos[3] = 1.0f;

        const Vector3 cameraRight = mCamera->getRight();
        const Vector3 cameraUp = mCamera->getUp();
        const Vector3 cameraFront = mCamera->getDirection();
        frame->cameraRight[0] = cameraRight.x;
        frame->cameraRight[1] = cameraRight.y;
        frame->cameraRight[2] = cameraRight.z;
        frame->cameraUp[0] = cameraUp.x;
        frame->cameraUp[1] = cameraUp.y;
        frame->cameraUp[2] = cameraUp.z;
        frame->cameraFront[0] = cameraFront.x;
        frame->cameraFront[1] = cameraFront.y;
        frame->cameraFront[2] = cameraFront.z;

        frame->skyZenith[0] = 0.72f;
        frame->skyZenith[1] = 0.74f;
        frame->skyZenith[2] = 0.68f;
        frame->skyZenith[3] = 1.0f;
        frame->skyHorizon[0] = 1.18f;
        frame->skyHorizon[1] = 1.05f;
        frame->skyHorizon[2] = 0.62f;
        frame->skyHorizon[3] = 1.0f;

        Vector2 projectionAB = mCamera->getProjectionParamsAB();
        projectionAB.y /= mCamera->getFarClipDistance();
        frame->projectionParams[0] = projectionAB.x;
        frame->projectionParams[1] = projectionAB.y;
        // Match the compute shader's bounds and UV normalization to the UAV that drives dispatch.
        frame->width = static_cast<float>( mRadianceTexture->getWidth() );
        frame->height = static_cast<float>( mRadianceTexture->getHeight() );
        frame->sampleIndex = mSampleCount;
        frame->maxBounces = 4u;
        frame->numLights = numLights;
        frame->flags = 0u;

        mFrameConstBuffer->unmap( UO_KEEP_PERSISTENT );
    }
    //-------------------------------------------------------------------------
    uint32 PathTracer::uploadLights( SceneManager *sceneManager )
    {
        PathTracerLightGpu *lightData = reinterpret_cast<PathTracerLightGpu *>(
            mLightsConstBuffer->map( 0, mLightsConstBuffer->getNumElements() ) );
        memset( lightData, 0, mLightsConstBuffer->getTotalSizeBytes() );

        uint32 numCollectedLights = 0u;
        const uint32 maxNumLights =
            static_cast<uint32>( mLightsConstBuffer->getNumElements() / sizeof( PathTracerLightGpu ) );

        ObjectMemoryManager &memoryManager = sceneManager->_getLightMemoryManager();
        const size_t numRenderQueues = memoryManager.getNumRenderQueues();
        for( size_t i = 0u; i < numRenderQueues && numCollectedLights < maxNumLights; ++i )
        {
            ObjectData objData;
            const size_t totalObjs = memoryManager.getFirstObjectData( objData, i );
            for( size_t j = 0u; j < totalObjs && numCollectedLights < maxNumLights;
                 j += ARRAY_PACKED_REALS )
            {
                for( size_t k = 0u; k < ARRAY_PACKED_REALS && j + k < totalObjs &&
                                   numCollectedLights < maxNumLights; ++k )
                {
                    Light *light = static_cast<Light *>( objData.mOwner[k] );
                    if( light->isVisible() &&
                        ( light->getType() == Light::LT_DIRECTIONAL ||
                          light->getType() == Light::LT_POINT ||
                          light->getType() == Light::LT_SPOTLIGHT ) )
                    {
                        addLight( lightData + numCollectedLights, light );
                        ++numCollectedLights;
                    }
                }
                objData.advancePack();
            }
        }

        mLightsConstBuffer->unmap( UO_KEEP_PERSISTENT );
        return numCollectedLights;
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadMaterialBuffer()
    {
        const PathTracerMaterialCache::MaterialRecordArray &materials =
            mScene.getMaterialCache().getMaterials();
        const size_t numMaterials = std::max<size_t>( materials.size(), 1u );
        const size_t bytesNeeded = numMaterials * sizeof( PathTracerMaterialGpu );

        if( !mMaterialBuffer || mMaterialBuffer->getTotalSizeBytes() < bytesNeeded )
        {
            if( mMaterialBuffer )
                mVaoManager->destroyReadOnlyBuffer( mMaterialBuffer );
            mMaterialBuffer = mVaoManager->createReadOnlyBuffer( PFG_RGBA32_FLOAT, bytesNeeded,
                                                                 BT_DEFAULT, 0, false );
        }

        std::vector<PathTracerMaterialGpu> staging( numMaterials );
        PathTracerMaterialGpu *dst = staging.data();
        memset( dst, 0, bytesNeeded );
        mDiffuseTextures.clear();
        mRoughnessTextures.clear();
        mNormalTextures.clear();
        mEmissiveTextures.clear();
        mReflectionTextures.clear();

        for( size_t i = 0u; i < materials.size(); ++i )
        {
            const HlmsPbsDatablock *datablock = materials[i].datablock;
            const Vector3 diffuse = datablock->getDiffuse();
            const Vector3 fresnel = datablock->getFresnel();
            const Vector3 emissive = datablock->getEmissive();
            const Vector3 specular = datablock->getSpecular();
            const float roughness = datablock->getRoughness();
            const float clampedRoughness = std::min( std::max( roughness, 0.0f ), 1.0f );
            const float smoothness = 1.0f - clampedRoughness;
            const bool isTransparent = datablock->getTransparencyMode() != HlmsPbsDatablock::None &&
                                       datablock->getTransparency() < 0.995f;
            const bool isMetallicWorkflow = datablock->getWorkflow() == HlmsPbsDatablock::MetallicWorkflow;
            const float metalness = isMetallicWorkflow ? datablock->getMetalness() : 0.0f;
            const float specularLuminance =
                std::max( 0.0f, specular.x * 0.2126f + specular.y * 0.7152f + specular.z * 0.0722f );

            float pathSpecularWeight = 1.0f;
            float normalMapWeight = static_cast<float>( datablock->getNormalMapWeight() );

            int diffuseTextureIdx = -1;
            Vector4 textureOffsetScale( 0.0f, 0.0f, 1.0f, 1.0f );
            TextureGpu *diffuseTexture = datablock->getTexture( PBSM_DIFFUSE );
            if( !diffuseTexture )
            {
                diffuseTexture = datablock->getTexture( PBSM_DETAIL0 );
                textureOffsetScale = datablock->getDetailMapOffsetScale( 0u );
            }

            diffuseTextureIdx = addTextureToList( diffuseTexture, mDiffuseTextures,
                                                  MaxPathTracerMaterialTextures );

            TextureGpu *roughnessTexture = datablock->getTexture( PBSM_ROUGHNESS );
            const int roughnessTextureIdx = addTextureToList( roughnessTexture, mRoughnessTextures,
                                                             MaxPathTracerMaterialTextures );

            TextureGpu *normalTexture = datablock->getTexture( PBSM_NORMAL );
            const int normalTextureIdx = addTextureToList( normalTexture, mNormalTextures,
                                                          MaxPathTracerMaterialTextures );

            TextureGpu *emissiveTexture = datablock->getTexture( PBSM_EMISSIVE );
            const int emissiveTextureIdx = addTextureToList( emissiveTexture, mEmissiveTextures,
                                                            MaxPathTracerMaterialTextures );

            TextureGpu *reflectionTexture = datablock->getTexture( PBSM_REFLECTION );
            const int reflectionTextureIdx = addTextureToList( reflectionTexture, mReflectionTextures,
                                                              MaxPathTracerReflectionTextures );
            const bool hasReflectionTexture = reflectionTextureIdx >= 0;

            if( isMetallicWorkflow )
            {
                pathSpecularWeight = std::max( 0.2f, metalness );
            }
            else if( !isTransparent && !hasReflectionTexture )
            {
                pathSpecularWeight = std::max( 0.03f, smoothness * smoothness * specularLuminance * 0.45f );
                normalMapWeight *= std::max( 0.15f, pathSpecularWeight );
            }

            dst[i].baseColour_roughness[0] = diffuse.x;
            dst[i].baseColour_roughness[1] = diffuse.y;
            dst[i].baseColour_roughness[2] = diffuse.z;
            dst[i].baseColour_roughness[3] = roughness;
            dst[i].fresnel_transparency[0] = fresnel.x;
            dst[i].fresnel_transparency[1] = fresnel.y;
            dst[i].fresnel_transparency[2] = fresnel.z;
            dst[i].fresnel_transparency[3] = datablock->getTransparency();
            dst[i].emissive_flags[0] = emissive.x;
            dst[i].emissive_flags[1] = emissive.y;
            dst[i].emissive_flags[2] = emissive.z;
            dst[i].emissive_flags[3] = static_cast<float>( datablock->getTransparencyMode() );
            dst[i].diffuseTextureIdx_slice_hasTexture[0] = static_cast<float>( diffuseTextureIdx );
            dst[i].diffuseTextureIdx_slice_hasTexture[1] =
                diffuseTexture ? static_cast<float>( diffuseTexture->getInternalSliceStart() ) : 0.0f;
            dst[i].diffuseTextureIdx_slice_hasTexture[2] = diffuseTextureIdx >= 0 ? 1.0f : 0.0f;
            dst[i].diffuseTextureIdx_slice_hasTexture[3] = 0.0f;
            dst[i].diffuseUvOffsetScale[0] = static_cast<float>( textureOffsetScale.x );
            dst[i].diffuseUvOffsetScale[1] = static_cast<float>( textureOffsetScale.y );
            dst[i].diffuseUvOffsetScale[2] = static_cast<float>( textureOffsetScale.z );
            dst[i].diffuseUvOffsetScale[3] = static_cast<float>( textureOffsetScale.w );
            dst[i].roughnessTextureIdx_slice_hasTexture[0] = static_cast<float>( roughnessTextureIdx );
            dst[i].roughnessTextureIdx_slice_hasTexture[1] =
                roughnessTexture ? static_cast<float>( roughnessTexture->getInternalSliceStart() ) : 0.0f;
            dst[i].roughnessTextureIdx_slice_hasTexture[2] = roughnessTextureIdx >= 0 ? 1.0f : 0.0f;
            dst[i].roughnessTextureIdx_slice_hasTexture[3] = 0.0f;
            dst[i].normalTextureIdx_slice_hasTexture[0] = static_cast<float>( normalTextureIdx );
            dst[i].normalTextureIdx_slice_hasTexture[1] =
                normalTexture ? static_cast<float>( normalTexture->getInternalSliceStart() ) : 0.0f;
            dst[i].normalTextureIdx_slice_hasTexture[2] = normalTextureIdx >= 0 ? 1.0f : 0.0f;
            dst[i].normalTextureIdx_slice_hasTexture[3] = normalMapWeight;
            dst[i].emissiveTextureIdx_slice_hasTexture[0] = static_cast<float>( emissiveTextureIdx );
            dst[i].emissiveTextureIdx_slice_hasTexture[1] =
                emissiveTexture ? static_cast<float>( emissiveTexture->getInternalSliceStart() ) : 0.0f;
            dst[i].emissiveTextureIdx_slice_hasTexture[2] = emissiveTextureIdx >= 0 ? 1.0f : 0.0f;
            dst[i].emissiveTextureIdx_slice_hasTexture[3] = 0.0f;
            dst[i].reflectionTextureIdx_slice_hasTexture[0] = static_cast<float>( reflectionTextureIdx );
            dst[i].reflectionTextureIdx_slice_hasTexture[1] =
                reflectionTexture ? static_cast<float>( reflectionTexture->getInternalSliceStart() ) : 0.0f;
            dst[i].reflectionTextureIdx_slice_hasTexture[2] = hasReflectionTexture ? 1.0f : 0.0f;
            dst[i].reflectionTextureIdx_slice_hasTexture[3] = pathSpecularWeight;
        }

        mMaterialBuffer->upload( staging.data(), 0u, bytesNeeded );
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadGeometryBuffer()
    {
        size_t numGeometryRecords = 0u;
        size_t numTriangleRecords = 0u;
        const PathTracerScene::ItemArray &items = mScene.getItems();
        for( size_t i = 0u; i < items.size(); ++i )
        {
            Item *item = items[i];
            numGeometryRecords += item->getNumSubItems();
            for( size_t subItemIdx = 0u; subItemIdx < item->getNumSubItems(); ++subItemIdx )
            {
                VertexArrayObject *vao = item->getMesh()->getSubMesh( static_cast<unsigned>( subItemIdx ) )
                                             ->mVao[VpNormal]
                                             .front();
                numTriangleRecords += vao->getPrimitiveCount() / 3u;
            }
        }

        numGeometryRecords = std::max<size_t>( numGeometryRecords, 1u );
        numTriangleRecords = std::max<size_t>( numTriangleRecords, 1u );
        const size_t bytesNeeded = numGeometryRecords * sizeof( PathTracerGeometryGpu );
        const size_t triangleBytesNeeded = numTriangleRecords * sizeof( PathTracerTriangleGpu );

        if( !mGeometryBuffer || mGeometryBuffer->getTotalSizeBytes() < bytesNeeded )
        {
            if( mGeometryBuffer )
                mVaoManager->destroyReadOnlyBuffer( mGeometryBuffer );
            mGeometryBuffer = mVaoManager->createReadOnlyBuffer( PFG_RGBA32_FLOAT, bytesNeeded,
                                                                 BT_DEFAULT, 0, false );
        }

        if( !mTriangleBuffer || mTriangleBuffer->getTotalSizeBytes() < triangleBytesNeeded )
        {
            if( mTriangleBuffer )
                mVaoManager->destroyReadOnlyBuffer( mTriangleBuffer );
            mTriangleBuffer = mVaoManager->createReadOnlyBuffer( PFG_RGBA32_FLOAT, triangleBytesNeeded,
                                                                 BT_DEFAULT, 0, false );
        }

        std::vector<PathTracerGeometryGpu> staging( numGeometryRecords );
        PathTracerGeometryGpu *dst = staging.data();
        memset( dst, 0, bytesNeeded );

        std::vector<PathTracerTriangleGpu> triangleStaging( numTriangleRecords );
        PathTracerTriangleGpu *triangleDst = triangleStaging.data();
        memset( triangleDst, 0, triangleBytesNeeded );

        size_t geometryIdx = 0u;
        size_t triangleIdx = 0u;
        for( size_t itemIdx = 0u; itemIdx < items.size(); ++itemIdx )
        {
            Item *item = items[itemIdx];
            for( size_t subItemIdx = 0u; subItemIdx < item->getNumSubItems(); ++subItemIdx )
            {
                uint32 materialIdx = 0u;
                HlmsDatablock *datablock = item->getSubItem( subItemIdx )->getDatablock();
                if( datablock && datablock->getCreator()->getType() == HLMS_PBS )
                {
                    materialIdx = mScene.getMaterialCache().addDatablock(
                        static_cast<HlmsPbsDatablock *>( datablock ) );
                }

                dst[geometryIdx].material_subMesh[0] = static_cast<float>( materialIdx );
                dst[geometryIdx].material_subMesh[1] = static_cast<float>( itemIdx );
                dst[geometryIdx].material_subMesh[2] = static_cast<float>( subItemIdx );
                dst[geometryIdx].material_subMesh[3] = static_cast<float>( triangleIdx );

                SubMesh *subMesh = item->getMesh()->getSubMesh( static_cast<unsigned>( subItemIdx ) );
                VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
                IndexBufferPacked *indexBuffer = vao->getIndexBuffer();
                const uint32 indexCount = vao->getPrimitiveCount();

                size_t positionBufferIdx = 0u;
                size_t positionOffset = 0u;
                size_t uvBufferIdx = 0u;
                size_t uvOffset = 0u;
                size_t normalBufferIdx = 0u;
                size_t normalOffset = 0u;
                const bool hasPosition = vao->findBySemantic( VES_POSITION, positionBufferIdx, positionOffset ) != 0;
                const bool hasUv = vao->findBySemantic( VES_TEXTURE_COORDINATES, uvBufferIdx, uvOffset ) != 0;
                const bool hasNormal = vao->findBySemantic( VES_NORMAL, normalBufferIdx, normalOffset ) != 0;

                VertexArrayObject::ReadRequestsVec readRequests;
                size_t positionRequestIdx = std::numeric_limits<size_t>::max();
                size_t uvRequestIdx = std::numeric_limits<size_t>::max();
                size_t normalRequestIdx = std::numeric_limits<size_t>::max();
                if( hasPosition )
                {
                    positionRequestIdx = readRequests.size();
                    readRequests.push_back( VES_POSITION );
                }
                if( hasUv )
                {
                    uvRequestIdx = readRequests.size();
                    readRequests.push_back( VES_TEXTURE_COORDINATES );
                }
                if( hasNormal )
                {
                    normalRequestIdx = readRequests.size();
                    readRequests.push_back( VES_NORMAL );
                }

                if( !readRequests.empty() )
                {
                    if( indexBuffer )
                        vao->readRequests( readRequests, 0u, 0u, true );
                    else
                        vao->readRequests( readRequests, vao->getPrimitiveStart(), indexCount, true );
                    vao->mapAsyncTickets( readRequests );
                }

                AsyncTicketPtr indexTicket;
                const uint8 *indexData = 0;
                if( indexBuffer )
                {
                    if( indexBuffer->getShadowCopy() )
                    {
                        indexData = reinterpret_cast<const uint8 *>( indexBuffer->getShadowCopy() ) +
                                    vao->getPrimitiveStart() * indexBuffer->getBytesPerElement();
                    }
                    else
                    {
                        indexTicket = indexBuffer->readRequest( vao->getPrimitiveStart(), indexCount );
                        indexData = reinterpret_cast<const uint8 *>( indexTicket->map() );
                    }
                }

                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                for( uint32 idx = 0u; idx + 2u < indexCount; idx += 3u )
                {
                    const uint32 vertexIdx0 = indexBuffer ? readIndexAt( indexData, indexBuffer, idx + 0u ) : idx + 0u;
                    const uint32 vertexIdx1 = indexBuffer ? readIndexAt( indexData, indexBuffer, idx + 1u ) : idx + 1u;
                    const uint32 vertexIdx2 = indexBuffer ? readIndexAt( indexData, indexBuffer, idx + 2u ) : idx + 2u;

                    float uv0[2] = { 0.0f, 0.0f };
                    float uv1[2] = { 0.0f, 0.0f };
                    float uv2[2] = { 0.0f, 0.0f };
                    if( hasUv )
                    {
                        readFloat2At( readRequests[uvRequestIdx], vertexIdx0, uv0 );
                        readFloat2At( readRequests[uvRequestIdx], vertexIdx1, uv1 );
                        readFloat2At( readRequests[uvRequestIdx], vertexIdx2, uv2 );
                    }

                    Vector3 normal( 0.0f, 1.0f, 0.0f );
                    Vector3 vertexNormal( 0.0f, 1.0f, 0.0f );
                    bool hasValidVertexNormal = false;
                    if( hasNormal )
                    {
                        vertexNormal = readNormalAt( readRequests[normalRequestIdx], vertexIdx0 ) +
                                       readNormalAt( readRequests[normalRequestIdx], vertexIdx1 ) +
                                       readNormalAt( readRequests[normalRequestIdx], vertexIdx2 );
                        if( vertexNormal.squaredLength() > 1e-8f )
                        {
                            vertexNormal.normalise();
                            const Vector4 worldNormal4 =
                                transform.transformAffine( Vector4( vertexNormal, 0.0f ) );
                            vertexNormal = Vector3( worldNormal4.x, worldNormal4.y, worldNormal4.z );
                            if( vertexNormal.squaredLength() > 1e-8f )
                            {
                                vertexNormal.normalise();
                                normal = vertexNormal;
                                hasValidVertexNormal = true;
                            }
                        }
                    }

                    Vector3 tangent( 1.0f, 0.0f, 0.0f );
                    Vector3 bitangent( 0.0f, 0.0f, 1.0f );
                    if( hasPosition )
                    {
                        const Vector3 localPos0 = readFloat3At( readRequests[positionRequestIdx], vertexIdx0 );
                        const Vector3 localPos1 = readFloat3At( readRequests[positionRequestIdx], vertexIdx1 );
                        const Vector3 localPos2 = readFloat3At( readRequests[positionRequestIdx], vertexIdx2 );
                        const Vector4 worldPos04 = transform.transformAffine( Vector4( localPos0, 1.0f ) );
                        const Vector4 worldPos14 = transform.transformAffine( Vector4( localPos1, 1.0f ) );
                        const Vector4 worldPos24 = transform.transformAffine( Vector4( localPos2, 1.0f ) );
                        const Vector3 edge1( worldPos14.x - worldPos04.x, worldPos14.y - worldPos04.y,
                                             worldPos14.z - worldPos04.z );
                        const Vector3 edge2( worldPos24.x - worldPos04.x, worldPos24.y - worldPos04.y,
                                             worldPos24.z - worldPos04.z );

                        Vector3 faceNormal = edge1.crossProduct( edge2 );
                        if( faceNormal.squaredLength() > 1e-8f )
                        {
                            faceNormal.normalise();
                            if( hasValidVertexNormal && faceNormal.dotProduct( vertexNormal ) < 0.0f )
                                faceNormal = -faceNormal;
                            normal = faceNormal;
                        }

                        if( hasUv )
                        {
                            const float du1 = uv1[0] - uv0[0];
                            const float dv1 = uv1[1] - uv0[1];
                            const float du2 = uv2[0] - uv0[0];
                            const float dv2 = uv2[1] - uv0[1];
                            const float determinant = du1 * dv2 - dv1 * du2;
                            if( Math::Abs( determinant ) > 1e-8f )
                            {
                                const float invDeterminant = 1.0f / determinant;
                                tangent = ( edge1 * dv2 - edge2 * dv1 ) * invDeterminant;
                                bitangent = ( edge2 * du1 - edge1 * du2 ) * invDeterminant;
                                tangent -= normal * tangent.dotProduct( normal );
                                if( tangent.squaredLength() > 1e-8f )
                                    tangent.normalise();
                                bitangent -= normal * bitangent.dotProduct( normal );
                                if( bitangent.squaredLength() > 1e-8f )
                                    bitangent.normalise();
                            }
                        }
                    }

                    triangleDst[triangleIdx].uv0_uv1[0] = uv0[0];
                    triangleDst[triangleIdx].uv0_uv1[1] = uv0[1];
                    triangleDst[triangleIdx].uv0_uv1[2] = uv1[0];
                    triangleDst[triangleIdx].uv0_uv1[3] = uv1[1];
                    triangleDst[triangleIdx].uv2_normalX_normalY[0] = uv2[0];
                    triangleDst[triangleIdx].uv2_normalX_normalY[1] = uv2[1];
                    triangleDst[triangleIdx].uv2_normalX_normalY[2] = normal.x;
                    triangleDst[triangleIdx].uv2_normalX_normalY[3] = normal.y;
                    triangleDst[triangleIdx].normalZ_flags[0] = normal.z;
                    triangleDst[triangleIdx].normalZ_flags[1] = hasUv ? 1.0f : 0.0f;
                    triangleDst[triangleIdx].tangent[0] = tangent.x;
                    triangleDst[triangleIdx].tangent[1] = tangent.y;
                    triangleDst[triangleIdx].tangent[2] = tangent.z;
                    triangleDst[triangleIdx].bitangent[0] = bitangent.x;
                    triangleDst[triangleIdx].bitangent[1] = bitangent.y;
                    triangleDst[triangleIdx].bitangent[2] = bitangent.z;
                    ++triangleIdx;
                }

                if( indexTicket )
                    indexTicket->unmap();
                if( !readRequests.empty() )
                    vao->unmapAsyncTickets( readRequests );

                ++geometryIdx;
            }
        }

        mGeometryBuffer->upload( staging.data(), 0u, bytesNeeded );
        mTriangleBuffer->upload( triangleStaging.data(), 0u, triangleBytesNeeded );
    }
    //-------------------------------------------------------------------------
    void PathTracer::updateTraceJobThreadGroups()
    {
        if( !mTraceJob || !mRadianceTexture )
            return;

        const uint32 *threadsPerGroup = mTraceJob->getThreadsPerGroup();
        const uint32 threadsX = std::max( threadsPerGroup[0], 1u );
        const uint32 threadsY = std::max( threadsPerGroup[1], 1u );
        const uint32 width = std::max<uint32>( mRadianceTexture->getWidth(), 1u );
        const uint32 height = std::max<uint32>( mRadianceTexture->getHeight(), 1u );

        mTraceJob->setNumThreadGroups( ( width + threadsX - 1u ) / threadsX,
                                       ( height + threadsY - 1u ) / threadsY, 1u );
    }
    //-------------------------------------------------------------------------
    void PathTracer::bindJobResources()
    {
        mTraceJob->setConstBuffer( 0, mFrameConstBuffer );
        mTraceJob->setConstBuffer( 1, mLightsConstBuffer );

        if( mMaterialBuffer )
        {
            DescriptorSetTexture2::BufferSlot materialSlot(
                DescriptorSetTexture2::BufferSlot::makeEmpty() );
            materialSlot.buffer = mMaterialBuffer;
            mTraceJob->setTexBuffer( 0, materialSlot );
        }

        if( mGeometryBuffer )
        {
            DescriptorSetTexture2::BufferSlot geometrySlot(
                DescriptorSetTexture2::BufferSlot::makeEmpty() );
            geometrySlot.buffer = mGeometryBuffer;
            mTraceJob->setTexBuffer( 1, geometrySlot );
        }

        if( mTriangleBuffer )
        {
            DescriptorSetTexture2::BufferSlot triangleSlot(
                DescriptorSetTexture2::BufferSlot::makeEmpty() );
            triangleSlot.buffer = mTriangleBuffer;
            mTraceJob->setTexBuffer( 2, triangleSlot );
        }

        TextureGpu *fallbackMaterialTexture = 0;
        if( !mDiffuseTextures.empty() )
            fallbackMaterialTexture = mDiffuseTextures[0];
        else if( !mRoughnessTextures.empty() )
            fallbackMaterialTexture = mRoughnessTextures[0];
        else if( !mNormalTextures.empty() )
            fallbackMaterialTexture = mNormalTextures[0];
        else if( !mEmissiveTextures.empty() )
            fallbackMaterialTexture = mEmissiveTextures[0];

        if( fallbackMaterialTexture )
        {
            for( size_t i = 3u; i < PathTracerDiffuseTextureSlotStart; ++i )
            {
                DescriptorSetTexture2::TextureSlot fillerSlot(
                    DescriptorSetTexture2::TextureSlot::makeEmpty() );
                fillerSlot.texture = fallbackMaterialTexture;
                mTraceJob->setTexture( static_cast<uint8>( i ), fillerSlot, 0, false );
            }

            FastArray<TextureGpu *> *materialTextureArrays[4] = {
                &mDiffuseTextures, &mRoughnessTextures, &mNormalTextures, &mEmissiveTextures };
            const size_t textureSlotStarts[4] = { PathTracerDiffuseTextureSlotStart,
                                                  PathTracerRoughnessTextureSlotStart,
                                                  PathTracerNormalTextureSlotStart,
                                                  PathTracerEmissiveTextureSlotStart };
            for( size_t arrayIdx = 0u; arrayIdx < 4u; ++arrayIdx )
            {
                FastArray<TextureGpu *> &textures = *materialTextureArrays[arrayIdx];
                for( size_t i = 0u; i < MaxPathTracerMaterialTextures; ++i )
                {
                    DescriptorSetTexture2::TextureSlot textureSlot(
                        DescriptorSetTexture2::TextureSlot::makeEmpty() );
                    textureSlot.texture = i < textures.size() ? textures[i] : fallbackMaterialTexture;
                    const bool setSampler = arrayIdx == 0u && i == 0u;
                    mTraceJob->setTexture( static_cast<uint8>( textureSlotStarts[arrayIdx] + i ),
                                           textureSlot, 0, setSampler );
                }
            }
        }

        TextureGpu *fallbackReflectionTexture = mReflectionTextures.empty() ? 0 : mReflectionTextures[0];
        if( fallbackReflectionTexture )
        {
            for( size_t i = 0u; i < MaxPathTracerReflectionTextures; ++i )
            {
                DescriptorSetTexture2::TextureSlot reflectionSlot(
                    DescriptorSetTexture2::TextureSlot::makeEmpty() );
                reflectionSlot.texture =
                    i < mReflectionTextures.size() ? mReflectionTextures[i] : fallbackReflectionTexture;
                mTraceJob->setTexture( static_cast<uint8>( PathTracerReflectionTextureSlotStart + i ),
                                       reflectionSlot, 0, false );
            }
        }

        DescriptorSetUav::TextureSlot accumulationSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        accumulationSlot.texture = mAccumulationTexture;
        accumulationSlot.access = ResourceAccess::ReadWrite;
        mTraceJob->_setUavTexture( 0, accumulationSlot );

        DescriptorSetUav::TextureSlot radianceSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        radianceSlot.texture = mRadianceTexture;
        radianceSlot.access = ResourceAccess::Write;
        mTraceJob->_setUavTexture( 1, radianceSlot );

        updateTraceJobThreadGroups();

        mTraceJob->analyzeBarriers( mResourceTransitions );
        mRenderSystem->executeResourceTransition( mResourceTransitions );
    }
    //-------------------------------------------------------------------------
    void PathTracer::update( SceneManager *sceneManager )
    {
        if( !mEnabled || !mInitialized )
            return;

        const Matrix4 currentViewMatrix = mCamera->getViewMatrix( true );
        const Matrix4 currentProjectionMatrix = mCamera->getProjectionMatrixWithRSDepth();
        const bool cameraChanged = !mHasLastCameraState ||
                                   matricesDiffer( mLastViewMatrix, currentViewMatrix ) ||
                                   matricesDiffer( mLastProjectionMatrix, currentProjectionMatrix );
        const bool resetNeeded = cameraChanged || mScene.needsBlasRebuild() ||
                                 mScene.needsTlasRebuild() || mScene.needsMaterialUpload();
        if( resetNeeded )
            resetAccumulation();
        mLastViewMatrix = currentViewMatrix;
        mLastProjectionMatrix = currentProjectionMatrix;
        mHasLastCameraState = true;

        updateAccelerationStructure();
        const uint32 numLights = uploadLights( sceneManager );
        uploadFrameConstants( numLights );
        if( mScene.needsMaterialUpload() )
            uploadMaterialBuffer();
        if( mScene.needsBlasRebuild() || mScene.needsTlasRebuild() || !mGeometryBuffer )
            uploadGeometryBuffer();
        bindJobResources();

        mScene.clearDirtyFlags();
    }
    //-------------------------------------------------------------------------
    void PathTracer::render()
    {
        if( !mEnabled )
            return;

        ++mSampleCount;
    }
}
