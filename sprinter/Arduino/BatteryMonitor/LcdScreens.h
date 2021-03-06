#ifndef LCDSCREENS_H
#define LCDSCREENS_H

// These #defines make it easy to set the backlight color on LCD
#define OFF 0x0
#define WHITE 0x7
#define BACKLIGHT_DURATION 30

//Buttons
#define NONE 0
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define SELECT 5

//Cursors
#define ADC_CURS 0
#define TIME_BUCKET_CURS 1

//Screens
#define TOTAL_SCREENS 6

#define MAIN 0
#define SUM 1
#define NET_CHARGE 2
#define VOLTS 3
#define GRAPH 4
#define STATUS 5

//Buffers
char lcdScratch[30];
  
//Bucket label list
#define TIME_BUCKET_COUNT 5
char* bucketLabels[TIME_BUCKET_COUNT]={"Now","Min","Hr ","Day","Mth"};

//LCD state
long backlightOnTime=0;
bool backlight=false;
int buttonPressed=0;
int lastScreen=-1;
int currentScreen=0;
int numOfCursors=0;
int currentCursor=-1;
int currentADC=0;
int currentTimeBucket=0;

//custom chars
byte bulk[] = {
  B11111,
  B11111,
  B00000,
  B11111,
  B00000,
  B00000,
  B00000,
  B00000
};

byte floatToBulk[] = {
  B00100,
  B01110,
  B10101,
  B00100,
  B00100,
  B00100,
  B11111,
  B11111
};

byte notToBulk[] = {
  B11111,
  B11111,
  B00100,
  B01110,
  B10101,
  B00100,
  B00100,
  B00100
};

byte upDown[] = {
  B11111,
  B11111,
  B00100,
  B00100,
  B00100,
  B00100,
  B11111,
  B11111
};

byte bulkToFloat[] = {
  B11111,
  B11111,
  B00100,
  B00100,
  B10101,
  B01110,
  B00100,
  B11111
};

byte bulkToNot[] = {
  B11111,
  B11111,
  B00100,
  B00100,
  B00100,
  B10101,
  B01110,
  B00100
};

byte multUpDown[] = {
  B11111,
  B11111,
  B01010,
  B01010,
  B01010,
  B01010,
  B11111,
  B11111
};

#endif 
