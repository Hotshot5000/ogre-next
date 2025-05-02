//
//  MetalViewCallbacks.hpp
//  RenderSystem_Metal
//
//  Created by Sebastian Bugiu on 23/07/2018.
//

#ifndef MetalViewCallbacks_hpp
#define MetalViewCallbacks_hpp

class MetalViewCallbacks
{
public:
    virtual void touchesBegan(void *touches) = 0;
    virtual void touchesMoved(void *touches) = 0;
    virtual void touchesEnded(void *touches) = 0;
    virtual void touchesCancelled(void *touches) = 0;
};

#endif /* MetalViewCallbacks_hpp */
