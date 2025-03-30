#include <stdio.h>
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "stdio_ble.h"


int main(void) {
    stdio_init_all();
    printf("Starting BLE stdio...\n");
    stdio_ble_init();

    int counter = 0;
    while (true) {
        printf("Log message via BLE #%d.\n", counter++);
        sleep_ms(1000);
    }
    return 0;
}
