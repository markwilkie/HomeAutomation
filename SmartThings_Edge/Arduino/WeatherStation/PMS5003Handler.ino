#include "PMS5003Handler.h"

extern unsigned long currentTime();
extern char* getTimeString(unsigned long);

void PMS5003Handler::init()
{
  //Initialize the sensor itself (turns on serial)
  pmsSensor.init();
}

bool PMS5003Handler::storeSamples()
{
  //Read sensor
  bool success=pmsSensor.readPMSData();

  //success??
  if(!success)
  {
    logger.log(ERROR,"Unable to read PMS5003 Sensor");
    return false;
  }

  //Add to our last 12 list
  concenHistory[currentIdx]={currentTime(), getPM25Standard(), getPM100Standard()};

  //dump history to the logger
  logger.log(VERBOSE,"Concentration History: ");    
  for(int i=0;i<CONCEN_HIST_SIZE;i++)
  {
    //only deal with entries that have a timestamp
    if(concenHistory[i].epoch>0)
    {
      logger.log(VERBOSE,"Epoc: %d, pm25: %d, pm100 %d",concenHistory[i].epoch,concenHistory[i].pm25,concenHistory[i].pm100);
    }
  }    
  
  //take care of index
  currentIdx++;
  if(currentIdx>=CONCEN_HIST_SIZE)
    currentIdx=0;

  //Make sure these are initialized
  pm25Label="n/a";
  pm100Label="n/a";

  //Now calc AQI
  calcBothAQI();

  return success;
}

//calc AQI for pm25
// https://forum.airnowtech.org/t/the-nowcast-for-pm2-5-and-pm10/172
void PMS5003Handler::calcBothAQI()
{
  calcPM25AQI();
  calcPM100AQI();
}

int PMS5003Handler::getPM25AQI()
{
  return pm25AQI;
}

const char *PMS5003Handler::getPM25Label()
{
  return pm25Label;
}

int PMS5003Handler::getPM100AQI()
{
  return pm100AQI;
}

const char *PMS5003Handler::getPM100Label()
{
  return pm100Label;
}

void PMS5003Handler::calcPM25AQI()
{
  int count;
  double pm25min,pm25max,pm100min,pm100max;
  double v1P25Sum,v2P25Sum,v1P100Sum,v2P100Sum;

  calcPMSMinMax(pm25min,pm25max,pm100min,pm100max,count);
  double weight = calcPMSWeight(pm25min,pm25max);
  sumWeights(count,weight,v1P25Sum,v2P25Sum,v1P100Sum,v2P100Sum);
  double nowCast = v1P25Sum/v2P25Sum;
  pm25Label=calcAQI(nowCast,pm25BreakPoints,pm25AQI);
 
  logger.log(VERBOSE,"PM25: Count: %d Nowcast: %f AQI: %d Label: %s",count,nowCast,pm25AQI,pm25Label);
}

void PMS5003Handler::calcPM100AQI()
{
  int count;
  double pm25min,pm25max,pm100min,pm100max;
  double v1P25Sum,v2P25Sum,v1P100Sum,v2P100Sum;

  calcPMSMinMax(pm25min,pm25max,pm100min,pm100max,count);
  double weight = calcPMSWeight(pm100min,pm100max);
  sumWeights(count,weight,v1P25Sum,v2P25Sum,v1P100Sum,v2P100Sum);
  double nowCast = v1P100Sum/v2P100Sum;
  pm100Label=calcAQI(nowCast,pm100BreakPoints,pm100AQI);
  
  logger.log(VERBOSE,"PM100: Count: %d Nowcast: %f AQI: %d Label: %s",count,nowCast,pm100AQI,pm100Label);
}

int PMS5003Handler::getLastReadTime()
{
  int lastReadTime=0;

  //loop through the table and find the most recent read time
  for(int i=0;i<CONCEN_HIST_SIZE;i++)
  {
    //only deal with entries that have a timestamp
    if(concenHistory[i].epoch>0)
    {
      if(concenHistory[i].epoch > lastReadTime) 
      {
        lastReadTime=concenHistory[i].epoch; 
      }
    }
  }

  return lastReadTime;  
}

//Get minmax for pms calculations
void PMS5003Handler::calcPMSMinMax(double &pm25min,double &pm25max,double &pm100min,double &pm100max,int &count)
{
  //init values
  count=0;
  pm25max=0;
  pm25min=999;
  pm100max=0;
  pm100min=999;

  //loop through the table and find min and max
  for(int i=0;i<CONCEN_HIST_SIZE;i++)
  {
    //only deal with entries that have a timestamp
    if(concenHistory[i].epoch>0)
    {
      count++;
      if(concenHistory[i].pm25 > pm25max) {
        pm25max=concenHistory[i].pm25; }
      if(concenHistory[i].pm25 < pm25min) {
        pm25min=concenHistory[i].pm25; }        

      if(concenHistory[i].pm100 > pm100max) {
        pm100max=concenHistory[i].pm100; }
      if(concenHistory[i].pm100 < pm100min) {
        pm100min=concenHistory[i].pm100; }  
    }
  }
}

//Calc weight factor
double PMS5003Handler::calcPMSWeight(double min,double max)
{
    double weight = 1.0;
    if(max>0){
      weight = 1-((max-min)/max); }
    if(weight < .5) {
      weight=.5; }
    if(weight>1) {
      weight=1; }

    return weight;
}

//Weight the history.  We'll make the assumption that the last few rows are all current. 
void PMS5003Handler::sumWeights(int count,double weight,double &v1P25Sum,double &v2P25Sum,double &v1P100Sum,double &v2P100Sum)
{
  v1P25Sum=0;
  v2P25Sum=0;
  v1P100Sum=0;
  v2P100Sum=0;

  //Loop through the history backwards summing as we go
  for(int i=CONCEN_HIST_SIZE-1;i>=0;i--)
  {
    if(concenHistory[i].epoch>0)
    {
      v1P25Sum = v1P25Sum + concenHistory[i].pm25*pow(weight,i);
      v2P25Sum = v2P25Sum + pow(weight,i);

      v1P100Sum = v1P100Sum + concenHistory[i].pm100*pow(weight,i);
      v2P100Sum = v2P100Sum + pow(weight,i);
    }
  }
}

//Finally, let's find the AQI value
const char* PMS5003Handler::calcAQI(double nowCast,struct breakPoint* breakPoints,int &AQI)
{
  AQI=0;
  const char* label="n/a";

  for(int i=0;i<6;i++)  //size of the breakpoint list
  {
    if(nowCast>=breakPoints[i].conc_lo && nowCast<=breakPoints[i].conc_hi)
    {     
      double numerator=breakPoints[i].AQI_hi-breakPoints[i].AQI_lo;
      double denom=breakPoints[i].conc_hi-breakPoints[i].conc_lo;
      double first=numerator/denom;
      double neg=nowCast-breakPoints[i].conc_lo;
      double aqidouble=(first*neg)+breakPoints[i].AQI_lo;
      AQI = round(aqidouble);   
      label=breakPoints[i].label;       
      break;
    }
  }

  return label;
}

int PMS5003Handler::getPM0_3um()
{
  return pmsSensor.getParticles03um();
}

int PMS5003Handler::getPM2_5um()
{
  return pmsSensor.getParticles25um();
}

int PMS5003Handler::getPM10_0um()
{
  return pmsSensor.getParticles100um();
}

int PMS5003Handler::getPM10Standard()
{
  return pmsSensor.getpm10_standard();
}

int PMS5003Handler::getPM25Standard()
{
  return pmsSensor.getpm25_standard();
}

int PMS5003Handler::getPM100Standard()
{
  return pmsSensor.getpm100_standard();
}
