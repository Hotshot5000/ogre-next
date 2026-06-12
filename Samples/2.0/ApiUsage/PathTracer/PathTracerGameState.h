#ifndef _Demo_PathTracerGameState_H_
#define _Demo_PathTracerGameState_H_

#include "OgrePrerequisites.h"
#include "TutorialGameState.h"

#include "SdlEmulationLayer.h"
#if OGRE_USE_SDL2
#    if defined( __clang__ )
#        pragma clang diagnostic push
#        pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#    elif defined( __GNUC__ )
#        pragma GCC diagnostic push
#        pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#    endif
#    include "SDL_keyboard.h"
#    if defined( __clang__ )
#        pragma clang diagnostic pop
#    elif defined( __GNUC__ )
#        pragma GCC diagnostic pop
#    endif
#endif

namespace Ogre
{
    class HlmsPbsDatablock;
    class PathTracer;
}

namespace Demo
{
    class PathTracerGameState : public TutorialGameState
    {
        Ogre::PathTracer *mPathTracer;
        Ogre::HlmsPbsDatablock *mMaterials[4];

        Ogre::SceneNode *mSceneNode[16];
        Ogre::SceneNode *mLightNodes[4];

        bool mAnimateObjects;

        size_t mNumSpheres;
        Ogre::uint8 mTransparencyMode;
        float mTransparencyValue;
        size_t mUpscaleScaleIdx;
        size_t mGpuCullDistanceIdx;
        size_t mGpuCullReflectionConeIdx;
        size_t mGpuCullModeIdx;
        size_t mForcedLodOverrideIdx;
        size_t mAccumulationLimitIdx;
        Ogre::uint64 mLastGeneratedFrameCount;
        Ogre::uint32 mDisplayFpsRealFrames;
        Ogre::uint32 mDisplayFpsGeneratedFrames;
        Ogre::Real mDisplayFps;

        void setTransparencyToMaterials();

        void generateDebugText( float timeSinceLast, Ogre::String &outText ) override;

    public:
        PathTracerGameState( const Ogre::String &helpDescription );

        void createScene01() override;
        void destroyScene() override;

        void update( float timeSinceLast ) override;

        void keyReleased( const SDL_KeyboardEvent &arg ) override;
    };
}  // namespace Demo

#endif
