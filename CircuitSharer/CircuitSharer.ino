const int inputPin = A3; // Pin to read input
const int outputPin = 8; // Pin to set high when true
const int threshold = 100; // Threshold value

void setup() 
{
  Serial.begin(115200); // Initialize serial communication at 9600 bits per second
  pinMode(outputPin, OUTPUT); // Set the digital pin as an output
  //pinMode(inputPin, INPUT_PULLUP); // Set the analog pin as an input
}

void loop() 
{
  int numSamples=100;
  long sumOfSquares = 0; // Variable to store the sum of squares of the current readings

  for (int i = 0; i < numSamples; i++) {
    int sensorValue = analogRead(inputPin); // Read the analog input
    //float voltage = sensorValue * (5.0 / 1023.0); // Convert the analog reading to voltage
    //float current = (voltage - offsetVoltage) / sensitivity; // Calculate the current in Amperes
    //sensorValue=sensorValue/10;  //make smaller
    sumOfSquares += sensorValue*sensorValue; // Add the square of the current to the sum of squares
    delay(10); // Wait for 10 milliseconds before taking another reading
  }

  int rmsValue = sqrt(sumOfSquares / numSamples); // Calculate the RMS current
  

  Serial.print("RMS Sensor Value: ");
  Serial.println(rmsValue); // Print the sensor value to the serial monitor

  if (rmsValue > threshold) {
    digitalWrite(outputPin, HIGH); // Set the digital pin high
    Serial.println("Threshold exceeded, digital pin set HIGH");
  } else {
    digitalWrite(outputPin, LOW); // Set the digital pin low
    Serial.println("Threshold not exceeded, digital pin set LOW");
  }

  delay(250); // Wait for a second before reading again
}
