/**
   File: gpio.h
   Author: Ashley Manson
   Header file to get the API from gpio.c
 */

#ifndef GPIO_H
#define GPIO_H

extern u8 read_half_byte(void);
extern int gpio_dummy_init(void);
extern void gpio_dummy_exit(void);

#endif
