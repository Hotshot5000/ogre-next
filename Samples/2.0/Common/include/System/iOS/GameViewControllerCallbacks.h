//
//  GameViewControllerCallbacks.h
//  OgreSamplesCommon
//
//  Created by Sebastian Bugiu on 22/07/2018.
//

#ifndef GameViewControllerCallbacks_h
#define GameViewControllerCallbacks_h

class GameViewControllerCallbacks
{
public:
    virtual void dealloc() = 0;
    virtual void viewDidLoad() = 0;
    virtual void viewWillAppear(bool animated) = 0;
    virtual void viewWillDisappear(bool animated) = 0;
    virtual void mainLoop() = 0;
};


#endif /* GameViewControllerCallbacks_h */
