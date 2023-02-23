#include "PrimaryForm.h"
#include "Gauge.h"

// first segment in a linked list  (each tripsegment has the next one)
PrimaryForm::PrimaryForm(Genie* _geniePtr,int _formID,const char* _label,TripData *_dataSinceLastStop,CurrentData *_currentDataPtr)
{
    //Poi
    geniePtr=_geniePtr;
    dataSinceLastStop=_dataSinceLastStop;
    currentDataPtr=_currentDataPtr;

    //Save context
    formID=_formID;
    label=_label;

    //Radial gauges
    loadGauge.init(geniePtr,0,0,0,100,150);  //genie*,service,pid,ang meter obj #,digits obj #,min,max,refresh ticks
    boostGauge.init(geniePtr,3,2,0,22,150); 
    coolantTempGauge.init(geniePtr,1,1,130,250,1000);  
    transTempGauge.init(geniePtr,2,4,130,250,1000);    //0x61, 0x30   

    //Split bar gauges
    windSpeedGauge.init(geniePtr,3,0,3,-29,30,500);  
    instMPG.init(geniePtr,1,2,0,20,500);     

    //Create objects for the digit displays
    avgMPG.init(geniePtr,6,0,99,1,1100);
    milesLeftInTank.init(geniePtr,7,0,999,0,2000);
    currentElevation.init(geniePtr,8,0,9999,0,1400);
    milesTravelled.init(geniePtr,9,0,999,0,1900);
    hoursDriving.init(geniePtr,5,0,9,1,1900); 

    //LED objects
    codesLed.init(geniePtr,0,1000);    
}

int PrimaryForm::getFormId()
{
    return formID;
}

//Update values, then update display
void PrimaryForm::updateData()
{   
    //Radial gauges
    loadGauge.setValue(currentDataPtr->currentLoad);
    boostGauge.setValue(currentDataPtr->currentBoost);
    coolantTempGauge.setValue(currentDataPtr->currentCoolantTemp);
    transTempGauge.setValue(currentDataPtr->currentTransmissionTemp);

    //Split bar gauges
    int headWind=currentDataPtr->currentPitotSpeed - currentDataPtr->currentSpeed;
    windSpeedGauge.setValue(headWind);
    instMPG.setValue(dataSinceLastStop->getInstantMPG());

    //Digits
    avgMPG.setValue(dataSinceLastStop->getAvgMPG());
    milesLeftInTank.setValue(dataSinceLastStop->getMilesLeftInTank());
    currentElevation.setValue(dataSinceLastStop->getCurrentElevation());
    milesTravelled.setValue(dataSinceLastStop->getMilesTravelled());
    hoursDriving.setValue(dataSinceLastStop->getHoursDriving());   

    //LED
    if(currentDataPtr->codesPresent && !codesLed.isActive())
        codesLed.startBlink();

    if(!currentDataPtr->codesPresent && codesLed.isActive())
        codesLed.turnOff();
}

//Update display
void PrimaryForm::updateDisplay()
{
    updateData();

    windSpeedGauge.update();
    loadGauge.update();
    coolantTempGauge.update();   
    transTempGauge.update();
    boostGauge.update();
    instMPG.update();
    avgMPG.update();
    milesLeftInTank.update();
    currentElevation.update();
    milesTravelled.update();
    hoursDriving.update();
    codesLed.update();
}
