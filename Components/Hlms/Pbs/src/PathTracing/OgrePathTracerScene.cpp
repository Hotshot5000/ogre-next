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

#include "PathTracing/OgrePathTracerScene.h"
#include "OgreException.h"
#include "OgreItem.h"
#include "OgreSubItem.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreHlms.h"

namespace Ogre
{
    PathTracerScene::PathTracerScene() :
        mRebuildBlas( true ),
        mRebuildTlas( true ),
        mEnabled( true )
    {
    }
    //-------------------------------------------------------------------------
    PathTracerScene::~PathTracerScene()
    {
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::addItem( Item *item )
    {
        if( std::find( mItems.begin(), mItems.end(), item ) != mItems.end() )
            return;

        mItems.push_back( item );

        for( size_t i = 0u; i < item->getNumSubItems(); ++i )
        {
            HlmsDatablock *datablock = item->getSubItem( i )->getDatablock();
            if( datablock && datablock->getCreator()->getType() == HLMS_PBS )
                mMaterialCache.addDatablock( static_cast<HlmsPbsDatablock *>( datablock ) );
        }

        markGeometryDirty();
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::removeItem( Item *item )
    {
        ItemArray::iterator itor = std::find( mItems.begin(), mItems.end(), item );
        if( itor == mItems.end() )
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND, "", "PathTracerScene::removeItem" );

        mItems.erase( itor );
        markGeometryDirty();
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::removeAllItems()
    {
        mItems.clear();
        mMaterialCache.clear();
        markGeometryDirty();
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::markGeometryDirty()
    {
        mRebuildBlas = true;
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::markInstancesDirty()
    {
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::markMaterialsDirty()
    {
        mMaterialCache.setDirty();
    }
    //-------------------------------------------------------------------------
    void PathTracerScene::clearDirtyFlags()
    {
        mRebuildBlas = false;
        mRebuildTlas = false;
        mMaterialCache.clearDirty();
    }
}
