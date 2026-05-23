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

#include "PathTracing/OgrePathTracerMaterialCache.h"
#include "OgreHlmsPbsDatablock.h"

namespace Ogre
{
    PathTracerMaterialCache::PathTracerMaterialCache() :
        mDirty( true )
    {
    }
    //-------------------------------------------------------------------------
    PathTracerMaterialCache::~PathTracerMaterialCache()
    {
    }
    //-------------------------------------------------------------------------
    uint32 PathTracerMaterialCache::addDatablock( HlmsPbsDatablock *datablock )
    {
        MaterialIndexMap::const_iterator itor = mMaterialIndex.find( datablock );
        if( itor != mMaterialIndex.end() )
            return itor->second;

        const uint32 materialIdx = static_cast<uint32>( mMaterials.size() );
        MaterialRecord record;
        record.datablock = datablock;
        record.materialIdx = materialIdx;

        mMaterialIndex[datablock] = materialIdx;
        mMaterials.push_back( record );
        mDirty = true;

        return materialIdx;
    }
    //-------------------------------------------------------------------------
    void PathTracerMaterialCache::removeDatablock( HlmsPbsDatablock *datablock )
    {
        MaterialIndexMap::iterator itor = mMaterialIndex.find( datablock );
        if( itor == mMaterialIndex.end() )
            return;

        const uint32 removedIdx = itor->second;
        mMaterialIndex.erase( itor );
        mMaterials.erase( mMaterials.begin() + removedIdx );

        for( size_t i = removedIdx; i < mMaterials.size(); ++i )
        {
            mMaterials[i].materialIdx = static_cast<uint32>( i );
            mMaterialIndex[mMaterials[i].datablock] = static_cast<uint32>( i );
        }

        mDirty = true;
    }
    //-------------------------------------------------------------------------
    void PathTracerMaterialCache::clear()
    {
        mMaterialIndex.clear();
        mMaterials.clear();
        mDirty = true;
    }
}
