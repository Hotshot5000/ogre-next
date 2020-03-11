#include "OgreVulkanWin32Window.h"

namespace Ogre
{
    OgreVulkanWin32Window::OgreVulkanWin32Window( const String &title, uint32 width, uint32 height,
                                                  bool fullscreenMode ):
        VulkanWindow( title, width, height, fullscreenMode )
    {
    }

    OgreVulkanWin32Window::~OgreVulkanWin32Window()
    {
    }

    void OgreVulkanWin32Window::reposition( int32 left, int32 top )
    {
    }

    void OgreVulkanWin32Window::_setVisible( bool visible )
    {
    }

    bool OgreVulkanWin32Window::isVisible() const
    {
    }

    void OgreVulkanWin32Window::setHidden( bool hidden )
    {
    }

    bool OgreVulkanWin32Window::isHidden() const
    {
    }

    void OgreVulkanWin32Window::_initialize( TextureGpuManager *textureGpuManager )
    {
    }
}
