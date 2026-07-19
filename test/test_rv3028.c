#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "rv-3028-c7.h"
#include "unity.h"

#define TEST_I2C_PORT I2C_NUM_0
#define TEST_I2C_SDA GPIO_NUM_14
#define TEST_I2C_SCL GPIO_NUM_13

#define RV3028_REG_TIMER_VALUE 0x0a
#define RV3028_REG_CTRL1 0x0f
#define RV3028_REG_CTRL2 0x10
#define RV3028_REG_INT_MASK 0x12
#define RV3028_REG_ID 0x28

static void init_device(i2c_dev_t *dev)
{
    memset(dev, 0, sizeof(*dev));
    TEST_ASSERT_EQUAL(ESP_OK, i2cdev_init());
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_init_desc(dev, TEST_I2C_PORT,
                                               TEST_I2C_SDA, TEST_I2C_SCL));
}

static void free_device(i2c_dev_t *dev)
{
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_free_desc(dev));
}

static void assert_time(const struct tm *expected, const struct tm *actual)
{
    TEST_ASSERT_EQUAL_INT(expected->tm_min, actual->tm_min);
    TEST_ASSERT_EQUAL_INT(expected->tm_hour, actual->tm_hour);
    TEST_ASSERT_EQUAL_INT(expected->tm_wday, actual->tm_wday);
    TEST_ASSERT_EQUAL_INT(expected->tm_mday, actual->tm_mday);
    TEST_ASSERT_EQUAL_INT(expected->tm_mon, actual->tm_mon);
    TEST_ASSERT_EQUAL_INT(expected->tm_year, actual->tm_year);
    TEST_ASSERT_TRUE(actual->tm_sec == expected->tm_sec ||
                     actual->tm_sec == expected->tm_sec + 1);
}

TEST_CASE("rv3028 communicates and reports time validity",
          "[rv3028]")
{
    i2c_dev_t dev;
    uint8_t id;
    uint8_t raw_id;
    uint8_t status;
    bool valid;

    init_device(&dev);

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_id(&dev, &id));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_register(&dev, RV3028_REG_ID,
                                                   &raw_id));
    TEST_ASSERT_EQUAL_HEX8(id, raw_id);

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_status(&dev, &status));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_is_time_valid(&dev, &valid));
    TEST_ASSERT_EQUAL(!(status & RV3028_STATUS_PORF), valid);

    free_device(&dev);
}

TEST_CASE("rv3028 rejects null arguments", "[rv3028]")
{
    i2c_dev_t dev = {0};
    struct tm timeinfo = {0};
    uint8_t value;
    uint32_t unix_time;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_init_desc(NULL, TEST_I2C_PORT,
                                       TEST_I2C_SDA, TEST_I2C_SCL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_free_desc(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_get_id(&dev, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_get_time(NULL, &timeinfo));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_get_time(&dev, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_set_time(NULL, &timeinfo));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rv3028_set_time(&dev, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_get_unix_time(&dev, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_get_unix_time(NULL, &unix_time));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_read_user_eeprom(NULL, 0, &value));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_read_user_ram(NULL, 0, &value));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_read_register(NULL, 0, &value));
}

TEST_CASE("rv3028 reads and writes calendar time", "[rv3028]")
{
    i2c_dev_t dev;
    struct tm saved;
    struct tm actual;
    struct tm expected = {
        .tm_sec = 10,
        .tm_min = 34,
        .tm_hour = 23,
        .tm_mday = 19,
        .tm_mon = 6,
        .tm_year = 126,
        .tm_wday = 0,
        .tm_isdst = -1,
    };
    bool saved_12h;
    bool value;

    init_device(&dev);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_time(&dev, &saved));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_is_12H(&dev, &saved_12h));

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_24H(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_time(&dev, &expected));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_time(&dev, &actual));
    assert_time(&expected, &actual);

    expected.tm_sec = 20;
    expected.tm_hour = 13;
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_12H(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_time(&dev, &expected));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_is_12H(&dev, &value));
    TEST_ASSERT_TRUE(value);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_is_PM(&dev, &value));
    TEST_ASSERT_TRUE(value);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_time(&dev, &actual));
    assert_time(&expected, &actual);

    expected.tm_hour = 1;
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_time(&dev, &expected));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_is_PM(&dev, &value));
    TEST_ASSERT_FALSE(value);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_reset(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_time(&dev, &actual));
    assert_time(&expected, &actual);

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_24H(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_time(&dev, &saved));
    if (saved_12h)
        TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_12H(&dev));

    free_device(&dev);
}

TEST_CASE("rv3028 reads and writes UNIX time", "[rv3028]")
{
    i2c_dev_t dev;
    const uint32_t expected = UINT32_C(1700000000);
    uint32_t saved;
    uint32_t actual;

    init_device(&dev);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_unix_time(&dev, &saved));

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_unix_time(&dev, expected));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_unix_time(&dev, &actual));
    TEST_ASSERT_TRUE(actual == expected || actual == expected + 1);

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_set_unix_time(&dev, saved));
    free_device(&dev);
}

TEST_CASE("rv3028 countdown timer works without interrupt output",
          "[rv3028]")
{
    i2c_dev_t dev;
    rv3028_timer_config_t config = {
        .repeat = false,
        .frequency = RV3028_TIMER_64_HZ,
        .value = 4,
        .enable_interrupt = false,
        .start = true,
        .enable_clock_output = false,
    };
    uint8_t saved_timer[2];
    uint8_t saved_ctrl1;
    uint8_t saved_ctrl2;
    uint8_t saved_int_mask;
    uint16_t timer;
    bool elapsed;

    init_device(&dev);
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_read_registers(&dev, RV3028_REG_TIMER_VALUE,
                                            saved_timer,
                                            sizeof(saved_timer)));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_register(&dev, RV3028_REG_CTRL1,
                                                   &saved_ctrl1));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_register(&dev, RV3028_REG_CTRL2,
                                                   &saved_ctrl2));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_register(&dev, RV3028_REG_INT_MASK,
                                                   &saved_int_mask));

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_configure_timer(&dev, &config));
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_timer_value(&dev, &timer));
    TEST_ASSERT_EQUAL_UINT16(0, timer);
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_get_timer_flag(&dev, &elapsed));
    TEST_ASSERT_TRUE(elapsed);

    TEST_ASSERT_EQUAL(ESP_OK, rv3028_enable_timer(&dev, false));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_clear_timer_flag(&dev));
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_write_registers(&dev, RV3028_REG_TIMER_VALUE,
                                             saved_timer,
                                             sizeof(saved_timer)));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_write_register(&dev, RV3028_REG_INT_MASK,
                                                    saved_int_mask));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_write_register(&dev, RV3028_REG_CTRL2,
                                                    saved_ctrl2));
    TEST_ASSERT_EQUAL(ESP_OK, rv3028_write_register(&dev, RV3028_REG_CTRL1,
                                                    saved_ctrl1));

    free_device(&dev);
}

TEST_CASE("rv3028 user RAM access checks bounds", "[rv3028]")
{
    i2c_dev_t dev;
    uint8_t saved[RV3028_USER_RAM_SIZE];
    uint8_t value;
    uint8_t test_value = 0xa5;

    init_device(&dev);

    for (uint8_t i = 0; i < RV3028_USER_RAM_SIZE; i++)
    {
        TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_user_ram(&dev, i, &saved[i]));
        TEST_ASSERT_EQUAL(ESP_OK,
                          rv3028_write_user_ram(&dev, i, test_value ^ i));
        TEST_ASSERT_EQUAL(ESP_OK, rv3028_read_user_ram(&dev, i, &value));
        TEST_ASSERT_EQUAL_HEX8(test_value ^ i, value);
    }

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_read_user_ram(&dev, RV3028_USER_RAM_SIZE,
                                           &value));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_write_user_ram(&dev, RV3028_USER_RAM_SIZE, 0));

    for (uint8_t i = 0; i < RV3028_USER_RAM_SIZE; i++)
        TEST_ASSERT_EQUAL(ESP_OK, rv3028_write_user_ram(&dev, i, saved[i]));

    free_device(&dev);
}

TEST_CASE("rv3028 user EEPROM access checks bounds", "[rv3028]")
{
    i2c_dev_t dev;
    const uint8_t address = RV3028_USER_EEPROM_SIZE - 1;
    uint8_t saved;
    uint8_t expected;
    uint8_t actual;

    init_device(&dev);

    // Exercise one byte and restore it to limit EEPROM wear.
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_read_user_eeprom(&dev, address, &saved));
    expected = saved ^ UINT8_C(0x5a);
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_write_user_eeprom(&dev, address, expected));
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_read_user_eeprom(&dev, address, &actual));
    TEST_ASSERT_EQUAL_HEX8(expected, actual);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_read_user_eeprom(&dev,
                                              RV3028_USER_EEPROM_SIZE,
                                              &actual));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      rv3028_write_user_eeprom(&dev,
                                               RV3028_USER_EEPROM_SIZE, 0));

    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_write_user_eeprom(&dev, address, saved));
    TEST_ASSERT_EQUAL(ESP_OK,
                      rv3028_read_user_eeprom(&dev, address, &actual));
    TEST_ASSERT_EQUAL_HEX8(saved, actual);

    free_device(&dev);
}
