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
#include "OgreRenderSystem.h"
#include "Vao/OgreVaoManager.h"

namespace Ogre
{
    PathTracer::PathTracer( TextureGpu *renderWindow, RenderSystem *renderSystem,
                            HlmsManager *hlmsManager, Camera *camera ) :
        mRenderWindow( renderWindow ),
        mRenderSystem( renderSystem ),
        mVaoManager( renderSystem ? renderSystem->getVaoManager() : 0 ),
        mHlmsManager( hlmsManager ),
        mCamera( camera ),
        mSampleCount( 0u ),
        mEnabled( false )
    {
    }
    //-------------------------------------------------------------------------
    PathTracer::~PathTracer()
    {
    }
    //-------------------------------------------------------------------------
    void PathTracer::setEnabled( bool enabled )
    {
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
    void PathTracer::update( SceneManager *sceneManager )
    {
        (void)sceneManager;

        if( !mEnabled )
            return;

        if( mScene.needsBlasRebuild() || mScene.needsTlasRebuild() ||
            mScene.needsMaterialUpload() )
        {
            resetAccumulation();
            mScene.clearDirtyFlags();
        }
    }
    //-------------------------------------------------------------------------
    void PathTracer::render()
    {
        if( !mEnabled )
            return;

        ++mSampleCount;
    }
}
