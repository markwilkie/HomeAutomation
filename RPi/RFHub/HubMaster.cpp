#include <stdio.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <time.h>
#include <curl/curl.h>
#include <signal.h>
#include <RF24/RF24.h>
#include <sys/resource.h> 

void sigHandler(int s);
void readDeviceData();
void callSecureWebAPI(char *urlRaw,char *authHeader);
void callWebAPI(char *urlRaw); 
void createAuthHeader(char *key, char *keyName, char *url, char *authHeader);
char * base64encode (const void *b64_encode_me, int encode_this_many_bytes,int *resLen);
void buildPipeOne();
void buildPipeTwo();
void buildPipeThree();
void buildPipeFour();
void buildPipeFive();
int createAndPostData(char *authHeader);
void timeStamp(FILE *file);
long getLocalEpoch();
void flushFileHandles();

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 8Mhz
RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);

// First pipe is for writing, 2nd, 3rd, 4th, 5th & 6th is for reading...
const uint64_t pipes[6] = { 0xF0F0F0F0D2LL, 0xF0F0F0F0E1LL, 
                            0xF0F0F0F0E2LL, 0xF0F0F0F0E3LL, 
                            0xF0F0F0F0F1LL, 0xF0F0F0F0F2LL };

//
// For posting to the Azure events
//
// Payload size - 32 is the default.  Must be the same on transmitter
const uint8_t MAX_PAYLOAD_SIZE = 32;  
const uint8_t MAX_POSTDATA_SIZE = 128+1; 
uint8_t pipeNo;
int lastPayloadLen;
uint8_t bytesRecv[MAX_PAYLOAD_SIZE+1];
char postData[MAX_POSTDATA_SIZE];
char *url=(char*)"https://homeautomation-ns.servicebus.windows.net/homeautomation/messages?api-version=2014-01";

//
// For posting to normal Azure web API
//
char *baseURL = (char *)"http://sensors.cloudapp.net";
char APIWebURL[512];

FILE *logFile;
FILE *rfFile;
time_t rawtime;
struct tm * timeinfo;
struct rusage memInfo;

int main()
{
    //Ignore sig pipe errors.  There's a bug in curl...
    signal(SIGPIPE, sigHandler);

    //open file for logging
    logFile = fopen("/home/pi/code/rfHub/HubMasterLog.txt", "w");
    rfFile = fopen("/home/pi/code/rfHub/RFLog.txt", "w");
    freopen ("/home/pi/code/rfHub/HubMasterStderr.txt","w",stderr);
    freopen ("/home/pi/code/rfHub/HubMasterStdout.txt","w",stdout);

    //tag log files
    time(&rawtime);
    fprintf(logFile,"-\n-\n-\nHubMaster Start - %s-\n\n",ctime(&rawtime));
    fprintf(rfFile, "-\n-\n-\nHubMaster Start - %s-\n\n", ctime(&rawtime));
    fprintf(stderr,"-\n-\n-\nHubMaster Start - %s\n-\n",ctime(&rawtime));
    fprintf(stdout,"-\n-\n-\nHubMaster Start - %s\n-\n",ctime(&rawtime));

    //setup radio
    radio.begin();
    radio.setPALevel(RF24_PA_MAX);          // Higher power level
    radio.setDataRate(RF24_250KBPS);        // Slower datarate for more distance
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.setRetries(15,15);                // Smallest time between retries, max no. of retries
    radio.enableDynamicPayloads();          // Read size off chip
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging

    // Open 6 pipes for readings ( 5 plus pipe0, also can be used for reading )
    radio.openWritingPipe(pipes[0]);
    radio.openReadingPipe(1,pipes[1]); //Power reading pipe
    radio.openReadingPipe(2,pipes[2]); //Event pipe
    radio.openReadingPipe(3,pipes[3]); //Alarm pipe
    radio.openReadingPipe(4,pipes[4]); //State protocol
    radio.openReadingPipe(5,pipes[5]); //Context

    //Start listening!
    radio.startListening();

    //Loop, checking for radio packets
    while(1)
    {
      //Check if something's on the radio
      if(radio.available(&pipeNo))
      {  
        //timeStamp(logFile);

        //read data from the pipe
        readDeviceData();

        //Create new SAS authorization header
        char *baseUrl=(char *)"homeautomation-ns.servicebus.windows.net";
        char *authHeader=(char*)malloc(256);
        createAuthHeader((char*)"3RdEfuPG4TeMbsBRhLgaCnyoq5ZttZpJWFdxajN0rZM=",(char*)"saspolicy",baseUrl,authHeader);

        //Parse data and send to Azure if appropriate
        createAndPostData(authHeader);

        //Free memory
        free(authHeader);
        flushFileHandles();
      }

      //check for carrier
      delay(150);
      if (radio.testCarrier())
      {
        fprintf(logFile,"^");
        flushFileHandles();
      }
      delay(350);
    }
    return 0;
}

void sigHandler(int s) 
{
    timeStamp(stderr);
    fprintf(stderr, "ERROR: Caught SIGPIPE error: %d\n", s);
    fflush(stderr);
}

void timeStamp(FILE *file)
{
    //Dump header
    getrusage(RUSAGE_SELF,&memInfo);
    time(&rawtime);
    timeinfo = localtime (&rawtime);
    char *time=asctime(timeinfo);
    time[(strlen(time))-1]=0;
    fprintf(file,"Mem:%lu %s: ",memInfo.ru_maxrss,time);
    fflush(file);  
}

long getLocalEpoch()
{
  time_t t = time(NULL);
  struct tm lt = {0};
  localtime_r(&t, &lt);

  return (t+lt.tm_gmtoff);
}

void flushFileHandles()
{
    static int newLineCounter = 0;

    //If time for a newline, do it now
    if(++newLineCounter > 80)
    {
       timeStamp(logFile);
       fprintf(logFile,"\n");

       //timeStamp(rfFile);
       //fprintf(rfFile, "\n");

       newLineCounter=0;
    }

    fflush(logFile);  
    fflush(rfFile);
    fflush(stderr); 
    fflush(stdout); 
}

int createAndPostData(char *authHeader)
{
    int retVal=0;

    //Init postData
    for(int i=0;i<MAX_POSTDATA_SIZE;i++)
      postData[i]=0;

    switch (pipeNo)
    { 
    case 1: //Power
        buildPipeOne();
        callSecureWebAPI(url, authHeader);
        break;
    case 2: //Motion
        buildPipeTwo();
        callSecureWebAPI(url, authHeader);
        break;
    case 3: //Alarm
        buildPipeThree();
        callSecureWebAPI(url, authHeader);  //Azure Event
        callWebAPI(APIWebURL);  //SMTP --> text web api call in Azure
        break;
    case 4: //state protocol
        buildPipeFour();
        callSecureWebAPI(url, authHeader);
        break;
    case 5: //context protocol
        buildPipeFive();
        //No need to send anything to Azure
        break;
    default: //Unexpected??
        timeStamp(stderr);
        fprintf(stderr, "ERROR: Unexpected pipe number: %d\n",pipeNo);
        fflush(stderr);
        retVal=-1;
    }

    //fprintf(logFile,"DATA: %s\n",postData);
    //fflush(logFile);  

    return retVal;
}

//Build post message for pipe 1
//
//Power sensor pipe
//
void buildPipeOne()
{
    time_t seconds_past_epoch = getLocalEpoch();
       uint8_t addr;
    float reading;

    addr = bytesRecv[0];
    memcpy(&reading, &bytesRecv[1], 4);
    sprintf(postData, "{'DeviceName':'Power%d','DeviceDate':'%ld','DeviceData1':'%f'}", addr, seconds_past_epoch, reading);
    //fprintf(logFile,"Addr: %d  Reading: %f\n",addr,reading);

    fprintf(logFile, ".");  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 2
//
// Event pipe
//
void buildPipeTwo()
{
    //
    // Over-the-air packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // 2 bytes: Number of retries on last send (int)
    // 2 bytes: Number of send attempts since last successful send (int)  
    // 1 byte:  eventCodeType (O-opening change)
    // 1 byte:  eventCode (O-opened, C-closed, D-Detection)
    //

    time_t seconds_past_epoch = getLocalEpoch();
        char eventCodeType,eventCode;
    int unitNum, lastRetryCount, lastGoodTransCount;

        if(lastPayloadLen<7) //legacy
        {
      unitNum=(uint8_t)bytesRecv[0];
      eventCodeType=bytesRecv[2];
      eventCode=bytesRecv[3];
        }
        else
        {
      unitNum=(uint8_t)bytesRecv[0];
          lastRetryCount=(uint16_t)bytesRecv[2];
          lastGoodTransCount=(uint16_t)bytesRecv[4];
      eventCodeType=bytesRecv[6];
      eventCode=bytesRecv[7];

          if(lastRetryCount > 0 || lastGoodTransCount>0)
          {
        timeStamp(rfFile);
        fprintf(rfFile, "EVENT: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
          }
        }

    sprintf(postData, "{'PayloadType':'%s','UnitNum':'%d','EventCodeType':'%c','EventCode':'%c','DeviceDate':'%ld'}", "EVENT", unitNum, eventCodeType, eventCode, seconds_past_epoch);

    fprintf(logFile, "%c",eventCode);  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 3 
//
//Alarm pipe
//
void buildPipeThree()
{
    //
    // Over-the-air packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // 2 bytes: Number of retries on last send (int)
    // 2 bytes: Number of send attempts since last successful send (int)  
    // 1 byte:  alarmCodeType (A-Alarm)
    // 1 byte:  alarmCode (W-Water,F-Fire)
    //

    time_t seconds_past_epoch = getLocalEpoch();
    char alarmCodeType, alarmCode;
    int unitNum, lastRetryCount, lastGoodTransCount;

        if(lastPayloadLen<7) //legacy
        {
      unitNum = (uint8_t)bytesRecv[0];
      alarmCodeType = bytesRecv[2];
      alarmCode = bytesRecv[3];
        }
        else
        {
      unitNum=(uint8_t)bytesRecv[0];
          lastRetryCount=(uint16_t)bytesRecv[2];
          lastGoodTransCount=(uint16_t)bytesRecv[4];
      alarmCodeType = bytesRecv[6];
      alarmCode = bytesRecv[7];

          if(lastRetryCount > 0 || lastGoodTransCount>0)
          {
        timeStamp(rfFile);
        fprintf(rfFile, "ALARM: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
          }
        }

    //Post data for ALARM in Azure event queue
    sprintf(postData, "{'PayloadType':'%s','UnitNum':'%d','EventCodeType':'%c','EventCode':'%c','DeviceDate':'%ld'}", "ALARM", unitNum, alarmCodeType, alarmCode, seconds_past_epoch);

    //GET data for SMTP w/ Azure web api
    sprintf(APIWebURL, "%s/Alarm/SendAlarm/%d/%c/%c", baseURL, unitNum, alarmCodeType, alarmCode);

    fprintf(logFile, "%c", alarmCode);  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 4
//
//State pipe
//
void buildPipeFour()
{
    //
    // Over-the-air packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event
    // 2 bytes: Number of retries on last send (int)
    // 2 bytes: Number of send attempts since last successful send (int)
    // 4 bytes: VCC (float)
    // 4 bytes: Temperature (float)
    // 1 byte:  Pin State (using bits, abcdefgh where 
    //                                 h=interrupt pin (usually reed switch)
    //                                 g=digital signal in from screw terminal
    //                                 f=D7
    //                                 e=D8
    //                                 d=sending interrupt  (h)
    //                                 c=sending digital signal (g)
    //                                 b=sending D7 (f)
    //                                 a=sending D8 (e)
    // 1 byte:  Number (uint8) of uint8 types and uint16 numbers coming (used for extra data from adc or whatever)
    //  ....these two lines for each extra indicated above...
    // 1 byte:  Data Type  (l=long, f=float, a=ascii)
    // 4 bytes: Data
    //

    float vcc, reading;
    time_t seconds_past_epoch = getLocalEpoch();
        int interruptPinState=-1;
        uint8_t pinState;
    int unitNum, lastRetryCount, lastGoodTransCount;

    //read known values
        if(lastPayloadLen<13) //legacy
        {
      unitNum = (uint8_t)bytesRecv[0];
      memcpy(&vcc, &bytesRecv[2], 4);
      memcpy(&reading, &bytesRecv[6], 4);

      //read pin state and create post data accordingly
      uint8_t pinState = bytesRecv[10];
         }
         else
        {
      unitNum=(uint8_t)bytesRecv[0];
          lastRetryCount=(uint16_t)bytesRecv[2];
          lastGoodTransCount=(uint16_t)bytesRecv[4];
      memcpy(&vcc, &bytesRecv[6], 4);
      memcpy(&reading, &bytesRecv[10], 4);

      //read pin state and create post data accordingly
      pinState = bytesRecv[14];

          if(lastRetryCount > 0 || lastGoodTransCount>0)
          {
        timeStamp(rfFile);
        fprintf(rfFile, "STATE: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
           }
         }

    if (pinState & 128) //B10000000, or pin sent 
    {
          interruptPinState=(pinState & 16)>>4;  //temp until arduino is updated
          //interruptPinState=(pinState & 8)>>4;  //mask then shift 
          //fprintf(logFile, "pinstate: %d\n",pinState);
           //fprintf(rfFile, "{'PayloadType':'%s','UnitNum':'%d','VCC':'%f','Temperature':'%f','IntPinState':'%d','DeviceDate':'%ld'}", "STATE", unitNum, vcc, reading, interruptPinState, seconds_past_epoch);
           //fflush(rfFile);
          sprintf(postData, "{'PayloadType':'%s','UnitNum':'%d','VCC':'%f','Temperature':'%f','IntPinState':'%d','DeviceDate':'%ld'}", "STATE", unitNum, vcc, reading, interruptPinState, seconds_past_epoch);
     }

         //build default post data (no pin state)
         if (pinState == 0)
         {
             //fprintf(rfFile, "{'PayloadType':'%s','UnitNum':'%d','VCC':'%f','Temperature':'%f','DeviceDate':'%ld'}", "STATE", unitNum, vcc, reading, seconds_past_epoch);
             //fflush(rfFile);
             sprintf(postData, "{'PayloadType':'%s','UnitNum':'%d','VCC':'%f','Temperature':'%f','DeviceDate':'%ld'}", "STATE", unitNum, vcc, reading, seconds_past_epoch);
         }

    fprintf(logFile, "#");  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 5
//
//Context pipe
//
void buildPipeFive()
{
    //
    // Over-the-air context packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // 1 byte:  location (byte)
    //

    //read known values
    int unitNum = bytesRecv[0];
    char location = bytesRecv[2];

    //Log file
    //fprintf(logFile, "location: %c\n", location);
    //fflush(logFile);
}

void readDeviceData()
{
   // Clear any unused ACK payloads	     
   radio.flush_tx();							 	
                  
   //Read now
   lastPayloadLen = radio.getDynamicPayloadSize();
   radio.read(bytesRecv,lastPayloadLen);

   // Since this is a call-response. Respond directly with an ack payload.
   // Ack payloads are much more efficient than switching to transmit mode to respond to a call
   radio.writeAckPayload(pipeNo,&pipeNo,1);


   //fprintf(logFile,"Now reading %d bytes on pipe: %d with ACK: %d\n",lastPayloadLen ,pipeNo, pipeNo);
   //fflush(logFile);   
}

void callSecureWebAPI(char *urlRaw, char *authHeader)
{
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (curl)
    {
        //custom headers
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, "Accept:");
        chunk = curl_slist_append(chunk, "Content-Type: application/json;type=entry;charset=utf-8");
        chunk = curl_slist_append(chunk, authHeader);
        res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        //verbose
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        //post data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);

        //set timeouts
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);  //set timeout for 15 seconds

        //set URL
        curl_easy_setopt(curl, CURLOPT_URL, urlRaw);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if (res != CURLE_OK)
        {
            timeStamp(stderr);
            fprintf(stderr, "ERROR: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            fflush(stderr);
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }
}

void callWebAPI(char *urlRaw)
{
  CURL *curl;
  CURLcode res;

  curl = curl_easy_init();
  if(curl) 
  {
    //verbose
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    //set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);  //set timeout for 15 seconds

    //set URL
    curl_easy_setopt(curl, CURLOPT_URL, urlRaw);
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);

    /* Check for errors */ 
    if(res != CURLE_OK)
    {
      timeStamp(stderr);
      fprintf(stderr, "ERROR: curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
      fflush(stderr);  
    }
 
    /* always cleanup */ 
    curl_easy_cleanup(curl);
    //curl_slist_free_all(chunk);
  }
}

/*This function will build new auth header -- fills in authHeader*/
void createAuthHeader (char *key,char *keyName,char *url,char *authHeader)
{
    // Time past epoch
    time_t seconds_past_epoch = time(0);
    //fprintf(logFile,"Time: %ld\n",seconds_past_epoch);
    long expiry = seconds_past_epoch + 7200;
   
    // The key to hash with
    //char *key = (char *)"3RdEfuPG4TeMbsBRhLgaCnyoq5ZttZpJWFdxajN0rZM=";
    //fprintf(logFile,"Key: %s Len: %d\n",key,strlen(key));

    // The data that we're going to hash using HMAC
    //const char *url = "homeautomation-ns.servicebus.windows.net";
    char *stringToSign=(char*)malloc(256);
    sprintf(stringToSign,"%s\n%ld",url,expiry);
    //fprintf(logFile,"To Sign: %s\n",stringToSign);
    
    // Using sha256 hash engine here.
    unsigned int digLen;
    unsigned char *digest = (unsigned char*)malloc(EVP_MAX_MD_SIZE);
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)stringToSign, strlen(stringToSign), digest, &digLen);    
    //fprintf(logFile,"HMAC digest: %s  Len: %d  Max Size: %d\n", digest,digLen,EVP_MAX_MD_SIZE);
 
    //base64 encoding
    int base64Len=0;
    char *base64data = base64encode(digest, digLen, &base64Len);  //Base-64 encodes data.
    //fprintf(logFile,"Base64 digest: %s Len: %d\n",base64data,base64Len);

    //ESC the digest
    CURL *curl = curl_easy_init();
    char *escDigest = curl_easy_escape(curl,(const char*)base64data,base64Len);
    //fprintf(logFile,"ESC'd digest: %s\n",escDigest);

    //create auth header 
    sprintf(authHeader,"Authorization: SharedAccessSignature sr=%s&sig=%s&se=%ld&skn=%s",url,escDigest,expiry,keyName);

    //free memory
    curl_easy_cleanup(curl);
    curl_free(escDigest);
    free(base64data);
    free(stringToSign);
    free(digest);
}

/*This function will Base-64 encode your data.*/
char * base64encode (const void *b64_encode_me, int encode_this_many_bytes,int *resLen)
{
    BIO *b64_bio, *mem_bio;   //Declare two BIOs.  One base64 encodes, the other stores memory.
    BUF_MEM *mem_bio_mem_ptr; //Pointer to the "memory BIO" structure holding the base64 data.

    b64_bio = BIO_new(BIO_f_base64());  //Initialize our base64 filter BIO.
    mem_bio = BIO_new(BIO_s_mem());  //Initialize our memory sink BIO.
    b64_bio = BIO_push(b64_bio, mem_bio);  //Link the BIOs (i.e. create a filter-sink BIO chain.)
    BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);  //Don't add a newline every 64 characters.

    BIO_write(b64_bio, b64_encode_me, encode_this_many_bytes); //Encode and write our b64 data.
    BIO_flush(b64_bio);  //Flush data.  Necessary for b64 encoding, because of pad characters.
    BIO_get_mem_ptr(b64_bio, &mem_bio_mem_ptr);

    char *resBuffer = (char *)malloc(mem_bio_mem_ptr->length);
    memcpy(resBuffer , mem_bio_mem_ptr->data, mem_bio_mem_ptr->length);
    //resBuffer[mem_bio_mem_ptr->length] = 0;
    *resLen=mem_bio_mem_ptr->length;

    BIO_free_all(b64_bio);
    return resBuffer;
}


