/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

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

#include "OgreVulkanWin32Support.h"

#include <sstream>

namespace Ogre
{

    template <class C>
    void remove_duplicates( C &c )
    {
        std::sort( c.begin(), c.end() );
        typename C::iterator p = std::unique( c.begin(), c.end() );
        c.erase( p, c.end() );
    }

    VulkanWin32Support::VulkanWin32Support()
    {
    }

    void VulkanWin32Support::addConfig()
    {
        // TODO: EnumDisplayDevices http://msdn.microsoft.com/library/en-us/gdi/devcons_2303.asp
        /*vector<string> DisplayDevices;
        DISPLAY_DEVICE DisplayDevice;
        DisplayDevice.cb = sizeof(DISPLAY_DEVICE);
        DWORD i=0;
        while (EnumDisplayDevices(NULL, i++, &DisplayDevice, 0) {
            DisplayDevices.push_back(DisplayDevice.DeviceName);
        }*/

        ConfigOption optFullScreen;
        ConfigOption optVideoMode;
        ConfigOption optColourDepth;
        ConfigOption optDisplayFrequency;
        ConfigOption optVSync;
        ConfigOption optVSyncInterval;
        ConfigOption optFSAA;
        ConfigOption optRTTMode;
        ConfigOption optSRGB;
#if OGRE_NO_QUAD_BUFFER_STEREO == 0
        ConfigOption optStereoMode;
#endif

        // FS setting possibilities
        optFullScreen.name = "Full Screen";
        optFullScreen.possibleValues.push_back( "Yes" );
        optFullScreen.possibleValues.push_back( "No" );
        optFullScreen.currentValue = "Yes";
        optFullScreen.immutable = false;

        // Video mode possibilities
        DEVMODE DevMode;
        DevMode.dmSize = sizeof( DEVMODE );
        optVideoMode.name = "Video Mode";
        optVideoMode.immutable = false;
        for( DWORD i = 0; EnumDisplaySettings( NULL, i, &DevMode ); ++i )
        {
            if( DevMode.dmBitsPerPel < 16 || DevMode.dmPelsHeight < 480 )
                continue;
            mDevModes.push_back( DevMode );
            StringStream str;
            str << DevMode.dmPelsWidth << " x " << DevMode.dmPelsHeight;
            optVideoMode.possibleValues.push_back( str.str() );
        }
        remove_duplicates( optVideoMode.possibleValues );
        optVideoMode.currentValue = optVideoMode.possibleValues.front();

        optColourDepth.name = "Colour Depth";
        optColourDepth.immutable = false;
        optColourDepth.currentValue.clear();

        optDisplayFrequency.name = "Display Frequency";
        optDisplayFrequency.immutable = false;
        optDisplayFrequency.currentValue.clear();

        optVSync.name = "VSync";
        optVSync.immutable = false;
        optVSync.possibleValues.push_back( "No" );
        optVSync.possibleValues.push_back( "Yes" );
        optVSync.currentValue = "No";

        optVSyncInterval.name = "VSync Interval";
        optVSyncInterval.immutable = false;
        optVSyncInterval.possibleValues.push_back( "1" );
        optVSyncInterval.possibleValues.push_back( "2" );
        optVSyncInterval.possibleValues.push_back( "3" );
        optVSyncInterval.possibleValues.push_back( "4" );
        optVSyncInterval.currentValue = "1";

        optFSAA.name = "FSAA";
        optFSAA.immutable = false;
        optFSAA.possibleValues.push_back( "1" );
        optFSAA.possibleValues.push_back( "2" );
        optFSAA.possibleValues.push_back( "4" );
        optFSAA.possibleValues.push_back( "8" );
        optFSAA.possibleValues.push_back( "16" );
        for( vector<int>::type::iterator it = mFSAALevels.begin(); it != mFSAALevels.end(); ++it )
        {
            String val = StringConverter::toString( *it );
            optFSAA.possibleValues.push_back( val );
            /* not implementing CSAA in GL for now
            if (*it >= 8)
                optFSAA.possibleValues.push_back(val + " [Quality]");
            */
        }
        optFSAA.currentValue = "1";

        optRTTMode.name = "RTT Preferred Mode";
        optRTTMode.possibleValues.push_back( "FBO" );
        optRTTMode.possibleValues.push_back( "PBuffer" );
        optRTTMode.possibleValues.push_back( "Copy" );
        optRTTMode.currentValue = "FBO";
        optRTTMode.immutable = false;

        // SRGB on auto window
        optSRGB.name = "sRGB Gamma Conversion";
        optSRGB.possibleValues.push_back( "Yes" );
        optSRGB.possibleValues.push_back( "No" );
        optSRGB.currentValue = "Yes";
        optSRGB.immutable = false;

#if OGRE_NO_QUAD_BUFFER_STEREO == 0
        optStereoMode.name = "Stereo Mode";
        optStereoMode.possibleValues.push_back( StringConverter::toString( SMT_NONE ) );
        optStereoMode.possibleValues.push_back( StringConverter::toString( SMT_FRAME_SEQUENTIAL ) );
        optStereoMode.currentValue = optStereoMode.possibleValues[0];
        optStereoMode.immutable = false;

        mOptions[optStereoMode.name] = optStereoMode;
#endif

        mOptions[optFullScreen.name] = optFullScreen;
        mOptions[optVideoMode.name] = optVideoMode;
        mOptions[optColourDepth.name] = optColourDepth;
        mOptions[optDisplayFrequency.name] = optDisplayFrequency;
        mOptions[optVSync.name] = optVSync;
        mOptions[optVSyncInterval.name] = optVSyncInterval;
        mOptions[optFSAA.name] = optFSAA;
        mOptions[optRTTMode.name] = optRTTMode;
        mOptions[optSRGB.name] = optSRGB;

        refreshConfig();
    }

    void VulkanWin32Support::refreshConfig()
    {
        ConfigOptionMap::iterator optVideoMode = mOptions.find( "Video Mode" );
        ConfigOptionMap::iterator moptColourDepth = mOptions.find( "Colour Depth" );
        ConfigOptionMap::iterator moptDisplayFrequency = mOptions.find( "Display Frequency" );
        if( optVideoMode == mOptions.end() || moptColourDepth == mOptions.end() ||
            moptDisplayFrequency == mOptions.end() )
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS, "Can't find mOptions!",
                         "Win32GLSupport::refreshConfig" );
        ConfigOption *optColourDepth = &moptColourDepth->second;
        ConfigOption *optDisplayFrequency = &moptDisplayFrequency->second;

        const String &val = optVideoMode->second.currentValue;
        String::size_type pos = val.find( 'x' );
        if( pos == String::npos )
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS, "Invalid Video Mode provided",
                         "Win32GLSupport::refreshConfig" );
        DWORD width = StringConverter::parseUnsignedInt( val.substr( 0, pos ) );
        DWORD height = StringConverter::parseUnsignedInt( val.substr( pos + 1, String::npos ) );

        for( vector<DEVMODE>::type::const_iterator i = mDevModes.begin(); i != mDevModes.end(); ++i )
        {
            if( i->dmPelsWidth != width || i->dmPelsHeight != height )
                continue;
            optColourDepth->possibleValues.push_back(
                StringConverter::toString( (unsigned int)i->dmBitsPerPel ) );
            optDisplayFrequency->possibleValues.push_back(
                StringConverter::toString( (unsigned int)i->dmDisplayFrequency ) );
        }
        remove_duplicates( optColourDepth->possibleValues );
        remove_duplicates( optDisplayFrequency->possibleValues );
        optColourDepth->currentValue = optColourDepth->possibleValues.back();
        bool freqValid =
            std::find( optDisplayFrequency->possibleValues.begin(),
                       optDisplayFrequency->possibleValues.end(),
                       optDisplayFrequency->currentValue ) != optDisplayFrequency->possibleValues.end();

        if( ( optDisplayFrequency->currentValue != "N/A" ) && !freqValid )
            optDisplayFrequency->currentValue = optDisplayFrequency->possibleValues.front();
    }

    void VulkanWin32Support::setConfigOption( const String &name, const String &value )
    {
        ConfigOptionMap::iterator it = mOptions.find( name );

        // Update
        if( it != mOptions.end() )
            it->second.currentValue = value;
        else
        {
            StringStream str;
            str << "Option named '" << name << "' does not exist.";
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS, str.str(), "Win32GLSupport::setConfigOption" );
        }

        if( name == "Video Mode" )
            refreshConfig();

        if( name == "Full Screen" )
        {
            it = mOptions.find( "Display Frequency" );
            if( value == "No" )
            {
                it->second.currentValue = "N/A";
                it->second.immutable = true;
            }
            else
            {
                if( it->second.currentValue.empty() || it->second.currentValue == "N/A" )
                    it->second.currentValue = it->second.possibleValues.front();
                it->second.immutable = false;
            }
        }
    }

    String VulkanWin32Support::validateConfig()
    {
        // TODO, DX9
        return BLANKSTRING;
    }
}
