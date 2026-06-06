#ifndef U8G2_ESP32_HAL_H
#define U8G2_ESP32_HAL_H

#include "u8g2.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#define U8G2_ESP32_HAL_UNDEFINED GPIO_NUM_NC

typedef struct {
  union {
    struct {
      gpio_num_t clk;
      gpio_num_t mosi;
      gpio_num_t cs;
    } spi;
    struct {
      gpio_num_t sda;
      gpio_num_t scl;
    } i2c;
  } bus;
  gpio_num_t reset;
  gpio_num_t dc;
} u8g2_esp32_hal_t;

#define U8G2_ESP32_HAL_DEFAULT                                        \
  {                                                                   \
    .bus = {.spi = {.clk = U8G2_ESP32_HAL_UNDEFINED,                  \
                    .mosi = U8G2_ESP32_HAL_UNDEFINED,                 \
                    .cs = U8G2_ESP32_HAL_UNDEFINED}},                 \
    .reset = U8G2_ESP32_HAL_UNDEFINED, .dc = U8G2_ESP32_HAL_UNDEFINED \
  }

void u8g2_esp32_hal_init(u8g2_esp32_hal_t u8g2_esp32_hal_param);
uint8_t u8g2_esp32_spi_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);
uint8_t u8g2_esp32_i2c_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);
uint8_t u8g2_esp32_gpio_and_delay_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);

#endif /* U8G2_ESP32_HAL_H */
