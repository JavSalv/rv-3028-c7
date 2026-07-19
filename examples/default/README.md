# RV-3028-C7 driver example

## What it does

This example reads the module ID, sets calendar and UNIX time, then prints
both values once per second.

## Wiring

Run `idf.py menuconfig` and open **RV-3028-C7 example configuration** to configure.

| Name             | Function                                                  | Default |
|------------------|-----------------------------------------------------------|---------|
| EXAMPLE_I2C_PORT | In MCU with multiple I2C ports, selects which port to use | 0       |
| EXAMPLE_I2C_SDA  | GPIO connected to the SDA pin                             | 14      |
| EXAMPLE_I2C_SCL  | GPIO connected to SCL pin                                 | 13      |


