
#define START 2
#define END 9

int pinValues[END+1];
bool newValues=true;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Let's begin!");

  for(int i=START;i<=END;i++)
  {
    pinMode(i, INPUT); 
    //digitalWrite(i, HIGH);
    pinValues[i]=0;
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  //check for change
  for(int i=START;i<=END;i++)
  {
    int val=digitalRead(i);
    if(val!=pinValues[i])
    {
      newValues=true;
    }
    pinValues[i]=val;
  }

  //show if updated
  if(newValues)
  {
    for(int i=START;i<=END;i++)
    {
      Serial.print(pinValues[i]);
      Serial.print(" ");
    }
    Serial.println();
    newValues=false;
  }

}
