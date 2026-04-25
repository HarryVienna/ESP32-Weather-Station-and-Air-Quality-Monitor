#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "button.h"

#include <string.h>

static const char *TAG = "config";

#define NVS_NAMESPACE   "sensor_cfg"
#define NVS_KEY_NR      "sensor_nr"
#define NVS_KEY_POWER   "tx_power"

#define BUTTON_GPIO     GPIO_NUM_0

#define SENSOR_NR_MIN   0
#define SENSOR_NR_MAX   15
#define TX_POWER_MIN    (-9)
#define TX_POWER_MAX    22

/* ============================================================================
 * NVS
 * ============================================================================ */

esp_err_t config_load(sensor_config_t *cfg) {
    cfg->sensor_nr = CONFIG_DEFAULT_SENSOR_NR;
    cfg->tx_power  = CONFIG_DEFAULT_TX_POWER;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved config, using defaults (nr=%d pwr=%d)",
                 cfg->sensor_nr, cfg->tx_power);
        return ESP_OK;
    }

    uint8_t nr;
    int8_t  pwr;
    if (nvs_get_u8(h, NVS_KEY_NR,    &nr)  == ESP_OK) cfg->sensor_nr = nr;
    if (nvs_get_i8(h, NVS_KEY_POWER, &pwr) == ESP_OK) cfg->tx_power  = pwr;

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: sensor_nr=%d tx_power=%d", cfg->sensor_nr, cfg->tx_power);
    return ESP_OK;
}

esp_err_t config_save(const sensor_config_t *cfg) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_u8(h, NVS_KEY_NR,    cfg->sensor_nr));
    ESP_ERROR_CHECK(nvs_set_i8(h, NVS_KEY_POWER, cfg->tx_power));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved: sensor_nr=%d tx_power=%d", cfg->sensor_nr, cfg->tx_power);
    return ESP_OK;
}

/* ============================================================================
 * Menu display
 * ============================================================================ */

typedef enum {
    ITEM_SENSOR_NR = 0,
    ITEM_TX_POWER,
    ITEM_SAVE,
    ITEM_COUNT
} menu_item_t;

static void draw_value(u8g2_t *u8g2, int y, const char *label, const char *value,
                       bool is_active, bool is_editing) {
    if (is_active && !is_editing) {
        u8g2_DrawStr(u8g2, 0, y, ">");
    }
    u8g2_DrawStr(u8g2, 9, y, label);

    int vw = u8g2_GetStrWidth(u8g2, value);
    int vx = 125 - vw;

    if (is_active && is_editing) {
        u8g2_DrawBox(u8g2, vx - 2, y - 10, vw + 4, 12);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, vx, y, value);
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawStr(u8g2, vx, y, value);
    }
}

static void menu_draw(u8g2_t *u8g2, menu_item_t item, bool editing,
                      const sensor_config_t *cfg) {
    char buf[16];

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);

    // Title
    u8g2_DrawStr(u8g2, 37, 10, "CONFIG");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    // Sensor Nr
    snprintf(buf, sizeof(buf), "%d", cfg->sensor_nr);
    draw_value(u8g2, 26, "Sensor Nr:", buf,
               item == ITEM_SENSOR_NR, editing && item == ITEM_SENSOR_NR);

    // TX Power
    snprintf(buf, sizeof(buf), "%d dBm", cfg->tx_power);
    draw_value(u8g2, 40, "TX Power:", buf,
               item == ITEM_TX_POWER, editing && item == ITEM_TX_POWER);

    // Save button (centered, highlighted when active)
    const char *save_str = "[ SAVE ]";
    int sw = u8g2_GetStrWidth(u8g2, save_str);
    int sx = (128 - sw) / 2;
    if (item == ITEM_SAVE) {
        u8g2_DrawBox(u8g2, sx - 3, 46, sw + 6, 12);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, sx, 56, save_str);
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawStr(u8g2, sx, 56, save_str);
    }

    u8g2_SendBuffer(u8g2);
}

/* ============================================================================
 * Button handling
 * ============================================================================ */

typedef enum { EVT_SHORT, EVT_LONG } btn_evt_t;

static QueueHandle_t s_btn_queue;

static void btn_callback(button_press_type_t type) {
    btn_evt_t evt = (type == BUTTON_PRESS_SHORT) ? EVT_SHORT : EVT_LONG;
    xQueueSend(s_btn_queue, &evt, 0);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void config_run_menu(u8g2_t *u8g2, sensor_config_t *cfg) {
    s_btn_queue = xQueueCreate(8, sizeof(btn_evt_t));

    button_config_t btn_cfg = {
        .gpio_num             = BUTTON_GPIO,
        .active_low           = true,
        .short_press_callback = btn_callback,
        .long_press_callback  = btn_callback,
        .double_click_callback = NULL,
        .enable_repeat        = false,
    };
    button_handle_t *btn = button_create(&btn_cfg);

    menu_item_t current = ITEM_SENSOR_NR;
    bool editing = false;

    menu_draw(u8g2, current, editing, cfg);

    bool done = false;
    while (!done) {
        btn_evt_t evt;
        if (xQueueReceive(s_btn_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (evt == EVT_SHORT) {
            if (editing) {
                if (current == ITEM_SENSOR_NR) {
                    cfg->sensor_nr++;
                    if (cfg->sensor_nr > SENSOR_NR_MAX) cfg->sensor_nr = SENSOR_NR_MIN;
                } else if (current == ITEM_TX_POWER) {
                    cfg->tx_power++;
                    if (cfg->tx_power > TX_POWER_MAX) cfg->tx_power = TX_POWER_MIN;
                }
            } else {
                current = (menu_item_t)((current + 1) % ITEM_COUNT);
            }
        } else { /* EVT_LONG */
            if (editing) {
                editing = false;
            } else if (current == ITEM_SAVE) {
                config_save(cfg);
                done = true;
            } else {
                editing = true;
            }
        }

        if (!done) {
            menu_draw(u8g2, current, editing, cfg);
        }
    }

    // Save confirmation
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
    u8g2_DrawStr(u8g2, 20, 30, "Config saved!");
    u8g2_DrawStr(u8g2, 20, 44, "Starting...");
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(1500));

    button_delete(btn);
    vQueueDelete(s_btn_queue);
    s_btn_queue = NULL;
}
