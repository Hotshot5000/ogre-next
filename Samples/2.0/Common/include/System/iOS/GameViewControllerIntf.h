//
//  GameViewControllerIntf.h
//  OgreSamplesCommon
//
//  Created by Sebastian Bugiu on 21/07/2018.
//

#ifndef GameViewControllerIntf_h
#define GameViewControllerIntf_h

class GameViewControllerCallbacks;

void* initializeViewController(void* uiWindowPtr);
void initializeGameViewControllerCallbacks(GameViewControllerCallbacks *callback);
void setView(void *renderWindow_, void *gameViewController_);

void pause(void *gameViewController_);
void resume(void *gameViewController_);

void setOnscreenKeyboardVisible(bool visible, void *gameViewController_, void *textDelegate_);


#endif /* GameViewControllerIntf_h */
