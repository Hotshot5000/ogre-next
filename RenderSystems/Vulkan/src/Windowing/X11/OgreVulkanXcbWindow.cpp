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

#include "Windowing/X11/OgreVulkanXcbWindow.h"

#include "OgreVulkanDevice.h"
#include "OgreVulkanTextureGpu.h"
#include "OgreVulkanTextureGpuManager.h"

#include "OgreTextureGpuListener.h"

#include "OgreException.h"
#include "OgrePixelFormatGpuUtils.h"
#include "OgreString.h"
#include "OgreWindowEventUtilities.h"

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan_xcb.h"

#define TODO_check_support_and_check_DepthBuffer_DefaultDepthBufferFormat

namespace Ogre
{
    static xcb_intern_atom_cookie_t intern_atom_cookie( xcb_connection_t *c, const std::string &s )
    {
        return xcb_intern_atom( c, false, static_cast<uint16>( s.size() ), s.c_str() );
    }

    static xcb_atom_t intern_atom( xcb_connection_t *c, xcb_intern_atom_cookie_t cookie )
    {
        xcb_atom_t atom = XCB_ATOM_NONE;
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply( c, cookie, 0 );
        if( reply )
        {
            atom = reply->atom;
            free( reply );
        }
        return atom;
    }

    VulkanXcbWindow::VulkanXcbWindow( FastArray<const char *> &inOutRequiredInstanceExts,
                                      const String &title, uint32 width, uint32 height,
                                      bool fullscreenMode ) :
        VulkanWindow( title, width, height, fullscreenMode ),
        mConnection( 0 ),
        mScreen( 0 ),
        mXcbWindow( 0 ),
        mWmProtocols( 0 ),
        mWmDeleteWindow( 0 ),
        mVisible( true ),
        mHidden( false ),
        mIsTopLevel( true ),
        mIsExternal( false )
    {
        inOutRequiredInstanceExts.push_back( VK_KHR_XCB_SURFACE_EXTENSION_NAME );
    }
    //-------------------------------------------------------------------------
    VulkanXcbWindow::~VulkanXcbWindow()
    {
        destroy();

        if( mTexture )
        {
            mTexture->notifyAllListenersTextureChanged( TextureGpuListener::Deleted );
            OGRE_DELETE mTexture;
            mTexture = 0;
        }
        if( mDepthBuffer )
        {
            mDepthBuffer->notifyAllListenersTextureChanged( TextureGpuListener::Deleted );
            OGRE_DELETE mDepthBuffer;
            mDepthBuffer = 0;
        }
        // Depth & Stencil buffers are the same pointer
        // OGRE_DELETE mStencilBuffer;
        mStencilBuffer = 0;

        xcb_destroy_window( mConnection, mXcbWindow );
        xcb_flush( mConnection );

        xcb_disconnect( mConnection );
        mConnection = 0;
    }
    //-----------------------------------------------------------------------------------
    void VulkanXcbWindow::destroy( void )
    {
        VulkanWindow::destroy();

        if( mClosed )
            return;

        mClosed = true;
        mFocused = false;

        if( !mIsExternal )
            WindowEventUtilities::_removeRenderWindow( this );

        if( mFullscreenMode )
        {
            // switchFullScreen( false );
            mRequestedFullscreenMode = false;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::_initialize( TextureGpuManager *textureGpuManager,
                                       const NameValuePairList *miscParams )
    {
        destroy();

        mFocused = true;
        mClosed = false;

        if( miscParams )
        {
            NameValuePairList::const_iterator opt;
            NameValuePairList::const_iterator end = miscParams->end();

            opt = miscParams->find( "SDL2x11" );
            if( opt != end )
            {
                struct SDLx11
                {
                    Display *display; /**< The X11 display */
                    ::Window window;  /**< The X11 window */
                };

                SDLx11 *sdlHandles =
                    reinterpret_cast<SDLx11 *>( StringConverter::parseUnsignedLong( opt->second ) );
                mConnection = XGetXCBConnection( sdlHandles->display );
                mXcbWindow = (xcb_window_t)sdlHandles->window;

                XWindowAttributes windowAttrib;
                XGetWindowAttributes( sdlHandles->display, sdlHandles->window, &windowAttrib );

                int scr = DefaultScreen( sdlHandles->display );

                const xcb_setup_t *setup = xcb_get_setup( mConnection );
                xcb_screen_iterator_t iter = xcb_setup_roots_iterator( setup );
                while( scr-- > 0 )
                    xcb_screen_next( &iter );

                mScreen = iter.data;
            }
        }

        if( !mXcbWindow )
        {
            initConnection();  // TODO: Connection must be shared by ALL windows
            createWindow( mTitle, mRequestedWidth, mRequestedHeight, miscParams );
            setHidden( false );
        }

        PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR get_xcb_presentation_support =
            (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)vkGetInstanceProcAddr(
                mDevice->mInstance, "vkGetPhysicalDeviceXcbPresentationSupportKHR" );
        PFN_vkCreateXcbSurfaceKHR create_xcb_surface = (PFN_vkCreateXcbSurfaceKHR)vkGetInstanceProcAddr(
            mDevice->mInstance, "vkCreateXcbSurfaceKHR" );

        if( !get_xcb_presentation_support( mDevice->mPhysicalDevice, mDevice->mGraphicsQueue.mFamilyIdx,
                                           mConnection, mScreen->root_visual ) )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Vulkan not supported on given X11 window",
                         "VulkanXcbWindow::_initialize" );
        }

        VkXcbSurfaceCreateInfoKHR xcbSurfCreateInfo;
        memset( &xcbSurfCreateInfo, 0, sizeof( xcbSurfCreateInfo ) );
        xcbSurfCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        xcbSurfCreateInfo.connection = mConnection;
        xcbSurfCreateInfo.window = mXcbWindow;
        create_xcb_surface( mDevice->mInstance, &xcbSurfCreateInfo, 0, &mSurfaceKHR );

        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( textureGpuManager );

        mTexture = textureManager->createTextureGpuWindow( this );
        mDepthBuffer = textureManager->createWindowDepthBuffer();
        mStencilBuffer = mDepthBuffer;

        // mDepthBuffer->setPixelFormat( PFG_D32_FLOAT_S8X24_UINT );

        bool hwGamma = true;

        setFinalResolution( mRequestedWidth, mRequestedHeight );
        mTexture->setPixelFormat( chooseSurfaceFormat( hwGamma ) );
        TODO_check_support_and_check_DepthBuffer_DefaultDepthBufferFormat;
        mDepthBuffer->setPixelFormat( PFG_D32_FLOAT_S8X24_UINT );
        if( PixelFormatGpuUtils::isStencil( mDepthBuffer->getPixelFormat() ) )
            mStencilBuffer = mDepthBuffer;

        mTexture->_transitionTo( GpuResidency::Resident, (uint8 *)0 );
        mDepthBuffer->_transitionTo( GpuResidency::Resident, (uint8 *)0 );

        createSwapchain();
    }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::initConnection( void )
    {
        int scr = 0;

        mConnection = xcb_connect( 0, &scr );
        if( !mConnection || xcb_connection_has_error( mConnection ) )
        {
            xcb_disconnect( mConnection );
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "failed to connect to the display server",
                         "VulkanXcbWindow::initConnection" );
        }

        const xcb_setup_t *setup = xcb_get_setup( mConnection );
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator( setup );
        while( scr-- > 0 )
            xcb_screen_next( &iter );

        mScreen = iter.data;
    }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::createWindow( const String &windowName, uint32 width, uint32 height,
                                        const NameValuePairList *miscParams )
    {
        mXcbWindow = xcb_generate_id( mConnection );

        uint32_t value_mask, value_list[32];
        value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
        value_list[0] = mScreen->black_pixel;
        value_list[1] = /*XCB_EVENT_MASK_KEY_PRESS |*/ XCB_EVENT_MASK_STRUCTURE_NOTIFY;

        xcb_create_window( mConnection, XCB_COPY_FROM_PARENT, mXcbWindow, mScreen->root, 0, 0,
                           static_cast<uint16>( width ), static_cast<uint16>( height ), 10u,
                           XCB_WINDOW_CLASS_INPUT_OUTPUT, mScreen->root_visual, value_mask, value_list );

        xcb_intern_atom_cookie_t utf8_string_cookie = intern_atom_cookie( mConnection, "UTF8_STRING" );
        xcb_intern_atom_cookie_t wm_name_cookie = intern_atom_cookie( mConnection, "WM_NAME" );
        xcb_intern_atom_cookie_t mWmProtocolscookie = intern_atom_cookie( mConnection, "WM_PROTOCOLS" );
        xcb_intern_atom_cookie_t mWmDeleteWindowcookie =
            intern_atom_cookie( mConnection, "WM_DELETE_WINDOW" );

        // set title
        xcb_atom_t utf8_string = intern_atom( mConnection, utf8_string_cookie );
        xcb_atom_t wm_name = intern_atom( mConnection, wm_name_cookie );
        xcb_change_property( mConnection, XCB_PROP_MODE_REPLACE, mXcbWindow, wm_name, utf8_string, 8u,
                             static_cast<uint32>( windowName.size() ), windowName.c_str() );

        // advertise WM_DELETE_WINDOW
        mWmProtocols = intern_atom( mConnection, mWmProtocolscookie );
        mWmDeleteWindow = intern_atom( mConnection, mWmDeleteWindowcookie );
        xcb_change_property( mConnection, XCB_PROP_MODE_REPLACE, mXcbWindow, mWmProtocols, XCB_ATOM_ATOM,
                             32u, 1u, &mWmDeleteWindow );

        WindowEventUtilities::_addRenderWindow( this );
    }
    //-----------------------------------------------------------------------------------
    void VulkanXcbWindow::reposition( int32 left, int32 top )
    {
        if( mClosed || !mIsTopLevel )
            return;

        const int32 values[2] = { left, top };
        xcb_configure_window( mConnection, mXcbWindow, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                              values );
    }
    //-----------------------------------------------------------------------------------
    void VulkanXcbWindow::requestResolution( uint32 width, uint32 height )
    {
        if( mClosed )
            return;

        if( mTexture && mTexture->getWidth() == width && mTexture->getHeight() == height )
            return;

        Window::requestResolution( width, height );

        if( width != 0 && height != 0 )
        {
            if( !mIsExternal )
            {
                const uint32_t values[] = { width, height };
                xcb_configure_window( mConnection, mXcbWindow,
                                      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values );
            }
            else
            {
                setFinalResolution( width, height );
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void VulkanXcbWindow::windowMovedOrResized( void )
    {
        if( mClosed || !mXcbWindow )
            return;

        xcb_get_geometry_cookie_t geomCookie = xcb_get_geometry( mConnection, mXcbWindow );
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply( mConnection, geomCookie, NULL );

        if( mIsTopLevel && !mFullscreenMode )
        {
            mLeft = geom->x;
            mTop = geom->y;
        }

        mDevice->stall();

        destroySwapchain();
        setFinalResolution( geom->width, geom->height );
        createSwapchain();

        free( geom );
    }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::_setVisible( bool visible ) { mVisible = visible; }
    //-------------------------------------------------------------------------
    bool VulkanXcbWindow::isVisible( void ) const { return mVisible; }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::setHidden( bool hidden )
    {
        mHidden = hidden;

        // ignore for external windows as these should handle
        // this externally
        if( mIsExternal )
            return;

        if( !hidden )
            xcb_map_window( mConnection, mXcbWindow );
        else
            xcb_unmap_window( mConnection, mXcbWindow );
        xcb_flush( mConnection );
    }
    //-------------------------------------------------------------------------
    bool VulkanXcbWindow::isHidden( void ) const { return false; }
    //-------------------------------------------------------------------------
    void VulkanXcbWindow::getCustomAttribute( IdString name, void *pData )
    {
        if( name == "xcb_connection_t" )
        {
            *static_cast<xcb_connection_t **>( pData ) = mConnection;
            return;
        }
        else if( name == "mWmProtocols" )
        {
            *static_cast<xcb_atom_t *>( pData ) = mWmProtocols;
            return;
        }
        else if( name == "mWmDeleteWindow" )
        {
            *static_cast<xcb_atom_t *>( pData ) = mWmDeleteWindow;
            return;
        }
        else if( name == "xcb_window_t" )
        {
            *static_cast<xcb_window_t *>( pData ) = mXcbWindow;
            return;
        }
    }
}  // namespace Ogre
