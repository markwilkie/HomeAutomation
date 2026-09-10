// Bit-level raw GPIO2 capture. No framing/decoding attempted here -- an
// interrupt records every edge's level + microsecond timestamp into a
// buffer (so the ~325-1550us IR bit pulses aren't lost to loop/Serial
// overhead), then the whole buffer is dumped as text only after capture
// stops. Nothing is printed *during* capture.
//
// Protocol (over Serial, 115200 8N1):
//   device -> host: "send 's' to start capture" prompt, then idles
//   host   -> device: 's'                          (start)
//   device -> host: "CAPTURING"
//   host   -> device: 'e'                           (stop)
//   device -> host: "CAPTURE_DONE count=<n> overflow=<0|1>"
//                    then n lines of "pin2=<0|1> t=<micros>"
//                    then "CAPTURE_END"
//   ... loop repeats, ready for another 's'

#define CAPTURE_PIN 2
#define BUF_SIZE 20000

volatile uint32_t edgeTime[BUF_SIZE];
volatile uint8_t edgeLevel[BUF_SIZE];
volatile uint32_t edgeCount = 0;
volatile bool capturing = false;
volatile bool overflowed = false;

void IRAM_ATTR onEdge() {
  if (!capturing) return;
  uint32_t i = edgeCount;
  if (i < BUF_SIZE) {
    edgeTime[i] = micros();
    edgeLevel[i] = digitalRead(CAPTURE_PIN);
    edgeCount = i + 1;
  } else {
    overflowed = true;
  }
}

void waitForChar(char c) {
  while (true) {
    if (Serial.available()) {
      if ((char)Serial.read() == c) return;
    }
    delay(5);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(50);
  pinMode(CAPTURE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(CAPTURE_PIN), onEdge, CHANGE);
  Serial.println("bit-level raw pin capture ready, watching GPIO2");
}

void loop() {
  Serial.println("send 's' to start capture");
  waitForChar('s');

  noInterrupts();
  edgeCount = 0;
  overflowed = false;
  interrupts();
  capturing = true;
  Serial.println("CAPTURING");

  waitForChar('e');
  capturing = false;

  uint32_t n = edgeCount;
  Serial.printf("CAPTURE_DONE count=%lu overflow=%d\n", (unsigned long)n, overflowed ? 1 : 0);
  for (uint32_t i = 0; i < n; i++) {
    Serial.printf("pin2=%d t=%lu\n", edgeLevel[i], (unsigned long)edgeTime[i]);
  }
  Serial.println("CAPTURE_END");
}
