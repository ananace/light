#ifndef _GPIO_H
#define _GPIO_H

#include <stdint.h>

#define GPIO_PINS 2

#define GPIO_DELAY_MICROSEC 20

#define GPIO_LOW 0
#define GPIO_HIGH 1

#define GPIO_IN 0
#define GPIO_OUT 1

int gpio_init(uint8_t dev);
int gpio_uninit();

int gpio_export(uint8_t id, int pin, int dir);
int gpio_unexport(uint8_t id);
int gpio_write(uint8_t id, int value);
int gpio_pulse(uint8_t id);

#endif
