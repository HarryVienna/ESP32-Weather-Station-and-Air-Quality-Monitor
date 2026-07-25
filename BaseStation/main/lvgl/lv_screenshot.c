/**
 * @file lv_screenshot.c
 *
 * Migrated to LVGL 9.5
 * --------------------
 * Major changes vs. the v8.4 original:
 *  - lv_img_dsc_t            -> lv_image_dsc_t
 *  - lv_scr_act()            -> lv_screen_active()
 *  - lv_img_buf_get_px_color() was REMOVED in v9. Pixels are now read directly
 *    from the snapshot buffer. We take the snapshot in a known format
 *    (LV_COLOR_FORMAT_RGB565, the natural format for a 16-bit ESP32 display)
 *    and convert RGB565 -> BMP24 by hand. This is also more correct than the
 *    old ch.red<<3 trick, which only worked for one specific color depth.
 *  - lv_snapshot_take_to_buf() now returns lv_result_t and takes a
 *    lv_color_format_t. Buffer size should be queried with
 *    lv_snapshot_buf_size_needed().
 *  - Row addressing uses snapshot.header.stride (bytes per line), because v9
 *    may pad rows for alignment; do NOT assume stride == width * 2.
 *
 * Requirements in lv_conf.h:
 *  - LV_USE_SNAPSHOT must be 1.
 *
 * NOTE: The matching declarations in lv_screenshot.h must change the
 *       lv_img_dsc_t parameter of send_screenshot_to_server() to
 *       lv_image_dsc_t as well.
 *
 * If your display runs at LV_COLOR_DEPTH 32, take the snapshot in
 * LV_COLOR_FORMAT_XRGB8888 / ARGB8888 instead and adapt the pixel loop.
 */

#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"

#include "lvgl.h"
#include "lv_screenshot.h"

#define WEBSERVER "http://192.168.0.100:8080/screenshot"  // Replace with your server URL

/* Color format used for the snapshot. RGB565 for a 16-bit display. */
#define SCREENSHOT_CF   LV_COLOR_FORMAT_RGB565

static const char* TAG = "screenhot";


/**
 * @brief Convert an LVGL RGB565 snapshot to 24-bit BMP and POST it to a server.
 *
 * @param snapshot Pointer to the lv_image_dsc_t produced by lv_snapshot_take_to_buf().
 * @param width    Width of the image in pixels.
 * @param height   Height of the image in pixels.
 *
 * @return ESP_OK on success, ESP_FAIL on allocation / HTTP error.
 */
esp_err_t send_screenshot_to_server(lv_image_dsc_t *snapshot, uint32_t width, uint32_t height) {

    // BMP header size calculation
    uint8_t bpp = 24; // 24-bit BMP output
    uint32_t header_size = sizeof(bitmap_fileheader_t) + sizeof(bitmap_infoheader_t);
    uint32_t image_size = width * height * (bpp / 8);
    uint32_t file_size = header_size + image_size;

    // Allocate memory for the BMP data
    uint8_t *bmp_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!bmp_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for BMP data");
        return ESP_FAIL;
    }

    bitmap_fileheader_t file_header;
    bitmap_infoheader_t info_header;

    file_header.file_type = 0x4D42; // 'BM'
    file_header.file_size = file_size;
    file_header.reserved1 = 0;
    file_header.reserved2 = 0;
    file_header.offset_data = sizeof(bitmap_fileheader_t) + sizeof(bitmap_infoheader_t);

    info_header.size = sizeof(bitmap_infoheader_t);
    info_header.width = width;
    info_header.height = height;
    info_header.planes = 1;
    info_header.bit_count = 24;
    info_header.compression = 0;
    info_header.size_image = image_size;
    info_header.x_pels_per_meter = 0;
    info_header.y_pels_per_meter = 0;
    info_header.clr_used = 0;
    info_header.clr_important = 0;

    // Copy the header structures to the BMP data
    memcpy(bmp_data, &file_header, sizeof(bitmap_fileheader_t));
    memcpy(bmp_data + sizeof(bitmap_fileheader_t), &info_header, sizeof(bitmap_infoheader_t));

    // Convert RGB565 snapshot data to BMP24 (BGR, bottom-up).
    // Use the descriptor's stride (bytes per row); v9 may pad rows.
    uint32_t stride = snapshot->header.stride;
    if (stride == 0) {
        stride = width * 2; // fallback: tightly packed RGB565
    }

    uint8_t *pixel_data = bmp_data + header_size;
    for (int y = (int)height - 1; y >= 0; y--) {

        const uint16_t *row = (const uint16_t *)(snapshot->data + (size_t)y * stride);

        for (uint32_t x = 0; x < width; x++) {
            uint16_t px = row[x];

            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5)  & 0x3F;
            uint8_t b5 =  px        & 0x1F;

            // 5/6-bit -> 8-bit with proper bit replication
            *pixel_data++ = (uint8_t)((b5 << 3) | (b5 >> 2)); // Blue
            *pixel_data++ = (uint8_t)((g6 << 2) | (g6 >> 4)); // Green
            *pixel_data++ = (uint8_t)((r5 << 3) | (r5 >> 2)); // Red
        }
    }

    // Set up HTTP request
    esp_http_client_config_t config = {
        .url = WEBSERVER,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set the headers and body
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)bmp_data, file_size);

    // Perform the HTTP POST request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Screenshot uploaded successfully");
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Clean up
    esp_http_client_cleanup(client);
    heap_caps_free(bmp_data);
    return err;
}

/**
 * @brief Capture a screenshot of the active LVGL screen and send it to a server.
 *
 * @return ESP_OK on success, ESP_FAIL on allocation / capture failure.
 */
esp_err_t make_screenshot() {

    lv_obj_t * screen = lv_screen_active();

    uint32_t width  = lv_obj_get_width(screen);
    uint32_t height = lv_obj_get_height(screen);

    // lv_snapshot_take() allocates and sizes the draw buffer itself (replaces the
    // removed lv_snapshot_buf_size_needed() / deprecated lv_snapshot_take_to_buf()).
    // lv_draw_buf_t starts with the same header+data layout as lv_image_dsc_t.
    lv_draw_buf_t * snapshot = lv_snapshot_take(screen, SCREENSHOT_CF);
    if (snapshot == NULL) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Screenshots");
        return ESP_FAIL;
    }

    // Write screenshot to server
    send_screenshot_to_server((lv_image_dsc_t *)snapshot, width, height);

    lv_draw_buf_destroy(snapshot);

    return ESP_OK;
}

/**
 * @brief Periodically captures and sends screenshots as part of a FreeRTOS task.
 *
 * @param pvParameter Pointer to a screenshot_task_params_t structure.
 */
void screenshot_task(void *pvParameter){

    ESP_LOGI(TAG, "Start Screenshot task");

    screenshot_task_params_t *params = (screenshot_task_params_t *)pvParameter;

    vTaskDelay(pdMS_TO_TICKS(1000 * params->initial_delay));

    for (;;) {
        make_screenshot();

        vTaskDelay(pdMS_TO_TICKS(1000 * params->task_delay));
    }
}

/**
 * @brief Initialize and start a task for periodic screenshot capturing and uploading.
 *
 * @param initial_delay Delay in seconds before the first screenshot is taken.
 * @param task_delay    Delay in seconds between each subsequent screenshot capture.
 */
void start_screenshot(uint32_t initial_delay, uint32_t task_delay) {

    // Allocate memory for the parameters struct
    screenshot_task_params_t *params = malloc(sizeof(screenshot_task_params_t));

    // Assign the parameter values
    params->initial_delay = initial_delay;
    params->task_delay = task_delay;

    xTaskCreatePinnedToCore(
        screenshot_task,   // Task function
        "Screeshot Task",  // Task name
        8000,              // Stack size (bytes)
        params,            // Task input parameter
        16,                // Task priority
        NULL,              // Task handle
        0                  // Core to run the task on (0 or 1)
    );
}
