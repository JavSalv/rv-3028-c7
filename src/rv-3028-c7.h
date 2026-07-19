/**
 * @file rv-3028-c7.h
 * @brief ESP-IDF driver for the Micro Crystal RV-3028-C7 RTC.
 *
 * Copyright (c) 2026 Javier Salvador
 *
 * Inspired by: Constantin Koch's RV-3028-C7 Arduino Library
 * (https://github.com/constiko/RV-3028_C7-Arduino_Library)
 *
 * This file is distributed under the MIT License as described in LICENSE.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <i2cdev.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RV3028C7_I2C_ADDR UINT8_C(0x52)
#define RV3028_I2C_FREQ_HZ UINT32_C(400000)
#define RV3028_USER_EEPROM_SIZE UINT8_C(43)
#define RV3028_USER_RAM_SIZE UINT8_C(2)
#define RV3028_EEPROM_TIMEOUT_MS UINT32_C(100)

    /** Status register flags. */
    typedef enum
    {
        RV3028_STATUS_PORF = 1U << 0,   /**< Power-on reset; time is not reliable. */
        RV3028_STATUS_EVF = 1U << 1,    /**< External event/backup event. */
        RV3028_STATUS_AF = 1U << 2,     /**< Alarm event. */
        RV3028_STATUS_TF = 1U << 3,     /**< Countdown timer event. */
        RV3028_STATUS_UF = 1U << 4,     /**< Periodic update event. */
        RV3028_STATUS_BSF = 1U << 5,    /**< Backup switchover event. */
        RV3028_STATUS_CLKF = 1U << 6,   /**< Interrupt-controlled clock event. */
        RV3028_STATUS_EEBUSY = 1U << 7, /**< EEPROM is busy (read-only). */
    } rv3028_status_flag_t;

    /** Alarm fields to ignore. Values match the three alarm-enable bits. */
    typedef enum
    {
        RV3028_ALARM_MATCH_MINUTE_HOUR_DAY = 0,
        RV3028_ALARM_MATCH_HOUR_DAY = 1,
        RV3028_ALARM_MATCH_MINUTE_DAY = 2,
        RV3028_ALARM_MATCH_DAY = 3,
        RV3028_ALARM_MATCH_MINUTE_HOUR = 4,
        RV3028_ALARM_MATCH_HOUR = 5,
        RV3028_ALARM_MATCH_MINUTE = 6,
        RV3028_ALARM_DISABLED = 7,
    } rv3028_alarm_mode_t;

    typedef enum
    {
        RV3028_TIMER_4096_HZ = 0,
        RV3028_TIMER_64_HZ = 1,
        RV3028_TIMER_1_HZ = 2,
        RV3028_TIMER_1_60_HZ = 3,
    } rv3028_timer_frequency_t;

    typedef enum
    {
        RV3028_UPDATE_EVERY_SECOND = 0,
        RV3028_UPDATE_EVERY_MINUTE = 1,
    } rv3028_update_frequency_t;

    typedef enum
    {
        RV3028_TRICKLE_3K = 0,
        RV3028_TRICKLE_5K = 1,
        RV3028_TRICKLE_9K = 2,
        RV3028_TRICKLE_15K = 3,
    } rv3028_trickle_resistance_t;

    typedef enum
    {
        RV3028_BACKUP_SWITCH_DISABLED = 0,
        RV3028_BACKUP_SWITCH_DIRECT = 1,
        RV3028_BACKUP_SWITCH_DISABLED_ALT = 2,
        RV3028_BACKUP_SWITCH_LEVEL = 3,
    } rv3028_backup_mode_t;

    typedef enum
    {
        RV3028_CLKOUT_32768_HZ = 0,
        RV3028_CLKOUT_8192_HZ = 1,
        RV3028_CLKOUT_1024_HZ = 2,
        RV3028_CLKOUT_64_HZ = 3,
        RV3028_CLKOUT_32_HZ = 4,
        RV3028_CLKOUT_1_HZ = 5,
        RV3028_CLKOUT_TIMER = 6,
        RV3028_CLKOUT_LOW = 7,
    } rv3028_clkout_frequency_t;

    typedef struct
    {
        uint8_t minute;   /**< 0..59. */
        uint8_t hour;     /**< 0..23, regardless of RTC hour mode. */
        uint8_t day;      /**< Weekday 0..6 or date 1..31. */
        bool use_weekday; /**< true: day is weekday; false: date. */
        rv3028_alarm_mode_t mode;
        bool enable_clock_output; /**< Select alarm as automatic CLKOUT source. */
    } rv3028_alarm_config_t;

    typedef struct
    {
        bool repeat;
        rv3028_timer_frequency_t frequency;
        uint16_t value; /**< 12-bit value, 0..4095. */
        bool enable_interrupt;
        bool start;
        bool enable_clock_output; /**< Select timer as automatic CLKOUT source. */
    } rv3028_timer_config_t;

    /**
     * Optional Arduino begin()-like initialization.
     *
     * Persistent settings are only committed when the EEPROM value changes.
     */
    typedef struct
    {
        bool use_24_hour_mode;        /**< Select 24-hour mode when true. */
        bool disable_trickle_charger; /**< Preserve resistor selection. */
        bool configure_backup_mode;   /**< Apply backup_mode when true. */
        rv3028_backup_mode_t backup_mode;
        bool clear_status; /**< Clears PORF too; false by default. */
    } rv3028_config_t;

#define RV3028_CONFIG_DEFAULT()                    \
    {                                              \
        .use_24_hour_mode = true,                  \
        .disable_trickle_charger = true,           \
        .configure_backup_mode = true,             \
        .backup_mode = RV3028_BACKUP_SWITCH_LEVEL, \
        .clear_status = false,                     \
    }

    /**
     * @brief Initialize device descriptor.
     *
     * Call i2cdev_init() once before using descriptors.
     *
     * @param dev      Device descriptor
     * @param port     I2C port
     * @param sda_gpio SDA GPIO
     * @param scl_gpio SCL GPIO
     * @return         `ESP_OK` on success
     */
    esp_err_t rv3028_init_desc(i2c_dev_t *dev, i2c_port_t port,
                               gpio_num_t sda_gpio, gpio_num_t scl_gpio);

    /**
     * @brief Free device descriptor.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_free_desc(i2c_dev_t *dev);

    /**
     * @brief Apply startup configuration.
     *
     * Reads the ID register to check communication before applying settings.
     *
     * @param dev    Device descriptor
     * @param config Startup configuration
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_configure(i2c_dev_t *dev, const rv3028_config_t *config);

    /**
     * @brief Read hardware and version ID.
     *
     * @param dev Device descriptor
     * @param id  ID register value
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_get_id(i2c_dev_t *dev, uint8_t *id);

    /**
     * @brief Read calendar time.
     *
     * `tm_mon` is 0..11 and `tm_year` is years since 1900.
     *
     * @param dev      Device descriptor
     * @param timeinfo Calendar time
     * @return         `ESP_OK` on success
     */
    esp_err_t rv3028_get_time(i2c_dev_t *dev, struct tm *timeinfo);

    /**
     * @brief Set calendar time.
     *
     * The RV-3028 supports years 2000..2099. Writing the seconds register also
     * resets the clock prescaler.
     *
     * @param dev      Device descriptor
     * @param timeinfo Calendar time
     * @return         `ESP_OK` on success
     */
    esp_err_t rv3028_set_time(i2c_dev_t *dev, const struct tm *timeinfo);

    /**
     * @brief Read 32-bit UNIX time counter.
     *
     * Calendar time and UNIX time are independent.
     *
     * @param dev       Device descriptor
     * @param unix_time UNIX time counter value
     * @return          `ESP_OK` on success
     */
    esp_err_t rv3028_get_unix_time(i2c_dev_t *dev, uint32_t *unix_time);

    /**
     * @brief Set 32-bit UNIX time counter.
     *
     * Resets the clock prescaler before writing the counter.
     *
     * @param dev       Device descriptor
     * @param unix_time UNIX time counter value
     * @return          `ESP_OK` on success
     */
    esp_err_t rv3028_set_unix_time(i2c_dev_t *dev, uint32_t unix_time);

    /**
     * @brief Check whether 12-hour mode is enabled.
     *
     * @param dev    Device descriptor
     * @param is_12h true when 12-hour mode is enabled
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_is_12H(i2c_dev_t *dev, bool *is_12h);

    /**
     * @brief Check whether the current time is PM.
     *
     * Returns false when the RTC is in 24-hour mode.
     *
     * @param dev   Device descriptor
     * @param is_pm true when the RTC is in 12-hour PM time
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_is_PM(i2c_dev_t *dev, bool *is_pm);

    /**
     * @brief Select 12-hour mode.
     *
     * The RTC converts the current hour automatically.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_set_12H(i2c_dev_t *dev);

    /**
     * @brief Select 24-hour mode.
     *
     * The RTC converts the current hour automatically.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_set_24H(i2c_dev_t *dev);

    /**
     * @brief Read status register.
     *
     * @param dev    Device descriptor
     * @param status Status register value
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_get_status(i2c_dev_t *dev, uint8_t *status);

    /**
     * @brief Clear selected status flags.
     *
     * @param dev   Device descriptor
     * @param flags Bitwise combination of ::rv3028_status_flag_t
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_clear_status(i2c_dev_t *dev, uint8_t flags);

    /**
     * @brief Clear all writable status flags.
     *
     * This also clears the power-on reset flag.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_clear_interrupts(i2c_dev_t *dev);

    /**
     * @brief Check whether calendar time is valid.
     *
     * Time is considered invalid while the power-on reset flag is set.
     *
     * @param dev   Device descriptor
     * @param valid true when calendar time is valid
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_is_time_valid(i2c_dev_t *dev, bool *valid);

    /**
     * @brief Reset clock prescaler.
     *
     * Calendar and UNIX time values are not changed.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_reset(i2c_dev_t *dev);

    /**
     * @brief Configure alarm.
     *
     * Disables and clears the alarm while changing its registers. The alarm
     * interrupt is enabled unless the selected mode is disabled.
     *
     * @param dev    Device descriptor
     * @param config Alarm configuration
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_configure_alarm(i2c_dev_t *dev,
                                     const rv3028_alarm_config_t *config);

    /**
     * @brief Enable or disable alarm interrupt output.
     *
     * @param dev    Device descriptor
     * @param enable true to enable interrupt output
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_enable_alarm_interrupt(i2c_dev_t *dev, bool enable);

    /**
     * @brief Read alarm event flag.
     *
     * @param dev Device descriptor
     * @param set true when an alarm event occurred
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_get_alarm_flag(i2c_dev_t *dev, bool *set);

    /**
     * @brief Clear alarm event flag.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_clear_alarm_flag(i2c_dev_t *dev);

    /**
     * @brief Configure periodic countdown timer.
     *
     * Stops and clears the timer before applying the new configuration.
     *
     * @param dev    Device descriptor
     * @param config Timer configuration
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_configure_timer(i2c_dev_t *dev,
                                     const rv3028_timer_config_t *config);

    /**
     * @brief Start or stop countdown timer.
     *
     * @param dev    Device descriptor
     * @param enable true to start the timer
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_enable_timer(i2c_dev_t *dev, bool enable);

    /**
     * @brief Enable or disable timer interrupt output.
     *
     * @param dev    Device descriptor
     * @param enable true to enable interrupt output
     * @return       `ESP_OK` on success
     */
    esp_err_t rv3028_enable_timer_interrupt(i2c_dev_t *dev, bool enable);

    /**
     * @brief Read current countdown timer value.
     *
     * @param dev   Device descriptor
     * @param value Current 12-bit timer value
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_get_timer_value(i2c_dev_t *dev, uint16_t *value);

    /**
     * @brief Read countdown timer event flag.
     *
     * @param dev Device descriptor
     * @param set true when a timer event occurred
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_get_timer_flag(i2c_dev_t *dev, bool *set);

    /**
     * @brief Clear countdown timer event flag.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_clear_timer_flag(i2c_dev_t *dev);

    /**
     * @brief Enable periodic time update interrupt.
     *
     * The interrupt can occur every second or every minute.
     *
     * @param dev                 Device descriptor
     * @param frequency           Update frequency
     * @param enable_clock_output Select update event as automatic CLKOUT source
     * @return                    `ESP_OK` on success
     */
    esp_err_t rv3028_enable_periodic_update_interrupt(
        i2c_dev_t *dev, rv3028_update_frequency_t frequency,
        bool enable_clock_output);

    /**
     * @brief Disable periodic time update interrupt.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_disable_periodic_update_interrupt(i2c_dev_t *dev);

    /**
     * @brief Read periodic time update event flag.
     *
     * @param dev Device descriptor
     * @param set true when an update event occurred
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_get_periodic_update_flag(i2c_dev_t *dev, bool *set);

    /**
     * @brief Clear periodic time update event flag.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_clear_periodic_update_flag(i2c_dev_t *dev);

    /**
     * @brief Configure trickle charger.
     *
     * The setting is stored in EEPROM. Resistance is ignored when disabling,
     * preserving the current selection.
     *
     * @param dev        Device descriptor
     * @param enable     true to enable trickle charger
     * @param resistance Series resistance
     * @return           `ESP_OK` on success
     */
    esp_err_t rv3028_set_trickle_charger(i2c_dev_t *dev, bool enable,
                                         rv3028_trickle_resistance_t resistance);

    /**
     * @brief Configure backup switchover mode.
     *
     * The setting is stored in EEPROM.
     *
     * @param dev  Device descriptor
     * @param mode Backup switchover mode
     * @return     `ESP_OK` on success
     */
    esp_err_t rv3028_set_backup_switchover_mode(i2c_dev_t *dev,
                                                rv3028_backup_mode_t mode);

    /**
     * @brief Enable normal clock output.
     *
     * The frequency and enable state are stored in EEPROM.
     *
     * @param dev       Device descriptor
     * @param frequency Clock output frequency
     * @return          `ESP_OK` on success
     */
    esp_err_t rv3028_enable_clock_output(i2c_dev_t *dev,
                                         rv3028_clkout_frequency_t frequency);

    /**
     * @brief Enable interrupt-controlled clock output.
     *
     * CLKOUT starts automatically for events selected in the interrupt mask.
     *
     * @param dev       Device descriptor
     * @param frequency Clock output frequency
     * @return          `ESP_OK` on success
     */
    esp_err_t rv3028_enable_interrupt_clock_output(
        i2c_dev_t *dev, rv3028_clkout_frequency_t frequency);

    /**
     * @brief Disable normal and interrupt-controlled clock output.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_disable_clock_output(i2c_dev_t *dev);

    /**
     * @brief Read clock output event flag.
     *
     * @param dev Device descriptor
     * @param set true when interrupt-controlled clock output was triggered
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_get_clock_output_flag(i2c_dev_t *dev, bool *set);

    /**
     * @brief Clear clock output event flag.
     *
     * @param dev Device descriptor
     * @return    `ESP_OK` on success
     */
    esp_err_t rv3028_clear_clock_output_flag(i2c_dev_t *dev);

    /**
     * @brief Read byte from user EEPROM.
     *
     * User EEPROM addresses are 0..42.
     *
     * @param dev     Device descriptor
     * @param address EEPROM address
     * @param value   EEPROM value
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_read_user_eeprom(i2c_dev_t *dev, uint8_t address,
                                      uint8_t *value);

    /**
     * @brief Write byte to user EEPROM.
     *
     * The write cycle is skipped when the stored value is unchanged.
     *
     * @param dev     Device descriptor
     * @param address EEPROM address, 0..42
     * @param value   Value to write
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_write_user_eeprom(i2c_dev_t *dev, uint8_t address,
                                       uint8_t value);

    /**
     * @brief Read byte from configuration EEPROM.
     *
     * Configuration EEPROM addresses are 0x30..0x37.
     *
     * @param dev     Device descriptor
     * @param address EEPROM address
     * @param value   EEPROM value
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_read_config_eeprom(i2c_dev_t *dev, uint8_t address,
                                        uint8_t *value);

    /**
     * @brief Write byte to configuration EEPROM and RAM mirror.
     *
     * The EEPROM write cycle is skipped when the stored value is unchanged.
     *
     * @param dev     Device descriptor
     * @param address Configuration address, 0x30..0x37
     * @param value   Value to write
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_write_config_eeprom(i2c_dev_t *dev, uint8_t address,
                                         uint8_t value);

    /**
     * @brief Read byte from user RAM.
     *
     * @param dev   Device descriptor
     * @param index RAM index, 0..1
     * @param value RAM value
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_read_user_ram(i2c_dev_t *dev, uint8_t index, uint8_t *value);

    /**
     * @brief Write byte to user RAM.
     *
     * @param dev   Device descriptor
     * @param index RAM index, 0..1
     * @param value Value to write
     * @return      `ESP_OK` on success
     */
    esp_err_t rv3028_write_user_ram(i2c_dev_t *dev, uint8_t index, uint8_t value);

    /**
     * @brief Read one register.
     *
     * @param dev     Device descriptor
     * @param address Register address
     * @param value   Register value
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_read_register(i2c_dev_t *dev, uint8_t address,
                                   uint8_t *value);

    /**
     * @brief Write one register.
     *
     * @param dev     Device descriptor
     * @param address Register address
     * @param value   Value to write
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_write_register(i2c_dev_t *dev, uint8_t address,
                                    uint8_t value);

    /**
     * @brief Read consecutive registers.
     *
     * @param dev     Device descriptor
     * @param address First register address
     * @param data    Destination buffer
     * @param size    Number of bytes to read
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_read_registers(i2c_dev_t *dev, uint8_t address,
                                    void *data, size_t size);

    /**
     * @brief Write consecutive registers.
     *
     * @param dev     Device descriptor
     * @param address First register address
     * @param data    Source buffer
     * @param size    Number of bytes to write
     * @return        `ESP_OK` on success
     */
    esp_err_t rv3028_write_registers(i2c_dev_t *dev, uint8_t address,
                                     const void *data, size_t size);

#ifdef __cplusplus
}
#endif
