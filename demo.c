/*
 * Copyright 2025, Hiroyuki OYAMA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "stdio_ble.h"

int main(void) {
    stdio_init_all();
    stdio_ble_init();

    int counter = 0;
    while (true) {
        printf("The %dth message output to stdout.\n", counter++);
        sleep_ms(1000);
    }
    return 0;
}
