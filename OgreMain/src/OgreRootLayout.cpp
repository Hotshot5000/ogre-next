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

#include "OgreRootLayout.h"

#include "OgreException.h"
#include "OgreGpuProgram.h"
#include "OgreLwString.h"
#include "OgreStringConverter.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#define TODO_limit_NUM_BIND_TEXTURES  // and co.

namespace Ogre
{
    static const char *c_rootLayoutVarNames[DescBindingTypes::NumDescBindingTypes] = {
        "has_params",     //
        "const_buffers",  //
        "tex_buffers",    //
        "textures",       //
        "samplers",       //
        "uav_textures",   //
        "uav_buffers",    //
    };
    static const uint8 c_allGraphicStagesMask = ( 1u << VertexShader ) | ( 1u << PixelShader ) |
                                                ( 1u << GeometryShader ) | ( 1u << HullShader ) |
                                                ( 1u << DomainShader );
    //-------------------------------------------------------------------------
    DescBindingRange::DescBindingRange() : start( 0u ), end( 0u ) {}
    //-------------------------------------------------------------------------
    RootLayout::RootLayout() : mCompute( false ), mParamsBuffStages( 0u ) {}
    //-------------------------------------------------------------------------
    void RootLayout::validate( const String &filename ) const
    {
        // Check start <= end
        for( size_t i = 0u; i < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < DescBindingTypes::NumDescBindingTypes; ++j )
            {
                if( !mDescBindingRanges[i][j].isValid() )
                {
                    char tmpBuffer[512];
                    LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

                    tmpStr.a( "Invalid descriptor start <= end must be true. Set ", (uint32)i,
                              " specifies ", c_rootLayoutVarNames[j], " in range [",
                              mDescBindingRanges[i][j].start );
                    tmpStr.a( ", ", mDescBindingRanges[i][j].end, ")" );

                    OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                 "Error at file " + filename + ":\n" + tmpStr.c_str(),
                                 "RootLayout::validate" );
                }
            }

            // Ensure texture and texture buffer ranges don't overlap for compatibility with
            // other APIs (we support it in , but explicitly forbid it)
            {
                const DescBindingRange &bufferRange = mDescBindingRanges[i][DescBindingTypes::TexBuffer];
                if( bufferRange.isInUse() )
                {
                    for( size_t j = i; j < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++j )
                    {
                        const DescBindingRange &texRange =
                            mDescBindingRanges[j][DescBindingTypes::Texture];

                        if( texRange.isInUse() )
                        {
                            if( !( bufferRange.end <= texRange.start ||
                                   bufferRange.start >= texRange.end ) )
                            {
                                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                             "Error at file " + filename + ":\n" +
                                                 "TexBuffer and Texture slots cannot overlap for "
                                                 "compatibility with other APIs",
                                             "RootLayout::validate" );
                            }
                        }
                    }
                }
            }
            {
                const DescBindingRange &bufferRange = mDescBindingRanges[i][DescBindingTypes::UavBuffer];
                if( bufferRange.isInUse() )
                {
                    for( size_t j = i; j < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++j )
                    {
                        const DescBindingRange &texRange =
                            mDescBindingRanges[j][DescBindingTypes::UavTexture];

                        if( texRange.isInUse() )
                        {
                            if( !( bufferRange.end <= texRange.start ||
                                   bufferRange.start >= texRange.end ) )
                            {
                                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                             "Error at file " + filename + ":\n" +
                                                 "UavBuffer and UavTexture slots cannot overlap for "
                                                 "compatibility with other APIs",
                                             "RootLayout::validate" );
                            }
                        }
                    }
                }
            }
        }

        // Check range[set = 0] does not overlap with range[set = 1] and comes before set = 1
        // TODO: Do we really need this restriction? Even so, this restriction keeps things sane?
        for( size_t i = 0u; i < DescBindingTypes::NumDescBindingTypes; ++i )
        {
            for( size_t j = 0u; j < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS - 1u; ++j )
            {
                if( mDescBindingRanges[j][i].end > mDescBindingRanges[j + 1][i].start &&
                    mDescBindingRanges[j + 1][i].isInUse() )
                {
                    char tmpBuffer[1024];
                    LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

                    tmpStr.a( "Descriptors cannot overlap across sets. However set ", (uint32)j,
                              " specifies ", c_rootLayoutVarNames[i], " in range [",
                              mDescBindingRanges[j][i].start );
                    tmpStr.a( ", ", (uint32)mDescBindingRanges[j][i].end, ") but set ",
                              ( uint32 )( j + 1u ), " is in range [",
                              mDescBindingRanges[j + 1][i].start );
                    tmpStr.a( ", ", mDescBindingRanges[j + 1][i].end, ")" );

                    OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                 "Error at file " + filename + ":\n" + tmpStr.c_str(),
                                 "RootLayout::validate" );
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    void RootLayout::parseSet( const rapidjson::Value &jsonValue, const size_t setIdx,
                               const String &filename )
    {
        rapidjson::Value::ConstMemberIterator itor;

        for( size_t i = 0u; i < DescBindingTypes::NumDescBindingTypes; ++i )
        {
            itor = jsonValue.FindMember( c_rootLayoutVarNames[i] );

            if( i == DescBindingTypes::ParamBuffer )
            {
                if( itor != jsonValue.MemberEnd() && itor->value.IsArray() )
                {
                    if( mParamsBuffStages != 0u )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "All 'has_params' declarations must live in the same set",
                                     "RootLayout::parseSet" );
                    }

                    const size_t numStages = itor->value.Size();
                    for( size_t j = 0u; j < numStages; ++j )
                    {
                        if( itor->value[j].IsString() )
                        {
                            if( !mCompute )
                            {
                                const char *stageAcronyms[NumShaderTypes] = { "vs", "ps", "gs", "hs",
                                                                              "ds" };
                                for( size_t k = 0u; k < NumShaderTypes; ++k )
                                {
                                    if( strncmp( itor->value[j].GetString(), stageAcronyms[k], 2u ) ==
                                        0 )
                                    {
                                        mParamsBuffStages |= 1u << k;
                                        ++mDescBindingRanges[setIdx][i].end;
                                    }
                                }

                                if( strncmp( itor->value[j].GetString(), "all", 3u ) == 0 )
                                {
                                    mParamsBuffStages = c_allGraphicStagesMask;
                                    mDescBindingRanges[setIdx][i].end = NumShaderTypes;
                                }
                            }
                            else
                            {
                                if( strncmp( itor->value[j].GetString(), "cs", 2u ) == 0 ||
                                    strncmp( itor->value[j].GetString(), "all", 3u ) == 0 )
                                {
                                    mParamsBuffStages = 1u << GPT_COMPUTE_PROGRAM;
                                    mDescBindingRanges[setIdx][i].end = 1u;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                if( itor != jsonValue.MemberEnd() && itor->value.IsArray() && itor->value.Size() == 2u &&
                    itor->value[0].IsUint() && itor->value[1].IsUint() )
                {
                    if( itor->value[0].GetUint() > 65535 || itor->value[1].GetUint() > 65535 )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "Root Layout descriptors must be in range [0; 65535]",
                                     "RootLayout::parseSet" );
                    }

                    TODO_limit_NUM_BIND_TEXTURES;  // Only for dynamic sets

                    mDescBindingRanges[setIdx][i].start =
                        static_cast<uint16>( itor->value[0].GetUint() );
                    mDescBindingRanges[setIdx][i].end = static_cast<uint16>( itor->value[1].GetUint() );

                    if( mDescBindingRanges[setIdx][i].start > mDescBindingRanges[setIdx][i].end )
                    {
                        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                                     "Error at file " + filename +
                                         ":\n"
                                         "Root Layout descriptors must satisfy start < end",
                                     "RootLayout::parseSet" );
                    }
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    void RootLayout::copyFrom( const RootLayout &other )
    {
        this->mCompute = other.mCompute;
        this->mParamsBuffStages = other.mParamsBuffStages;
        for( size_t i = 0u; i < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < DescBindingTypes::NumDescBindingTypes; ++j )
                this->mDescBindingRanges[i][j] = other.mDescBindingRanges[i][j];
        }
    }
    //-------------------------------------------------------------------------
    void RootLayout::parseRootLayout( const char *rootLayout, const bool bCompute,
                                      const String &filename )
    {
        mCompute = bCompute;
        mParamsBuffStages = 0u;

        rapidjson::Document d;
        d.Parse( rootLayout );

        if( d.HasParseError() )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "Invalid JSON string in file " + filename + " at line " +
                             StringConverter::toString( d.GetErrorOffset() ) +
                             " Reason: " + rapidjson::GetParseError_En( d.GetParseError() ),
                         "RootLayout::parseRootLayout" );
        }

        char tmpBuffer[16];
        LwString tmpStr( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );

        for( size_t i = 0u; i < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < DescBindingTypes::NumDescBindingTypes; ++j )
                mDescBindingRanges[i][j] = DescBindingRange();

            tmpStr.clear();
            tmpStr.a( static_cast<uint32_t>( i ) );

            rapidjson::Value::ConstMemberIterator itor;

            itor = d.FindMember( tmpStr.c_str() );
            if( itor != d.MemberEnd() && itor->value.IsObject() )
                parseSet( itor->value, i, filename );
        }

        validate( filename );
    }
    //-------------------------------------------------------------------------
    size_t RootLayout::calculateNumUsedSets( void ) const
    {
        size_t numSets = 0u;
        for( size_t i = 0u; i < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            for( size_t j = 0u; j < DescBindingTypes::NumDescBindingTypes; ++j )
            {
                if( mDescBindingRanges[i][j].isInUse() )
                    numSets = i + 1u;
            }
        }
        return numSets;
    }
    //-------------------------------------------------------------------------
    size_t RootLayout::calculateNumBindings( const size_t setIdx ) const
    {
        size_t numBindings = 0u;
        for( size_t i = 0u; i < DescBindingTypes::NumDescBindingTypes; ++i )
            numBindings += mDescBindingRanges[setIdx][i].getNumUsedSlots();

        return numBindings;
    }
    //-------------------------------------------------------------------------
    bool RootLayout::findParamsBuffer( uint32 shaderStage, size_t &outSetIdx,
                                       size_t &outBindingIdx ) const
    {
        if( !( mParamsBuffStages & ( 1u << shaderStage ) ) )
            return false;  // There is no param buffer in this stage

        size_t numStagesWithParams = 0u;
        if( mCompute )
        {
            numStagesWithParams = 1u;
        }
        else
        {
            for( size_t i = 0u; i < shaderStage; ++i )
            {
                if( mParamsBuffStages & ( 1u << i ) )
                    ++numStagesWithParams;
            }
        }

        for( size_t i = 0u; i < OGRE_MAX_NUM_BOUND_DESCRIPTOR_SETS; ++i )
        {
            if( mDescBindingRanges[i][DescBindingTypes::ParamBuffer].isInUse() )
            {
                outSetIdx = i;
                outBindingIdx = numStagesWithParams;
                return true;
            }
        }

        OGRE_ASSERT_LOW( false && "This path should not be reached" );

        return false;
    }
}  // namespace Ogre
