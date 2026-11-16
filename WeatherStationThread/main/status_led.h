// The board's status LED: a plain GPIO LED on the FireBeetle, the WS2812 RGB
// LED on the DevKitC (driven dim green).
#pragma once
#include <stdbool.h>

void status_led_init(void);
void status_led_set(bool on);
