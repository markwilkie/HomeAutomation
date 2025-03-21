void setupRain()
{
  //Setup circular buffer to capture a rolling window for the last hour
  //
  //Size is based on one hour * size of thing we're storing
  int bufferSize=(3600/(SLEEPCYCLES*8))*sizeof(int);
  rainBuffer.init(bufferSize);
}

//Read how much rain in last hour
float getInchesRain()
{
  //Get rain
  //Each bucket is 0.0204"   http://www.randomuseless.info/weather/calibration/

  //Add latest count to circular buffer
  rainBuffer.putInt(interruptCount);  //interruptCount is cleared later when weather packet is sent

  //Add up all bucket drops in the last hour (basically all that are in the circular buffer)
  int bucketCount=0;
  int totalCount=0;
  for(int i=0;i<rainBuffer.getSize();i=i+sizeof(int))
  {
    //Grab count
    byte *pointer = (byte *)&bucketCount;
    pointer[1] = rainBuffer.peek(i);
    pointer[0] = rainBuffer.peek(i+1);
    
    //Sum
    totalCount=totalCount+bucketCount;
  }  

  VERBOSE_PRINT("Bucket count: ");
  VERBOSE_PRINTLN(totalCount);  
  
  return ((float)totalCount*0.0204);  //.0204 is bucket size
}

