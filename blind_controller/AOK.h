/*
 * AOK Library for A-OK Blind Controllers
 * 
 * This library provides basic functionality to send RF commands
 * to A-OK compatible blinds using 433MHz transmission
 */

#ifndef AOK_H
#define AOK_H

#include <Arduino.h>

class AOK {
private:
    int _pin;
    int _pulse_length;
    int _repeat_transmit;
    
    void transmit(int high_pulse, int low_pulse);
    void sendBit(bool bit);
    void sendPreamble();
    
public:
    AOK(int pin);
    void begin();
    void send(unsigned long code);
    void setPulseLength(int pulse_length);
    void setRepeatTransmit(int repeat);
};

#endif