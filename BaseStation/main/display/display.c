/*******************************************************************************
 * display.c - Waveshare ESP32-P4-Module-DEV-KIT-C with 10.1" DSI Display Driver
 *******************************************************************************
 *
 * Complete display subsystem initialization for:
 *   - 10.1" MIPI DSI display (800x1280, JD9365 controller)
 *   - GT911 capacitive touch controller (I2C)
 *   - LVGL 8 graphics library (landscape mode via software rotation)
 *
 * Display: Waveshare 10.1-DSI-TOUCH-A
 *   https://www.waveshare.com/wiki/10.1-DSI-TOUCH-A
 *
 * Board: Waveshare ESP32-P4-Module-DEV-KIT
 *   https://www.waveshare.com/esp32-p4-module-dev-kit.htm?sku=30844
 *
 ******************************************************************************/

#include "display.h"

#include <string.h>

/* ---- FreeRTOS ---- */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---- ESP-IDF Drivers ---- */
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/i2c_master.h"

/* ---- JD9365 LCD panel driver (Espressif component) ---- */
#include "esp_lcd_jd9365.h"

/* ---- Touch driver ---- */
#include "esp_lcd_touch_gt911.h"

/* ---- LVGL & ESP LVGL Port ---- */
#include "esp_lvgl_port.h"

static const char *TAG = "display";

/*******************************************************************************
 * Hardware Configuration
 ******************************************************************************/

/* Physical display resolution (portrait, as wired) */
#define PHYS_H_RES  800
#define PHYS_V_RES  1280

/* MIPI DSI PHY power (internal LDO) */
#define DSI_PHY_LDO_CHAN    3
#define DSI_PHY_LDO_MV     2500

/* I2C bus (shared between backlight and touch) */
#define I2C_SCL_PIN         GPIO_NUM_8
#define I2C_SDA_PIN         GPIO_NUM_7
#define I2C_PORT_NUM        I2C_NUM_0
#define I2C_CLK_SPEED_HZ    400000

/* Backlight controller */
#define BL_I2C_ADDR         0x45
#define BL_BRIGHTNESS_REG   0x96

/* Touch controller (GT911) */
#define TOUCH_I2C_ADDR      ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS

/*******************************************************************************
 * Module State
 ******************************************************************************/
static i2c_master_bus_handle_t  s_i2c_bus       = NULL;
static i2c_master_dev_handle_t  s_bl_dev        = NULL;
static esp_lcd_panel_handle_t   s_panel         = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus       = NULL;
static esp_lcd_touch_handle_t   s_touch         = NULL;
static lv_disp_t               *s_lvgl_display  = NULL;


/*******************************************************************************
 * Waveshare 10.1" JD9365 Vendor Init Commands
 *
 * The JD9365 uses a paged register model: register 0xE0 selects
 * the active page, then subsequent writes target registers on that page.
 *
 * This table configures:
 *   - Page 0: Unlock password (0x93/0x65/0xF8), DSI 2-lane mode
 *   - Page 1: Power (VCOM, VGHO/VGLO), positive/negative gamma curves
 *   - Page 2: GIP (Gate-In-Panel) forward/reverse mapping and timing
 *   - Page 4: Additional panel-specific settings
 *   - Page 0: Final config, SLPOUT (120ms wait), DISPON (20ms wait)
 *
 * Without this sequence, the JD9365 stays unconfigured and the display
 * remains dark even if the DSI video stream is active.
 *
 * Source: Waveshare esp_lcd_jd9365_10_1 component
 *   https://github.com/waveshareteam/Waveshare-ESP32-components
 ******************************************************************************/
static const jd9365_lcd_init_cmd_t s_ws_init_cmds[] = {
    /* ===== Page 0: Unlock password & DSI lane config ==================== */
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},   /* DSI 2-lane mode */

    /* ===== Page 1: Power and Gamma ====================================== */
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x38}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x38}, 1, 0},

    {0x0C, (uint8_t[]){0x74}, 1, 0},

    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xAF}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xAF}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},

    {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0},

    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x01}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0x7F}, 1, 0},

    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0xA0}, 1, 0},
    {0x42, (uint8_t[]){0x81}, 1, 0},
    {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x0D}, 1, 0},
    {0x45, (uint8_t[]){0x28}, 1, 0},

    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x69}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x2A}, 1, 0},
    {0x5B, (uint8_t[]){0x17}, 1, 0},

    /* Positive Gamma */
    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x6A}, 1, 0},
    {0x5F, (uint8_t[]){0x5B}, 1, 0},
    {0x60, (uint8_t[]){0x4F}, 1, 0},
    {0x61, (uint8_t[]){0x4A}, 1, 0},
    {0x62, (uint8_t[]){0x3D}, 1, 0},
    {0x63, (uint8_t[]){0x41}, 1, 0},
    {0x64, (uint8_t[]){0x2A}, 1, 0},
    {0x65, (uint8_t[]){0x44}, 1, 0},
    {0x66, (uint8_t[]){0x43}, 1, 0},
    {0x67, (uint8_t[]){0x44}, 1, 0},
    {0x68, (uint8_t[]){0x62}, 1, 0},
    {0x69, (uint8_t[]){0x52}, 1, 0},
    {0x6A, (uint8_t[]){0x59}, 1, 0},
    {0x6B, (uint8_t[]){0x4C}, 1, 0},
    {0x6C, (uint8_t[]){0x48}, 1, 0},
    {0x6D, (uint8_t[]){0x3A}, 1, 0},
    {0x6E, (uint8_t[]){0x26}, 1, 0},
    {0x6F, (uint8_t[]){0x00}, 1, 0},

    /* Negative Gamma */
    {0x70, (uint8_t[]){0x7F}, 1, 0},
    {0x71, (uint8_t[]){0x6A}, 1, 0},
    {0x72, (uint8_t[]){0x5B}, 1, 0},
    {0x73, (uint8_t[]){0x4F}, 1, 0},
    {0x74, (uint8_t[]){0x4A}, 1, 0},
    {0x75, (uint8_t[]){0x3D}, 1, 0},
    {0x76, (uint8_t[]){0x41}, 1, 0},
    {0x77, (uint8_t[]){0x2A}, 1, 0},
    {0x78, (uint8_t[]){0x44}, 1, 0},
    {0x79, (uint8_t[]){0x43}, 1, 0},
    {0x7A, (uint8_t[]){0x44}, 1, 0},
    {0x7B, (uint8_t[]){0x62}, 1, 0},
    {0x7C, (uint8_t[]){0x52}, 1, 0},
    {0x7D, (uint8_t[]){0x59}, 1, 0},
    {0x7E, (uint8_t[]){0x4C}, 1, 0},
    {0x7F, (uint8_t[]){0x48}, 1, 0},
    {0x80, (uint8_t[]){0x3A}, 1, 0},
    {0x81, (uint8_t[]){0x26}, 1, 0},
    {0x82, (uint8_t[]){0x00}, 1, 0},

    /* ===== Page 2: GIP (Gate-In-Panel) mapping ========================== */
    {0xE0, (uint8_t[]){0x02}, 1, 0},

    /* Forward GIP channel mapping */
    {0x00, (uint8_t[]){0x42}, 1, 0},
    {0x01, (uint8_t[]){0x42}, 1, 0},
    {0x02, (uint8_t[]){0x40}, 1, 0},
    {0x03, (uint8_t[]){0x40}, 1, 0},
    {0x04, (uint8_t[]){0x5E}, 1, 0},
    {0x05, (uint8_t[]){0x5E}, 1, 0},
    {0x06, (uint8_t[]){0x5F}, 1, 0},
    {0x07, (uint8_t[]){0x5F}, 1, 0},
    {0x08, (uint8_t[]){0x5F}, 1, 0},
    {0x09, (uint8_t[]){0x57}, 1, 0},
    {0x0A, (uint8_t[]){0x57}, 1, 0},
    {0x0B, (uint8_t[]){0x77}, 1, 0},
    {0x0C, (uint8_t[]){0x77}, 1, 0},
    {0x0D, (uint8_t[]){0x47}, 1, 0},
    {0x0E, (uint8_t[]){0x47}, 1, 0},
    {0x0F, (uint8_t[]){0x45}, 1, 0},
    {0x10, (uint8_t[]){0x45}, 1, 0},
    {0x11, (uint8_t[]){0x4B}, 1, 0},
    {0x12, (uint8_t[]){0x4B}, 1, 0},
    {0x13, (uint8_t[]){0x49}, 1, 0},
    {0x14, (uint8_t[]){0x49}, 1, 0},
    {0x15, (uint8_t[]){0x5F}, 1, 0},

    /* Reverse GIP channel mapping */
    {0x16, (uint8_t[]){0x41}, 1, 0},
    {0x17, (uint8_t[]){0x41}, 1, 0},
    {0x18, (uint8_t[]){0x40}, 1, 0},
    {0x19, (uint8_t[]){0x40}, 1, 0},
    {0x1A, (uint8_t[]){0x5E}, 1, 0},
    {0x1B, (uint8_t[]){0x5E}, 1, 0},
    {0x1C, (uint8_t[]){0x5F}, 1, 0},
    {0x1D, (uint8_t[]){0x5F}, 1, 0},
    {0x1E, (uint8_t[]){0x5F}, 1, 0},
    {0x1F, (uint8_t[]){0x57}, 1, 0},
    {0x20, (uint8_t[]){0x57}, 1, 0},
    {0x21, (uint8_t[]){0x77}, 1, 0},
    {0x22, (uint8_t[]){0x77}, 1, 0},
    {0x23, (uint8_t[]){0x46}, 1, 0},
    {0x24, (uint8_t[]){0x46}, 1, 0},
    {0x25, (uint8_t[]){0x44}, 1, 0},
    {0x26, (uint8_t[]){0x44}, 1, 0},
    {0x27, (uint8_t[]){0x4A}, 1, 0},
    {0x28, (uint8_t[]){0x4A}, 1, 0},
    {0x29, (uint8_t[]){0x48}, 1, 0},
    {0x2A, (uint8_t[]){0x48}, 1, 0},
    {0x2B, (uint8_t[]){0x5F}, 1, 0},

    /* Forward GIP timing */
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x2D, (uint8_t[]){0x01}, 1, 0},
    {0x2E, (uint8_t[]){0x00}, 1, 0},
    {0x2F, (uint8_t[]){0x00}, 1, 0},
    {0x30, (uint8_t[]){0x1F}, 1, 0},
    {0x31, (uint8_t[]){0x1F}, 1, 0},
    {0x32, (uint8_t[]){0x1E}, 1, 0},
    {0x33, (uint8_t[]){0x1E}, 1, 0},
    {0x34, (uint8_t[]){0x1F}, 1, 0},
    {0x35, (uint8_t[]){0x17}, 1, 0},
    {0x36, (uint8_t[]){0x17}, 1, 0},
    {0x37, (uint8_t[]){0x37}, 1, 0},
    {0x38, (uint8_t[]){0x37}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x08}, 1, 0},
    {0x3B, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x0A}, 1, 0},
    {0x3D, (uint8_t[]){0x04}, 1, 0},
    {0x3E, (uint8_t[]){0x04}, 1, 0},
    {0x3F, (uint8_t[]){0x06}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},

    /* Reverse GIP timing */
    {0x42, (uint8_t[]){0x02}, 1, 0},
    {0x43, (uint8_t[]){0x02}, 1, 0},
    {0x44, (uint8_t[]){0x00}, 1, 0},
    {0x45, (uint8_t[]){0x00}, 1, 0},
    {0x46, (uint8_t[]){0x1F}, 1, 0},
    {0x47, (uint8_t[]){0x1F}, 1, 0},
    {0x48, (uint8_t[]){0x1E}, 1, 0},
    {0x49, (uint8_t[]){0x1E}, 1, 0},
    {0x4A, (uint8_t[]){0x1F}, 1, 0},
    {0x4B, (uint8_t[]){0x17}, 1, 0},
    {0x4C, (uint8_t[]){0x17}, 1, 0},
    {0x4D, (uint8_t[]){0x37}, 1, 0},
    {0x4E, (uint8_t[]){0x37}, 1, 0},
    {0x4F, (uint8_t[]){0x09}, 1, 0},
    {0x50, (uint8_t[]){0x09}, 1, 0},
    {0x51, (uint8_t[]){0x0B}, 1, 0},
    {0x52, (uint8_t[]){0x0B}, 1, 0},
    {0x53, (uint8_t[]){0x05}, 1, 0},
    {0x54, (uint8_t[]){0x05}, 1, 0},
    {0x55, (uint8_t[]){0x07}, 1, 0},
    {0x56, (uint8_t[]){0x07}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},

    /* GIP control registers */
    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0},
    {0x5C, (uint8_t[]){0x00}, 1, 0},
    {0x5D, (uint8_t[]){0x34}, 1, 0},
    {0x5E, (uint8_t[]){0x05}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x63, (uint8_t[]){0x00}, 1, 0},
    {0x64, (uint8_t[]){0x6A}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x07}, 1, 0},
    {0x69, (uint8_t[]){0x08}, 1, 0},
    {0x6A, (uint8_t[]){0x6A}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},
    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x00}, 1, 0},
    {0x6E, (uint8_t[]){0x00}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0},
    {0x75, (uint8_t[]){0xFF}, 1, 0},
    {0x77, (uint8_t[]){0xDD}, 1, 0},
    {0x78, (uint8_t[]){0x2C}, 1, 0},
    {0x79, (uint8_t[]){0x15}, 1, 0},
    {0x7A, (uint8_t[]){0x17}, 1, 0},
    {0x7D, (uint8_t[]){0x14}, 1, 0},
    {0x7E, (uint8_t[]){0x82}, 1, 0},

    /* ===== Page 4: Additional panel settings ============================ */
    {0xE0, (uint8_t[]){0x04}, 1, 0},
    {0x00, (uint8_t[]){0x0E}, 1, 0},
    {0x02, (uint8_t[]){0xB3}, 1, 0},
    {0x09, (uint8_t[]){0x61}, 1, 0},
    {0x0E, (uint8_t[]){0x48}, 1, 0},
    {0x37, (uint8_t[]){0x58}, 1, 0},
    {0x2B, (uint8_t[]){0x0F}, 1, 0},

    /* ===== Back to Page 0: Final commands =============================== */
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE6, (uint8_t[]){0x02}, 1, 0},
    {0xE7, (uint8_t[]){0x0C}, 1, 0},

    /* Sleep Out + Display On */
    {0x11, (uint8_t[]){0x00}, 1, 120},  /* SLPOUT - wait 120ms */
    {0x29, (uint8_t[]){0x00}, 1, 20},   /* DISPON - wait 20ms  */
};


/*******************************************************************************
 * I2C Bus (Display-intern: Backlight + Touch)
 ******************************************************************************/
static esp_err_t init_i2c(void)
{
    ESP_LOGI(TAG, "Initializing display I2C bus (SDA=GPIO%d, SCL=GPIO%d, %d Hz)",
             I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLK_SPEED_HZ);

    i2c_master_bus_config_t cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = I2C_PORT_NUM,
        .sda_io_num        = I2C_SDA_PIN,
        .scl_io_num        = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}


/*******************************************************************************
 * Backlight
 ******************************************************************************/
static esp_err_t init_backlight(void)
{
    ESP_LOGI(TAG, "Initializing backlight (I2C addr=0x%02X)", BL_I2C_ADDR);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BL_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_bl_dev),
                        TAG, "Failed to add backlight I2C device");

    uint8_t cmd1[] = {0x95, 0x11};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_bl_dev, cmd1, sizeof(cmd1), 50),
                        TAG, "Backlight enable step 1 failed");
    uint8_t cmd2[] = {0x95, 0x17};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_bl_dev, cmd2, sizeof(cmd2), 50),
                        TAG, "Backlight enable step 2 failed");
    uint8_t cmd3[] = {0x96, 0x00};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_bl_dev, cmd3, sizeof(cmd3), 50),
                        TAG, "Backlight off failed");

    vTaskDelay(pdMS_TO_TICKS(100));
    display_set_brightness(DISPLAY_INIT_BRIGHTNESS);
    //vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Backlight initialized");
    return ESP_OK;
}


esp_err_t display_set_brightness(uint8_t val)
{
    uint8_t cmd[] = {BL_BRIGHTNESS_REG, val};
    return i2c_master_transmit(s_bl_dev, cmd, sizeof(cmd), 50/*ms*/);
}


/*******************************************************************************
 * MIPI DSI Display (JD9365)
 ******************************************************************************/
static esp_err_t init_display_panel(void)
{
    ESP_LOGI(TAG, "Powering MIPI DSI PHY (LDO ch%d, %d mV)",
             DSI_PHY_LDO_CHAN, DSI_PHY_LDO_MV);

    esp_ldo_channel_handle_t phy_pwr = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id     = DSI_PHY_LDO_CHAN,
        .voltage_mv  = DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr),
                        TAG, "Failed to acquire LDO channel");

    ESP_LOGI(TAG, "Creating MIPI DSI bus (2 lanes, 1500 Mbps/lane)");
    esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                        TAG, "Failed to create DSI bus");

    ESP_LOGI(TAG, "Creating DBI command interface");
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &dbi_io),
                        TAG, "Failed to create DBI panel IO");

    ESP_LOGI(TAG, "Creating JD9365 panel (%dx%d)", PHYS_H_RES, PHYS_V_RES);

    esp_lcd_dpi_panel_config_t dpi_cfg =
        JD9365_800_1280_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    dpi_cfg.num_fbs = 2;
    dpi_cfg.video_timing.vsync_back_porch = 10;

    jd9365_vendor_config_t vendor_cfg = {
        .init_cmds      = s_ws_init_cmds,
        .init_cmds_size = sizeof(s_ws_init_cmds) / sizeof(s_ws_init_cmds[0]),
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = 2,
        },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(dbi_io, &panel_cfg, &s_panel),
                        TAG, "Failed to create JD9365 panel");

    ESP_LOGI(TAG, "Initializing display (reset -> init -> display on)");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),            TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),             TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Panel display-on failed");

    ESP_LOGI(TAG, "Display panel initialized");
    return ESP_OK;
}


/*******************************************************************************
 * Touch Controller (GT911)
 ******************************************************************************/
static esp_err_t init_touch(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch (I2C addr=0x%02X)", TOUCH_I2C_ADDR);

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr            = TOUCH_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "Failed to create touch I2C panel IO");

    esp_lcd_touch_config_t touch_cfg = {
        .x_max        = PHYS_H_RES,
        .y_max        = PHYS_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt911(tp_io, &touch_cfg, &s_touch),
        TAG, "Failed to create GT911 touch driver");

    ESP_LOGI(TAG, "Touch controller initialized");
    return ESP_OK;
}


/*******************************************************************************
 * LVGL Integration
 *
 * Landscape mode via software rotation:
 *   - Register display at physical size (800x1280)
 *   - sw_rotate=true, avoid_tearing=false (needed for sw rotation)
 *   - lv_disp_set_rotation(LV_DISP_ROT_270) for landscape (1280x800)
 ******************************************************************************/
static esp_err_t init_lvgl(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16384;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port init failed");

    ESP_LOGI(TAG, "Registering display (%dx%d physical, landscape via sw_rotate)",
             PHYS_H_RES, PHYS_V_RES);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle      = NULL,
        .panel_handle   = s_panel,
        .control_handle = NULL,
        .buffer_size    = PHYS_H_RES * 100,
        .double_buffer  = true,
        .hres           = PHYS_H_RES,
        .vres           = PHYS_V_RES,
        .monochrome     = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,
            .sw_rotate   = true,
        },
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,  /* Must be false for sw_rotate */
        },
    };

    s_lvgl_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(s_lvgl_display, ESP_FAIL, TAG, "Failed to add display to LVGL");

    lvgl_port_lock(0);
    lv_disp_set_rotation(s_lvgl_display, LV_DISP_ROTATION_270);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Display set to landscape rotated 270 (1280x800)");

    ESP_LOGI(TAG, "Registering touch input");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lvgl_display,
        .handle = s_touch,
    };
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (touch_indev == NULL) {
        ESP_LOGW(TAG, "Failed to add touch input (continuing without touch)");
    }

    ESP_LOGI(TAG, "LVGL initialized");
    return ESP_OK;
}


/*******************************************************************************
 * Public API
 ******************************************************************************/

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "===== Display subsystem init start =====");

    ESP_RETURN_ON_ERROR(init_i2c(),           TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(init_backlight(),     TAG, "Backlight init failed");
    ESP_RETURN_ON_ERROR(init_display_panel(), TAG, "Display panel init failed");
    ESP_RETURN_ON_ERROR(init_touch(),         TAG, "Touch init failed");
    ESP_RETURN_ON_ERROR(init_lvgl(),          TAG, "LVGL init failed");

    ESP_LOGI(TAG, "===== Display subsystem ready =====");
    return ESP_OK;
}

lv_disp_t *display_get(void)
{
    return s_lvgl_display;
}
