#ifndef __BH1750_H__
#define __BH1750_H__

#include <stdint.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef enum{
  POWER_DOWN               = 0b00000000,
  POWER_UP                 = 0b00000001,
  RESET                    = 0b00000111,
  CONT_HIGH_MODE           = 0b00010000,
  CONT_HIGH_MODE2          = 0b00010001,
  CONT_LOW_MODE            = 0b00010011,
  ONETIME_HIGH_MODE        = 0b00100000,
  ONETIME_HIGH_MODE2       = 0b00100001,
  ONETIME_LOW_MODE         = 0b00100011,
  CHANGE_MEASURE_TIME_HIGH = 0b01000000,
  CHANGE_MEASURE_TIME_LOW  = 0b01100000,
} bh1750_opcode_t;


typedef struct {
    i2c_master_dev_handle_t dev_handle;
} bh_1750_t;

esp_err_t bh1750_init(bh_1750_t *sensor, i2c_master_bus_handle_t bus_handle);

esp_err_t bh1750_power_down(bh_1750_t *sensor);
esp_err_t bh1750_power_on(bh_1750_t *sensor);

esp_err_t bh1750_send_opcode(bh_1750_t *sensor, bh1750_opcode_t opcode);
esp_err_t bh1750_set_measure_time(bh_1750_t *sensor, uint8_t time);

esp_err_t bh1750_read(bh_1750_t *sensor, uint16_t *lux);

#ifdef __cplusplus
}
#endif

#endif /* __BH1750_H__ */
