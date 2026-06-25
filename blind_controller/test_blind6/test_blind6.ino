/*
 * test_blind6 — Diagnostic test harness.
 *
 * Blinds 1-5 are the REAL captured arrays (known working).
 * Blind 6 is DERIVED (channel one-hot 0x20, checksum recomputed).
 *
 * Purpose: verify the TX path with a known-good blind, then test blind 6.
 *   If pressing '2' moves blind 2 but '6' moves nothing, the transmitter and
 *   sketch are fine and blind 6's motor simply isn't responding to channel
 *   0x20 (pairing / different code) -- not a code-generation problem.
 *
 * Open Serial Monitor @115200 and send:
 *   1 2 3 4 5 6  -> that blind DOWN
 *   a b c d e f  -> that blind UP   (a=1 ... f=6)
 *
 * Replays raw 511-element arrays exactly like the confirmed-working path:
 * sendRawData(arr, 511) x9, HIGH on even index, delay(10) between repeats.
 */

#include "blind_data.h"

const int RF_TX_PIN = 14;
const int REPEAT_COUNT = 9;
const int RAW_LEN = 511;

unsigned int* downArr[6] = { blind1_down, blind2_down, blind3_down,
                             blind4_down, blind5_down, blind6_down };
unsigned int* upArr[6]   = { blind1_up,   blind2_up,   blind3_up,
                             blind4_up,   blind5_up,   blind6_up };

void sendRawData(unsigned int rawData[], int length) {
  for (int i = 0; i < length; i++) {
    digitalWrite(RF_TX_PIN, (i % 2 == 0) ? HIGH : LOW);  // even = HIGH
    delayMicroseconds(rawData[i]);
  }
  digitalWrite(RF_TX_PIN, LOW);
}

void sendBlind(int idx, bool down) {
  unsigned int* arr = down ? downArr[idx] : upArr[idx];
  Serial.print("Blind ");
  Serial.print(idx + 1);
  Serial.print(down ? " DOWN" : " UP");
  Serial.print(idx == 5 ? "  (DERIVED)" : "  (real capture)");
  Serial.println(" ...");
  for (int i = 0; i < REPEAT_COUNT; i++) {
    sendRawData(arr, RAW_LEN);
    delay(10);
  }
  Serial.println("  done");
}

void printMenu() {
  Serial.println();
  Serial.println("=== Blind diagnostic ===");
  Serial.println("  1-5 = real blind DOWN   |  6 = blind6 DOWN (derived)");
  Serial.println("  a-e = real blind UP     |  f = blind6 UP   (derived)");
  Serial.println("Tip: press '2' first to confirm the TX path works.");
}

void setup() {
  pinMode(RF_TX_PIN, OUTPUT);
  digitalWrite(RF_TX_PIN, LOW);
  Serial.begin(115200);
  delay(1000);
  Serial.println("test_blind6 diagnostic ready (TX pin 14)");
  printMenu();
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c >= '1' && c <= '6') {
      sendBlind(c - '1', true);
    } else if (c >= 'a' && c <= 'f') {
      sendBlind(c - 'a', false);
    } else if (c == '\r' || c == '\n' || c == ' ') {
      // ignore
    } else {
      Serial.print("Unknown: ");
      Serial.println(c);
      printMenu();
    }
  }
}
