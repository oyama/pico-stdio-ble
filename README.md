# Support for stdout over BLE for Raspberry Pi Pico W

This library routes your device's standard output (stdout) logs over BLE using the Nordic UART Service.
In addition to the traditional UART and USB CDC options, you can now capture logs wirelessly without any cables.
This is especially handy for field troubleshooting and debugging during deployment, specifically on the Raspberry Pi Pico W and Pico 2 W.

## Features

- Wireless stdout Logging via BLE: In addition to UART and USB CDC, you can now check your logs wirelessly using BLE.
- Easy to Enable: Just add `pico_enable_stdio_ble()` to your `CMakeLists.txt` and call `stdio_ble_init()` in your code.
- Compatible with Generic BLE Debug Tools: View your logs with common BLE debugging apps like LightBlue.
- Seamless Integration: Easily incorporate the library into your existing projects without major code changes.

## Setup and Configuration

To add _pico_stdio_ble_ to your project, first place the _pico_stdio_ble_ source in your project directory using the following command:

```bash
git clone https://github.com/oyama/pico-stdio-ble.git
```

Next, add the following lines to your project's `CMakeLists.txt` to include _pico_stdio_ble_ in the build process:

```CMakeLists.txt
add_subdirectory(pico-stdio-ble)
```

Then, add the `pico_enable_stdio_ble` function to `CMakeLists.txt` to enable the feature:

```CMakeLists.txt
pico_enable_stdio_ble(${CMAKE_PROJECT_NAME} 1)
```
This sets up your project to use the _pico_stdio_ble_ functionality. Finally, in your program, call the BLE stdio initialization function `stdio_ble_init()` to start using it:

```c
#include <pico/stdlib.h>
#include <stdio.h>
#include "stdio_ble.h"

int main(void) {
    stdio_init_all();
    stdio_ble_init();

    printf("Hello BLE World!\n");
    return 0;
}
```

## Building the Demo Program

A demo program is provided to showcase the functionality of _pico_stdio_ble_. To build the demo program, simply run:

```bash
make pico_stdio_ble_demo
```
After a successful compilation, the `pico_stdio_ble_demo.uf2` binary will be produced.
Simply drag and drop this file onto your Raspberry Pi Pico W while in BOOTSEL mode to install it.
This firmware outputs stdout to UART, USB CDC, and BLE NUS. To check the logs, connect to the device with BLE debugging software such as [LightBlue](https://punchthrough.com/lightblue/) and subscribe to Notify.

## Notes

- This project is currently a proof-of-concept.
- Bug reports and feature suggestions are highly welcome via Issues or pull requests.

## License

This project is licensed under the 3-Clause BSD License. For details, see the [LICENSE](LICENSE.md) file.
