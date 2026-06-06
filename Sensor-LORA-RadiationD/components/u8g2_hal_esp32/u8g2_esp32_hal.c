#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "u8g2_esp32_hal.h"

static const char* TAG = "u8g2_hal";

static spi_device_handle_t handle_spi;
static u8g2_esp32_hal_t u8g2_esp32_hal;

#define HOST SPI2_HOST

#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)                   \
  do {                                       \
    esp_err_t rc = (x);                      \
    if (rc != ESP_OK) {                      \
      ESP_LOGE("err", "esp_err_t = %d", rc); \
      assert(0 && #x);                       \
    }                                        \
  } while (0);

void u8g2_esp32_hal_init(u8g2_esp32_hal_t u8g2_esp32_hal_param) {
  u8g2_esp32_hal = u8g2_esp32_hal_param;
}

uint8_t u8g2_esp32_spi_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  ESP_LOGD(TAG, "spi_byte_cb: msg=%d arg_int=%d", msg, arg_int);
  switch (msg) {
    case U8X8_MSG_BYTE_SET_DC:
      if (u8g2_esp32_hal.dc != U8G2_ESP32_HAL_UNDEFINED)
        gpio_set_level(u8g2_esp32_hal.dc, arg_int);
      break;

    case U8X8_MSG_BYTE_INIT: {
      if (u8g2_esp32_hal.bus.spi.clk == U8G2_ESP32_HAL_UNDEFINED ||
          u8g2_esp32_hal.bus.spi.mosi == U8G2_ESP32_HAL_UNDEFINED ||
          u8g2_esp32_hal.bus.spi.cs == U8G2_ESP32_HAL_UNDEFINED)
        break;

      spi_bus_config_t bus_config = {0};
      bus_config.sclk_io_num   = u8g2_esp32_hal.bus.spi.clk;
      bus_config.mosi_io_num   = u8g2_esp32_hal.bus.spi.mosi;
      bus_config.miso_io_num   = GPIO_NUM_NC;
      bus_config.quadwp_io_num = GPIO_NUM_NC;
      bus_config.quadhd_io_num = GPIO_NUM_NC;
      ESP_ERROR_CHECK(spi_bus_initialize(HOST, &bus_config, 1));

      spi_device_interface_config_t dev_config = {0};
      dev_config.mode           = 0;
      dev_config.clock_speed_hz = 10000;
      dev_config.spics_io_num   = u8g2_esp32_hal.bus.spi.cs;
      dev_config.queue_size     = 200;
      ESP_ERROR_CHECK(spi_bus_add_device(HOST, &dev_config, &handle_spi));
      break;
    }

    case U8X8_MSG_BYTE_SEND: {
      spi_transaction_t trans_desc = {0};
      trans_desc.length    = 8 * arg_int;
      trans_desc.tx_buffer = arg_ptr;
      ESP_ERROR_CHECK(spi_device_transmit(handle_spi, &trans_desc));
      break;
    }
  }
  return 0;
}

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t i2c_dev_handle = NULL;
static uint8_t i2c_tx_buf[256];
static size_t  i2c_tx_len = 0;

uint8_t u8g2_esp32_i2c_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  switch (msg) {
    case U8X8_MSG_BYTE_INIT: {
      i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_1,
        .sda_io_num          = u8g2_esp32_hal.bus.i2c.sda,
        .scl_io_num          = u8g2_esp32_hal.bus.i2c.scl,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
      };
      ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_handle));

      i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = u8x8_GetI2CAddress(u8x8) >> 1,
        .scl_speed_hz    = 400000,
      };
      ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle));
      break;
    }

    case U8X8_MSG_BYTE_START_TRANSFER:
      i2c_tx_len = 0;
      break;

    case U8X8_MSG_BYTE_SEND:
      memcpy(&i2c_tx_buf[i2c_tx_len], arg_ptr, arg_int);
      i2c_tx_len += arg_int;
      break;

    case U8X8_MSG_BYTE_END_TRANSFER:
      ESP_ERROR_CHECK(i2c_master_transmit(i2c_dev_handle, i2c_tx_buf, i2c_tx_len, 100));
      break;
  }
  return 0;
}

uint8_t u8g2_esp32_gpio_and_delay_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  ESP_LOGD(TAG, "gpio_and_delay_cb: msg=%d arg_int=%d", msg, arg_int);

  switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT: {
      uint64_t bitmask = 0;
      if (u8g2_esp32_hal.dc    != U8G2_ESP32_HAL_UNDEFINED) bitmask |= (1ull << u8g2_esp32_hal.dc);
      if (u8g2_esp32_hal.reset != U8G2_ESP32_HAL_UNDEFINED) bitmask |= (1ull << u8g2_esp32_hal.reset);
      if (u8g2_esp32_hal.bus.spi.cs != U8G2_ESP32_HAL_UNDEFINED) bitmask |= (1ull << u8g2_esp32_hal.bus.spi.cs);
      if (bitmask == 0) break;

      gpio_config_t gpioConfig = {
        .pin_bit_mask = bitmask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
      };
      gpio_config(&gpioConfig);
      break;
    }

    case U8X8_MSG_GPIO_RESET:
      if (u8g2_esp32_hal.reset != U8G2_ESP32_HAL_UNDEFINED)
        gpio_set_level(u8g2_esp32_hal.reset, arg_int);
      break;

    case U8X8_MSG_GPIO_CS:
      if (u8g2_esp32_hal.bus.spi.cs != U8G2_ESP32_HAL_UNDEFINED)
        gpio_set_level(u8g2_esp32_hal.bus.spi.cs, arg_int);
      break;

    case U8X8_MSG_GPIO_I2C_CLOCK:
      if (u8g2_esp32_hal.bus.i2c.scl != U8G2_ESP32_HAL_UNDEFINED)
        gpio_set_level(u8g2_esp32_hal.bus.i2c.scl, arg_int);
      break;

    case U8X8_MSG_GPIO_I2C_DATA:
      if (u8g2_esp32_hal.bus.i2c.sda != U8G2_ESP32_HAL_UNDEFINED)
        gpio_set_level(u8g2_esp32_hal.bus.i2c.sda, arg_int);
      break;

    case U8X8_MSG_DELAY_MILLI:
      vTaskDelay(arg_int / portTICK_PERIOD_MS);
      break;
  }
  return 0;
}
