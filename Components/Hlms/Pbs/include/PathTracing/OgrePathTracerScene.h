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

#ifndef OgrePathTracerScene_h
#define OgrePathTracerScene_h

#include "OgreHlmsPbsPrerequisites.h"
#include "PathTracing/OgrePathTracerMaterialCache.h"

#include "OgreHeaderPrefix.h"

namespace Ogre
{
    class _OgreHlmsPbsExport PathTracerScene
    {
    public:
        typedef FastArray<Item *> ItemArray;

    private:
        ItemArray               mItems;
        PathTracerMaterialCache mMaterialCache;
        bool                    mRebuildBlas;
        bool                    mRebuildTlas;
        bool                    mEnabled;

    public:
        PathTracerScene();
        ~PathTracerScene();

        void addItem( Item *item );
        void removeItem( Item *item );
        void removeAllItems();

        void markGeometryDirty();
        void markInstancesDirty();
        void markMaterialsDirty();
        void clearDirtyFlags();

        void setEnabled( bool enabled ) { mEnabled = enabled; }
        bool getEnabled() const { return mEnabled; }

        bool needsBlasRebuild() const { return mRebuildBlas; }
        bool needsTlasRebuild() const { return mRebuildTlas; }
        bool needsMaterialUpload() const { return mMaterialCache.getDirty(); }

        const ItemArray &getItems() const { return mItems; }
        PathTracerMaterialCache &getMaterialCache() { return mMaterialCache; }
        const PathTracerMaterialCache &getMaterialCache() const { return mMaterialCache; }
    };
}

#include "OgreHeaderSuffix.h"

#endif /* OgrePathTracerScene_h */
