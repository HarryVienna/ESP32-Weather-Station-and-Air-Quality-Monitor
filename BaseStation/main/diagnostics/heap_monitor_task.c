#include "heap_monitor_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "heap_monitor_task";

// Runs independently of weather_task's 15-minute fetch cycle so a trend
// within that window - or a correlation with some other task's activity -
// is visible, instead of only the value at each cycle boundary.
static void heap_monitor_task(void *pvParameter)
{
  for (;;) {
    ESP_LOGI(TAG, "Heap: internal free=%u largest=%u | spiram free=%u largest=%u",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    vTaskDelay(pdMS_TO_TICKS(1000 * 60)); // Every minute
  }
}

void heap_monitor_start_task(void)
{
  xTaskCreatePinnedToCore(
      heap_monitor_task,
      "Heap Monitor Task",
      4096,
      NULL,
      1,
      NULL,
      1);
}
