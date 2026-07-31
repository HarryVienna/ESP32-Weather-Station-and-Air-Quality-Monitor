#include "clock_task.h"

#include <stdio.h>
#include <time.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gui_status.h"
#include "i18n/i18n.h"

static const char* TAG = "clock_task";

/**
 * @brief     Task for updating and displaying date and time
 *
 * @param     pvParameter   Pointer to task parameters (not used in this function)
 *
 * @details   Updates the date and time information continuously and displays it.
 *            Utilizes a loop to update and format the date-time string at regular intervals.
 */
void clock_task(void *pvParameter)
{
  ESP_LOGI(TAG, "Start Clock task");

  struct tm timeinfo;
  time_t now;

  char str_ftime[24];
  char wday_tag[16];   // "%A" z.B. "Wednesday" (9+\0) - Marge fuer laengere Namen
  char date_time[48];  // laengster Wochentag ("Donnerstag", 10) + " " + str_ftime + Marge

  for (;;) {

    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(str_ftime, sizeof(str_ftime), _("%d.%m.%Y  %H:%M:%S"), &timeinfo);
    strftime(wday_tag, sizeof(wday_tag), "%A", &timeinfo);
    snprintf(date_time, sizeof(date_time), "%s %s", _(wday_tag), str_ftime);

    disp_date_time(date_time);

    vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep for 1 second
  }
}
