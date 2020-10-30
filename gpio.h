#ifndef _GPIO_H
#define _GPIO_H

#define GPIO_DELAY_MICROSEC 20

#define GPIO_LOW 0
#define GPIO_HIGH 1

#define GPIO_IN 0
#define GPIO_OUT 1

int gpio_export(int pin);
int gpio_unexport(int pin);
int gpio_direction(int pin, int dir);
int gpio_write(int pin, int value);
int gpio_pulse(int pin);

#endif
