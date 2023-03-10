# sensor-rp2040
## Overview

Sensor RP2040 is a library of sensors for the RP2040 microcontroller using shield 

## Installation

```bash
# clone this repository
git clone ###
cd sensor-rp2040

# create a debug/release build directory
mkdir build-debug  # or build-release, change debug for release
cd build-debug

cmake .. -DCMAKE_BUILD_TYPE=Debug -DSENSOR_TYPE=AI # or release, change AI for other sensor type

# put board to bootloader mode holding BOOTSEL button while pressing reset button

# build and upload firmware
make -j 16 install
```

### Sensor types

| Sensor type | Description                                                  |
|-------------|--------------------------------------------------------------|
| SCL3300     | 2 axis inclinometer, range +- 90°                            |
| SCL3400     | 2 axis inclinometer, range +- 30°                            |
| AI          | Analog input - 4 analog inputs                               |
| AnalogInput | Same as "AI")                                                |
| 4DI4DO      | 4 digital inputs and 4 digital outputs                       |
| ABP         | pressure sensor, range 0-60 mbar = 0-6 kPa (differential)    |

## Other remarks
### low latency USB Serial
```bash
echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer  # change ttyUSB0 for your device
```