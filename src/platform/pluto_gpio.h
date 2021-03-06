/*
 * HNAP4PlutoSDR - HAMNET Access Protocol implementation for the Adalm Pluto SDR
 *
 * Copyright (C) 2020 Lukas Ostendorf <lukas.ostendorf@gmail.com>
 *                    and the project contributors
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the
 * GNU Lesser General Public License as published by the Free Software Foundation; version 3.0.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library;
 * if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#ifndef TRANSCEIVER_PLUTO_GPIO_H
#define TRANSCEIVER_PLUTO_GPIO_H

#include <pthread.h>

enum pin_level {LOW=0, HIGH=1};
enum pin_direction {IN=0, OUT=1};

#define PLUTO_GPIO_BASE 906

#define PIN_MIO0 0
#define PIN_MIO10 10

struct gpio_pin_s;
typedef struct gpio_pin_s* gpio_pin;

// initializer
gpio_pin pluto_gpio_init(int pin_id, int direction);
void pluto_gpio_destroy(gpio_pin gpio);

// pin write functions
void pluto_gpio_pin_write(gpio_pin gpio, int level);
void pluto_gpio_pin_write_delayed(gpio_pin gpio, int level, int delay_us);


#endif //TRANSCEIVER_PLUTO_GPIO_H
