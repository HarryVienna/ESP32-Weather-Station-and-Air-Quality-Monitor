/**
 * @file i2c_slave.c
 * @brief I2C Slave Interface — the ESP32-S3 responds to read requests from the ESP32-P4 master
 *
 * =============================================================================
 * ARCHITECTURE OVERVIEW
 * =============================================================================
 *
 * The fundamental problem: the I2C master (P4) can send or request data at
 * any time - in the middle of other code. The ESP32-S3 uses hardware
 * interrupts (ISR = Interrupt Service Routine) for this.
 *
 * ISR callbacks have strict restrictions though:
 *   - No blocking functions (no vTaskDelay, no mutex lock)
 *   - No ESP_LOG* (uses a mutex internally -> deadlock risk)
 *   - As short as possible (microseconds, not milliseconds)
 *
 * Solution: ISR -> Queue -> Task
 *
 *   [I2C Hardware]
 *        │
 *        ├─ on_receive (ISR): Master has sent bytes
 *        │    -> Copy bytes into context, send EVT_RX to the queue
 *        │
 *        └─ on_request (ISR): Master wants to read bytes
 *             -> Send EVT_TX to the queue
 *
 *   [FreeRTOS Queue]  <- xQueueSendFromISR() is ISR-safe and non-blocking
 *        │
 *        ▼
 *   [i2c_slave_task]  <- normal FreeRTOS task, allowed to do anything
 *        │
 *        ├─ EVT_RX: evaluate register
 *        │    ├─ Write register (0x10, 0x11, 0x23): execute action immediately
 *        │    └─ Read register  (0x00, 0x01, 0x24, 0x28): load data into ring buffer
 *        │
 *        └─ EVT_TX: nothing to do (data already loaded on EVT_RX)
 *
 * =============================================================================
 * WHY LOAD DATA ON EVT_RX, NOT ON EVT_TX?
 * =============================================================================
 *
 * The ESP-IDF I2C Slave Driver v2 has no clock stretching. The master
 * starts reading the hardware FIFO immediately once it starts the READ
 * transaction.
 *
 * Bypassing the FIFO directly via low-level registers (i2c_ll_write_txfifo)
 * leads to ESP_ERR_INVALID_STATE and a stuck bus (SDA=0, SCL=0).
 *
 * Solution: the task loads the data into the slave driver's software ring
 * buffer on EVT_RX (after the master's WRITE). The driver automatically
 * pushes it into the hardware FIFO once the master starts the READ
 * transaction.
 *
 * The master must use two SEPARATE transactions with a sufficient pause in
 * between:
 *
 *   Master: WRITE 0x00, STOP          <- on_receive fires -> task fills ring buffer
 *   (pause, ~50ms)                    <- data is already in the buffer
 *   Master: READ, STOP                <- driver pushes it into the FIFO automatically
 *
 * With a repeated start (WRITE -> no STOP -> READ), the master would read
 * before the task has filled the ring buffer -> master gets 0xFF.
 *
 * =============================================================================
 * PROTOCOL (register map)
 * =============================================================================
 *
 *   0x00  READ        -> Number of available packets (1 byte)
 *   0x01  READ        -> Read + remove next packet from the stack (15-79 bytes)
 *   0x10  WRITE 4B    -> Set UTC Unix timestamp (uint32_t little-endian)
 *   0x11  WRITE nB    -> Set POSIX timezone string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3")
 *   0x23  WRITE 0x01  -> Reset drop counter
 *   0x24  READ        -> Statistics: total packets received (uint32_t little-endian)
 *   0x28  READ        -> Statistics: packets overwritten   (uint32_t little-endian)
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_slave.h"

#include "sensor_stack.h"
#include "i2c_slave.h"
#include "ota/ota_task.h"

static const char* TAG = "I2C_SLAVE";

/* ==========================================================================
 * Internal state
 * ========================================================================== */

/* Event types sent from the ISR callbacks to the task */
typedef enum {
    I2C_SLAVE_EVT_TX = 0,   /* Master wants to read data (on_request fired) */
    I2C_SLAVE_EVT_RX,       /* Master has written data (on_receive fired) */
} i2c_slave_event_t;

/*
 * Internal state of the I2C slave.
 * Used by both the ISR callbacks and the task.
 * The callbacks only write current_reg/rx_data (from ISR context, atomic
 * writes), the task reads these values and writes write_data/write_len.
 */
typedef struct {
    QueueHandle_t event_queue;               /* ISR -> task communication */
    i2c_slave_dev_handle_t handle;           /* ESP-IDF handle for the slave driver */
    uint8_t current_reg;                     /* Register address last sent by the master */
    uint8_t rx_data[64];                     /* Write data bytes from the master (after the register address) */
    uint8_t rx_data_len;                     /* Number of received write bytes (excluding the register address) */
    uint8_t write_data[I2C_MAX_PACKET_SIZE]; /* Buffer with the data to be sent to the master */
    uint32_t write_len;                      /* Number of valid bytes in write_data */
} i2c_slave_context_t;

static i2c_slave_context_t g_i2c_ctx = {0};

/* ==========================================================================
 * ISR callbacks — as short as possible, no ESP_LOG, no blocking
 * ========================================================================== */

/*
 * Called when the master starts a READ transaction (slave should provide data).
 *
 * We only signal the task via the queue. The actual data has already been
 * prepared on the preceding RX event (FIFO is already filled).
 *
 * Return value: whether a higher-priority task was woken (for the FreeRTOS scheduler).
 */
static bool i2c_slave_request_cb(i2c_slave_dev_handle_t i2c_slave,
                                  const i2c_slave_request_event_data_t *evt_data,
                                  void *arg)
{
    i2c_slave_context_t *context = (i2c_slave_context_t *)arg;
    i2c_slave_event_t evt = I2C_SLAVE_EVT_TX;
    BaseType_t xTaskWoken = pdFALSE;

    xQueueSendFromISR(context->event_queue, &evt, &xTaskWoken);

    return xTaskWoken;
}

/*
 * Called when the master has written data (WRITE transaction completed).
 *
 * Byte 0 is always the register address (the "command byte").
 * Optional further bytes are write data (e.g. timestamp for SET_TIME).
 *
 * We copy everything into the context and signal the task. The task then
 * runs the actual logic (action for write registers, FIFO fill for read
 * registers).
 */
static bool i2c_slave_receive_cb(i2c_slave_dev_handle_t i2c_slave,
                                  const i2c_slave_rx_done_event_data_t *evt_data,
                                  void *arg)
{
    i2c_slave_context_t *context = (i2c_slave_context_t *)arg;

    if (evt_data->buffer != NULL && evt_data->length > 0) {
        context->current_reg = evt_data->buffer[0];
        context->rx_data_len = 0;

        /* Save optional write data (bytes after the register address) */
        if (evt_data->length > 1) {
            uint8_t data_len = (uint8_t)(evt_data->length - 1);
            if (data_len > sizeof(context->rx_data))
                data_len = sizeof(context->rx_data);
            memcpy(context->rx_data, &evt_data->buffer[1], data_len);
            context->rx_data_len = data_len;
        }
    }

    i2c_slave_event_t evt = I2C_SLAVE_EVT_RX;
    BaseType_t xTaskWoken = pdFALSE;
    xQueueSendFromISR(context->event_queue, &evt, &xTaskWoken);

    return xTaskWoken;
}

/* ==========================================================================
 * Prepare response data for read registers
 * ========================================================================== */

/*
 * Prepares the response data for the current read register.
 * Writes the result into context->write_data / write_len.
 * Called in task context (not ISR), so it may use anything.
 *
 * IMPORTANT: the data is NOT sent immediately, but only once the master
 * starts a READ transaction (EVT_TX). It's then loaded into the ring
 * buffer via i2c_slave_write().
 */
static void i2c_slave_prepare_register_data(void)
{
    i2c_slave_context_t *context = &g_i2c_ctx;

    switch (context->current_reg) {

        case I2C_REG_COUNT:
            context->write_data[0] = (uint8_t)sensor_stack_count();
            context->write_len = 1;
            ESP_LOGD(TAG, "Reg COUNT -> %d packets", context->write_data[0]);
            break;

        case I2C_REG_PACKET_READ: {
            /*
             * Get the next packet from the stack and copy it into the send buffer.
             *
             * IMPORTANT: the master ALWAYS reads PACKET_MAX_SIZE (79 bytes).
             * We therefore always have to send 79 bytes - either a complete
             * packet (padded to 79 bytes) or 79 bytes of 0xFF as an "empty" signal.
             *
             * The padding is necessary because the master calls
             * i2c_master_receive(dev, buf, 79, ...) and would otherwise read
             * undefined bytes from the FIFO.
             */
            sensor_packet_t packet;
            if (sensor_stack_pop(&packet) == ESP_OK) {
                /* Copy the packet and fill the rest of the buffer with 0x00 */
                memset(context->write_data, 0x00, I2C_MAX_PACKET_SIZE);
                memcpy(context->write_data, &packet, sizeof(sensor_packet_t));
                context->write_len = I2C_MAX_PACKET_SIZE;
                ESP_LOGD(TAG, "Reg PACKET -> sensor=%d type=%d payload=%dB (padded to %dB)",
                         packet.header.sensor_nr, packet.header.sensor_type,
                         packet.header.payload_len, context->write_len);
            } else {
                /*
                 * No packet available - fill the whole buffer with 0xFF.
                 * The master recognizes this by payload_len=0xFF in the header (byte 3).
                 */
                memset(context->write_data, 0xFF, I2C_MAX_PACKET_SIZE);
                context->write_len = I2C_MAX_PACKET_SIZE;
                ESP_LOGD(TAG, "Reg PACKET -> stack empty (79x 0xFF)");
            }
            break;
        }

        case I2C_REG_STATS_RECV: {
            uint32_t received, overwritten;
            sensor_stack_stats(&received, &overwritten);
            /* uint32_t as little-endian (LSB first) */
            context->write_data[0] = (uint8_t)(received & 0xFF);
            context->write_data[1] = (uint8_t)((received >> 8)  & 0xFF);
            context->write_data[2] = (uint8_t)((received >> 16) & 0xFF);
            context->write_data[3] = (uint8_t)((received >> 24) & 0xFF);
            context->write_len = 4;
            ESP_LOGD(TAG, "Reg STATS_RECV -> %lu", received);
            break;
        }

        case I2C_REG_STATS_OVERWR: {
            uint32_t received, overwritten;
            sensor_stack_stats(&received, &overwritten);
            context->write_data[0] = (uint8_t)(overwritten & 0xFF);
            context->write_data[1] = (uint8_t)((overwritten >> 8)  & 0xFF);
            context->write_data[2] = (uint8_t)((overwritten >> 16) & 0xFF);
            context->write_data[3] = (uint8_t)((overwritten >> 24) & 0xFF);
            context->write_len = 4;
            ESP_LOGD(TAG, "Reg STATS_OVERWR -> %lu", overwritten);
            break;
        }

        /* Write-only registers: handled in the task RX handler, never here */
        case I2C_REG_SET_TIME:
        case I2C_REG_SET_TZ:
        case I2C_REG_RESET_DROP:
        case I2C_REG_SET_WIFI_SSID:
        case I2C_REG_SET_WIFI_PASS:
        case I2C_REG_OTA_START:
            context->write_len = 0;
            ESP_LOGW(TAG, "Reg 0x%02X is write-only, read not possible", context->current_reg);
            break;

        default:
            context->write_len = 0;
            ESP_LOGE(TAG, "Unknown register: 0x%02X", context->current_reg);
            break;
    }
}

/* ==========================================================================
 * Task — processes events from the queue
 * ========================================================================== */

static void i2c_slave_task(void *arg)
{
    i2c_slave_context_t *context = (i2c_slave_context_t *)arg;

    ESP_LOGI(TAG, "I2C slave started (addr=0x%02X, SDA=GPIO%d, SCL=GPIO%d)",
             I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL);

    while (true) {
        i2c_slave_event_t evt;

        /* Blocks until an event arrives - no timeout needed. */
        xQueueReceive(context->event_queue, &evt, portMAX_DELAY);

        if (evt == I2C_SLAVE_EVT_RX) {
            /*
             * Master has completed a WRITE transaction.
             * context->current_reg holds the register address.
             * context->rx_data / rx_data_len hold optional write bytes.
             *
             * Write register: run the action immediately.
             * Read register:  only prepare the data (copy into write_data),
             *                 but do NOT send it yet.
             */

            if (context->current_reg == I2C_REG_SET_TIME) {
                /* Set UTC Unix timestamp (4 bytes little-endian) */
                if (context->rx_data_len >= 4) {
                    uint32_t ts = (uint32_t)context->rx_data[0]
                                | ((uint32_t)context->rx_data[1] << 8)
                                | ((uint32_t)context->rx_data[2] << 16)
                                | ((uint32_t)context->rx_data[3] << 24);
                    struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
                    settimeofday(&tv, NULL);
                    ESP_LOGI(TAG, "System time set: %lu (UTC)", (unsigned long)ts);
                } else {
                    ESP_LOGE(TAG, "SET_TIME: expected 4 bytes, got %d", context->rx_data_len);
                }

            } else if (context->current_reg == I2C_REG_SET_TZ) {
                /* Set POSIX timezone string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" */
                if (context->rx_data_len >= 1) {
                    /* Ensure NUL termination */
                    uint8_t idx = context->rx_data_len < sizeof(context->rx_data)
                                  ? context->rx_data_len
                                  : sizeof(context->rx_data) - 1;
                    context->rx_data[idx] = '\0';
                    setenv("TZ", (const char *)context->rx_data, 1);
                    tzset();
                    ESP_LOGI(TAG, "Timezone set: '%s'", context->rx_data);
                } else {
                    ESP_LOGE(TAG, "SET_TZ: no data received");
                }

            } else if (context->current_reg == I2C_REG_RESET_DROP) {
                /* Reset drop counter (master must send 0x01 as the data byte) */
                if (context->rx_data_len >= 1 && context->rx_data[0] == 0x01) {
                    sensor_stack_reset_dropped();
                    ESP_LOGD(TAG, "Drop counter reset");
                } else {
                    ESP_LOGE(TAG, "RESET_DROP: expected 0x01, got 0x%02X",
                             context->rx_data_len > 0 ? context->rx_data[0] : 0x00);
                }
                /* Important: set write_len to 0 so it isn't treated as a read register */
                context->write_len = 0;

            } else if (context->current_reg == I2C_REG_SET_WIFI_SSID) {
                /* SSID for the next receiver OTA attempt (RAM only, see ota_task.h) */
                if (context->rx_data_len >= 1) {
                    uint8_t idx = context->rx_data_len < sizeof(context->rx_data)
                                  ? context->rx_data_len
                                  : sizeof(context->rx_data) - 1;
                    context->rx_data[idx] = '\0';
                    ota_task_set_wifi_ssid((const char *)context->rx_data);
                    ESP_LOGI(TAG, "OTA WiFi SSID set: '%s'", context->rx_data);
                } else {
                    ESP_LOGE(TAG, "SET_WIFI_SSID: no data received");
                }
                context->write_len = 0;

            } else if (context->current_reg == I2C_REG_SET_WIFI_PASS) {
                /* Password for the next receiver OTA attempt (RAM only, never logged) */
                if (context->rx_data_len >= 1) {
                    uint8_t idx = context->rx_data_len < sizeof(context->rx_data)
                                  ? context->rx_data_len
                                  : sizeof(context->rx_data) - 1;
                    context->rx_data[idx] = '\0';
                    ota_task_set_wifi_password((const char *)context->rx_data);
                    ESP_LOGI(TAG, "OTA WiFi password set (%d characters)", idx);
                } else {
                    ESP_LOGE(TAG, "SET_WIFI_PASS: no data received");
                }
                context->write_len = 0;

            } else if (context->current_reg == I2C_REG_OTA_START) {
                /* Starts the receiver firmware update task with the most
                 * recently set SSID/password - see ota_task_trigger(). */
                if (context->rx_data_len >= 1 && context->rx_data[0] == 0x01) {
                    ota_task_trigger();
                } else {
                    ESP_LOGE(TAG, "OTA_START: expected 0x01, got 0x%02X",
                              context->rx_data_len > 0 ? context->rx_data[0] : 0x00);
                }
                context->write_len = 0;

            } else if (context->current_reg == I2C_REG_COUNT      ||
                       context->current_reg == I2C_REG_PACKET_READ ||
                       context->current_reg == I2C_REG_STATS_RECV  ||
                       context->current_reg == I2C_REG_STATS_OVERWR) {
                /*
                 * Read register: ONLY prepare the response data (copy into
                 * write_data). The actual sending happens on the EVT_TX
                 * event when the master starts a READ transaction.
                 */
                i2c_slave_prepare_register_data();
                ESP_LOGD(TAG, "RX: data for reg 0x%02X prepared (%lu bytes)",
                         context->current_reg, context->write_len);

            } else {
                ESP_LOGE(TAG, "Unknown register 0x%02X received", context->current_reg);
            }

        } else if (evt == I2C_SLAVE_EVT_TX) {
            /*
             * Master has started a READ transaction.
             * Data was already prepared into write_data[] on EVT_RX.
             * Now we load it into the slave driver.
             *
             * Same as the Espressif I2C slave example: a loop that ensures
             * ALL data gets sent, with a generous timeout.
             */
            if (context->write_len > 0) {
                uint8_t *data_buffer = context->write_data;
                uint32_t buffer_size = context->write_len;
                uint32_t total_written = 0;
                uint32_t written = 0;
                esp_err_t err;

                while (total_written < buffer_size) {
                    err = i2c_slave_write(
                        context->handle,
                        data_buffer + total_written,
                        buffer_size - total_written,
                        &written,
                        pdMS_TO_TICKS(1000)  /* 1000ms timeout, same as the Espressif example */
                    );

                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "TX: i2c_slave_write failed (reg 0x%02X): %s",
                                 context->current_reg, esp_err_to_name(err));
                        break;
                    }

                    if (written == 0) {
                        ESP_LOGW(TAG, "TX: no further bytes written (reg 0x%02X)",
                                 context->current_reg);
                        break;
                    }

                    total_written += written;
                }

                if (total_written == buffer_size) {
                    ESP_LOGD(TAG, "TX: %lu bytes sent for reg 0x%02X",
                             buffer_size, context->current_reg);
                }
            }
            /* write_len == 0 means write-only register - nothing to send */
        }
    }

    vTaskDelete(NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

esp_err_t i2c_slave_init(void)
{
    esp_err_t err;

    /* Configure the pins as inputs first, so any bus state stuck from a
     * previous session (SDA stuck low) gets released. The I2C driver
     * reconfigures the pins itself afterwards. */
    gpio_reset_pin(I2C_SLAVE_SDA);
    gpio_reset_pin(I2C_SLAVE_SCL);
    gpio_set_direction(I2C_SLAVE_SDA, GPIO_MODE_INPUT);
    gpio_set_direction(I2C_SLAVE_SCL, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Step 1: create the queue (16 slots of 1 byte are enough, events arrive sequentially) */
    g_i2c_ctx.event_queue = xQueueCreate(16, sizeof(i2c_slave_event_t));
    if (!g_i2c_ctx.event_queue) {
        ESP_LOGE(TAG, "Queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    /* Step 2: initialize the I2C slave hardware */
    i2c_slave_config_t i2c_slv_config = {
        .i2c_port         = I2C_SLAVE_PORT,
        .clk_source       = I2C_CLK_SRC_DEFAULT,
        .scl_io_num       = I2C_SLAVE_SCL,
        .sda_io_num       = I2C_SLAVE_SDA,
        .slave_addr       = I2C_SLAVE_ADDR,
        .send_buf_depth    = 256,   /* Software ring buffer for outgoing data */
        .receive_buf_depth = 256,   /* Software ring buffer for incoming data */
        .flags = {
            .enable_internal_pullup = 1,  /* Internal pull-ups (sufficient for short wires) */
        },
    };

    err = i2c_new_slave_device(&i2c_slv_config, &g_i2c_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_slave_device failed: %s", esp_err_to_name(err));
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return err;
    }

    /* Step 3: register ISR callbacks, pass the context pointer as user_data */
    i2c_slave_event_callbacks_t cbs = {
        .on_receive = i2c_slave_receive_cb,  /* Master has written */
        .on_request = i2c_slave_request_cb,  /* Master wants to read */
    };
    err = i2c_slave_register_event_callbacks(g_i2c_ctx.handle, &cbs, &g_i2c_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Callback registration failed: %s", esp_err_to_name(err));
        i2c_del_slave_device(g_i2c_ctx.handle);
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return err;
    }

    /* Step 4: start the task that processes the queue events */
    TaskHandle_t task_handle;
    err = xTaskCreate(i2c_slave_task, "i2c_slave", 4096, &g_i2c_ctx, 10, &task_handle);
    if (err != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed (err=%d)", err);
        i2c_del_slave_device(g_i2c_ctx.handle);
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2C slave ready (addr=0x%02X, SDA=GPIO%d, SCL=GPIO%d)",
             I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL);
    return ESP_OK;
}

esp_err_t i2c_slave_deinit(void)
{
    esp_err_t err = ESP_OK;

    if (g_i2c_ctx.handle != NULL) {
        err = i2c_del_slave_device(g_i2c_ctx.handle);
        g_i2c_ctx.handle = NULL;
    }

    if (g_i2c_ctx.event_queue != NULL) {
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
    }

    return err;
}
