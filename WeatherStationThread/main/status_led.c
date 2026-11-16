#include "status_led.h"
#include "board.h"

#if STATUS_LED_RGB
#include "led_strip.h"

static led_strip_handle_t s_strip;

void status_led_init(void)
{
    led_strip_config_t strip = {
        .strip_gpio_num = PIN_STATUS_LED,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip, &rmt, &s_strip));
    led_strip_clear(s_strip);
}

void status_led_set(bool on)
{
    if (on) {
        led_strip_set_pixel(s_strip, 0, 0, 16, 0);
        led_strip_refresh(s_strip);
    } else {
        led_strip_clear(s_strip);
    }
}
#else
void status_led_init(void)
{
    gpio_reset_pin(PIN_STATUS_LED);
    gpio_set_direction(PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_STATUS_LED, 0);
}

void status_led_set(bool on)
{
    gpio_set_level(PIN_STATUS_LED, on);
}
#endif
