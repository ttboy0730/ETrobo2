//
//  app.cpp
//  aflac2019
//
//  Created by Wataru Taniguchi on 2019/04/28.
//  Copyright © 2019 Wataru Taniguchi. All rights reserved.
//

#include "app.h"

#include "crew.hpp"

#define DEBUG

#ifdef DEBUG
#define _debug(x) (x)
#else
#define _debug(x)
#endif

Captain*    captain;
Observer*   observer;
Navigator*  activeNavigator = NULL;
bool landing = false;

// a cyclic handler to activate a task
void task_activator(intptr_t tskid) {
    ER ercd = act_tsk(tskid);
    assert(ercd == E_OK);
}

// Captain's periodic task
void captain_task(intptr_t unused) {
    captain->operate();
}

// Observer's periodic task
void observer_task(intptr_t unused) {
    observer->operate();
}

// Navigator's periodic task
void navigator_task(intptr_t unused) {
    activeNavigator->operate();
}

void main_task(intptr_t unused) {
    Clock* clock   = new Clock();
    captain = new Captain;
    
    captain->takeoff();
    // infinite loop until stop condition is met
    while (!landing) {
        clock->sleep(10);
    }
    captain->land();
    
    delete captain;
    
    clock->sleep(1000); // wait a while
    ext_tsk();
}