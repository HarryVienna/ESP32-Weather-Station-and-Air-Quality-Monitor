/**
 * @file i2c_slave.c
 * @brief I2C Slave Interface — ESP32-S3 antwortet auf Leseanfragen des ESP32-P4 Masters
 *
 * =============================================================================
 * ARCHITEKTUR-ÜBERBLICK
 * =============================================================================
 *
 * Das grundlegende Problem: Der I2C-Master (P4) kann jederzeit Daten schicken
 * oder anfordern — mitten in anderem Code. Dafür verwendet der ESP32-S3 Hardware-
 * Interrupts (ISR = Interrupt Service Routine).
 *
 * ISR-Callbacks haben aber strikte Einschränkungen:
 *   - Keine blockierenden Funktionen (kein vTaskDelay, kein Mutex-Lock)
 *   - Kein ESP_LOG* (verwendet intern einen Mutex → Deadlock-Risiko)
 *   - So kurz wie möglich (Mikrosekunden, nicht Millisekunden)
 *
 * Lösung: ISR → Queue → Task
 *
 *   [I2C Hardware]
 *        │
 *        ├─ on_receive (ISR): Master hat Bytes geschickt
 *        │    → Bytes in Context kopieren, EVT_RX in Queue senden
 *        │
 *        └─ on_request (ISR): Master will Bytes lesen
 *             → EVT_TX in Queue senden
 *
 *   [FreeRTOS Queue]  ← xQueueSendFromISR() ist ISR-sicher und nicht-blockierend
 *        │
 *        ▼
 *   [i2c_slave_task]  ← normaler FreeRTOS Task, darf alles
 *        │
 *        ├─ EVT_RX: Register auswerten
 *        │    ├─ Write-Register (0x10, 0x11, 0x23): Aktion sofort ausführen
 *        │    └─ Read-Register  (0x00, 0x01, 0x24, 0x28): TX-FIFO befüllen
 *        │
 *        └─ EVT_TX: kein Handlungsbedarf (Daten bereits im FIFO)
 *
 * =============================================================================
 * WARUM TX-FIFO BEIM RX-EVENT BEFÜLLEN, NICHT BEIM TX-EVENT?
 * =============================================================================
 *
 * ESP-IDF I2C Slave Driver v2 hat kein Clock Stretching. Ohne Clock Stretching
 * kann der Slave den Master nicht warten lassen — der Master beginnt sofort mit
 * dem Auslesen des Hardware-FIFOs, sobald er die Leseanfrage gesendet hat.
 *
 * Wenn man erst beim TX-Event den FIFO befüllt, ist es zu spät: der FreeRTOS-Task
 * hat keine Zeit mehr zu reagieren bevor der Master bereits liest.
 *
 * Deshalb muss der Master zwei GETRENNTE Transaktionen verwenden (kein Repeated-Start):
 *
 *   Master: WRITE 0x00, STOP          ← on_receive feuert → Task befüllt FIFO
 *   (kurze Pause, ~5ms)               ← Task hat Zeit zu laufen
 *   Master: READ, STOP                ← Daten bereits im FIFO → korrekte Antwort
 *
 * Mit Repeated-Start (WRITE → ohne STOP → READ) würde der Master lesen bevor
 * der Task den FIFO befüllen konnte → Master bekommt 0xFF.
 *
 * =============================================================================
 * PROTOKOLL (Register Map)
 * =============================================================================
 *
 *   0x00  READ        → Anzahl verfügbarer Pakete (1 Byte)
 *   0x01  READ        → Nächstes Paket vom Stack lesen + entfernen (15-79 Byte)
 *   0x10  WRITE 4B    → UTC Unix-Timestamp setzen (uint32_t Little-Endian)
 *   0x11  WRITE nB    → POSIX Timezone-String setzen (z.B. "CET-1CEST,M3.5.0,M10.5.0/3")
 *   0x23  WRITE 0x01  → Drop-Counter zurücksetzen
 *   0x24  READ        → Statistik: total empfangene Pakete (uint32_t Little-Endian)
 *   0x28  READ        → Statistik: überschriebene Pakete   (uint32_t Little-Endian)
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c_slave.h"

#include "sensor_stack.h"
#include "i2c_slave.h"

static const char* TAG = "I2C_SLAVE";

/* ==========================================================================
 * Interner State
 * ========================================================================== */

/* Event-Typen die von den ISR-Callbacks an den Task gesendet werden */
typedef enum {
    I2C_SLAVE_EVT_TX = 0,   /* Master will Daten lesen (on_request gefeuert) */
    I2C_SLAVE_EVT_RX,       /* Master hat Daten geschrieben (on_receive gefeuert) */
} i2c_slave_event_t;

/*
 * Interner Zustand des I2C Slaves.
 * Wird sowohl von den ISR-Callbacks als auch vom Task verwendet.
 * Die Callbacks schreiben nur current_reg/rx_data (aus ISR-Kontext, atomare Writes),
 * der Task liest diese Werte und schreibt write_data/write_len/has_pending_data.
 */
typedef struct {
    QueueHandle_t event_queue;               /* ISR → Task Kommunikation */
    i2c_slave_dev_handle_t handle;           /* ESP-IDF Handle für den Slave-Treiber */
    uint8_t current_reg;                     /* Zuletzt vom Master gesendete Register-Adresse */
    uint8_t rx_data[64];                     /* Write-Datenbytes vom Master (nach der Registeradresse) */
    uint8_t rx_data_len;                     /* Anzahl empfangener Write-Bytes (ohne Registeradresse) */
    uint8_t write_data[I2C_MAX_PACKET_SIZE]; /* Puffer mit den Daten die zum Master gesendet werden */
    uint32_t write_len;                      /* Anzahl gültiger Bytes in write_data */
    bool has_pending_data;                   /* true = write_data enthält gültige Daten */
} i2c_slave_context_t;

static i2c_slave_context_t g_i2c_ctx = {0};

/* ==========================================================================
 * ISR Callbacks — so kurz wie möglich, kein ESP_LOG, kein Blocking
 * ========================================================================== */

/*
 * Wird aufgerufen wenn der Master eine READ-Transaktion startet (Slave soll Daten liefern).
 *
 * Wir signalisieren nur den Task über die Queue. Die eigentliche Datenbereitstellung
 * hat bereits beim vorherigen RX-Event stattgefunden (FIFO ist schon befüllt).
 *
 * Rückgabewert: ob ein höher-priorisierter Task aufgeweckt wurde (für FreeRTOS Scheduler).
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
 * Wird aufgerufen wenn der Master Daten geschrieben hat (WRITE-Transaktion abgeschlossen).
 *
 * Byte 0 ist immer die Register-Adresse (das "Command-Byte").
 * Optionale weitere Bytes sind Write-Daten (z.B. Timestamp für SET_TIME).
 *
 * Wir kopieren alles in den Context und signalisieren den Task. Der Task führt
 * dann die eigentliche Logik aus (Aktion für Write-Register, FIFO-Befüllung
 * für Read-Register).
 */
static bool i2c_slave_receive_cb(i2c_slave_dev_handle_t i2c_slave,
                                  const i2c_slave_rx_done_event_data_t *evt_data,
                                  void *arg)
{
    i2c_slave_context_t *context = (i2c_slave_context_t *)arg;

    if (evt_data->buffer != NULL && evt_data->length > 0) {
        context->current_reg = evt_data->buffer[0];
        context->rx_data_len = 0;

        /* Optionale Write-Daten (Bytes nach der Registeradresse) sichern */
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
 * TX-FIFO Befüllung für Read-Register
 * ========================================================================== */

/*
 * Bereitet die Antwortdaten für das aktuelle Read-Register vor.
 * Schreibt das Ergebnis in context->write_data / write_len / has_pending_data.
 * Wird im Task-Kontext aufgerufen (nicht ISR), darf daher alles verwenden.
 */
static void i2c_slave_fill_register_data(void)
{
    i2c_slave_context_t *context = &g_i2c_ctx;

    switch (context->current_reg) {

        case I2C_REG_COUNT:
            context->write_data[0] = (uint8_t)sensor_stack_count();
            context->write_len = 1;
            context->has_pending_data = true;
            ESP_LOGD(TAG, "Reg COUNT -> %d Pakete", context->write_data[0]);
            break;

        case I2C_REG_PACKET_READ: {
            /* Nächstes Paket vom Stack holen und in den Sendepuffer kopieren.
             * write_len = feste Header+Metadata-Größe (15 Byte) + variable Payload. */
            sensor_packet_t packet;
            if (sensor_stack_pop(&packet) == ESP_OK) {
                memcpy(context->write_data, &packet, sizeof(packet));
                context->write_len = PACKET_MIN_SIZE + packet.header.payload_len;
                context->has_pending_data = true;
                ESP_LOGD(TAG, "Reg PACKET -> sensor=%d type=%d payload=%dB total=%dB",
                         packet.header.sensor_nr, packet.header.sensor_type,
                         packet.header.payload_len, context->write_len);
            } else {
                context->write_len = 0;
                context->has_pending_data = false;
                ESP_LOGD(TAG, "Reg PACKET -> Stack leer");
            }
            break;
        }

        case I2C_REG_STATS_RECV: {
            uint32_t received, overwritten;
            sensor_stack_stats(&received, &overwritten);
            /* uint32_t als Little-Endian (LSB zuerst) */
            context->write_data[0] = (uint8_t)(received & 0xFF);
            context->write_data[1] = (uint8_t)((received >> 8)  & 0xFF);
            context->write_data[2] = (uint8_t)((received >> 16) & 0xFF);
            context->write_data[3] = (uint8_t)((received >> 24) & 0xFF);
            context->write_len = 4;
            context->has_pending_data = true;
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
            context->has_pending_data = true;
            ESP_LOGD(TAG, "Reg STATS_OVERWR -> %lu", overwritten);
            break;
        }

        /* Write-only Register: werden im Task-RX-Handler behandelt, nie hier */
        case I2C_REG_SET_TIME:
        case I2C_REG_SET_TZ:
        case I2C_REG_RESET_DROP:
            context->write_len = 0;
            context->has_pending_data = false;
            ESP_LOGW(TAG, "Reg 0x%02X ist write-only, kein Read möglich", context->current_reg);
            break;

        default:
            context->write_len = 0;
            context->has_pending_data = false;
            ESP_LOGE(TAG, "Unbekanntes Register: 0x%02X", context->current_reg);
            break;
    }
}

/* ==========================================================================
 * Task — verarbeitet Events aus der Queue
 * ========================================================================== */

static void i2c_slave_task(void *arg)
{
    i2c_slave_context_t *context = (i2c_slave_context_t *)arg;

    ESP_LOGI(TAG, "I2C Slave gestartet (addr=0x%02X, SDA=GPIO%d, SCL=GPIO%d)",
             I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL);

    while (true) {
        i2c_slave_event_t evt;

        /* Blockiert bis ein Event eintrifft — kein Timeout nötig. */
        xQueueReceive(context->event_queue, &evt, portMAX_DELAY);

        if (evt == I2C_SLAVE_EVT_RX) {
                /*
                 * Master hat eine WRITE-Transaktion abgeschlossen.
                 * context->current_reg enthält die Register-Adresse.
                 * context->rx_data / rx_data_len enthalten optionale Write-Bytes.
                 *
                 * Write-Register: Aktion sofort ausführen.
                 * Read-Register:  TX-FIFO jetzt befüllen, damit die Daten bereit sind
                 *                 wenn der Master die folgende READ-Transaktion sendet.
                 */

                if (context->current_reg == I2C_REG_SET_TIME) {
                    /* UTC Unix-Timestamp setzen (4 Byte Little-Endian) */
                    if (context->rx_data_len >= 4) {
                        uint32_t ts = (uint32_t)context->rx_data[0]
                                    | ((uint32_t)context->rx_data[1] << 8)
                                    | ((uint32_t)context->rx_data[2] << 16)
                                    | ((uint32_t)context->rx_data[3] << 24);
                        struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
                        settimeofday(&tv, NULL);
                        ESP_LOGI(TAG, "Systemzeit gesetzt: %lu (UTC)", (unsigned long)ts);
                    } else {
                        ESP_LOGE(TAG, "SET_TIME: erwartet 4 Byte, erhalten %d", context->rx_data_len);
                    }

                } else if (context->current_reg == I2C_REG_SET_TZ) {
                    /* POSIX Timezone-String setzen, z.B. "CET-1CEST,M3.5.0,M10.5.0/3" */
                    if (context->rx_data_len >= 1) {
                        /* Null-Terminierung sicherstellen */
                        uint8_t idx = context->rx_data_len < sizeof(context->rx_data)
                                      ? context->rx_data_len
                                      : sizeof(context->rx_data) - 1;
                        context->rx_data[idx] = '\0';
                        setenv("TZ", (const char *)context->rx_data, 1);
                        tzset();
                        ESP_LOGI(TAG, "Timezone gesetzt: '%s'", context->rx_data);
                    } else {
                        ESP_LOGE(TAG, "SET_TZ: keine Daten empfangen");
                    }

                } else if (context->current_reg == I2C_REG_RESET_DROP) {
                    /* Drop-Counter zurücksetzen (Master muss 0x01 als Datenbyte senden) */
                    if (context->rx_data_len >= 1 && context->rx_data[0] == 0x01) {
                        sensor_stack_reset_dropped();
                        ESP_LOGD(TAG, "Drop-Counter zurückgesetzt");
                    } else {
                        ESP_LOGE(TAG, "RESET_DROP: erwartet 0x01, erhalten 0x%02X",
                                 context->rx_data_len > 0 ? context->rx_data[0] : 0x00);
                    }

                } else if (context->current_reg == I2C_REG_COUNT      ||
                           context->current_reg == I2C_REG_PACKET_READ ||
                           context->current_reg == I2C_REG_STATS_RECV  ||
                           context->current_reg == I2C_REG_STATS_OVERWR) {
                    /*
                     * Read-Register: Antwortdaten vorbereiten und in den Hardware-FIFO schreiben.
                     * Die Daten müssen VOR der nächsten READ-Transaktion des Masters im FIFO sein,
                     * da Driver v2 kein Clock Stretching unterstützt.
                     */
                    i2c_slave_fill_register_data();

                    if (context->has_pending_data && context->write_len > 0) {
                        uint32_t total_written = 0;
                        while (total_written < context->write_len) {
                            uint32_t written = 0;
                            esp_err_t err = i2c_slave_write(
                                context->handle,
                                context->write_data + total_written,
                                context->write_len - total_written,
                                &written,
                                pdMS_TO_TICKS(100)
                            );
                            if (err != ESP_OK || written == 0) {
                                ESP_LOGE(TAG, "FIFO-Befüllung fehlgeschlagen (err=%d)", err);
                                break;
                            }
                            total_written += written;
                        }
                        ESP_LOGD(TAG, "FIFO befüllt: %lu Byte für Reg 0x%02X",
                                 total_written, context->current_reg);
                    }

                } else {
                    ESP_LOGE(TAG, "Unbekanntes Register 0x%02X empfangen", context->current_reg);
                }

            } else if (evt == I2C_SLAVE_EVT_TX) {
                /*
                 * Master hat eine READ-Transaktion gestartet.
                 * Die Daten wurden bereits beim EVT_RX in den FIFO geschrieben —
                 * hier ist nichts mehr zu tun.
                 */
                ESP_LOGD(TAG, "TX-Request für Reg 0x%02X (FIFO bereits befüllt)", context->current_reg);
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

    /* Schritt 1: Queue anlegen (16 Slots à 1 Byte reichen, Events kommen sequenziell) */
    g_i2c_ctx.event_queue = xQueueCreate(16, sizeof(i2c_slave_event_t));
    if (!g_i2c_ctx.event_queue) {
        ESP_LOGE(TAG, "Queue-Erstellung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    /* Schritt 2: I2C Slave Hardware initialisieren */
    i2c_slave_config_t i2c_slv_config = {
        .i2c_port         = I2C_SLAVE_PORT,
        .clk_source       = I2C_CLK_SRC_DEFAULT,
        .scl_io_num       = I2C_SLAVE_SCL,
        .sda_io_num       = I2C_SLAVE_SDA,
        .slave_addr       = I2C_SLAVE_ADDR,
        .send_buf_depth    = 256,   /* Software-Ringpuffer für ausgehende Daten */
        .receive_buf_depth = 256,   /* Software-Ringpuffer für eingehende Daten */
        .flags = {
            .enable_internal_pullup = 1,  /* Interne Pull-Ups (für kurze Leitungen ausreichend) */
        },
    };

    err = i2c_new_slave_device(&i2c_slv_config, &g_i2c_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_slave_device fehlgeschlagen: %s", esp_err_to_name(err));
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return err;
    }

    /* Schritt 3: ISR-Callbacks registrieren, Context-Pointer als user_data übergeben */
    i2c_slave_event_callbacks_t cbs = {
        .on_receive = i2c_slave_receive_cb,  /* Master hat geschrieben */
        .on_request = i2c_slave_request_cb,  /* Master will lesen     */
    };
    err = i2c_slave_register_event_callbacks(g_i2c_ctx.handle, &cbs, &g_i2c_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Callback-Registrierung fehlgeschlagen: %s", esp_err_to_name(err));
        i2c_del_slave_device(g_i2c_ctx.handle);
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return err;
    }

    /* Schritt 4: Task starten der die Queue-Events verarbeitet */
    TaskHandle_t task_handle;
    err = xTaskCreate(i2c_slave_task, "i2c_slave", 4096, &g_i2c_ctx, 5, &task_handle);
    if (err != pdPASS) {
        ESP_LOGE(TAG, "Task-Erstellung fehlgeschlagen (err=%d)", err);
        i2c_del_slave_device(g_i2c_ctx.handle);
        vQueueDelete(g_i2c_ctx.event_queue);
        g_i2c_ctx.event_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2C Slave bereit (addr=0x%02X, SDA=GPIO%d, SCL=GPIO%d)",
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
