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

#include "RTShadows/OgreRTShadowsMeshCache.h"

#include "OgreStableHeaders.h"

#include "OgreRoot.h"
#include "OgreRenderSystem.h"

#include "OgreMesh2.h"
#include "OgreSceneManager.h"
#include "OgreSubMesh2.h"
#include "OgreItem.h"

namespace Ogre
{
    RTShadowsMeshCache::RTShadowsMeshCache() :
        mRebuildBlas( true ),
        mRebuildTlas( true )
    {
        
    }
    //-------------------------------------------------------------------------
    RTShadowsMeshCache::~RTShadowsMeshCache()
    {
        
    }
    //-------------------------------------------------------------------------
    const RTShadowsMeshCache::ShadowsCachedMesh &RTShadowsMeshCache::addMeshToCache(
        const MeshPtr &mesh, Item *refItem )
    {
        const String &meshName = mesh->getName();
        MeshCacheMap::iterator itor = mMeshCaches.find( meshName );
        if( itor == mMeshCaches.end() )
        {
            ShadowsCachedMesh shadowCachedMesh;
            shadowCachedMesh.meshName = meshName;
            shadowCachedMesh.mesh = mesh.get();
            shadowCachedMesh.blasStart = 0u;
            shadowCachedMesh.numBlas = 0u;
            itor = mMeshCaches.insert( std::pair<IdString, ShadowsCachedMesh>( meshName, shadowCachedMesh ) ).first;
            mMeshes.push_back( mesh );
            mRebuildBlas = true;
        }
        mItems.push_back( refItem );
        mRebuildTlas = true;
        return itor->second;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::removeMeshFromCache( Item *item )
    {
        ItemArray::iterator itor = std::find( mItems.begin(), mItems.end(), item );
        if( itor == mItems.end() )
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND, "", "RTShadowsMeshCache::removeMeshFromCache" );

        mItems.erase( itor );
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::removeAllItems()
    {
        mItems.clear();
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::updateAS()
    {
        std::vector<VertexArrayObject *> meshVaos;
        if( mRebuildBlas )
        {
            uint32 blasIdx = 0u;
            MeshPtrArray::iterator itor = mMeshes.begin();
            MeshPtrArray::iterator end = mMeshes.end();

            while( itor != end )
            {
                const Mesh *mesh = itor->get();
                const unsigned numSubmeshes = mesh->getNumSubMeshes();
                MeshCacheMap::iterator meshCacheIt = mMeshCaches.find( mesh->getName() );
                if( meshCacheIt != mMeshCaches.end() )
                {
                    meshCacheIt->second.blasStart = blasIdx;
                    meshCacheIt->second.numBlas = numSubmeshes;
                }

                for( unsigned subMeshIdx = 0; subMeshIdx < numSubmeshes; ++subMeshIdx )
                {
                    SubMesh *subMesh = mesh->getSubMesh( subMeshIdx );
                    VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
                    meshVaos.push_back( vao );
                    ++blasIdx;
                }

                ++itor;
            }
        }
        
        ItemArray::iterator itemItor = mItems.begin();
        ItemArray::iterator itemEnd = mItems.end();
        
        std::vector<uint32> instanceMeshIndex;
        std::vector<Matrix4> instanceTransform;
        
        while( itemItor != itemEnd )
        {
            const Item *item = *itemItor;
            
            MeshCacheMap::iterator meshCacheIt = mMeshCaches.find( item->getMesh()->getName() );
            if( meshCacheIt != mMeshCaches.end() )
            {
                const ShadowsCachedMesh &cachedMesh = meshCacheIt->second;
                const Ogre::Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                for( uint32 blasOffset = 0u; blasOffset < cachedMesh.numBlas; ++blasOffset )
                {
                    instanceMeshIndex.push_back( cachedMesh.blasStart + blasOffset );
                    instanceTransform.push_back( transform );
                }
            }
            
            ++itemItor;
        }
        
        RenderSystem *renderSystem = Root::getSingleton().getRenderSystem();
        if( instanceMeshIndex.empty() )
        {
            renderSystem->clearAccelerationStructure();
            mRebuildTlas = true;
            return;
        }

        if( mRebuildBlas )
        {
            renderSystem->createAccelerationStructure( mMeshes, meshVaos, instanceMeshIndex, instanceTransform );
            mRebuildBlas = false;
            mRebuildTlas = false;
        }
        else if( mRebuildTlas )
        {
            renderSystem->rebuildAccelerationStructure( instanceMeshIndex, instanceTransform );
            mRebuildTlas = false;
        }
        else
        {
            renderSystem->refitAccelerationStructure( instanceMeshIndex, instanceTransform );
        }
    }
}
