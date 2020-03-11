#pragma once
#include "OgreVulkanWindow.h"

namespace Ogre
{

class OgreVulkanWin32Window : public VulkanWindow
{
public:
    OgreVulkanWin32Window( const String &title, uint32 width, uint32 height, bool fullscreenMode );

    virtual ~OgreVulkanWin32Window();

    virtual void reposition( int32 left, int32 top ) override;
    virtual void _setVisible( bool visible ) override;
    virtual bool isVisible() const override;
    virtual void setHidden( bool hidden ) override;
    virtual bool isHidden() const override;
    virtual void _initialize( TextureGpuManager *textureGpuManager ) override;
    
};

}  // namespace Ogre
