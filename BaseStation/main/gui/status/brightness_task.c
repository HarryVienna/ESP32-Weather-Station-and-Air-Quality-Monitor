#include "brightness_task.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c/i2c_manager.h"
#include "display/display.h"
#include "gui_status.h"
#include "c4001.h"
#include "bh1750.h"

/*
0   Lux - 15
25 Lux - 50
28  Lux -  75
170 Lux - 100
190 Lux - 122
213 Lux -  132
250  Lux -  150
*/

static const char* TAG = "brightness_task";

#define LUX_CALIBRATION_OFFSET   5     // Eigenlicht der Gehaeuse-LEDs (wetterstationsspezifisch
#define BRIGHTNESS_NO_PRESENCE   5     // Display-Helligkeit, wenn keine Anwesenheit erkannt wird

/* Der C4001-Radar liefert direkt nach dem Konfigurieren (RECOVER_SEN/
 * PRESENCE_MODE/START_SEN/Range/Sensitivity/Delay) noch eine Weile
 * "presence=false", auch wenn jemand davorsteht (vermutlich interne
 * Kalibrierung/Einschwingzeit) - deshalb wird nach der Konfiguration erst
 * gewartet, bevor ueberhaupt ein Presence-Wert abgefragt wird. Wert ist
 * eine erste Schaetzung, ggf. anhand der Lux/Brightness-Logs nachjustieren
 * (faengt der erste Wert schon bei einem sinnvollen Ziel statt bei
 * BRIGHTNESS_NO_PRESENCE an?). */
#define PRESENCE_WARMUP_MS 5000

static uint16_t map_brightness_power(uint16_t lux, bool presence)
{
  if (!presence) {
    return BRIGHTNESS_NO_PRESENCE;
  }
  const float a = 9.658973f;
  const float b = 0.461319f;
  int brightness = (int)(a * powf((float)lux, b) + 10.0f);
  if (brightness < 10)   brightness = 10;
  if (brightness > 255) brightness = 255;
  return (uint16_t)brightness;
}

/**
 * @brief     Task for adjusting brightness based on sensor readings
 *
 * @param     pvParameter   Pointer to task parameters (not used in this function)
 *
 * @details   Monitors sensor values and adjusts brightness levels accordingly.
 *            Uses hysteresis thresholds to control smooth transitions in brightness changes.
 *            Implements a loop to continuously monitor and update brightness levels.
 */
void brightness_task(void *pvParameter)
{
  ESP_LOGI(TAG, "Start Brighness task");

  // Init Lux sensor
  bh_1750_t lux_sensor;
  bh1750_init(&lux_sensor, i2c_manager_get_bus());

  bh1750_power_on(&lux_sensor);
  bh1750_set_measure_time(&lux_sensor, 254);
  bh1750_send_opcode(&lux_sensor, CONT_HIGH_MODE);

  // Init presence sensor
  c4001_t presence_sensor;
  c4001_init(&presence_sensor, i2c_manager_get_bus());

  c4001_set_sensor(&presence_sensor, RECOVER_SEN);

  uint32_t soft_version = c4001_get_soft_version(&presence_sensor);
  ESP_LOGI(TAG, "Software version  = %lu", soft_version);

  // Set sensor mode
  c4001_set_mode(&presence_sensor, PRESENCE_MODE);

  c4001_set_sensor(&presence_sensor, START_SEN);

  if(c4001_set_detect_range(&presence_sensor, /*min*/30, /*max*/400, /*trig*/300) == ESP_OK){
    ESP_LOGI(TAG, "set detection range successfully");
  }

  // set trigger sensitivity 0 - 9
  if(c4001_set_trig_sensitivity(&presence_sensor, 2) == ESP_OK){
    ESP_LOGI(TAG, "set trig sensitivity successfully");
  }

  // set keep sensitivity 0 - 9
  if(c4001_set_keep_sensitivity(&presence_sensor, 4) == ESP_OK){
    ESP_LOGI(TAG, "set keep sensitivity successfully");
  }

  // set delay  - keep ist in Einheiten von 0.5 Sekunden, trig ist in 10ms-Einheiten
  if(c4001_set_delay(&presence_sensor, /*trig*/ 10, /*keep*/ 60) == ESP_OK){
    ESP_LOGI(TAG, "set delay successfully");
  }

  // Radar-Einschwingzeit abwarten (siehe PRESENCE_WARMUP_MS oben), bevor
  // ueberhaupt ein Presence-Wert abgefragt wird - die Beleuchtung steht bis
  // dahin unveraendert auf DISPLAY_INIT_BRIGHTNESS (siehe unten).
  vTaskDelay(pdMS_TO_TICKS(PRESENCE_WARMUP_MS));

  presence_data_t presence_data = {0};

  uint16_t lux = 0;
  // Beide auf die tatsaechliche Boot-Helligkeit vorbelegt (nicht 0) - sonst
  // rampt current_brightness beim Task-Start sichtbar von schwarz hoch,
  // obwohl die Beleuchtung real schon auf DISPLAY_INIT_BRIGHTNESS steht.
  uint16_t target_brightness = DISPLAY_INIT_BRIGHTNESS;
  uint16_t current_brightness = DISPLAY_INIT_BRIGHTNESS;
  uint8_t sensor_tick = 0;

  for (;;) {

    // Sensor alle 250ms lesen (10 * 25ms)
    if (sensor_tick++ >= 10) {
      sensor_tick = 0;

      uint16_t lux_raw = 0;
      bh1750_read(&lux_sensor, &lux_raw);
      lux = (lux_raw > LUX_CALIBRATION_OFFSET) ? (lux_raw - LUX_CALIBRATION_OFFSET) : 0;

      c4001_get_presence_status(&presence_sensor, &presence_data);

      target_brightness = map_brightness_power(lux, presence_data.presence);
    }

    uint16_t diff = (target_brightness > current_brightness)
                    ? target_brightness - current_brightness
                    : current_brightness - target_brightness;
    uint16_t hysteresis = current_brightness / 10;  // 10%

    if (diff > hysteresis) {
      //          Brightness	Step
      //          < 20	      1
      //          40          2
      //          100         5
      //          150         7
      //          200         10
      uint16_t step = (current_brightness < 20) ? 1 : current_brightness / 20;

      if (current_brightness < target_brightness) {
        current_brightness = (target_brightness - current_brightness > step)
                             ? current_brightness + step : target_brightness;
      } else {
        current_brightness = (current_brightness - target_brightness > step)
                             ? current_brightness - step : target_brightness;
      }
      set_brightness(current_brightness);
      ESP_LOGI(TAG, "Lux/Brightness: %d  %d", lux, current_brightness);
    }

    vTaskDelay(pdMS_TO_TICKS(25));

  }
}
