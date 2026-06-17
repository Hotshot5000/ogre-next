#include "PathTracerGameState.h"
#include "CameraController.h"
#include "GraphicsSystem.h"

#include "OgreItem.h"
#include "OgreSceneManager.h"

#include "OgreMesh2.h"
#include "OgreMeshManager.h"
#include "OgreMeshManager2.h"

#include "OgreCamera.h"

#include "OgreHlmsPbsDatablock.h"

#include "OgreFrameStats.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreRenderSystem.h"
#include "OgreRoot.h"

#include "../LocalCubemaps/LocalCubemapScene.h"

#include "OgreTextureFilters.h"
#include "OgreTextureGpuManager.h"

#include "OgreForward3D.h"
#include "PathTracing/OgrePathTracer.h"

#include "OgreWindow.h"

using namespace Demo;

namespace
{
    const Ogre::Real cPathTracerUpscaleScales[] = { 1.0f, 0.77f, 0.67f, 0.5f };
    const Ogre::Real cPathTracerGpuCullDistances[] = { 0.0f, 32.0f, 48.0f, 64.0f, 96.0f, 128.0f };
    const Ogre::Real cPathTracerGpuCullReflectionCones[] = { 1.0f, 1.5f, 2.5f, 4.0f, 6.0f };
    const Ogre::uint32 cPathTracerGpuCullModes[] = { 0u, 1u, 2u, 3u };
    const char *cPathTracerGpuCullModeNames[] = { "Off", "Distance", "Frustum", "Frustum+Distance" };
    const Ogre::uint32 cPathTracerForcedLodOverrides[] = { 0u, 1u, 2u, 3u };
    const char *cPathTracerForcedLodOverrideNames[] = { "Auto", "Full", "Simplified", "Proxy" };
    const Ogre::uint32 cPathTracerAccumulationLimits[] = { 0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u };
    const Ogre::PathTracerOidnQuality cPathTracerOidnQualities[] = {
        Ogre::PathTracerOidnQualityFast, Ogre::PathTracerOidnQualityBalanced,
        Ogre::PathTracerOidnQualityHigh
    };
    const char *cPathTracerOidnQualityNames[] = { "Fast", "Balanced", "High" };

    size_t getPathTracerOidnQualityIdx( const Ogre::PathTracerOidnQuality quality )
    {
        for( size_t i = 0u; i < sizeof( cPathTracerOidnQualities ) / sizeof( cPathTracerOidnQualities[0] );
             ++i )
        {
            if( cPathTracerOidnQualities[i] == quality )
                return i;
        }

        return 0u;
    }
}

namespace Demo
{
    PathTracerGameState::PathTracerGameState( const Ogre::String &helpDescription ) :
        TutorialGameState( helpDescription ),
        mPathTracer( 0 ),
        mAnimateObjects( true ),
        mForceOpaqueSpheresDebug( false ),
        mNumSpheres( 0u ),
        mTransparencyMode( Ogre::HlmsPbsDatablock::Transparent ),
        mTransparencyValue( 1.0f ),
        mUpscaleScaleIdx( 0u ),
        mGpuCullDistanceIdx( 0u ),
        mGpuCullReflectionConeIdx( 2u ),
        mGpuCullModeIdx( 0u ),
        mForcedLodOverrideIdx( 0u ),
        mAccumulationLimitIdx( 0u ),
        mOidnQualityIdx( 2u ),
        mPreferOidnDenoiser( false ),
        mLastGeneratedFrameCount( 0u ),
        mDisplayFpsRealFrames( 0u ),
        mDisplayFpsGeneratedFrames( 0u ),
        mDisplayFps( 0.0f )
    {
        mDisplayHelpMode = 2;
        mNumDisplayHelpModes = 3;

        memset( mMaterials, 0, sizeof( mMaterials ) );
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::createScene01()
    {
        Ogre::SceneManager *sceneManager = mGraphicsSystem->getSceneManager();
        sceneManager->setForwardClustered( true, 16, 8, 24, 96, 0, 0, 5, 500 );

        Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
        Ogre::HlmsManager *hlmsManager = mGraphicsSystem->getRoot()->getHlmsManager();
        Ogre::Camera *camera = mGraphicsSystem->getCamera();

        mPathTracer = new Ogre::PathTracer( mGraphicsSystem->getRenderWindow()->getTexture(),
                                            renderSystem, hlmsManager, camera,
                                            mGraphicsSystem->getCompositorWorkspace() );
        mPathTracer->setGpuCullReflectionConeExpansion(
            cPathTracerGpuCullReflectionCones[mGpuCullReflectionConeIdx] );
        mPathTracer->setGpuCullMode( cPathTracerGpuCullModes[mGpuCullModeIdx] );
        mPathTracer->setForcedLodOverride( cPathTracerForcedLodOverrides[mForcedLodOverrideIdx] );
        mPathTracer->setMaxAccumulatedSamples( cPathTracerAccumulationLimits[mAccumulationLimitIdx] );
        mPreferOidnDenoiser = renderSystem && renderSystem->getPathTracerPreferOidnDenoiser();
        if( renderSystem )
        {
            mOidnQualityIdx = getPathTracerOidnQualityIdx( renderSystem->getPathTracerOidnQuality() );
            renderSystem->setPathTracerPreferOidnDenoiser( mPreferOidnDenoiser );
            renderSystem->setPathTracerOidnQuality( cPathTracerOidnQualities[mOidnQualityIdx] );
        }
        mPathTracer->setEnabled( true );

        assert( dynamic_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) ) );

        const float armsLength = 2.5f;

        Ogre::v1::MeshPtr planeMeshV1 = Ogre::v1::MeshManager::getSingleton().createPlane(
            "PathTracer Plane v1", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::Plane( Ogre::Vector3::UNIT_Y, 1.0f ), 50.0f, 50.0f, 1, 1, true, 1, 4.0f, 4.0f,
            Ogre::Vector3::UNIT_Z, Ogre::v1::HardwareBuffer::HBU_STATIC,
            Ogre::v1::HardwareBuffer::HBU_STATIC );

        Ogre::MeshPtr planeMesh = Ogre::MeshManager::getSingleton().createByImportingV1(
            "PathTracer Plane", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            planeMeshV1.get(), true, true, true );

        {
            Ogre::Item *item = sceneManager->createItem( planeMesh, Ogre::SCENE_DYNAMIC );
            item->setDatablock( "Marble" );
            Ogre::SceneNode *sceneNode = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                             ->createChildSceneNode( Ogre::SCENE_DYNAMIC );
            sceneNode->setPosition( 0, -1, 0 );
            sceneNode->attachObject( item );

            assert( dynamic_cast<Ogre::HlmsPbsDatablock *>( item->getSubItem( 0 )->getDatablock() ) );
            Ogre::HlmsPbsDatablock *datablock =
                static_cast<Ogre::HlmsPbsDatablock *>( item->getSubItem( 0 )->getDatablock() );

            Ogre::HlmsSamplerblock samplerblock( *datablock->getSamplerblock( Ogre::PBSM_ROUGHNESS ) );
            samplerblock.mU = Ogre::TAM_WRAP;
            samplerblock.mV = Ogre::TAM_WRAP;
            samplerblock.mW = Ogre::TAM_WRAP;
            datablock->setSamplerblock( Ogre::PBSM_ROUGHNESS, samplerblock );

            mPathTracer->getScene().addItem( item );
        }

        for( int i = 0; i < 4; ++i )
        {
            for( int j = 0; j < 4; ++j )
            {
                Ogre::String meshName;

                if( i == j )
                    meshName = "Sphere1000.mesh";
                else
                    meshName = "Cube_d.mesh";

                Ogre::Item *item = sceneManager->createItem(
                    meshName, Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                    Ogre::SCENE_DYNAMIC );
                if( i % 2 == 0 )
                    item->setDatablock( "Rocks" );
                else
                    item->setDatablock( "Marble" );

                item->setVisibilityFlags( 0x000000001 );

                const size_t idx = static_cast<size_t>( i * 4 + j );

                mSceneNode[idx] = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                      ->createChildSceneNode( Ogre::SCENE_DYNAMIC );

                mSceneNode[idx]->setPosition( ( Ogre::Real( i ) - 1.5f ) * armsLength, 2.0f,
                                              ( Ogre::Real( j ) - 1.5f ) * armsLength );
                mSceneNode[idx]->setScale( 0.65f, 0.65f, 0.65f );

                mSceneNode[idx]->roll( Ogre::Radian( (Ogre::Real)idx ) );

                mSceneNode[idx]->attachObject( item );
                mPathTracer->getScene().addItem( item );
            }
        }

        {
            mNumSpheres = 0;
            Ogre::HlmsPbs *hlmsPbs =
                static_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) );

            const int numX = 8;
            const int numZ = 8;

            const float armsLengthSphere = 1.0f;
            const float startX = ( numX - 1 ) / 2.0f;
            const float startZ = ( numZ - 1 ) / 2.0f;

            Ogre::Root *root = mGraphicsSystem->getRoot();
            Ogre::TextureGpuManager *textureMgr = root->getRenderSystem()->getTextureGpuManager();

            for( int x = 0; x < numX; ++x )
            {
                for( int z = 0; z < numZ; ++z )
                {
                    Ogre::String datablockName =
                        "PathTracerTest" + Ogre::StringConverter::toString( mNumSpheres++ );
                    Ogre::HlmsPbsDatablock *datablock = static_cast<Ogre::HlmsPbsDatablock *>(
                        hlmsPbs->createDatablock( datablockName, datablockName, Ogre::HlmsMacroblock(),
                                                  Ogre::HlmsBlendblock(), Ogre::HlmsParamVec() ) );

                    Ogre::TextureGpu *texture = textureMgr->createOrRetrieveTexture(
                        "SaintPetersBasilica.dds", Ogre::GpuPageOutStrategy::Discard,
                        Ogre::TextureFlags::PrefersLoadingFromFileAsSRGB, Ogre::TextureTypes::TypeCube,
                        Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                        Ogre::TextureFilter::TypeGenerateDefaultMipmaps );

                    datablock->setTexture( Ogre::PBSM_REFLECTION, texture );
                    datablock->setTransparency( 1.0f, Ogre::HlmsPbsDatablock::None );
                    datablock->setDiffuse( Ogre::Vector3( 0.0f, 1.0f, 0.0f ) );
                    datablock->setRoughness(
                        std::max( 0.02f, float( x ) / std::max( 1.0f, (float)( numX - 1 ) ) ) );
                    datablock->setFresnel(
                        Ogre::Vector3( float( z ) / std::max( 1.0f, (float)( numZ - 1 ) ) ), false );

                    Ogre::Item *item = sceneManager->createItem(
                        "Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                        Ogre::SCENE_DYNAMIC );
                    item->setDatablock( datablock );
                    item->setVisibilityFlags( 0x000000002 );

                    Ogre::SceneNode *sceneNode = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                                     ->createChildSceneNode( Ogre::SCENE_DYNAMIC );
                    sceneNode->setPosition(
                        Ogre::Vector3( armsLengthSphere * Ogre::Real( x ) - startX, 1.0f,
                                       armsLengthSphere * Ogre::Real( z ) - startZ ) );
                    sceneNode->attachObject( item );

                    mPathTracer->getScene().addItem( item );
                }
            }
        }

        Ogre::SceneNode *rootNode = sceneManager->getRootSceneNode();

        Ogre::Light *light = sceneManager->createLight();
        Ogre::SceneNode *lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setPowerScale( 1.0f );
        light->setType( Ogre::Light::LT_DIRECTIONAL );
        light->setDirection( Ogre::Vector3( -1, -1, -1 ).normalisedCopy() );

        mLightNodes[0] = lightNode;

        sceneManager->setAmbientLight( Ogre::ColourValue( 0.3f, 0.5f, 0.7f ) * 0.1f * 0.75f,
                                       Ogre::ColourValue( 0.6f, 0.45f, 0.3f ) * 0.065f * 0.75f,
                                       -light->getDirection() + Ogre::Vector3::UNIT_Y * 0.2f );

        mGraphicsSystem->createAtmosphere( light );

        light = sceneManager->createLight();
        lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setDiffuseColour( 0.8f, 0.4f, 0.2f );
        light->setSpecularColour( 0.8f, 0.4f, 0.2f );
        light->setPowerScale( Ogre::Math::PI );
        light->setType( Ogre::Light::LT_SPOTLIGHT );
        lightNode->setPosition( -10.0f, 10.0f, 10.0f );
        light->setDirection( Ogre::Vector3( 1, -1, -1 ).normalisedCopy() );
        light->setAttenuationBasedOnRadius( 10.0f, 0.01f );

        mLightNodes[1] = lightNode;

        light = sceneManager->createLight();
        lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setDiffuseColour( 0.2f, 0.4f, 0.8f );
        light->setSpecularColour( 0.2f, 0.4f, 0.8f );
        light->setPowerScale( Ogre::Math::PI );
        light->setType( Ogre::Light::LT_SPOTLIGHT );
        lightNode->setPosition( 10.0f, 10.0f, -10.0f );
        light->setDirection( Ogre::Vector3( -1, -1, 1 ).normalisedCopy() );
        light->setAttenuationBasedOnRadius( 10.0f, 0.01f );

        mLightNodes[2] = lightNode;

//        light = sceneManager->createLight();
//        lightNode = rootNode->createChildSceneNode();
//        lightNode->attachObject( light );
//        light->setDiffuseColour( 1.0f, 0.86f, 0.62f );
//        light->setSpecularColour( 1.0f, 0.86f, 0.62f );
//        light->setPowerScale( 5.0f );
//        light->setType( Ogre::Light::LT_AREA_LTC );
//        light->setRectSize( Ogre::Vector2( 60.0f, 30.0f ) );
//        light->setDoubleSided( false );
//        lightNode->setPosition( -5.0f, 7.0f, -7.0f );
//        light->setDirection( Ogre::Vector3( 0, -1, -1 ).normalisedCopy() );
//        light->setAttenuationBasedOnRadius( 18.0f, 0.01f );
//
//        mLightNodes[3] = lightNode;

        mCameraController = new CameraController( mGraphicsSystem, false );
        mCameraController->mCameraBaseSpeed = 1.0f;
        mCameraController->mCameraSpeedBoost = 10.0f;

        TutorialGameState::createScene01();
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::destroyScene()
    {
        delete mPathTracer;
        mPathTracer = 0;
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::update( float timeSinceLast )
    {
        if( mAnimateObjects )
        {
            for( int i = 0; i < 16; ++i )
                mSceneNode[i]->yaw( Ogre::Radian( timeSinceLast * float( i ) * 0.125f ) );

            if( mPathTracer )
                mPathTracer->getScene().markInstancesDirty();
        }

        TutorialGameState::update( timeSinceLast );

        if( mPathTracer )
        {
            mPathTracer->update( mGraphicsSystem->getSceneManager() );
            mPathTracer->render();

            Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
            const Ogre::uint64 generatedFrameCount =
                renderSystem ? renderSystem->getPathTracerGeneratedFrameCount() : 0u;
            if( generatedFrameCount < mLastGeneratedFrameCount )
                mLastGeneratedFrameCount = generatedFrameCount;

            const Ogre::uint64 generatedFrameDelta64 = generatedFrameCount - mLastGeneratedFrameCount;
            const Ogre::uint32 generatedFrameDelta = generatedFrameDelta64 > Ogre::uint64( 0xffffffffu )
                                                         ? 0xffffffffu
                                                         : Ogre::uint32( generatedFrameDelta64 );
            mLastGeneratedFrameCount = generatedFrameCount;

            ++mDisplayFpsRealFrames;
            mDisplayFpsGeneratedFrames += generatedFrameDelta;

            if( mDisplayFpsRealFrames >= 30u )
            {
                const Ogre::FrameStats *frameStats = mGraphicsSystem->getRoot()->getFrameStats();
                const Ogre::Real realAvgFps = frameStats ? frameStats->getAvgFps() : 0.0f;
                const Ogre::Real displayFrameRatio =
                    Ogre::Real( mDisplayFpsRealFrames + mDisplayFpsGeneratedFrames ) /
                    Ogre::Real( mDisplayFpsRealFrames );
                mDisplayFps = realAvgFps * displayFrameRatio;
                mDisplayFpsRealFrames = 0u;
                mDisplayFpsGeneratedFrames = 0u;
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::generateDebugText( float timeSinceLast, Ogre::String &outText )
    {
        Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();

        TutorialGameState::generateDebugText( timeSinceLast, outText );
        outText += "\nPath tracer accumulated samples: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getAccumulatedSamples() : 0u );
        outText += "\nPath tracer accumulation limit: ";
        const Ogre::uint32 accumulationLimit =
            mPathTracer ? mPathTracer->getMaxAccumulatedSamples() : 0u;
        if( accumulationLimit > 0u )
            outText += Ogre::StringConverter::toString( accumulationLimit );
        else
            outText += "Unlimited";
        outText += "\nPath tracer fixed RNG pattern: ";
        outText += mPathTracer && mPathTracer->getFreezeRngPattern() ? "On" : "Off";
        outText += "\nPath tracer bounces: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getMaxBounces() :
                                                                Ogre::PathTracer::DefaultBounces );
        outText += " / ";
        outText += Ogre::StringConverter::toString( Ogre::PathTracer::MaxBounces );
        outText += "\nPath tracer samples per pixel per frame: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getSamplesPerPixel() :
                                                                Ogre::PathTracer::DefaultSamplesPerPixel );
        outText += " / ";
        outText += Ogre::StringConverter::toString( Ogre::PathTracer::MaxSamplesPerPixel );
        outText += "\nPath tracer input scale: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getUpscaleInputScale() : 1.0f,
                                                    2u );
        outText += " [";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getInternalWidth() : 0u );
        outText += "x";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getInternalHeight() : 0u );
        outText += "]";
        outText += "\nPath tracer avg display fps: ";
        outText += Ogre::StringConverter::toString( mDisplayFps );
        outText += "\nPath tracer GPU cull mode: ";
        const Ogre::uint32 gpuCullMode = mPathTracer ? mPathTracer->getGpuCullMode() : 0u;
        outText += cPathTracerGpuCullModeNames[std::min<Ogre::uint32>( gpuCullMode, 3u )];
        outText += "\nPath tracer forced LOD: ";
        const Ogre::uint32 forcedLodOverride = mPathTracer ? mPathTracer->getForcedLodOverride() : 0u;
        outText += cPathTracerForcedLodOverrideNames[std::min<Ogre::uint32>( forcedLodOverride, 3u )];
        outText += "\nPath tracer GPU active meshlets: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getActiveMeshletCount() : 0u );
        outText += " / ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getTotalMeshletCount() : 0u );
        outText += "\nPath tracer tier objects full/simplified/proxy: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getFullTierObjectCount() : 0u );
        outText += " / ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getSimplifiedTierObjectCount() : 0u );
        outText += " / ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getProxyTierObjectCount() : 0u );
        outText += "\nPath tracer tier meshlets full/simplified/proxy: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getFullTierMeshletCount() : 0u );
        outText += " / ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getSimplifiedTierMeshletCount() : 0u );
        outText += " / ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getProxyTierMeshletCount() : 0u );
        outText += "\nPath tracer GPU cull distance: ";
        if( mPathTracer && mPathTracer->getGpuCullDistance() > 0.0f )
        {
            outText += Ogre::StringConverter::toString( mPathTracer->getGpuCullDistance(), 1u, 0u,
                                                        ' ', std::ios::fixed );
        }
        else
            outText += "Off";
        outText += " cone x";
        outText += Ogre::StringConverter::toString(
            mPathTracer ? mPathTracer->getGpuCullReflectionConeExpansion() : 1.0f,
            1u, 0u, ' ', std::ios::fixed );
        outText += "\nPath tracer denoiser: ";
        Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
        if( renderSystem && renderSystem->getPathTracerOidnDenoiserSupported() )
        {
            outText += renderSystem->getPathTracerUsingOidnDenoiser() ? "OIDN" : "MetalFX";
            outText += renderSystem->getPathTracerPreferOidnDenoiser() ? " [requested: OIDN]" :
                                                                       " [requested: MetalFX]";
            outText += " OIDN quality: ";
            outText += cPathTracerOidnQualityNames[getPathTracerOidnQualityIdx(
                renderSystem->getPathTracerOidnQuality() )];
        }
        else
            outText += "MetalFX [OIDN unavailable on current Metal device]";
        outText += "\nPress [ or ] to decrease/increase path bounces.";
        outText += "\nPress , or . to decrease/increase samples per pixel per frame.";
        outText += "\nPress U to cycle MetalFX input scale.";
        outText += "\nPress N to cycle accumulation limit.";
        outText += "\nPress O to cycle OIDN quality.";
        outText += "\nPress R to toggle fixed RNG pattern.";
        outText += "\nPress B to cycle GPU meshlet culling mode.";
        outText += "\nPress C to cycle GPU meshlet culling distance.";
        outText += "\nPress V to cycle GPU reflection cone expansion.";
        outText += "\nPress L to cycle forced path tracer LOD override.";
        outText += "\nPress M to switch the path tracer denoiser.";
        outText += "\nPress F6 to toggle MetalFX frame generation. ";
        outText += mPathTracer && mPathTracer->getFrameGenerationEnabled() ? "[On]" : "[Off]";
        outText += "\nPress F2 to toggle animation. ";
        outText += mAnimateObjects ? "[On]" : "[Off]";
        outText += "\nPress F3 to show/hide animated objects. ";
        outText += ( visibilityMask & 0x000000001 ) ? "[On]" : "[Off]";
        outText += "\nPress F4 to show/hide palette of spheres. ";
        outText += ( visibilityMask & 0x000000002 ) ? "[On]" : "[Off]";
        outText += "\nPress F5 to toggle transparency mode. ";
        outText += mTransparencyMode == Ogre::HlmsPbsDatablock::Fade ? "[Fade]" : "[Transparent]";
        outText += "\nPress F7 to force sphere palette opaque. ";
        outText += mForceOpaqueSpheresDebug ? "[On]" : "[Off]";
        outText += "\n+/- to change transparency. [";
        outText += Ogre::StringConverter::toString( mTransparencyValue ) + "]";
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::setTransparencyToMaterials()
    {
        Ogre::HlmsManager *hlmsManager = mGraphicsSystem->getRoot()->getHlmsManager();

        assert( dynamic_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) ) );

        Ogre::HlmsPbs *hlmsPbs = static_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) );

        Ogre::HlmsPbsDatablock::TransparencyModes mode =
            static_cast<Ogre::HlmsPbsDatablock::TransparencyModes>( mTransparencyMode );
        float transparencyValue = mTransparencyValue;

        if( transparencyValue >= 1.0f )
            mode = Ogre::HlmsPbsDatablock::None;

        if( mTransparencyMode < 1.0f && mode == Ogre::HlmsPbsDatablock::None )
            mode = Ogre::HlmsPbsDatablock::Transparent;

        if( mForceOpaqueSpheresDebug )
        {
            transparencyValue = 1.0f;
            mode = Ogre::HlmsPbsDatablock::None;
        }

        for( size_t i = 0; i < mNumSpheres; ++i )
        {
            Ogre::String datablockName = "PathTracerTest" + Ogre::StringConverter::toString( i );
            Ogre::HlmsPbsDatablock *datablock =
                static_cast<Ogre::HlmsPbsDatablock *>( hlmsPbs->getDatablock( datablockName ) );

            datablock->setTransparency( transparencyValue, mode );
        }

        if( mPathTracer )
            mPathTracer->getScene().markMaterialsDirty();
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::keyReleased( const SDL_KeyboardEvent &arg )
    {
        if( ( arg.keysym.mod & ~( KMOD_NUM | KMOD_CAPS ) ) != 0 )
        {
            TutorialGameState::keyReleased( arg );
            return;
        }

        if( arg.keysym.sym == SDLK_F2 )
        {
            mAnimateObjects = !mAnimateObjects;
        }
        else if( arg.keysym.sym == SDLK_F3 )
        {
            Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();
            bool showMovingObjects = ( visibilityMask & 0x00000001 );
            showMovingObjects = !showMovingObjects;
            visibilityMask &= static_cast<uint32_t>( ~0x00000001 );
            visibilityMask |= (Ogre::uint32)showMovingObjects;
            mGraphicsSystem->getSceneManager()->setVisibilityMask( visibilityMask );
        }
        else if( arg.keysym.sym == SDLK_F4 )
        {
            Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();
            bool showPalette = ( visibilityMask & 0x00000002 ) != 0;
            showPalette = !showPalette;
            visibilityMask &= static_cast<uint32_t>( ~0x00000002 );
            visibilityMask |= ( Ogre::uint32 )( showPalette ) << 1;
            mGraphicsSystem->getSceneManager()->setVisibilityMask( visibilityMask );
        }
        else if( arg.keysym.sym == SDLK_F5 )
        {
            mTransparencyMode = mTransparencyMode == Ogre::HlmsPbsDatablock::Fade
                                    ? Ogre::HlmsPbsDatablock::Transparent
                                    : Ogre::HlmsPbsDatablock::Fade;
            if( mTransparencyValue != 1.0f )
                setTransparencyToMaterials();
        }
        else if( mPathTracer && arg.keysym.sym == SDLK_F6 )
        {
            mPathTracer->setFrameGenerationEnabled( !mPathTracer->getFrameGenerationEnabled() );
        }
        else if( arg.keysym.sym == SDLK_F7 )
        {
            mForceOpaqueSpheresDebug = !mForceOpaqueSpheresDebug;
            setTransparencyToMaterials();
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_LEFTBRACKET )
        {
            const Ogre::uint32 currentBounces = mPathTracer->getMaxBounces();
            if( currentBounces > Ogre::PathTracer::MinBounces )
                mPathTracer->setMaxBounces( currentBounces - 1u );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET )
        {
            const Ogre::uint32 currentBounces = mPathTracer->getMaxBounces();
            if( currentBounces < Ogre::PathTracer::MaxBounces )
                mPathTracer->setMaxBounces( currentBounces + 1u );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_COMMA )
        {
            const Ogre::uint32 currentSamplesPerPixel = mPathTracer->getSamplesPerPixel();
            if( currentSamplesPerPixel > Ogre::PathTracer::MinSamplesPerPixel )
                mPathTracer->setSamplesPerPixel( currentSamplesPerPixel - 1u );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_PERIOD )
        {
            const Ogre::uint32 currentSamplesPerPixel = mPathTracer->getSamplesPerPixel();
            if( currentSamplesPerPixel < Ogre::PathTracer::MaxSamplesPerPixel )
                mPathTracer->setSamplesPerPixel( currentSamplesPerPixel + 1u );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_U )
        {
            const size_t numScales = sizeof( cPathTracerUpscaleScales ) / sizeof( cPathTracerUpscaleScales[0] );
            mUpscaleScaleIdx = ( mUpscaleScaleIdx + 1u ) % numScales;
            mPathTracer->setUpscaleInputScale( cPathTracerUpscaleScales[mUpscaleScaleIdx] );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_N )
        {
            const size_t numAccumulationLimits = sizeof( cPathTracerAccumulationLimits ) /
                                                 sizeof( cPathTracerAccumulationLimits[0] );
            mAccumulationLimitIdx = ( mAccumulationLimitIdx + 1u ) % numAccumulationLimits;
            mPathTracer->setMaxAccumulatedSamples(
                cPathTracerAccumulationLimits[mAccumulationLimitIdx] );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_R )
        {
            mPathTracer->setFreezeRngPattern( !mPathTracer->getFreezeRngPattern() );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_M )
        {
            Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
            if( renderSystem && renderSystem->getPathTracerOidnDenoiserSupported() )
            {
                mPreferOidnDenoiser = !mPreferOidnDenoiser;
                renderSystem->setPathTracerPreferOidnDenoiser( mPreferOidnDenoiser );
                mPathTracer->resetAccumulation();
            }
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_O )
        {
            Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
            if( renderSystem && renderSystem->getPathTracerOidnDenoiserSupported() )
            {
                const size_t numQualities =
                    sizeof( cPathTracerOidnQualities ) / sizeof( cPathTracerOidnQualities[0] );
                mOidnQualityIdx = ( mOidnQualityIdx + 1u ) % numQualities;
                renderSystem->setPathTracerOidnQuality( cPathTracerOidnQualities[mOidnQualityIdx] );
                mPathTracer->resetAccumulation();
            }
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_B )
        {
            const size_t numCullModes = sizeof( cPathTracerGpuCullModes ) /
                                        sizeof( cPathTracerGpuCullModes[0] );
            mGpuCullModeIdx = ( mGpuCullModeIdx + 1u ) % numCullModes;
            mPathTracer->setGpuCullMode( cPathTracerGpuCullModes[mGpuCullModeIdx] );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_C )
        {
            const size_t numCullDistances = sizeof( cPathTracerGpuCullDistances ) /
                                            sizeof( cPathTracerGpuCullDistances[0] );
            mGpuCullDistanceIdx = ( mGpuCullDistanceIdx + 1u ) % numCullDistances;
            mPathTracer->setGpuCullDistance( cPathTracerGpuCullDistances[mGpuCullDistanceIdx] );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_L )
        {
            const size_t numForcedLodOverrides = sizeof( cPathTracerForcedLodOverrides ) /
                                                 sizeof( cPathTracerForcedLodOverrides[0] );
            mForcedLodOverrideIdx = ( mForcedLodOverrideIdx + 1u ) % numForcedLodOverrides;
            mPathTracer->setForcedLodOverride(
                cPathTracerForcedLodOverrides[mForcedLodOverrideIdx] );
        }
        else if( mPathTracer && arg.keysym.scancode == SDL_SCANCODE_V )
        {
            const size_t numReflectionCones = sizeof( cPathTracerGpuCullReflectionCones ) /
                                              sizeof( cPathTracerGpuCullReflectionCones[0] );
            mGpuCullReflectionConeIdx = ( mGpuCullReflectionConeIdx + 1u ) % numReflectionCones;
            mPathTracer->setGpuCullReflectionConeExpansion(
                cPathTracerGpuCullReflectionCones[mGpuCullReflectionConeIdx] );
        }
        else if( arg.keysym.scancode == SDL_SCANCODE_KP_PLUS )
        {
            if( mTransparencyValue < 1.0f )
            {
                mTransparencyValue += 0.1f;
                mTransparencyValue = std::min( mTransparencyValue, 1.0f );
                setTransparencyToMaterials();
            }
        }
        else if( arg.keysym.scancode == SDL_SCANCODE_MINUS ||
                 arg.keysym.scancode == SDL_SCANCODE_KP_MINUS )
        {
            if( mTransparencyValue > 0.0f )
            {
                mTransparencyValue -= 0.1f;
                mTransparencyValue = std::max( mTransparencyValue, 0.0f );
                setTransparencyToMaterials();
            }
        }
        else
        {
            TutorialGameState::keyReleased( arg );
        }
    }
}  // namespace Demo
