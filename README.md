# RV-3028-C7 ESP-IDF component

![RV-3028-C7 package image](https://www.mouser.de/images/microcrystal/lrg/RV-3028-C7_series_t.jpg)

ESP-IDF I2C driver for the Micro Crystal RV-3028-C7 ultra-low-power
real-time clock. It uses the `i2cdev` component for thread-safe communication.

## Features

- Calendar and independent 32-bit UNIX time counters
- 12-hour and 24-hour modes
- Alarms, countdown timer, and periodic update events
- Status flags, interrupt control, and programmable clock output
- Backup switchover and trickle-charger configuration
- User RAM, user EEPROM, configuration EEPROM, and raw register access

## Structure

```text
.
├── src/                 Driver implementation and public header
├── test/                Unity hardware tests
├── examples/default/    Basic polling example
├── CMakeLists.txt       ESP-IDF component registration
├── idf_component.yml    Component metadata and dependencies
└── LICENSE.md           MIT license
```
