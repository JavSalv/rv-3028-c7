/**
 * @file rv-3028-c7.c
 * @brief ESP-IDF driver for the Micro Crystal RV-3028-C7 RTC.
 *
 * Copyright (c) 2026 Javier Salvador
 *
 * Inspired by: Constantin Koch's RV-3028-C7 Arduino Library
 * (https://github.com/constiko/RV-3028_C7-Arduino_Library)
 *
 * This file is distributed under the MIT License as described in LICENSE.md.
 */

#include <esp_idf_lib_helpers.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "rv-3028-c7.h"

#define CHECK(x)                  \
    do                            \
    {                             \
        esp_err_t __;             \
        if ((__ = (x)) != ESP_OK) \
            return __;            \
    } while (0)
#define CHECK_ARG(x)                    \
    do                                  \
    {                                   \
        if (!(x))                       \
            return ESP_ERR_INVALID_ARG; \
    } while (0)

#define RV3028_SECONDS 0x00
#define RV3028_MINUTES 0x01
#define RV3028_HOURS 0x02
#define RV3028_WEEKDAY 0x03
#define RV3028_DATE 0x04
#define RV3028_MONTH 0x05
#define RV3028_YEAR 0x06
#define RV3028_MINUTES_ALARM 0x07
#define RV3028_TIMER_VALUE_0 0x0a
#define RV3028_TIMER_STATUS_0 0x0c
#define RV3028_STATUS 0x0e
#define RV3028_CTRL1 0x0f
#define RV3028_CTRL2 0x10
#define RV3028_INT_MASK 0x12
#define RV3028_UNIX_TIME_0 0x1b
#define RV3028_USER_RAM_1 0x1f
#define RV3028_EEPROM_ADDR 0x25
#define RV3028_EEPROM_DATA 0x26
#define RV3028_EEPROM_CMD 0x27
#define RV3028_ID 0x28
#define RV3028_CONFIG_CLKOUT 0x35
#define RV3028_CONFIG_BACKUP 0x37

#define CTRL1_TD_MASK 0x03
#define CTRL1_TE BIT(2)
#define CTRL1_EERD BIT(3)
#define CTRL1_USEL BIT(4)
#define CTRL1_WADA BIT(5)
#define CTRL1_TRPT BIT(7)

#define CTRL2_RESET BIT(0)
#define CTRL2_12_24 BIT(1)
#define CTRL2_AIE BIT(3)
#define CTRL2_TIE BIT(4)
#define CTRL2_UIE BIT(5)
#define CTRL2_CLKIE BIT(6)

#define INT_MASK_CUIE BIT(0)
#define INT_MASK_CTIE BIT(1)
#define INT_MASK_CAIE BIT(2)

#define HOURS_PM BIT(5)
#define ALARM_DISABLE BIT(7)

#define CLKOUT_FD_MASK 0x07
#define CLKOUT_CLKOE BIT(7)

#define BACKUP_TCR_MASK 0x03
#define BACKUP_BSM_MASK 0x0c
#define BACKUP_FEDE BIT(4)
#define BACKUP_TCE BIT(5)

#define EEPROM_CMD_FIRST 0x00
#define EEPROM_CMD_WRITE 0x21
#define EEPROM_CMD_READ 0x22

#define BCD_TO_DEC(v) ((uint8_t)((((uint8_t)(v) >> 4) * 10) + ((uint8_t)(v) & 0x0f)))
#define DEC_TO_BCD(v) ((uint8_t)((((uint8_t)(v) / 10) << 4) | ((uint8_t)(v) % 10)))

static esp_err_t read_regs_locked(i2c_dev_t *dev, uint8_t reg,
                                  void *data, size_t size)
{
    return i2c_dev_read_reg(dev, reg, data, size);
}

static esp_err_t write_regs_locked(i2c_dev_t *dev, uint8_t reg,
                                   const void *data, size_t size)
{
    return i2c_dev_write_reg(dev, reg, data, size);
}

static esp_err_t read_reg_locked(i2c_dev_t *dev, uint8_t reg, uint8_t *value)
{
    return read_regs_locked(dev, reg, value, 1);
}

static esp_err_t write_reg_locked(i2c_dev_t *dev, uint8_t reg, uint8_t value)
{
    return write_regs_locked(dev, reg, &value, 1);
}

static esp_err_t finish_locked(i2c_dev_t *dev, esp_err_t err)
{
    esp_err_t unlock_err = i2c_dev_give_mutex(dev);
    return err == ESP_OK ? unlock_err : err;
}

static esp_err_t update_reg_locked(i2c_dev_t *dev, uint8_t reg,
                                   uint8_t mask, uint8_t value)
{
    uint8_t old;
    CHECK(read_reg_locked(dev, reg, &old));

    value = (old & ~mask) | (value & mask);
    if (value == old)
        return ESP_OK;
    return write_reg_locked(dev, reg, value);
}

static esp_err_t wait_eeprom_locked(i2c_dev_t *dev)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(RV3028_EEPROM_TIMEOUT_MS);
    uint8_t status;

    do
    {
        CHECK(read_reg_locked(dev, RV3028_STATUS, &status));
        if (!(status & RV3028_STATUS_EEBUSY))
            return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));

    } while ((xTaskGetTickCount() - start) < timeout);

    return ESP_ERR_TIMEOUT;
}

/* EEPROM access requires EERD=1 and backup switchover disabled. */
static esp_err_t begin_eeprom_locked(i2c_dev_t *dev, uint8_t *ctrl1,
                                     uint8_t *backup)
{
    bool backup_valid = false;
    esp_err_t err = read_reg_locked(dev, RV3028_CTRL1, ctrl1);
    if (err != ESP_OK)
        return err;

    if (!(*ctrl1 & CTRL1_EERD))
    {
        err = write_reg_locked(dev, RV3028_CTRL1, *ctrl1 | CTRL1_EERD);
        if (err != ESP_OK)
            return err;
    }

    err = wait_eeprom_locked(dev);
    if (err == ESP_OK)
    {
        err = read_reg_locked(dev, RV3028_CONFIG_BACKUP, backup);
        backup_valid = err == ESP_OK;
    }
    if (err == ESP_OK)
        err = write_reg_locked(dev, RV3028_CONFIG_BACKUP,
                               *backup & ~BACKUP_BSM_MASK);
    if (err == ESP_OK)
        return ESP_OK;

    if (backup_valid)
        write_reg_locked(dev, RV3028_CONFIG_BACKUP, *backup);
    if (!(*ctrl1 & CTRL1_EERD))
        write_reg_locked(dev, RV3028_CTRL1, *ctrl1);
    return err;
}

static esp_err_t end_eeprom_locked(i2c_dev_t *dev, uint8_t ctrl1,
                                   uint8_t backup)
{
    esp_err_t err = write_reg_locked(dev, RV3028_CONFIG_BACKUP, backup);
    if (!(ctrl1 & CTRL1_EERD))
    {
        esp_err_t e = write_reg_locked(dev, RV3028_CTRL1, ctrl1);
        if (err == ESP_OK)
            err = e;
    }
    return err;
}

static esp_err_t eeprom_cmd_locked(i2c_dev_t *dev, uint8_t cmd,
                                   uint32_t delay)
{
    CHECK(write_reg_locked(dev, RV3028_EEPROM_CMD, EEPROM_CMD_FIRST));
    CHECK(write_reg_locked(dev, RV3028_EEPROM_CMD, cmd));
    vTaskDelay(pdMS_TO_TICKS(delay));
    return wait_eeprom_locked(dev);
}

static esp_err_t read_eeprom_locked(i2c_dev_t *dev, uint8_t addr,
                                    uint8_t *value)
{
    CHECK(write_reg_locked(dev, RV3028_EEPROM_ADDR, addr));
    CHECK(eeprom_cmd_locked(dev, EEPROM_CMD_READ, 1));
    return read_reg_locked(dev, RV3028_EEPROM_DATA, value);
}

static esp_err_t write_eeprom_locked(i2c_dev_t *dev, uint8_t addr,
                                     uint8_t value)
{
    uint8_t data[] = {addr, value};
    CHECK(write_regs_locked(dev, RV3028_EEPROM_ADDR, data, sizeof(data)));
    return eeprom_cmd_locked(dev, EEPROM_CMD_WRITE, 10);
}

static uint8_t encode_hour(uint8_t hour, bool mode_12h)
{
    if (!mode_12h)
        return DEC_TO_BCD(hour);

    bool pm = hour >= 12;
    hour %= 12;
    if (!hour)
        hour = 12;
    return DEC_TO_BCD(hour) | (pm ? HOURS_PM : 0);
}

static uint8_t decode_hour(uint8_t hour, bool mode_12h)
{
    if (!mode_12h)
        return BCD_TO_DEC(hour & 0x3f);

    bool pm = hour & HOURS_PM;
    hour = BCD_TO_DEC(hour & 0x1f) % 12;
    return hour + (pm ? 12 : 0);
}

static esp_err_t set_control_bit(i2c_dev_t *dev, uint8_t reg,
                                 uint8_t bit, bool enabled)
{
    CHECK_ARG(dev);

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, update_reg_locked(dev, reg, bit, enabled ? bit : 0));
    I2C_DEV_GIVE_MUTEX(dev);

    return ESP_OK;
}

static esp_err_t get_status_flag(i2c_dev_t *dev, uint8_t flag, bool *set)
{
    CHECK_ARG(set);

    uint8_t status;
    CHECK(rv3028_get_status(dev, &status));
    *set = status & flag;

    return ESP_OK;
}

///////////////////////////////////////////////////////////////////////////////

esp_err_t rv3028_init_desc(i2c_dev_t *dev, i2c_port_t port,
                           gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    CHECK_ARG(dev);

    dev->port = port;
    dev->addr = RV3028C7_I2C_ADDR;
    dev->cfg.sda_io_num = sda_gpio;
    dev->cfg.scl_io_num = scl_gpio;
#if HELPER_TARGET_IS_ESP32
    dev->cfg.master.clk_speed = RV3028_I2C_FREQ_HZ;
#endif

    return i2c_dev_create_mutex(dev);
}

esp_err_t rv3028_free_desc(i2c_dev_t *dev)
{
    CHECK_ARG(dev);

    return i2c_dev_delete_mutex(dev);
}

esp_err_t rv3028_configure(i2c_dev_t *dev, const rv3028_config_t *config)
{
    CHECK_ARG(dev);
    CHECK_ARG(config);

    uint8_t id;
    CHECK(rv3028_get_id(dev, &id));

    if (config->use_24_hour_mode)
        CHECK(rv3028_set_24H(dev));
    if (config->disable_trickle_charger)
        CHECK(rv3028_set_trickle_charger(dev, false, RV3028_TRICKLE_15K));
    if (config->configure_backup_mode)
        CHECK(rv3028_set_backup_switchover_mode(dev, config->backup_mode));
    if (config->clear_status)
        CHECK(rv3028_clear_interrupts(dev));

    return ESP_OK;
}

esp_err_t rv3028_get_id(i2c_dev_t *dev, uint8_t *id)
{
    CHECK_ARG(id);

    return rv3028_read_register(dev, RV3028_ID, id);
}

esp_err_t rv3028_get_time(i2c_dev_t *dev, struct tm *timeinfo)
{
    CHECK_ARG(dev);
    CHECK_ARG(timeinfo);

    uint8_t ctrl2;
    uint8_t time[7];

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_CTRL2, &ctrl2));
    I2C_DEV_CHECK(dev, read_regs_locked(dev, RV3028_SECONDS,
                                        time, sizeof(time)));
    I2C_DEV_GIVE_MUTEX(dev);

    memset(timeinfo, 0, sizeof(*timeinfo));
    timeinfo->tm_sec = BCD_TO_DEC(time[0] & 0x7f);
    timeinfo->tm_min = BCD_TO_DEC(time[1] & 0x7f);
    timeinfo->tm_hour = decode_hour(time[2], ctrl2 & CTRL2_12_24);
    timeinfo->tm_wday = time[3] & 0x07;
    timeinfo->tm_mday = BCD_TO_DEC(time[4] & 0x3f);
    timeinfo->tm_mon = BCD_TO_DEC(time[5] & 0x1f) - 1;
    timeinfo->tm_year = BCD_TO_DEC(time[6]) + 100;
    timeinfo->tm_isdst = -1;

    return ESP_OK;
}

esp_err_t rv3028_set_time(i2c_dev_t *dev, const struct tm *timeinfo)
{
    CHECK_ARG(dev);
    CHECK_ARG(timeinfo);

    uint8_t ctrl2;
    uint8_t time[7];

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_CTRL2, &ctrl2));

    time[0] = DEC_TO_BCD(timeinfo->tm_sec);
    time[1] = DEC_TO_BCD(timeinfo->tm_min);
    time[2] = encode_hour(timeinfo->tm_hour, ctrl2 & CTRL2_12_24);
    time[3] = timeinfo->tm_wday;
    time[4] = DEC_TO_BCD(timeinfo->tm_mday);
    time[5] = DEC_TO_BCD(timeinfo->tm_mon + 1);
    time[6] = DEC_TO_BCD(timeinfo->tm_year - 100);

    I2C_DEV_CHECK(dev, write_regs_locked(dev, RV3028_SECONDS,
                                         time, sizeof(time)));
    I2C_DEV_GIVE_MUTEX(dev);

    return ESP_OK;
}

esp_err_t rv3028_get_unix_time(i2c_dev_t *dev, uint32_t *unix_time)
{
    CHECK_ARG(unix_time);

    uint8_t data[4];
    CHECK(rv3028_read_registers(dev, RV3028_UNIX_TIME_0,
                                data, sizeof(data)));
    *unix_time = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

    return ESP_OK;
}

esp_err_t rv3028_set_unix_time(i2c_dev_t *dev, uint32_t unix_time)
{
    CHECK_ARG(dev);

    uint8_t data[] = {
        unix_time,
        unix_time >> 8,
        unix_time >> 16,
        unix_time >> 24,
    };

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                         CTRL2_RESET, CTRL2_RESET));
    I2C_DEV_CHECK(dev, write_regs_locked(dev, RV3028_UNIX_TIME_0,
                                         data, sizeof(data)));
    I2C_DEV_GIVE_MUTEX(dev);

    return ESP_OK;
}

esp_err_t rv3028_is_12H(i2c_dev_t *dev, bool *is_12h)
{
    CHECK_ARG(is_12h);

    uint8_t ctrl2;
    CHECK(rv3028_read_register(dev, RV3028_CTRL2, &ctrl2));
    *is_12h = ctrl2 & CTRL2_12_24;

    return ESP_OK;
}

esp_err_t rv3028_is_PM(i2c_dev_t *dev, bool *is_pm)
{
    CHECK_ARG(dev);
    CHECK_ARG(is_pm);

    uint8_t ctrl2;
    uint8_t hour;

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_CTRL2, &ctrl2));
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_HOURS, &hour));
    I2C_DEV_GIVE_MUTEX(dev);

    *is_pm = (ctrl2 & CTRL2_12_24) && (hour & HOURS_PM);

    return ESP_OK;
}

esp_err_t rv3028_set_12H(i2c_dev_t *dev)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_12_24, true);
}

esp_err_t rv3028_set_24H(i2c_dev_t *dev)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_12_24, false);
}

esp_err_t rv3028_get_status(i2c_dev_t *dev, uint8_t *status)
{
    CHECK_ARG(status);

    return rv3028_read_register(dev, RV3028_STATUS, status);
}

esp_err_t rv3028_clear_status(i2c_dev_t *dev, uint8_t flags)
{
    return rv3028_write_register(dev, RV3028_STATUS, ~flags);
}

esp_err_t rv3028_clear_interrupts(i2c_dev_t *dev)
{
    return rv3028_clear_status(dev, 0x7f);
}

esp_err_t rv3028_is_time_valid(i2c_dev_t *dev, bool *valid)
{
    CHECK_ARG(valid);

    uint8_t status;
    CHECK(rv3028_get_status(dev, &status));
    *valid = !(status & RV3028_STATUS_PORF);

    return ESP_OK;
}

esp_err_t rv3028_reset(i2c_dev_t *dev)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_RESET, true);
}

esp_err_t rv3028_configure_alarm(i2c_dev_t *dev,
                                 const rv3028_alarm_config_t *config)
{
    CHECK_ARG(dev);
    CHECK_ARG(config);

    uint8_t ctrl2;
    uint8_t mode = config->mode & 0x07;
    uint8_t alarm[3];

    I2C_DEV_TAKE_MUTEX(dev);

    // Disable and clear the alarm before changing its registers.
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                         CTRL2_AIE, 0));
    I2C_DEV_CHECK(dev, write_reg_locked(dev, RV3028_STATUS,
                                        ~RV3028_STATUS_AF));
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_CTRL2, &ctrl2));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL1, CTRL1_WADA,
                                         config->use_weekday ? 0 : CTRL1_WADA));

    alarm[0] = DEC_TO_BCD(config->minute);
    alarm[1] = encode_hour(config->hour, ctrl2 & CTRL2_12_24);
    alarm[2] = config->use_weekday
                   ? config->day
                   : DEC_TO_BCD(config->day);
    if (mode & BIT(0))
        alarm[0] |= ALARM_DISABLE;
    if (mode & BIT(1))
        alarm[1] |= ALARM_DISABLE;
    if (mode & BIT(2))
        alarm[2] |= ALARM_DISABLE;

    I2C_DEV_CHECK(dev, write_regs_locked(dev, RV3028_MINUTES_ALARM,
                                         alarm, sizeof(alarm)));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_INT_MASK,
                                         INT_MASK_CAIE,
                                         config->enable_clock_output
                                             ? INT_MASK_CAIE
                                             : 0));
    if (mode != RV3028_ALARM_DISABLED)
        I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                             CTRL2_AIE, CTRL2_AIE));

    I2C_DEV_GIVE_MUTEX(dev);
    return ESP_OK;
}

esp_err_t rv3028_enable_alarm_interrupt(i2c_dev_t *dev, bool enable)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_AIE, enable);
}

esp_err_t rv3028_get_alarm_flag(i2c_dev_t *dev, bool *set)
{
    return get_status_flag(dev, RV3028_STATUS_AF, set);
}

esp_err_t rv3028_clear_alarm_flag(i2c_dev_t *dev)
{
    return rv3028_clear_status(dev, RV3028_STATUS_AF);
}

esp_err_t rv3028_configure_timer(i2c_dev_t *dev,
                                 const rv3028_timer_config_t *config)
{
    CHECK_ARG(dev);
    CHECK_ARG(config);

    uint8_t ctrl1;
    uint8_t timer[] = {config->value, (config->value >> 8) & 0x0f};

    I2C_DEV_TAKE_MUTEX(dev);

    // Stop and clear the timer before loading a new value.
    I2C_DEV_CHECK(dev, read_reg_locked(dev, RV3028_CTRL1, &ctrl1));
    ctrl1 &= ~(CTRL1_TRPT | CTRL1_TE | CTRL1_TD_MASK);
    ctrl1 |= config->frequency & CTRL1_TD_MASK;
    if (config->repeat)
        ctrl1 |= CTRL1_TRPT;

    I2C_DEV_CHECK(dev, write_reg_locked(dev, RV3028_CTRL1, ctrl1));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                         CTRL2_TIE, 0));
    I2C_DEV_CHECK(dev, write_reg_locked(dev, RV3028_STATUS,
                                        ~RV3028_STATUS_TF));
    I2C_DEV_CHECK(dev, write_regs_locked(dev, RV3028_TIMER_VALUE_0,
                                         timer, sizeof(timer)));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_INT_MASK,
                                         INT_MASK_CTIE,
                                         config->enable_clock_output
                                             ? INT_MASK_CTIE
                                             : 0));
    if (config->enable_interrupt)
        I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                             CTRL2_TIE, CTRL2_TIE));
    if (config->start)
        I2C_DEV_CHECK(dev, write_reg_locked(dev, RV3028_CTRL1,
                                            ctrl1 | CTRL1_TE));

    I2C_DEV_GIVE_MUTEX(dev);
    return ESP_OK;
}

esp_err_t rv3028_enable_timer(i2c_dev_t *dev, bool enable)
{
    return set_control_bit(dev, RV3028_CTRL1, CTRL1_TE, enable);
}

esp_err_t rv3028_enable_timer_interrupt(i2c_dev_t *dev, bool enable)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_TIE, enable);
}

esp_err_t rv3028_get_timer_value(i2c_dev_t *dev, uint16_t *value)
{
    CHECK_ARG(value);

    uint8_t timer[2];
    CHECK(rv3028_read_registers(dev, RV3028_TIMER_STATUS_0,
                                timer, sizeof(timer)));
    *value = timer[0] | ((uint16_t)(timer[1] & 0x0f) << 8);

    return ESP_OK;
}

esp_err_t rv3028_get_timer_flag(i2c_dev_t *dev, bool *set)
{
    return get_status_flag(dev, RV3028_STATUS_TF, set);
}

esp_err_t rv3028_clear_timer_flag(i2c_dev_t *dev)
{
    return rv3028_clear_status(dev, RV3028_STATUS_TF);
}

esp_err_t rv3028_enable_periodic_update_interrupt(
    i2c_dev_t *dev, rv3028_update_frequency_t frequency,
    bool enable_clock_output)
{
    CHECK_ARG(dev);

    I2C_DEV_TAKE_MUTEX(dev);

    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                         CTRL2_UIE, 0));
    I2C_DEV_CHECK(dev, write_reg_locked(dev, RV3028_STATUS,
                                        ~RV3028_STATUS_UF));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL1,
                                         CTRL1_USEL,
                                         frequency == RV3028_UPDATE_EVERY_MINUTE
                                             ? CTRL1_USEL
                                             : 0));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_INT_MASK,
                                         INT_MASK_CUIE,
                                         enable_clock_output
                                             ? INT_MASK_CUIE
                                             : 0));
    I2C_DEV_CHECK(dev, update_reg_locked(dev, RV3028_CTRL2,
                                         CTRL2_UIE, CTRL2_UIE));

    I2C_DEV_GIVE_MUTEX(dev);
    return ESP_OK;
}

esp_err_t rv3028_disable_periodic_update_interrupt(i2c_dev_t *dev)
{
    return set_control_bit(dev, RV3028_CTRL2, CTRL2_UIE, false);
}

esp_err_t rv3028_get_periodic_update_flag(i2c_dev_t *dev, bool *set)
{
    return get_status_flag(dev, RV3028_STATUS_UF, set);
}

esp_err_t rv3028_clear_periodic_update_flag(i2c_dev_t *dev)
{
    return rv3028_clear_status(dev, RV3028_STATUS_UF);
}

esp_err_t rv3028_set_trickle_charger(i2c_dev_t *dev, bool enable,
                                     rv3028_trickle_resistance_t resistance)
{
    CHECK_ARG(dev);

    uint8_t backup;
    CHECK(rv3028_read_register(dev, RV3028_CONFIG_BACKUP, &backup));

    backup &= ~BACKUP_TCE;
    if (enable)
    {
        backup &= ~BACKUP_TCR_MASK;
        backup |= (resistance & BACKUP_TCR_MASK) | BACKUP_TCE;
    }
    return rv3028_write_config_eeprom(dev, RV3028_CONFIG_BACKUP, backup);
}

esp_err_t rv3028_set_backup_switchover_mode(i2c_dev_t *dev,
                                            rv3028_backup_mode_t mode)
{
    CHECK_ARG(dev);

    uint8_t backup;
    CHECK(rv3028_read_register(dev, RV3028_CONFIG_BACKUP, &backup));
    backup &= ~BACKUP_BSM_MASK;
    backup |= BACKUP_FEDE | ((mode << 2) & BACKUP_BSM_MASK);

    return rv3028_write_config_eeprom(dev, RV3028_CONFIG_BACKUP, backup);
}

esp_err_t rv3028_enable_clock_output(i2c_dev_t *dev,
                                     rv3028_clkout_frequency_t frequency)
{
    CHECK_ARG(dev);

    uint8_t clkout;
    CHECK(rv3028_read_register(dev, RV3028_CONFIG_CLKOUT, &clkout));
    clkout &= ~CLKOUT_FD_MASK;
    clkout |= (frequency & CLKOUT_FD_MASK) | CLKOUT_CLKOE;
    CHECK(rv3028_write_config_eeprom(dev, RV3028_CONFIG_CLKOUT, clkout));

    return set_control_bit(dev, RV3028_CTRL2, CTRL2_CLKIE, false);
}

esp_err_t rv3028_enable_interrupt_clock_output(
    i2c_dev_t *dev, rv3028_clkout_frequency_t frequency)
{
    CHECK_ARG(dev);

    uint8_t clkout;
    CHECK(set_control_bit(dev, RV3028_CTRL2, CTRL2_CLKIE, false));
    CHECK(rv3028_read_register(dev, RV3028_CONFIG_CLKOUT, &clkout));
    clkout &= ~(CLKOUT_FD_MASK | CLKOUT_CLKOE);
    clkout |= frequency & CLKOUT_FD_MASK;
    CHECK(rv3028_write_config_eeprom(dev, RV3028_CONFIG_CLKOUT, clkout));

    return set_control_bit(dev, RV3028_CTRL2, CTRL2_CLKIE, true);
}

esp_err_t rv3028_disable_clock_output(i2c_dev_t *dev)
{
    CHECK_ARG(dev);

    uint8_t clkout;
    CHECK(set_control_bit(dev, RV3028_CTRL2, CTRL2_CLKIE, false));
    CHECK(rv3028_read_register(dev, RV3028_CONFIG_CLKOUT, &clkout));

    return rv3028_write_config_eeprom(dev, RV3028_CONFIG_CLKOUT,
                                      clkout & ~CLKOUT_CLKOE);
}

esp_err_t rv3028_get_clock_output_flag(i2c_dev_t *dev, bool *set)
{
    return get_status_flag(dev, RV3028_STATUS_CLKF, set);
}

esp_err_t rv3028_clear_clock_output_flag(i2c_dev_t *dev)
{
    return rv3028_clear_status(dev, RV3028_STATUS_CLKF);
}

static esp_err_t read_eeprom(i2c_dev_t *dev, uint8_t address, uint8_t *value)
{
    uint8_t ctrl1 = 0;
    uint8_t backup = 0;
    esp_err_t err;

    I2C_DEV_TAKE_MUTEX(dev);
    err = begin_eeprom_locked(dev, &ctrl1, &backup);
    if (err != ESP_OK)
        return finish_locked(dev, err);
    err = read_eeprom_locked(dev, address, value);
    if (err == ESP_OK)
        err = end_eeprom_locked(dev, ctrl1, backup);
    else
        end_eeprom_locked(dev, ctrl1, backup);
    return finish_locked(dev, err);
}

esp_err_t rv3028_read_user_eeprom(i2c_dev_t *dev, uint8_t address,
                                  uint8_t *value)
{
    CHECK_ARG(dev);
    CHECK_ARG(value);
    CHECK_ARG(address < RV3028_USER_EEPROM_SIZE);

    return read_eeprom(dev, address, value);
}

esp_err_t rv3028_write_user_eeprom(i2c_dev_t *dev, uint8_t address,
                                   uint8_t value)
{
    CHECK_ARG(dev);
    CHECK_ARG(address < RV3028_USER_EEPROM_SIZE);

    uint8_t ctrl1 = 0;
    uint8_t backup = 0;
    uint8_t old;
    esp_err_t err;

    I2C_DEV_TAKE_MUTEX(dev);
    err = begin_eeprom_locked(dev, &ctrl1, &backup);
    if (err != ESP_OK)
        return finish_locked(dev, err);
    err = read_eeprom_locked(dev, address, &old);
    if (err == ESP_OK && old != value)
        err = write_eeprom_locked(dev, address, value);
    if (err == ESP_OK)
        err = end_eeprom_locked(dev, ctrl1, backup);
    else
        end_eeprom_locked(dev, ctrl1, backup);
    return finish_locked(dev, err);
}

esp_err_t rv3028_read_config_eeprom(i2c_dev_t *dev, uint8_t address,
                                    uint8_t *value)
{
    CHECK_ARG(dev);
    CHECK_ARG(value);

    return read_eeprom(dev, address, value);
}

esp_err_t rv3028_write_config_eeprom(i2c_dev_t *dev, uint8_t address,
                                     uint8_t value)
{
    CHECK_ARG(dev);

    uint8_t ctrl1 = 0;
    uint8_t backup = 0;
    uint8_t old;
    esp_err_t err;

    I2C_DEV_TAKE_MUTEX(dev);
    err = begin_eeprom_locked(dev, &ctrl1, &backup);
    if (err != ESP_OK)
        return finish_locked(dev, err);
    err = read_eeprom_locked(dev, address, &old);
    if (err == ESP_OK && address != RV3028_CONFIG_BACKUP)
        err = write_reg_locked(dev, address, value);
    if (err == ESP_OK && old != value)
        err = write_eeprom_locked(dev, address, value);

    uint8_t restore = address == RV3028_CONFIG_BACKUP && err == ESP_OK
                          ? value
                          : backup;
    if (err == ESP_OK)
        err = end_eeprom_locked(dev, ctrl1, restore);
    else
        end_eeprom_locked(dev, ctrl1, backup);
    return finish_locked(dev, err);
}

esp_err_t rv3028_read_user_ram(i2c_dev_t *dev, uint8_t index, uint8_t *value)
{
    CHECK_ARG(value);
    CHECK_ARG(index < RV3028_USER_RAM_SIZE);

    return rv3028_read_register(dev, RV3028_USER_RAM_1 + index, value);
}

esp_err_t rv3028_write_user_ram(i2c_dev_t *dev, uint8_t index, uint8_t value)
{
    CHECK_ARG(index < RV3028_USER_RAM_SIZE);

    return rv3028_write_register(dev, RV3028_USER_RAM_1 + index, value);
}

esp_err_t rv3028_read_register(i2c_dev_t *dev, uint8_t address,
                               uint8_t *value)
{
    CHECK_ARG(value);

    return rv3028_read_registers(dev, address, value, 1);
}

esp_err_t rv3028_write_register(i2c_dev_t *dev, uint8_t address,
                                uint8_t value)
{
    return rv3028_write_registers(dev, address, &value, 1);
}

esp_err_t rv3028_read_registers(i2c_dev_t *dev, uint8_t address,
                                void *data, size_t size)
{
    CHECK_ARG(dev);
    CHECK_ARG(data);

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, read_regs_locked(dev, address, data, size));
    I2C_DEV_GIVE_MUTEX(dev);

    return ESP_OK;
}

esp_err_t rv3028_write_registers(i2c_dev_t *dev, uint8_t address,
                                 const void *data, size_t size)
{
    CHECK_ARG(dev);
    CHECK_ARG(data);

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, write_regs_locked(dev, address, data, size));
    I2C_DEV_GIVE_MUTEX(dev);

    return ESP_OK;
}
