/* ***************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/


#include "device_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"


//returns 'true' if motion detected
int get_motion_event1()
{
    return (int)gpio_get_level(GPIO_INPUT_MOTION1);
}

int get_motion_event2()
{
    return (int)gpio_get_level(GPIO_INPUT_MOTION2);
}

int get_motion_event3()
{
    return (int)gpio_get_level(GPIO_INPUT_MOTION3);
}

void led_blink(int switch_state, int delay, int count)
{
    for (int i = 0; i < count; i++) {
        vTaskDelay(delay / portTICK_PERIOD_MS);
        //change_switch_state(1 - switch_state);
        vTaskDelay(delay / portTICK_PERIOD_MS);
        //change_switch_state(switch_state);
    }
}


void iot_gpio_init(void)
{
	gpio_config_t io_conf;
    esp_err_t error;

	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = 1 << GPIO_OUTPUT_MAINLED;
	io_conf.pull_down_en = 1;
	io_conf.pull_up_en = 0;
	error=gpio_config(&io_conf);
    if(error!=ESP_OK){
        printf("error configuring led pin\n");
    }

    //motion pin1
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;//set as inputmode
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_MOTION1);//bit mask of the pins that you want to set,e.g.GPIO15
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;//enable pull-down mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;//disable pull-up mode
    error=gpio_config(&io_conf);//configure GPIO with the given settings
    if(error!=ESP_OK){
        printf("error configuring input pin1\n");
    }
    gpio_set_intr_type(GPIO_INPUT_MOTION1, GPIO_INTR_ANYEDGE);

    //motion pin2
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;//set as inputmode
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_MOTION2);//bit mask of the pins that you want to set,e.g.GPIO15
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;//enable pull-down mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;//disable pull-up mode
    error=gpio_config(&io_conf);//configure GPIO with the given settings
    if(error!=ESP_OK){
        printf("error configuring input pin2\n");
    }

    //motion pin3
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;//disable interrupt
    io_conf.mode = GPIO_MODE_INPUT;//set as inputmode
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_MOTION3);//bit mask of the pins that you want to set,e.g.GPIO15
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;//enable pull-down mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;//disable pull-up mode
    error=gpio_config(&io_conf);//configure GPIO with the given settings
    if(error!=ESP_OK){
        printf("error configuring input pin3\n");                
    }

    gpio_install_isr_service(0);

	gpio_set_level(GPIO_OUTPUT_MAINLED, MAINLED_GPIO_OFF);
	//gpio_set_level(GPIO_OUTPUT_MAINLED_0, 0);
}


