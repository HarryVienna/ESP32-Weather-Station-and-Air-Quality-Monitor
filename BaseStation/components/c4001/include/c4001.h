#ifndef __c4001_H__
#define __c4001_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

typedef struct{
  uint8_t work_status;
  uint8_t work_mode;
  uint8_t init_status;
} sensor_status_t;

typedef struct{
  bool presence;
  uint16_t range;
} presence_data_t;

typedef struct{
  uint8_t number;
  int16_t speed;
  uint16_t range;
  uint16_t energy;
} speed_data_t;

typedef enum{
  PRESENCE_MODE  = 0x00,
  SPEED_MODE     = 0x01,
} sensor_mode_t;

typedef enum{
  MICRO_ON  = 0x01,
  MICRO_OFF = 0x00,
} switch_t;

typedef enum{
  START_SEN   = 0x55,
  STOP_SEN    = 0x33,
  RESET_SEN   = 0xCC,
  RECOVER_SEN = 0xAA,
  SAVE_PARAMS = 0x5C,
  CHANGE_MODE = 0x3B,
} set_cmd_t;

typedef struct {
    i2c_master_dev_handle_t dev_handle;
} c4001_t;

esp_err_t c4001_init(c4001_t *sensor, i2c_master_bus_handle_t bus_handle);

sensor_status_t c4001_get_status(c4001_t *sensor);
uint32_t c4001_get_soft_version(c4001_t *sensor);
void c4001_set_sensor(c4001_t *sensor, set_cmd_t mode);
esp_err_t c4001_set_mode(c4001_t *sensor, sensor_mode_t mode);

esp_err_t c4001_set_trig_sensitivity(c4001_t *sensor, uint8_t sensitivity);
uint8_t c4001_get_trig_sensitivity(c4001_t *sensor);

esp_err_t c4001_set_keep_sensitivity(c4001_t *sensor, uint8_t sensitivity);
uint8_t c4001_get_keep_sensitivity(c4001_t *sensor);

esp_err_t c4001_set_delay(c4001_t *sensor, uint8_t trig, uint16_t keep);
uint8_t c4001_get_trig_delay(c4001_t *sensor);
uint16_t c4001_get_keep_timerout(c4001_t *sensor);

esp_err_t c4001_set_detect_range(c4001_t *sensor, uint16_t min, uint16_t max, uint16_t trig);
uint16_t c4001_get_min_range(c4001_t *sensor);
uint16_t c4001_get_max_range(c4001_t *sensor);
uint16_t c4001_get_trig_range(c4001_t *sensor);

void c4001_get_presence_status(c4001_t *sensor, presence_data_t *presence_data);

esp_err_t c4001_set_detect_thres(c4001_t *sensor, uint16_t min, uint16_t max, uint16_t thres);
uint16_t c4001_get_tmin_range(c4001_t *sensor);
uint16_t c4001_get_tmax_range(c4001_t *sensor);
uint16_t c4001_get_thres_range(c4001_t *sensor);

void c4001_set_micro_detection(c4001_t *sensor, switch_t sta);
switch_t c4001_get_micro_detection(c4001_t *sensor);

void c4001_get_speed_status(c4001_t *sensor, speed_data_t *speed_data);

#ifdef __cplusplus
}
#endif

#endif /* __c4001_H__ */
