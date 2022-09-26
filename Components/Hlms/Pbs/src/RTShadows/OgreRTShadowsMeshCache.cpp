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
    RTShadowsMeshCache::RTShadowsMeshCache()
    {
        
    }
    //-------------------------------------------------------------------------
    RTShadowsMeshCache::~RTShadowsMeshCache()
    {
        
    }
    //-------------------------------------------------------------------------
    const RTShadowsMeshCache::ShadowsCachedMesh &RTShadowsMeshCache::addMeshToCache(
        const MeshPtr &mesh, SceneManager *sceneManager, RenderSystem *renderSystem,
        HlmsManager *hlmsManager, Item *refItem )
    {
        const String &meshName = mesh->getName();
        MeshCacheMap::iterator itor = mMeshCaches.find( meshName );
        if( itor != mMeshCaches.end() )
        {
            
        }
        else
        {
            ShadowsCachedMesh shadowCachedMesh;
            shadowCachedMesh.meshName = meshName;
            shadowCachedMesh.mesh = mesh.get();
            shadowCachedMesh.meshIndex = mMeshes.size();
            itor = mMeshCaches.insert( std::pair<IdString, ShadowsCachedMesh>( meshName, shadowCachedMesh ) ).first;
            mItems.push_back( refItem );
            mMeshes.push_back( mesh );
        }
        return itor->second;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::updateAS()
    {
        std::vector<VertexArrayObject *> meshVaos;
        MeshPtrArray::iterator itor = mMeshes.begin();
        MeshPtrArray::iterator end = mMeshes.end();

        while( itor != end )
        {
            const Mesh *mesh = itor->get();
            const unsigned numSubmeshes = mesh->getNumSubMeshes();

            for( unsigned subMeshIdx = 0; subMeshIdx < numSubmeshes; ++subMeshIdx )
            {
                SubMesh *subMesh = mesh->getSubMesh( subMeshIdx );
                VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
                
                meshVaos.push_back( vao );

                size_t numVertices = vao->getBaseVertexBuffer()->getNumElements();

                IndexBufferPacked *indexBuffer = vao->getIndexBuffer();
                
                
            }
            
            ++itor;
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
                
            }
            else
            {
                const ShadowsCachedMesh &cachedMesh = meshCacheIt->second;
                instanceMeshIndex.push_back( cachedMesh.meshIndex );
                const Ogre::Matrix4 transform = item->getParentSceneNode()->_getFullTransform();
                instanceTransform.push_back( transform );
            }
            
            
            ++itemItor;
        }
        
        RenderSystem *renderSystem = Root::getSingleton().getRenderSystem();
        renderSystem->createAccelerationStructure( mMeshes, meshVaos, instanceMeshIndex, instanceTransform );
        
//        MeshCacheMap::iterator itor = mMeshCaches.begin();
//        MeshCacheMap::iterator end = mMeshCaches.end();
//
//        while( itor != end )
//        {
//
//
//
//            ++itor;
//        }
        
    }
}
