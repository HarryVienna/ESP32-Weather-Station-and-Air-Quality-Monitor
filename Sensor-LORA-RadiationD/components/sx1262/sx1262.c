#include "sx1262.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SX1262";

// Global variables
static spi_device_handle_t spi_handle;
static sx1262_config_t current_config;

static TaskHandle_t rx_task_handle = NULL;
static sx1262_rx_callback_t rx_callback_ptr = NULL;

// Helper functions (Forward Declarations)
static void sx1262_reset(void);
static void sx1262_wait_on_busy(void);

static esp_err_t sx1262_write_command(uint8_t cmd, uint8_t *data, uint8_t len);
static esp_err_t sx1262_write_command_nowait(uint8_t cmd, uint8_t *data, uint8_t len);
static esp_err_t sx1262_read_command(uint8_t cmd, uint8_t *data, uint8_t len);

static esp_err_t sx1262_write_register(uint16_t addr, uint8_t *data, uint8_t len);
static esp_err_t sx1262_read_register(uint16_t addr, uint8_t *data, uint8_t len);

static esp_err_t sx1262_spi_write_general(uint8_t *tx_header, uint8_t tx_header_len, uint8_t *data, uint8_t data_len, bool wait_after);
static esp_err_t sx1262_spi_read_general(uint8_t *tx_header, uint8_t tx_header_len, uint8_t *rx_data, uint8_t rx_len);

static esp_err_t sx1262_calibrate_image(uint32_t frequency);
static esp_err_t sx1262_set_dio_irq_params(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask);
static esp_err_t sx1262_clear_irq_status(uint16_t irq_mask);
static uint16_t sx1262_get_irq_status(void);

// ============================================================================
// PHASE 1: HARDWARE INITIALIZATION
// ============================================================================

esp_err_t sx1262_init_bus(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "SPI initialization...");

    // GPIO Configuration
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LORA_PIN_DIO1) | (1ULL << LORA_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    // BUSY & DIO1 as Input
    gpio_config(&io_conf);


    io_conf.pin_bit_mask = (1ULL << LORA_PIN_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    
    // RST as Output
    gpio_config(&io_conf);
    gpio_set_level(LORA_PIN_RST, 1); // Configure Reset Pin as output and set to 1 (High) as a precaution


    // SPI Bus Configuration
    spi_bus_config_t buscfg = {
        .miso_io_num = LORA_PIN_MISO,
        .mosi_io_num = LORA_PIN_MOSI,
        .sclk_io_num = LORA_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Bus Init failed");
        return ret;
    }

    // SPI Device Configuration
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 8 * 1000 * 1000,  // 8 MHz
        .mode = 0,
        .spics_io_num = LORA_PIN_NSS,
        .queue_size = 7,
        .flags = 0,
        .pre_cb = NULL
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Device Add failed");
        return ret;
    }

    ESP_LOGI(TAG, "SPI initialized successfully");
    
    return ESP_OK;
}

esp_err_t sx1262_init_radio(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Radio initialization...");

    // Hardware Reset
    sx1262_reset();
    vTaskDelay(pdMS_TO_TICKS(10));

    // Wait on BUSY
    sx1262_wait_on_busy();

    // Set Standby Mode
    uint8_t standby_config = 0x01; // STDBY_XOSC (for TCXO)
    ret = sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby_config, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting Standby failed");
        return ret;
    }

    // Configure DIO3 as TCXO Control (3.3V, 5ms timeout)
    // wait_on_busy() inside write_command handles TCXO stabilisation
    uint8_t tcxo_config[4] = {0x07, 0x00, 0x01, 0x40}; // 320 * 15.625us = 5ms
    ret = sx1262_write_command(SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_config, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCXO configuration failed");
    }

    // DIO2 as RF Switch Control
    uint8_t dio2_config = 0x01; // Enable
    ret = sx1262_write_command(SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &dio2_config, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DIO2 configuration failed");
    }

    // Set Regulator Mode (DC-DC)
    uint8_t regulator_mode = 0x01; // DC-DC + LDO
    ret = sx1262_write_command(SX1262_CMD_SET_REGULATOR_MODE, &regulator_mode, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Regulator Mode failed");
    }

    // Calibrate — BUSY stays HIGH for the full duration, wait_on_busy() handles it
    uint8_t calib_param = 0x7F; // All
    ret = sx1262_write_command(SX1262_CMD_CALIBRATE, &calib_param, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Calibration failed");
    }

    // Set RxTxFallbackMode (Chip goes back to STDBY_XOSC after TX/RX)
    uint8_t fallback_mode = 0x30; // STDBY_XOSC
    ret = sx1262_write_command(SX1262_CMD_SET_RX_TX_FALLBACK_MODE, &fallback_mode, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RxTx Fallback Mode failed");
        return ret;
    }

    ESP_LOGI(TAG, "Radio initialized successfully");
    
    return ESP_OK;
}

esp_err_t sx1262_wakeup(void)
{

    ESP_LOGI(TAG, "Performing Warm Start (Retention Wakeup)...");

    // Manual wakeup (NSS Low)
    gpio_set_level(LORA_PIN_NSS, 0);

    // Wait for BUSY Low (chip boots up, TCXO stabilizes)
    // Your existing function is safe here since NSS is already Low
    sx1262_wait_on_busy(); 

    // NSS High (end transaction)
    gpio_set_level(LORA_PIN_NSS, 1);

    // Check if retention was successful (Optional but recommended)
    // We read the Packet Type. If it is 0x01 (LoRa), the chip has retained its state.
    // After a Hard Reset, it would default to FSK (or undefined/Standby).
    uint8_t packet_type;
    esp_err_t ret = sx1262_read_command(SX1262_CMD_GET_PACKET_TYPE, &packet_type, 1);
    
    if (ret == ESP_OK && packet_type == 0x01) {
        ESP_LOGI(TAG, "Warmstart successful. Chip retained state.");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Warmstart failed (Register lost). Fallback to Cold Start.");
        return ESP_FAIL; // Signals the app that it must call sx1262_init_radio()
    }
}

// ============================================================================
// PHASE 2: LORA CONFIGURATION
// ============================================================================

esp_err_t sx1262_configure(const sx1262_config_t *config)
{
    esp_err_t ret = ESP_OK;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Configuring LoRa Parameters...");

    // Copy Config
    memcpy(&current_config, config, sizeof(sx1262_config_t));

    // Standby
    ret = sx1262_standby();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Standby failed");
        return ret;
    }

    // Set Packet Type (LoRa or FSK)
    uint8_t packet_type = (config->modem_mode == SX1262_MODEM_LORA) ? 
                           SX1262_PACKET_TYPE_LORA : SX1262_PACKET_TYPE_GFSK;
    ret = sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, &packet_type, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting Packet Type failed");
        return ret;
    }

    // Set RF Frequency
    uint32_t freq_reg = ((uint64_t)config->frequency << 25) / 32000000;
    uint8_t freq_params[4];
    freq_params[0] = (freq_reg >> 24) & 0xFF;
    freq_params[1] = (freq_reg >> 16) & 0xFF;
    freq_params[2] = (freq_reg >> 8) & 0xFF;
    freq_params[3] = freq_reg & 0xFF;
    
    ret = sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, freq_params, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting Frequency failed");
        return ret;
    }

    // Perform Image Calibration for the configured frequency band
    ret = sx1262_calibrate_image(config->frequency);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Image Calibration failed");
        return ret;
    }

    // Set PA Config (Datasheet 15.2 TxClampConfig fix)
    uint8_t tx_clamp_cfg;
    ret = sx1262_read_register(SX1262_REG_TX_CLAMP_CFG, &tx_clamp_cfg, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX_CLAMP_CFG Read failed");
        return ret;
    }
    tx_clamp_cfg = tx_clamp_cfg | 0x1E;
    ret = sx1262_write_register(SX1262_REG_TX_CLAMP_CFG, &tx_clamp_cfg, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX_CLAMP_CFG Read failed");
        return ret;
    }

    uint8_t pa_config[4];
    int8_t power = config->tx_power;
    
    if (power > 22) power = 22;
    if (power < -9) power = -9;
    
    if (power <= 14) {
        pa_config[0] = 0x02;
        pa_config[1] = 0x02;
        pa_config[2] = 0x00;
        pa_config[3] = 0x01;
    } else if (power <= 17) {
        pa_config[0] = 0x02;
        pa_config[1] = 0x03;
        pa_config[2] = 0x00;
        pa_config[3] = 0x01;
    } else if (power <= 20) {
        pa_config[0] = 0x03;
        pa_config[1] = 0x05;
        pa_config[2] = 0x00;
        pa_config[3] = 0x01;
    } else {
        pa_config[0] = 0x04;
        pa_config[1] = 0x07;
        pa_config[2] = 0x00;
        pa_config[3] = 0x01;
    }
    
    ret = sx1262_write_command(SX1262_CMD_SET_PA_CONFIG, pa_config, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PA Config failed");
        return ret;
    }
    
    // Set OCP
    uint8_t ocp_value;
    if (power >= 20) {
        ocp_value = 0x38;  // 140 mA
    } else if (power >= 17) {
        ocp_value = 0x28;  // 100 mA
    } else {
        ocp_value = 0x18;  // 60 mA
    }
    
    ret = sx1262_write_register(SX1262_REG_OCP_CONFIGURATION, &ocp_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Setting OCP failed");
        return ret;
    }

    // Set TX Params
    uint8_t tx_params[2];
    tx_params[0] = power;
    tx_params[1] = 0x04; // 200us ramp
    
    ret = sx1262_write_command(SX1262_CMD_SET_TX_PARAMS, tx_params, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting TX Params failed");
        return ret;
    }

    // Set Modulation Params
    if (config->modem_mode == SX1262_MODEM_LORA) {
        // LoRa Modulation
        uint8_t mod_params[4];
        mod_params[0] = config->spreading_factor;
        mod_params[1] = config->bandwidth;
        mod_params[2] = config->coding_rate;
        mod_params[3] = (config->spreading_factor >= 11) ? 0x01 : 0x00; // Low Data Rate Optimize
        
        ret = sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, mod_params, 4);
    } else {
        // FSK Modulation
        uint32_t br_reg = (uint32_t)((32.0 * 32000000.0) / config->fsk_bitrate);
        uint32_t fdev_reg = (uint32_t)((config->fsk_fdev * 33554432.0) / 32000000.0);
        
        uint8_t mod_params[8];
        mod_params[0] = (br_reg >> 16) & 0xFF;
        mod_params[1] = (br_reg >> 8) & 0xFF;
        mod_params[2] = br_reg & 0xFF;
        mod_params[3] = config->fsk_shaping;
        mod_params[4] = config->fsk_rx_bw;
        mod_params[5] = (fdev_reg >> 16) & 0xFF;
        mod_params[6] = (fdev_reg >> 8) & 0xFF;
        mod_params[7] = fdev_reg & 0xFF;
        
        ret = sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, mod_params, 8);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modulation Params failed");
        return ret;
    }

    // Set Packet Params
    if (config->modem_mode == SX1262_MODEM_LORA) {
        // LoRa Packet Parameters
        uint8_t packet_params[6];

        // Datasheet 6.1.1.1 Preamble length for SF5 and SF6
        uint16_t preamble_length = (config->spreading_factor <= 6) ? 12 : config->preamble_length;

        packet_params[0] = (preamble_length >> 8) & 0xFF;
        packet_params[1] = preamble_length & 0xFF;
        packet_params[2] = config->payload_length == 0 ? 0x00 : 0x01; // Variable/Fixed
        packet_params[3] = config->payload_length;
        packet_params[4] = config->crc_on ? 0x01 : 0x00;
        packet_params[5] = config->iq_inverted ? 0x01 : 0x00;
        
        ret = sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, packet_params, 6);
    } else {
        // FSK Packet Parameters
        uint8_t packet_params[9];
        packet_params[0] = (config->preamble_length >> 8) & 0xFF;
        packet_params[1] = config->preamble_length & 0xFF;
        packet_params[2] = 0x04; // Preamble detector length
        packet_params[3] = 0x08; // Sync word length
        packet_params[4] = 0x01; // Address filtering
        packet_params[5] = config->payload_length == 0 ? 0x01 : 0x00; // Variable/Fixed
        packet_params[6] = config->payload_length;
        packet_params[7] = config->crc_on ? 0x01 : 0x00;
        packet_params[8] = 0x00; // Whitening
        
        ret = sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, packet_params, 9);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Packet Params failed");
        return ret;
    }

    // Datasheet 15.4 Workaround: Optimizing Inverted IQ Operation
    if (config->modem_mode == SX1262_MODEM_LORA) {
        uint8_t iq_reg;
        sx1262_read_register(SX1262_REG_IQ_POLARITY_SETUP, &iq_reg, 1); // Register IQ Polarity Setup
        
        if (config->iq_inverted) {
            iq_reg &= ~(1 << 2); // Bit 2 auf 0 setzen
        } else {
            iq_reg |= (1 << 2);  // Bit 2 auf 1 setzen (Standard)
        }
        sx1262_write_register(SX1262_REG_IQ_POLARITY_SETUP, &iq_reg, 1);
    }

    if (config->modem_mode == SX1262_MODEM_LORA) {

        // Datasheet 15.1.2 Workaround: Quality with 500kHz LoRa BW 
        uint8_t tx_mod_reg;
        sx1262_read_register(SX1262_REG_TX_MODULATION, &tx_mod_reg, 1);
        
        if (config->bandwidth == LORA_BW_500) {
            tx_mod_reg &= ~(1 << 2); // Delete Bit 2 
        } else {
            tx_mod_reg |= (1 << 2);  // Set Bit 2 
        }
        sx1262_write_register(SX1262_REG_TX_MODULATION, &tx_mod_reg, 1);
    }

    // Set Sync Word (LoRa only, optional)
    if (config->modem_mode == SX1262_MODEM_LORA && config->sync_word != 0) {
        uint8_t sync_word_msb = (config->sync_word >> 8) & 0xFF;
        uint8_t sync_word_lsb = config->sync_word & 0xFF;
        
        ret = sx1262_write_register(SX1262_REG_LORA_SYNC_WORD_MSB, &sync_word_msb, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Setting Sync Word MSB failed");
            return ret;
        }
        
        ret = sx1262_write_register(SX1262_REG_LORA_SYNC_WORD_LSB, &sync_word_lsb, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Setting Sync Word LSB failed");
            return ret;
        }
        
        ESP_LOGI(TAG, "Sync Word set: 0x%04X", config->sync_word);
    }

    // Set Buffer Base Address
    uint8_t buffer_params[2] = {0x00, 0x00}; // TX=0, RX=0
    ret = sx1262_write_command(SX1262_CMD_SET_BUFFER_BASE_ADDRESS, buffer_params, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Buffer Base Address failed");
        return ret;
    }

    // Configure IRQ
    ret = sx1262_set_dio_irq_params(SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT,
                                   SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT,
                                   0x0000, 0x0000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IRQ Config failed");
        return ret;
    }

    // Set RX Gain
    if (config->rx_gain_boosted) {
        uint8_t rx_gain_boosted = 0x96;
        ret = sx1262_write_register(SX1262_REG_RX_GAIN, &rx_gain_boosted, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Setting RX Gain Boosted failed");
            return ret;
        } else {
            ESP_LOGI(TAG, "RX Gain: Boosted");
        }
    } else {
        uint8_t rx_gain_power_save = 0x94;
        ret = sx1262_write_register(SX1262_REG_RX_GAIN, &rx_gain_power_save, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Setting RX Gain Power Save failed");
            return ret;
        } else {
            ESP_LOGI(TAG, "RX Gain: Power Save");
        }
    }

    ESP_LOGI(TAG, "Configuration complete: Mode=%s, Freq=%luHz, SF=%d, BW=%d, CR=%d, TX=%ddBm",
             config->modem_mode == SX1262_MODEM_LORA ? "LoRa" : "FSK",
             config->frequency,
             config->spreading_factor,
             config->bandwidth,
             config->coding_rate,
             config->tx_power);    

    return ret;
}

// ============================================================================
// PHASE 3: COMMUNICATION 
// ============================================================================

esp_err_t sx1262_send(uint8_t *data, uint8_t len)
{
    esp_err_t ret = ESP_OK;

    if (data == NULL || len == 0 || len > 255) {
        return ESP_ERR_INVALID_ARG;
    }

    // Standby Mode
    ret = sx1262_standby();
    if (ret != ESP_OK) {
        return ret; 
    }

    // Clear IRQ Status
    ret = sx1262_clear_irq_status(0xFFFF);
    if (ret != ESP_OK) {
        return ret; 
    }

    // Write data to buffer
    uint8_t offset = 0x00;
    uint8_t buffer[256];
    buffer[0] = offset;
    memcpy(&buffer[1], data, len);
    
    ret = sx1262_write_command(SX1262_CMD_WRITE_BUFFER, buffer, len + 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Writing buffer failed");
        return ret;
    }

    // Update packet parameters with current length
    uint8_t packet_params[6];
    packet_params[0] = (current_config.preamble_length >> 8) & 0xFF;
    packet_params[1] = current_config.preamble_length & 0xFF;
    packet_params[2] = current_config.payload_length == 0 ? 0x00 : 0x01;
    packet_params[3] = len;
    packet_params[4] = current_config.crc_on ? 0x01 : 0x00;
    packet_params[5] = current_config.iq_inverted ? 0x01 : 0x00;

    ret = sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, packet_params, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Packet Params Update failed");
        return ret; 
    }

    // Set TX Mode
    uint8_t tx_params[3] = {0x00, 0x00, 0x00}; // No timeout
    ret = sx1262_write_command(SX1262_CMD_SET_TX, tx_params, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting TX Mode failed");
        return ret;
    }

    // Wait for TX Done
    uint32_t timeout = 5000;
    uint32_t start = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout)) {
        uint16_t irq_status = sx1262_get_irq_status();
        if (irq_status & SX1262_IRQ_TX_DONE) {
            ret = sx1262_clear_irq_status(SX1262_IRQ_TX_DONE);
            if (ret != ESP_OK) {
                return ret; 
            }

            ESP_LOGD(TAG, "TX Done");
            ret = ESP_OK;
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGE(TAG, "TX Timeout");
    
    // Timeout
    ret = ESP_ERR_TIMEOUT;
    
    return ret;
}

esp_err_t sx1262_receive(uint8_t *data, uint8_t *len, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;

    if (data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Standby Mode
    ret = sx1262_standby();
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear IRQ Status
    sx1262_clear_irq_status(0xFFFF);

    // Set RX Mode
    uint8_t rx_params[3];
    if (timeout_ms == 0) {
        // Continuous RX
        rx_params[0] = 0xFF;
        rx_params[1] = 0xFF;
        rx_params[2] = 0xFF;
    } else {
        // Timeout in 15.625 us steps
        uint32_t timeout_steps = (timeout_ms * 1000) / 15.625;
        rx_params[0] = (timeout_steps >> 16) & 0xFF;
        rx_params[1] = (timeout_steps >> 8) & 0xFF;
        rx_params[2] = timeout_steps & 0xFF;
    }

    ret = sx1262_write_command(SX1262_CMD_SET_RX, rx_params, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Setting RX Mode failed");
        return ret;
    }

    // Wait for RX Done
    uint32_t wait_timeout = timeout_ms == 0 ? 60000 : timeout_ms + 1000;
    uint32_t start = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(wait_timeout)) {
        uint16_t irq_status = sx1262_get_irq_status();
               
        if (irq_status & (SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR)) {
            sx1262_clear_irq_status(SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR | SX1262_IRQ_RX_DONE);
            ESP_LOGW(TAG, "CRC/Header Error");
            ret = ESP_FAIL;
            return ret;
        } else if (irq_status & SX1262_IRQ_RX_DONE) {
            sx1262_clear_irq_status(SX1262_IRQ_RX_DONE);
            
            // Read Buffer Status
            uint8_t buffer_status[2];
            ret = sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS, buffer_status, 2);
            if (ret != ESP_OK) {
                return ret;
            }

            uint8_t payload_len = buffer_status[0];
            uint8_t rx_start_ptr = buffer_status[1];

            if (payload_len > 255) {
                payload_len = 255;
            }

            // Read data from buffer using the general function
            // We need the SPI sequence: [CMD_READ_BUFFER, OFFSET, NOP]
            
            uint8_t tx_header[3];
            tx_header[0] = SX1262_CMD_READ_BUFFER; // The command
            tx_header[1] = rx_start_ptr;          // The offset
            tx_header[2] = 0x00;                  // The NOP
            
            // Read 'payload_len' bytes directly into the 'data' buffer
            // after sending the 3-byte header.
            ret = sx1262_spi_read_general(tx_header, 3, data, payload_len);

            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "ReadBuffer failed");
                return ret;
            }

            // No more memcpy() needed, data was filled directly!
            *len = payload_len;

            ESP_LOGD(TAG, "RX Done: %d bytes", payload_len);
            ret = ESP_OK;
            return ret;

        } else if (irq_status & SX1262_IRQ_TIMEOUT) {
            sx1262_clear_irq_status(SX1262_IRQ_TIMEOUT);
            ESP_LOGD(TAG, "RX Timeout");
            ret = ESP_ERR_TIMEOUT;
            return ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ret = ESP_ERR_TIMEOUT;

    return ret;
}

// The actual interrupt handler (runs in ISR context -> NO SPI here!)
static void IRAM_ATTR sx1262_dio1_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Notify the RX task that DIO1 has fired
    if (rx_task_handle != NULL) {
        vTaskNotifyGiveFromISR(rx_task_handle, &xHigherPriorityTaskWoken);
    }
    
    // Force a context switch if the task has a higher priority than the current one
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// The background task that does the work (runs in task context -> SPI allowed)
static void sx1262_rx_task(void *arg)
{
    uint8_t rx_buffer[255];
    uint8_t rx_len = 0;
    sx1262_packet_status_t pkt_status;
    
    ESP_LOGI(TAG, "RX Interrupt Task started");

    while (1) {
        // Wait for signal from ISR (blocking, consumes no CPU)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Check what happened (read IRQ status)
        uint16_t irq_status = sx1262_get_irq_status();

        if (irq_status & (SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR)) {
            ESP_LOGW(TAG, "RX Error (CRC/Header), record dropped");
            sx1262_clear_irq_status(SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR | SX1262_IRQ_RX_DONE);
        } else if (irq_status & SX1262_IRQ_RX_DONE) {

            // Clear IRQ
            sx1262_clear_irq_status(SX1262_IRQ_RX_DONE);

            // Get buffer status
            uint8_t buffer_status[2];
            if (sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS, buffer_status, 2) == ESP_OK) {
                uint8_t payload_len = buffer_status[0];
                uint8_t rx_start_ptr = buffer_status[1];

                // Read data
                uint8_t tx_header[3] = {SX1262_CMD_READ_BUFFER, rx_start_ptr, 0x00};
                if (sx1262_spi_read_general(tx_header, 3, rx_buffer, payload_len) == ESP_OK) {

                    // Get packet info (RSSI/SNR)
                    sx1262_get_packet_status(&pkt_status);

                    // CALL CALLBACK
                    if (rx_callback_ptr != NULL) {
                        rx_callback_ptr(rx_buffer, payload_len, &pkt_status);
                    }
                }
            }
        }

        // Important: REACTIVATE reception mode (Continuous Mode)
        // Since we often fall back to Standby in Fallback Mode, we reset RX here.
        // 0xFFFFFF = Continuous RX
        uint8_t rx_params[3] = {0xFF, 0xFF, 0xFF};
        sx1262_write_command(SX1262_CMD_SET_RX, rx_params, 3);
    }
}

esp_err_t sx1262_start_receive_async(sx1262_rx_callback_t callback)
{
    esp_err_t ret;

    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (rx_task_handle != NULL) {
        ESP_LOGW(TAG, "RX Interrupt mode already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Save callback
    rx_callback_ptr = callback;

    // Set radio to standby to change config
    sx1262_standby();

    // Configure DIO1 pin for interrupt
    // We need to update the GPIO config to allow interrupts
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LORA_PIN_DIO1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE // Interrupt bei steigender Flanke (High = Done)
    };
    gpio_config(&io_conf);

    // Create FreeRTOS task
    // Stack size 4096 is safe for SPI and logs, high priority (e.g. 10) so packet is processed quickly
    BaseType_t task_ret = xTaskCreate(sx1262_rx_task, "sx1262_rx_task", 4096, NULL, 10, &rx_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        return ESP_FAIL;
    }

    // Install ISR service (if not already done) and add handler
    // Ignore error if service is already running (ESP_ERR_INVALID_STATE)
    gpio_install_isr_service(0); 
    
    ret = gpio_isr_handler_add(LORA_PIN_DIO1, sx1262_dio1_isr_handler, NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // Only fail on real errors, not "already installed"
        ESP_LOGE(TAG, "Failed to add ISR handler");
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
        return ret;
    }

    // Set IRQ mask on chip (RX Done, CRC Error)
    sx1262_set_dio_irq_params(SX1262_IRQ_RX_DONE | SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR,
                              SX1262_IRQ_RX_DONE | SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR,
                              0x0000, 0x0000);

    // Put radio in Continuous RX mode
    uint8_t rx_params[3] = {0xFF, 0xFF, 0xFF}; // Continuous
    ret = sx1262_write_command(SX1262_CMD_SET_RX, rx_params, 3);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RX Interrupt Mode started");
    }

    return ret;
}

void sx1262_stop_receive_async(void)
{
    if (rx_task_handle == NULL) return;

    // Deactivate interrupt
    gpio_isr_handler_remove(LORA_PIN_DIO1);
    
    // Set GPIO back to interrupt-less
    gpio_set_intr_type(LORA_PIN_DIO1, GPIO_INTR_DISABLE);

    // Delete task
    vTaskDelete(rx_task_handle);
    rx_task_handle = NULL;
    rx_callback_ptr = NULL;

    // Radio in standby
    sx1262_standby();

    ESP_LOGI(TAG, "RX Interrupt Mode stopped");
}

// ============================================================================
// HELPER FUNCTIONS 
// ============================================================================

esp_err_t sx1262_sleep(void)
{
    uint8_t sleep_config = 0x00; // cold start (~0.9 µA)
    // Nach SET_SLEEP schläft der Chip sofort — BUSY bleibt HIGH.
    // Kein wait_on_busy() nach diesem Befehl!
    return sx1262_write_command_nowait(SX1262_CMD_SET_SLEEP, &sleep_config, 1);
}

esp_err_t sx1262_deinit_bus(void)
{
    // 1. NSS SOFORT als Output auf HIGH zwingen, BEVOR SPI loslässt.
    // So garantieren wir, dass der SX1262 nicht versehentlich aufwacht!
    gpio_set_direction(LORA_PIN_NSS, GPIO_MODE_OUTPUT);
    gpio_set_level(LORA_PIN_NSS, 1);

    // 2. SPI Treiber beenden
    if (spi_handle != NULL) {
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
    }
    spi_bus_free(SPI2_HOST);

    // 3. SPI Pins NICHT floaten lassen (gpio_reset_pin ist hier schlecht).
    // Besser: Als Input setzen und interne Pull-downs aktivieren.
    gpio_set_direction(LORA_PIN_MOSI, GPIO_MODE_INPUT);
    gpio_pullup_dis(LORA_PIN_MOSI);
    gpio_pulldown_en(LORA_PIN_MOSI);

    gpio_set_direction(LORA_PIN_SCK, GPIO_MODE_INPUT);
    gpio_pullup_dis(LORA_PIN_SCK);
    gpio_pulldown_en(LORA_PIN_SCK);

    gpio_set_direction(LORA_PIN_MISO, GPIO_MODE_INPUT);
    gpio_pullup_dis(LORA_PIN_MISO);
    gpio_pulldown_en(LORA_PIN_MISO);

    return ESP_OK;
}

esp_err_t sx1262_standby(void)
{
    uint8_t standby_config = 0x01; // STDBY_XOSC
    esp_err_t ret = sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby_config, 1);
    
    return ret;   
}

int16_t sx1262_get_rssi(void)
{
    uint8_t rssi_data[1];
    esp_err_t ret = sx1262_read_command(SX1262_CMD_GET_RSSI_INST, rssi_data, 1);
    
    int16_t rssi;
    if (ret != ESP_OK) {
        rssi = -999;
    } else {
        rssi = -(int16_t)(rssi_data[0]) / 2;
    }
    
    return rssi;
}

esp_err_t sx1262_get_packet_status(sx1262_packet_status_t *status)
{
    esp_err_t ret = ESP_OK;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t pkt_status[3];
    ret = sx1262_read_command(SX1262_CMD_GET_PACKET_STATUS, pkt_status, 3);
    
    if (ret != ESP_OK) {
        return ret;
    }

    if (current_config.modem_mode == SX1262_MODEM_LORA) {
        // LoRa Mode
        status->rssi_pkt = -(int16_t)(pkt_status[0]) / 2;
        status->snr_pkt = ((int8_t)pkt_status[1]) * 0.25;
        status->signal_rssi = -(int16_t)(pkt_status[2]) / 2;
    } else {
        // FSK Mode
        status->rssi_pkt = -(int16_t)(pkt_status[0]) / 2;
        status->snr_pkt = 0;
        status->signal_rssi = 0;
    }

    
    return ret;
}

esp_err_t sx1262_get_chip_info(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== SX1262 Chip Information ===");
    
    // Get Status
    uint8_t status[1];
    ret = sx1262_read_command(SX1262_CMD_GET_STATUS, status, 1);
    if (ret == ESP_OK) {
        uint8_t chip_mode = (status[0] >> 4) & 0x07;
        uint8_t cmd_status = (status[0] >> 1) & 0x07;
        
        const char* mode_str[] = {"UNUSED", "RFU", "STDBY_RC", "STDBY_XOSC", 
                                  "FS", "RX", "TX", "UNUSED"};
        const char* cmd_str[] = {"UNUSED", "RFU", "Data available", "Timeout",
                                 "Processing error", "Execution failure", "TX done", "UNUSED"};
        
        ESP_LOGI(TAG, "Status: 0x%02X", status[0]);
        ESP_LOGI(TAG, "  Chip Mode: %s", mode_str[chip_mode]);
        ESP_LOGI(TAG, "  Command Status: %s", cmd_str[cmd_status]);
    } else {
        ESP_LOGE(TAG, "Status query failed - Chip not reachable!");
        return ret;
    }
    
    // Read Packet Type
    uint8_t packet_type[1];
    ret = sx1262_read_command(SX1262_CMD_GET_PACKET_TYPE, packet_type, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Packet Type: %s", 
                 packet_type[0] == 0x00 ? "GFSK" : 
                 packet_type[0] == 0x01 ? "LoRa" : "Unknown");
    }
    
    // Random Number Generator Test (checks if chip is working)
    uint8_t random[4];
    ret = sx1262_read_register(SX1262_REG_RANDOM_NUMBER_GEN, random, 4);
    if (ret == ESP_OK) {
        uint32_t rnd = (random[0] << 24) | (random[1] << 16) | 
                       (random[2] << 8) | random[3];
        ESP_LOGI(TAG, "Random Number: 0x%08lX", rnd);
        
        // If Random = 0 or 0xFFFFFFFF -> Problem
        if (rnd == 0 || rnd == 0xFFFFFFFF) {
            ESP_LOGW(TAG, "  ⚠ Suspicious Random Number - possible chip problem");
        } else {
            ESP_LOGI(TAG, "  ✓ Chip is responding correctly");
        }
    }
    
    // Read Sync Word (LoRa only)
    if (current_config.modem_mode == SX1262_MODEM_LORA) {
        uint8_t sync_msb, sync_lsb;
        ret = sx1262_read_register(SX1262_REG_LORA_SYNC_WORD_MSB, &sync_msb, 1);
        if (ret == ESP_OK) {
            sx1262_read_register(SX1262_REG_LORA_SYNC_WORD_LSB, &sync_lsb, 1);
            uint16_t sync_word = (sync_msb << 8) | sync_lsb;
            ESP_LOGI(TAG, "Sync Word: 0x%04X", sync_word);
            
            if (sync_word == 0x1424) {
                ESP_LOGI(TAG, "  → LoRaWAN Public Network");
            } else if (sync_word == 0x3444) {
                ESP_LOGI(TAG, "  → LoRaWAN Private Network");
            } else {
                ESP_LOGI(TAG, "  → Custom Network");
            }
        }
    }
    
    // Over Current Protection
    uint8_t ocp[1];
    ret = sx1262_read_register(SX1262_REG_OCP_CONFIGURATION, ocp, 1);
    if (ret == ESP_OK) {
        uint8_t ocp_trim = ocp[0] & 0x3F;
        float ocp_ma = ocp_trim * 2.5;
        ESP_LOGI(TAG, "OCP Limit: %.1f mA", ocp_ma);
    }
    
    // RX Gain
    uint8_t rx_gain[1];
    ret = sx1262_read_register(SX1262_REG_RX_GAIN, rx_gain, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RX Gain: 0x%02X (%s)", rx_gain[0],
                 rx_gain[0] == 0x94 ? "Power Saving" :
                 rx_gain[0] == 0x96 ? "Boosted" : "Unknown");
    }
    
    ESP_LOGI(TAG, "=================================\n");
    
    return ret;
}

// ============================================================================
// STATIC HELPER FUNCTIONS 
// ============================================================================

static void sx1262_reset(void)
{
    gpio_set_level(LORA_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void sx1262_wait_on_busy(void)
{
    uint32_t timeout = 1000;
    uint32_t start = xTaskGetTickCount();
    
    while (gpio_get_level(LORA_PIN_BUSY) == 1) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout)) {
            ESP_LOGW(TAG, "BUSY Timeout");
            return;
        }
        taskYIELD();
    }
}

/**
 * @brief Perform Image Calibration for a specific frequency band
 * 
 * The SX1262 requires image calibration to optimize RX performance for
 * specific frequency bands. This function automatically selects the 
 * correct calibration frequencies based on the configured RF frequency.
 * 
 * @param frequency RF frequency in Hz
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
static esp_err_t sx1262_calibrate_image(uint32_t frequency)
{
    uint8_t cal_freq[2];
    
    // Determine calibration frequencies based on operating frequency
    // Values from Datasheet Table 9-2: Image Calibration Over the ISM Bands
    
    if (frequency >= 430000000 && frequency <= 440000000) {
        // 430-440 MHz band
        cal_freq[0] = 0x6B;  // freq1
        cal_freq[1] = 0x6F;  // freq2
        ESP_LOGI(TAG, "Image Calibration for 430-440 MHz");
        
    } else if (frequency >= 470000000 && frequency <= 510000000) {
        // 470-510 MHz band
        cal_freq[0] = 0x75;  // freq1
        cal_freq[1] = 0x81;  // freq2
        ESP_LOGI(TAG, "Image Calibration for 470-510 MHz");
        
    } else if (frequency >= 779000000 && frequency <= 787000000) {
        // 779-787 MHz band
        cal_freq[0] = 0xC1;  // freq1
        cal_freq[1] = 0xC5;  // freq2
        ESP_LOGI(TAG, "Image Calibration for 779-787 MHz");
        
    } else if (frequency >= 863000000 && frequency <= 870000000) {
        // 863-870 MHz band (Europa ISM)
        cal_freq[0] = 0xD7;  // freq1
        cal_freq[1] = 0xDB;  // freq2
        ESP_LOGI(TAG, "Image Calibration for 863-870 MHz (EU868)");
        
    } else if (frequency >= 902000000 && frequency <= 928000000) {
        // 902-928 MHz band (US ISM)
        cal_freq[0] = 0xE1;  // freq1
        cal_freq[1] = 0xE9;  // freq2
        ESP_LOGI(TAG, "Image Calibration for 902-928 MHz (US915)");
        
    } else {
        // Frequency outside standard ISM bands
        ESP_LOGW(TAG, "Frequency %lu Hz outside standard ISM bands - skipping image calibration", frequency);
        return ESP_OK;  // Not an error, just not applicable
    }

    // Execute calibration command
    esp_err_t ret = sx1262_write_command(SX1262_CMD_CALIBRATE_IMAGE, cal_freq, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Image Calibration failed");
        return ret;
    }
    
    // Wait for calibration to complete (typical: 2ms, worst case: 10ms)
    vTaskDelay(pdMS_TO_TICKS(10));
    
    ESP_LOGI(TAG, "Image Calibration completed");
    return ESP_OK;
}

static esp_err_t sx1262_write_command(uint8_t cmd, uint8_t *data, uint8_t len)
{
    return sx1262_spi_write_general(&cmd, 1, data, len, true);
}

static esp_err_t sx1262_write_command_nowait(uint8_t cmd, uint8_t *data, uint8_t len)
{
    return sx1262_spi_write_general(&cmd, 1, data, len, false);
}

static esp_err_t sx1262_read_command(uint8_t cmd, uint8_t *data, uint8_t len)
{
    // Send [CMD, NOP], read 'len' bytes
    uint8_t tx_header[2];
    tx_header[0] = cmd;
    tx_header[1] = 0x00; // NOP
    
    // Header is 2 bytes long
    return sx1262_spi_read_general(tx_header, 2, data, len);
}


static esp_err_t sx1262_write_register(uint16_t addr, uint8_t *data, uint8_t len)
{
    uint8_t tx_header[3];
    tx_header[0] = SX1262_CMD_WRITE_REGISTER;
    tx_header[1] = (addr >> 8) & 0xFF;
    tx_header[2] = addr & 0xFF;
    return sx1262_spi_write_general(tx_header, 3, data, len, true);
}

static esp_err_t sx1262_read_register(uint16_t addr, uint8_t *data, uint8_t len)
{
    // Send [CMD_READ, ADDR_H, ADDR_L, NOP], read 'len' bytes
    uint8_t tx_header[4];
    tx_header[0] = SX1262_CMD_READ_REGISTER;
    tx_header[1] = (addr >> 8) & 0xFF;
    tx_header[2] = addr & 0xFF;
    tx_header[3] = 0x00; // NOP
    
    // Header is 4 bytes long
    return sx1262_spi_read_general(tx_header, 4, data, len);
}


/**
 * @brief General, flexible SPI write function
 * Sends a variable-length header, followed by optional data.
 * This function implements the correct "wait-transmit-wait" cycle.
 *
 * @param tx_header         Buffer with the command/address bytes to be sent
 * @param tx_header_len     Number of bytes in tx_header
 * @param data              Optional data sent after the header
 * @param data_len          Number of optional data bytes
 * @return esp_err_t 
 */
static esp_err_t sx1262_spi_write_general(uint8_t *tx_header, uint8_t tx_header_len, uint8_t *data, uint8_t data_len, bool wait_after)
{
    // Wait until the chip is ready for a command
    sx1262_wait_on_busy();

    // Total transaction size
    uint8_t total_len = tx_header_len + data_len;

    // We need a single, contiguous buffer for SPI
    uint8_t tx_buffer[total_len];

    // Copy header into the buffer
    memcpy(tx_buffer, tx_header, tx_header_len);
    // Copy optional data into the buffer
    if (data != NULL && data_len > 0)
        memcpy(&tx_buffer[tx_header_len], data, data_len);

    spi_transaction_t trans = {
        .length    = total_len * 8,
        .tx_buffer = tx_buffer,
        .rx_buffer = NULL,
    };

    esp_err_t ret = spi_device_transmit(spi_handle, &trans);

     // Wait until the chip has finished executing the command
    if (wait_after)
        sx1262_wait_on_busy();

    return ret;
}



/**
 * @brief General, flexible SPI read function
 * Sends a variable-length header (e.g., [CMD] or [CMD, OFFSET] or [CMD, ADDR_H, ADDR_L])
 * followed by NOPs to receive 'rx_len' bytes.
 *
 * @param tx_header         Buffer with the command/address bytes to be sent
 * @param tx_header_len     Number of bytes in tx_header
 * @param rx_data           Destination buffer for the read data
 * @param rx_len            Number of data bytes to read
 * @return esp_err_t 
 */
static esp_err_t sx1262_spi_read_general(uint8_t *tx_header, uint8_t tx_header_len, uint8_t *rx_data, uint8_t rx_len)
{
    sx1262_wait_on_busy();
    
    // We need a transaction buffer that can hold the header + read part
    // The ESP-IDF SPI drivers require buffers suitable for DMA.
    // A static buffer is often problematic here. It is better to
    // allocate tx_buffer and rx_buffer dynamically on the stack.
    
    uint8_t tx_buffer[tx_header_len + rx_len];
    uint8_t rx_buffer[tx_header_len + rx_len];

    // 1. Copy send header
    memcpy(tx_buffer, tx_header, tx_header_len);
    
    // 2. Fill the rest of the send buffer with NOPs (optional, 0x00 is default)
    // memset(&tx_buffer[tx_header_len], 0x00, rx_len);
    
    spi_transaction_t trans = {
        .length = (tx_header_len + rx_len) * 8, // Total length in bits
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer
    };
    
    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    
    if (ret == ESP_OK && rx_data != NULL && rx_len > 0) {
        // The read data starts *after* the header part
        memcpy(rx_data, &rx_buffer[tx_header_len], rx_len);
    }
    
    return ret;
}


static esp_err_t sx1262_set_dio_irq_params(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask)
{
    uint8_t irq_params[8];
    irq_params[0] = (irq_mask >> 8) & 0xFF;
    irq_params[1] = irq_mask & 0xFF;
    irq_params[2] = (dio1_mask >> 8) & 0xFF;
    irq_params[3] = dio1_mask & 0xFF;
    irq_params[4] = (dio2_mask >> 8) & 0xFF;
    irq_params[5] = dio2_mask & 0xFF;
    irq_params[6] = (dio3_mask >> 8) & 0xFF;
    irq_params[7] = dio3_mask & 0xFF;
    
    return sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_params, 8);
}

static esp_err_t sx1262_clear_irq_status(uint16_t irq_mask)
{
    uint8_t irq_params[2];
    irq_params[0] = (irq_mask >> 8) & 0xFF;
    irq_params[1] = irq_mask & 0xFF;
    
    return sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS, irq_params, 2);
}

static uint16_t sx1262_get_irq_status(void)
{
    uint8_t irq_status[2];
    esp_err_t ret = sx1262_read_command(SX1262_CMD_GET_IRQ_STATUS, irq_status, 2);
    
    if (ret != ESP_OK) {
        return 0;
    }
    
    return ((uint16_t)irq_status[0] << 8) | irq_status[1];
}