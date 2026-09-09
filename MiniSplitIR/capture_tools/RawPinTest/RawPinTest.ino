void setup() {
  Serial.begin(115200);
  while (!Serial) delay(50);
  pinMode(2, INPUT);
  Serial.println("raw pin test ready, watching GPIO2");
}

void loop() {
  static int last = -1;
  int v = digitalRead(2);
  if (v != last) {
    Serial.printf("pin2=%d t=%lu\n", v, millis());
    last = v;
  }
}
