//
//  MetalDeviceIntf.h
//  OGRE
//
//  Created by Sebastian Bugiu on 09.07.2023.
//

#ifndef MetalDeviceIntf_h
#define MetalDeviceIntf_h

void setMetalDeviceToUse(void *device);
void setLayerRenderer(void *layerRenderer);
void updateFrameStart();
void updateFrameEnd();
void waitUntilOptimalInputTime();

#endif /* MetalDeviceIntf_h */
