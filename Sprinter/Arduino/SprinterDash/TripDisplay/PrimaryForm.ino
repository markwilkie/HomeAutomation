#include "PrimaryForm.h"

// first segment in a linked list  (each tripsegment has the next one)
PrimaryForm::PrimaryForm(Genie* _geniePtr,int _formID,const char* _label)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
    label=_label;
}

int PrimaryForm::getFormId()
{
    return formID;
}

void PrimaryForm::displayForm()
{
    geniePtr->WriteObject(GENIE_OBJ_FORM, formID, 0);  //3rd parameter is a no-op
}

//Update values, then update display
void PrimaryForm::updateData(int service,int pid,int value)
{
    //Update gauges that are specific to this form
    updateGaugeData(service,pid,value);

    //Update values in the current segment and this form
    dataSinceLastStop.update(service,pid,value);
    
    //Now update widgets on this form with the segment data
    instMPG.setValue(dataSinceLastStop.getInstantMPG());
    avgMPG.setValue(dataSinceLastStop.getAvgMPG());
    milesLeftInTank.setValue(dataSinceLastStop.getMilesLeftInTank());
    currentElevation.setValue(dataSinceLastStop.getHoursDriving());
    milesTravelled.setValue(dataSinceLastStop.getFuelGallonsUsed());
    hoursDriving.setValue(dataSinceLastStop.getHoursDriving());      
}

void PrimaryForm::updateGaugeData(int service,int pid,int value)
{
    //update wind speed when we get a vehicle speed
    if(speed.isMatch(service,pid))
    {        
      int pitotSpeed=pitot.readSpeed();
      windSpeedGauge.setValue(pitotSpeed-(value*0.621371));        //we're showing the delta     
    }

    //grab values used for calculations
    if(baraPressure.isMatch(service,pid))
    {        
      baraPressure.setValue(value);
    }

    //update gauges after doing any needed calculations
    if(loadGauge.isMatch(service,pid))
    { 
      loadGauge.setValue(value);
    }  
  
    if(coolantTempGauge.isMatch(service,pid))
    {
      if(value>0)
        coolantTempGauge.setValue((float)value*(9.0/5.0)+32.0);
    }
  
    if(transTempGauge.isMatch(service,pid))
    {
      if(value>0)
        transTempGauge.setValue((float)value*(9.0/5.0)+32.0);      
    }    
     
    if(boostGauge.isMatch(service,pid))
    {        
      int bara=baraPressure.getValue();
      float boost=value-bara;
      boostGauge.setValue(boost*.145);
    }       
}

//Update display
void PrimaryForm::updateDisplay()
{
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
}
