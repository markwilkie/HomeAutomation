#include "IgnState.h"

//
// Reads pin from OBDII which will be high if ignition is on
//

//Pin used for reads
#define IGN_PIN D4

void IgnState::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
}

bool IgnState::getIgnState()
{
    //don't update if it's not time to
    if(millis()<nextTickCount)
        return ignState;

    //Update timing
    nextTickCount=millis()+refreshTicks;

    //Read state
    ignState = digitalRead(IGN_PIN);

    //Serial.print("IGN State: ");
    //Serial.println(ignState);

    return ignState;
}
