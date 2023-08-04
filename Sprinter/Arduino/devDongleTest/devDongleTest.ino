const int pinPwrCtrl = A3;   //12;      // for RP2040 verison
//const int pinPwrCtrl = A3;      // for Atmaega32U4 version
void setup()
{
    //pinMode(pinPwrCtrl, OUTPUT);
    //digitalWrite(pinPwrCtrl, HIGH);         // power on

    Serial.begin(9600);
    Serial1.begin(9600);
}


void loop()
{
    while(Serial1.available())
    {
        Serial.write(Serial1.read());
    }

    //Serial.print(".");
}

// END FILE
