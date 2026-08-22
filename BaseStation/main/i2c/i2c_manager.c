#include "i2c_manager.h"

#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "i2c_manager";

#define I2C_SDA_PIN         GPIO_NUM_7
#define I2C_SCL_PIN         GPIO_NUM_8
#define I2C_PORT_NUM        I2C_NUM_0

static i2c_master_bus_handle_t s_bus = NULL;

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "=== I2C Bus Scan ===");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  Device found at 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  Kein Gerät gefunden — Verdrahtung prüfen!");
    }
    ESP_LOGI(TAG, "===================");
}

esp_err_t i2c_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus (SDA=GPIO%d, SCL=GPIO%d)",
             I2C_SDA_PIN, I2C_SCL_PIN);

    i2c_master_bus_config_t cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = I2C_PORT_NUM,
        .sda_io_num        = I2C_SDA_PIN,
        .scl_io_num        = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c_new_master_bus failed");

    ESP_LOGI(TAG, "I2C bus ready");

    i2c_scan();

    return ESP_OK;
}

esp_err_t i2c_manager_del_bus(void)
{
    if (s_bus != NULL) {
        esp_err_t ret = i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ret;
    }
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_manager_get_bus(void)
{
    return s_bus;
}
