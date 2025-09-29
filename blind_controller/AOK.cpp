/*
 * AOK Library Implementation
 * 
 * Basic implementation for A-OK blind controller RF transmission
 * Based on typical 433MHz RF protocols used by A-OK devices
 */

#include "AOK.h"

AOK::AOK(int pin) {
    _pin = pin;
    _pulse_length = 350;  // Default pulse length in microseconds
    _repeat_transmit = 10;  // Default repeat count
}

void AOK::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void AOK::setPulseLength(int pulse_length) {
    _pulse_length = pulse_length;
}

void AOK::setRepeatTransmit(int repeat) {
    _repeat_transmit = repeat;
}

void AOK::transmit(int high_pulse, int low_pulse) {
    digitalWrite(_pin, HIGH);
    delayMicroseconds(high_pulse);
    digitalWrite(_pin, LOW);
    delayMicroseconds(low_pulse);
}

void AOK::sendBit(bool bit) {
    if (bit) {
        // Send '1' bit: short high, long low
        transmit(_pulse_length, _pulse_length * 3);
    } else {
        // Send '0' bit: long high, short low
        transmit(_pulse_length * 3, _pulse_length);
    }
}

void AOK::sendPreamble() {
    // Send sync pattern - typical for many 433MHz protocols
    transmit(_pulse_length, _pulse_length * 31);
}

void AOK::send(unsigned long code) {
    for (int repeat = 0; repeat < _repeat_transmit; repeat++) {
        sendPreamble();
        
        // Send 24-bit code (most A-OK devices use 24-bit codes)
        for (int i = 23; i >= 0; i--) {
            bool bit = (code >> i) & 1;
            sendBit(bit);
        }
        
        // End with a short pulse
        digitalWrite(_pin, HIGH);
        delayMicroseconds(_pulse_length);
        digitalWrite(_pin, LOW);
        
        // Delay between repeats
        delay(10);
    }
}