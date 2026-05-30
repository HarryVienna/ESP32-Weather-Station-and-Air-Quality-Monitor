#ifndef SX1262_H
#define SX1262_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include <stdint.h>
#include <stdbool.h>

// Pin definitions for Heltec WiFi LoRa 32 V3.2
#define LORA_PIN_MISO       11
#define LORA_PIN_MOSI       10
#define LORA_PIN_SCK        9
#define LORA_PIN_NSS        8
#define LORA_PIN_RST        12
#define LORA_PIN_BUSY       13
#define LORA_PIN_DIO1       14

// SX1262 Commands
#define SX1262_CMD_SET_SLEEP                0x84
#define SX1262_CMD_SET_STANDBY              0x80
#define SX1262_CMD_SET_FS                   0xC1
#define SX1262_CMD_SET_TX                   0x83
#define SX1262_CMD_SET_RX                   0x82
#define SX1262_CMD_STOP_TIMER_ON_PREAMBLE   0x9F
#define SX1262_CMD_SET_RX_DUTY_CYCLE        0x94
#define SX1262_CMD_SET_CAD                 0xC5
#define SX1262_CMD_SET_TX_CONTINUOUS_WAVE   0xD1
#define SX1262_CMD_SET_TX_INFINITE_PREAMBLE 0xD2
#define SX1262_CMD_SET_REGULATOR_MODE       0x96
#define SX1262_CMD_CALIBRATE                0x89
#define SX1262_CMD_CALIBRATE_IMAGE          0x98
#define SX1262_CMD_SET_PA_CONFIG            0x95
#define SX1262_CMD_SET_RX_TX_FALLBACK_MODE   0x93

// Configuration commands
#define SX1262_CMD_WRITE_REGISTER           0x0D
#define SX1262_CMD_READ_REGISTER            0x1D
#define SX1262_CMD_WRITE_BUFFER             0x0E
#define SX1262_CMD_READ_BUFFER              0x1E

// Communication commands
#define SX1262_CMD_GET_STATUS               0xC0
#define SX1262_CMD_GET_RX_BUFFER_STATUS     0x13
#define SX1262_CMD_GET_PACKET_STATUS        0x14
#define SX1262_CMD_GET_RSSI_INST            0x15
#define SX1262_CMD_GET_STATS                0x10
#define SX1262_CMD_RESET_STATS              0x00

// DIO and IRQ commands
#define SX1262_CMD_SET_DIO_IRQ_PARAMS       0x08
#define SX1262_CMD_GET_IRQ_STATUS           0x12
#define SX1262_CMD_CLR_IRQ_STATUS           0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SWITCH    0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL    0x97

// RF Modulation and Packet commands
#define SX1262_CMD_SET_RF_FREQUENCY         0x86
#define SX1262_CMD_SET_PACKET_TYPE          0x8A
#define SX1262_CMD_GET_PACKET_TYPE          0x11
#define SX1262_CMD_SET_TX_PARAMS            0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS    0x8B
#define SX1262_CMD_SET_PACKET_PARAMS        0x8C
#define SX1262_CMD_SET_CAD_PARAMS           0x88
#define SX1262_CMD_SET_BUFFER_BASE_ADDRESS  0x8F
#define SX1262_CMD_SET_LORA_SYMB_NUM_TIMEOUT 0xA0

// IRQ Flags
#define SX1262_IRQ_TX_DONE                  0x0001
#define SX1262_IRQ_RX_DONE                  0x0002
#define SX1262_IRQ_PREAMBLE_DETECTED        0x0004
#define SX1262_IRQ_SYNC_WORD_VALID          0x0008
#define SX1262_IRQ_HEADER_VALID             0x0010
#define SX1262_IRQ_HEADER_ERROR             0x0020
#define SX1262_IRQ_CRC_ERROR                0x0040
#define SX1262_IRQ_CAD_DONE                 0x0080
#define SX1262_IRQ_CAD_DETECTED             0x0100
#define SX1262_IRQ_TIMEOUT                  0x0200

// Packet Types
#define SX1262_PACKET_TYPE_GFSK             0x00
#define SX1262_PACKET_TYPE_LORA             0x01

// Register Addresses
#define SX1262_REG_IQ_POLARITY_SETUP        0x0736
#define SX1262_REG_LORA_SYNC_WORD_MSB       0x0740
#define SX1262_REG_LORA_SYNC_WORD_LSB       0x0741
#define SX1262_REG_RANDOM_NUMBER_GEN        0x0819
#define SX1262_REG_TX_MODULATION            0x0889
#define SX1262_REG_RX_GAIN                  0x08AC
#define SX1262_REG_TX_CLAMP_CFG             0x08D8
#define SX1262_REG_OCP_CONFIGURATION        0x08E7
#define SX1262_REG_XTA_TRIM                 0x0911
#define SX1262_REG_XTB_TRIM                 0x0912


// Bandwidths
typedef enum {
    LORA_BW_7_8 = 0x00,
    LORA_BW_10_4 = 0x08,
    LORA_BW_15_6 = 0x01,
    LORA_BW_20_8 = 0x09,
    LORA_BW_31_25 = 0x02,
    LORA_BW_41_7 = 0x0A,
    LORA_BW_62_5 = 0x03,
    LORA_BW_125 = 0x04,
    LORA_BW_250 = 0x05,
    LORA_BW_500 = 0x06
} sx1262_bandwidth_t;

// Coding Rates
typedef enum {
    LORA_CR_4_5 = 0x01,
    LORA_CR_4_6 = 0x02,
    LORA_CR_4_7 = 0x03,
    LORA_CR_4_8 = 0x04
} sx1262_coding_rate_t;

// Modem Mode
typedef enum {
    SX1262_MODEM_FSK = 0x00,
    SX1262_MODEM_LORA = 0x01
} sx1262_modem_mode_t;

// FSK Bitrate
typedef enum {
    FSK_BR_4800 = 4800,
    FSK_BR_9600 = 9600,
    FSK_BR_19200 = 19200,
    FSK_BR_38400 = 38400,
    FSK_BR_50000 = 50000,
    FSK_BR_100000 = 100000,
    FSK_BR_150000 = 150000,
    FSK_BR_300000 = 300000
} sx1262_fsk_bitrate_t;

// FSK Modulation Shaping
typedef enum {
    FSK_MOD_SHAPING_OFF = 0x00,
    FSK_MOD_SHAPING_BT_0_3 = 0x08,
    FSK_MOD_SHAPING_BT_0_5 = 0x09,
    FSK_MOD_SHAPING_BT_0_7 = 0x0A,
    FSK_MOD_SHAPING_BT_1_0 = 0x0B
} sx1262_fsk_mod_shaping_t;

// FSK RX Bandwidth
typedef enum {
    FSK_RX_BW_4800 = 0x1F,
    FSK_RX_BW_5800 = 0x17,
    FSK_RX_BW_7300 = 0x0F,
    FSK_RX_BW_9700 = 0x1E,
    FSK_RX_BW_11700 = 0x16,
    FSK_RX_BW_14600 = 0x0E,
    FSK_RX_BW_19500 = 0x1D,
    FSK_RX_BW_23400 = 0x15,
    FSK_RX_BW_29300 = 0x0D,
    FSK_RX_BW_39000 = 0x1C,
    FSK_RX_BW_46900 = 0x14,
    FSK_RX_BW_58600 = 0x0C,
    FSK_RX_BW_78200 = 0x1B,
    FSK_RX_BW_93800 = 0x13,
    FSK_RX_BW_117300 = 0x0B,
    FSK_RX_BW_156200 = 0x1A,
    FSK_RX_BW_187200 = 0x12,
    FSK_RX_BW_234300 = 0x0A,
    FSK_RX_BW_312000 = 0x19,
    FSK_RX_BW_373600 = 0x11,
    FSK_RX_BW_467000 = 0x09
} sx1262_fsk_rx_bw_t;

// Configuration structure
typedef struct {
    sx1262_modem_mode_t modem_mode; // LoRa or FSK
    uint32_t frequency;             // Frequency in Hz
    int8_t tx_power;                // TX Power in dBm
    
    // LoRa specific parameters
    sx1262_bandwidth_t bandwidth;     // Bandwidth (LoRa only)
    uint8_t spreading_factor;       // Spreading Factor 5-12 (LoRa only)
    sx1262_coding_rate_t coding_rate;// Coding Rate (LoRa only)
    bool iq_inverted;               // IQ inverted (LoRa only)
    bool rx_gain_boosted;           // true = Boosted RX Gain (+3dB sensitivity)
    
    // FSK specific parameters
    uint32_t fsk_bitrate;           // Bitrate in bps (FSK only)
    uint32_t fsk_fdev;              // Frequency deviation in Hz (FSK only)
    sx1262_fsk_rx_bw_t fsk_rx_bw;   // RX Bandwidth (FSK only)
    sx1262_fsk_mod_shaping_t fsk_shaping; // Pulse shaping (FSK only)
    
    // Common parameters
    uint16_t preamble_length;       // Preamble length
    uint8_t payload_length;         // Payload length (0 = variable)
    bool crc_on;                    // CRC enabled
    uint16_t sync_word;             // LoRa Sync Word (0x1424 = public, 0x3444 = private, 0 = default)
} sx1262_config_t;

typedef struct {
    int16_t rssi_pkt;      // RSSI of the packet in dBm
    float snr_pkt;         // SNR in dB
    int16_t signal_rssi;   // Signal RSSI in dBm
} sx1262_packet_status_t;


// Callback function type for async receive
typedef void (*sx1262_rx_callback_t)(uint8_t *data, uint8_t len, sx1262_packet_status_t *status);

// Function declarations

// ============================================================================
// PHASE 1: HARDWARE INITIALIZATION (once at startup)
// ============================================================================

/**
 * @brief Initialize the ESP32 SPI bus and GPIOs without resetting the radio.
 *
 * @details
 * Performs the hardware abstraction layer (HAL) initialization required for the 
 * ESP32 to communicate with the SX1262:
 * - configures GPIO pins (NSS, BUSY, DIO1) and the RESET pin (kept HIGH),
 * - initializes the SPI bus driver and adds the device.
 * * Unlike sx1262_init_radio_cold(), this function **DOES NOT** toggle the NRESET pin.
 * It is intended to be called after waking up from ESP32 Deep Sleep to restore
 * the SPI handle while preserving the SX1262's internal state (retention memory).
 *
 * @return
 * - ESP_OK: SPI bus and GPIOs successfully initialized.
 * - ESP_FAIL: SPI bus initialization failed (e.g., invalid pin map).
 * - ESP_ERR_INVALID_STATE: Driver already initialized.
 *
 * @note
 * - This function MUST be called first in `app_main`, regardless of whether a 
 * Cold Start or Warm Start follows.
 * - **NOT thread-safe**: Caller must ensure exclusive access.
 */
esp_err_t sx1262_init_bus(void);

/**
 * @brief Perform a full "Cold Start" initialization of the SX1262 radio.
 *
 * @details
 * This function performs a hard reset and full re-configuration sequence. 
 * It is intended for power-on (cold boot) or as a fallback if warm start fails.
 * Steps performed:
 * - Toggles NRESET pin (Hard Reset) to clear all internal radio registers,
 * - Configures DIO3 for TCXO supply and DIO2 for RF Switch,
 * - Performs internal RC and PLL calibration,
 * - Sets the radio to STDBY_XOSC mode with optimal regulator settings.
 *
 * @pre sx1262_init_bus() must have been called successfully.
 *
 * @return
 * - ESP_OK: Radio successfully reset, calibrated, and ready for configuration.
 * - ESP_ERR_TIMEOUT: Radio did not release the BUSY line (hardware failure).
 * - ESP_ERR_INVALID_STATE: Bus not initialized.
 *
 * @note
 * - Calling this function wipes all previous radio configurations (frequency, LoRa params).
 * - This operation takes several milliseconds (due to calibration and TCXO startup).
 */
esp_err_t sx1262_init_radio(void);

/**
 * @brief Wake up the SX1262 from sleep using retention memory (Warm Start).
 *
 * @details
 * Attempts to wake the radio from Sleep mode without performing a reset.
 * This function:
 * - Toggles the NSS pin low to trigger the SX1262 wakeup sequence,
 * - Waits for the TCXO to stabilize (BUSY low),
 * - Verifies if the retention memory is intact by checking the Packet Type.
 *
 * This is significantly faster and more energy-efficient than a cold start
 * as it skips calibration and configuration.
 *
 * @pre 
 * - sx1262_init_bus() must have been called.
 * - The radio must have been put to sleep previously using sx1262_sleep() 
 * (which uses the retention parameter).
 *
 * @return
 * - ESP_OK: Wakeup successful, configuration retained. Ready to send/receive.
 * - ESP_FAIL: Retention check failed (e.g. power loss occurred). 
 * The application SHOULD fall back to sx1262_init_radio_cold().
 * - ESP_ERR_TIMEOUT: Radio did not wake up (BUSY stuck high).
 */
esp_err_t sx1262_wakeup(void);

// ============================================================================
// PHASE 2: LORA CONFIGURATION (callable any time)
// ============================================================================

/**
 * @brief Configure SX1262 radio driver with the supplied parameters.
 *
 * Apply the requested radio configuration to the SX1262 device. The function
 * validates the provided configuration values, programs the device via the
 * underlying bus (e.g. SPI), and updates driver state to reflect the new
 * settings.
 *
 * @param config Pointer to a sx1262_config_t structure that contains the
 *               desired radio parameters (frequency, output power, bandwidth,
 *               spreading factor, coding rate, preamble length, sync word,
 *               packet format, IQ inversion, and other device-specific
 *               options). The pointer must be non-NULL and remain valid for
 *               the duration of this call.
 *
 * @return
 * - ESP_OK: Configuration was successfully applied.
 * - ESP_ERR_INVALID_ARG: The provided config pointer is NULL or contains
 *   parameter values outside the supported ranges.
 * - ESP_ERR_INVALID_STATE: The driver or device is not in a state that allows
 *   configuration (e.g. not initialized or already in a critical operation).
 * - ESP_FAIL: A hardware communication error occurred while programming the
 *   device (e.g. SPI transfer failure, device not responding).
 *
 * @note
 * - This function is blocking and will perform the necessary synchronous
 *   bus transactions to program the SX1262. It may take a short time to
 *   complete depending on the operations required.
 * - The implementation may temporarily change the radio mode (for example,
 *   place the device in standby) in order to apply certain settings.
 * - **NOT thread-safe**: Caller must ensure exclusive access to the radio
 *   while calling this function and any concurrent sx1262_* operations.
 *   Use external synchronization (mutex, task serialization) if needed.
 * - Invalid or unsupported field combinations in the configuration may be
 *   rejected even if individual values are within range.
 *
 * @see sx1262_config_t
 */
esp_err_t sx1262_configure(const sx1262_config_t *config);

// ============================================================================
// PHASE 3: COMMUNICATION
// ============================================================================

/**
 * @brief Transmit a raw payload via the SX1262 radio.
 *
 * Transmits up to 'len' bytes from the buffer pointed to by 'data' using the
 * SX1262 LoRa radio. The function initiates the transmission and blocks until
 * the radio reports completion or an error occurs.
 *
 * @param[in] data Pointer to the buffer containing the bytes to send. Must be
 *                 non-NULL if len > 0.
 * @param[in] len  Number of bytes to transmit. Maximum payload size is 255 bytes.
 *
 * @return
 *  - ESP_OK on successful transmission.
 *  - ESP_ERR_INVALID_ARG if 'data' is NULL while 'len' > 0, if 'len' is zero, or if 'len' > 255.
 *  - ESP_ERR_INVALID_STATE if the SX1262 driver/module has not been initialized.
 *  - ESP_ERR_TIMEOUT if the transmission timed out (5 second hard timeout).
 *  - ESP_FAIL for other hardware or protocol errors.
 *
 * @note
 *  - The caller is responsible for ensuring the SX1262 has been initialized
 *    before calling this function.
 *  - This function may block while the radio performs the transmission and
 *    typically uses SPI/GPIO; do not call from an ISR.
 *  - **NOT thread-safe**: Serialize concurrent calls (e.g., with a mutex) or
 *    ensure exclusive access to the radio as the implementation is not thread-safe.
 *  - The function copies the provided buffer internally; callers may modify
 *    or free 'data' immediately after this function returns.
 */
esp_err_t sx1262_send(uint8_t *data, uint8_t len);

/**
 * @brief Receive a LoRa packet from the SX1262 radio.
 *
 * Blocks until a packet is received or the specified timeout elapses. The
 * received payload is copied into the buffer pointed to by @p data.
 *
 * @param[out] data Pointer to a buffer that will receive the payload bytes. Must be non-NULL.
 * @param[in,out] len On input: pointer to buffer size (maximum number of bytes that can be written).
 *                    On output: number of bytes actually written into @p data.
 *                    Must be non-NULL.
 * @param[in] timeout_ms Maximum time to wait for a packet, in milliseconds.
 *                       - If zero: Continuous RX mode with 60-second hard timeout.
 *                       - If non-zero: RX with specified timeout plus 1 second margin.
 *
 * @return ESP_OK          Packet received and copied to @p data; @p len updated with received length.
 * @return ESP_ERR_TIMEOUT No packet received before timeout elapsed or timeout occurred.
 * @return ESP_ERR_INVALID_ARG @p data or @p len pointers are NULL.
 * @return ESP_ERR_CRC_ERROR Received packet has CRC error.
 * @return ESP_FAIL        Radio/hardware error or other unrecoverable condition.
 *
 * @note
 *  - Caller must provide a buffer large enough for the incoming packet. There is NO buffer
 *    overflow protection; if the received packet exceeds the buffer size, data will be truncated.
 *  - This function is intended for task context and is NOT safe for use in interrupt handlers.
 *  - **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 *  - Maximum packet size is 255 bytes (SX1262 hardware limit).
 */
esp_err_t sx1262_receive(uint8_t *data, uint8_t *len, uint32_t timeout_ms);

/**
 * @brief Start asynchronous packet reception with background task.
 *
 * Configures the SX1262 to receive packets in continuous mode.
 * An internal FreeRTOS task is started which waits for the DIO1 interrupt.
 * When a packet is received, the task reads the data via SPI and invokes 
 * the provided callback function.
 *
 * @param[in] callback Function to call when a packet is received. The callback receives:
 * - data: Pointer to received packet data (buffer is reused, copy if needed!)
 * - len: Length of received packet in bytes
 * - status: Pointer to struct containing RSSI (dBm) and SNR (dB)
 * Must be non-NULL.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if callback is NULL
 * @return ESP_ERR_INVALID_STATE if driver not initialized or RX already running
 * @return ESP_FAIL on hardware/configuration error
 *
 * @note The callback runs in a **High Priority Task context**, NOT in an ISR. 
 * You can safely use blocking functions (like logging or other SPI calls), 
 * but keep it efficient to not block the next packet reception.
 * @note Call sx1262_stop_receive_async() to stop reception and kill the task.
 * @note Only one callback can be active at a time.
 */
esp_err_t sx1262_start_receive_async(sx1262_rx_callback_t callback);

/**
 * @brief Stops the interrupt-driven receive mode.
 *
 * Disables the DIO1 interrupt, deletes the background FreeRTOS task responsible
 * for processing incoming packets, and puts the SX1262 radio into Standby mode.
 *
 * @note This function is safe to call even if the interrupt mode is not currently active.
 * @note After calling this, the radio will be in STDBY_XOSC mode. You must configure
 * it again (e.g., call sx1262_receive or sx1262_start_receive_async) to
 * resume reception.
 * @note **NOT thread-safe**: Ensure exclusive access to the radio when calling this.
 */
void sx1262_stop_receive_async(void);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Put the SX1262 radio into low-power sleep mode.
 *
 * @details
 * Sends the appropriate command to the SX1262 to transition the radio into
 * sleep (low-power) mode. While sleeping, the radio will not process normal
 * radio operations or respond to typical commands until it is woken up. After
 * waking, some radio state or volatile registers may need to be reconfigured
 * before normal operation can resume.
 *
 * This function blocks until the sleep command has been accepted by the
 * device or an error/timeout occurs.
 *
 * @return
 * @retval ESP_OK The sleep command was successfully issued and acknowledged.
 * @retval ESP_ERR_INVALID_STATE The driver or device is not in a state where
 *         sleep can be entered (e.g., not initialized or already in an invalid mode).
 * @retval ESP_ERR_TIMEOUT A timeout occurred waiting for the device to acknowledge the command.
 * @retval ESP_FAIL Communication with the device failed or an unspecified error occurred.
 *
 * @note After putting the SX1262 to sleep, the radio must be brought back to
 * active mode (e.g., via sx1262_standby() or similar) and may require reconfiguration
 * of volatile radio parameters before attempting radio operations.
 * @note **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 */
esp_err_t sx1262_sleep(void);

/**
 * @brief Put the SX1262 transceiver into standby mode.
 *
 * Transition the radio to a low-power standby state (STDBY_XOSC) while preserving its
 * configuration and registers. This function sends the appropriate command
 * to the device over the configured SPI bus.
 *
 * @return esp_err_t
 *         - ESP_OK: The command was accepted and the device entered standby.
 *         - ESP_ERR_TIMEOUT: No response from the device or operation timed out.
 *         - ESP_FAIL: Communication with the device failed or an unexpected error occurred.
 *
 * @note If the radio is performing a time-critical operation (e.g. TX/RX),
 *       calling this function may abort that operation. Ensure it is safe to
 *       move the device to standby before calling.
 * @note **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 */
esp_err_t sx1262_standby(void);

/**
 * @brief Retrieve the current RSSI (Received Signal Strength Indicator) from the SX1262.
 *
 * Reads the radio's RSSI measurement and returns it as a signed 16-bit value representing
 * the signal power in dBm. Typical values are negative (for example, -120 .. 0 dBm) and
 * indicate the received signal strength.
 *
 * Preconditions:
 *  - The SX1262 must be initialized.
 *  - RSSI is meaningful when the radio is in receive mode or immediately after a packet has
 *    been received; calling this while the radio is inactive may return an undefined value.
 *  - Returns -999 on error (e.g., if read fails).
 *
 * @return int16_t
 *   RSSI in dBm (signed 16-bit) representing decibels relative to one milliwatt.
 *   Special value: -999 indicates an error or unavailable measurement.
 *
 * @note This function should not change the radio operating state.
 * @note **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 */
int16_t sx1262_get_rssi(void);

/**
 * @brief Get the packet status from the SX1262 LoRa module
 * 
 * Retrieves the status information of the last received or transmitted packet,
 * including signal strength (RSSI), signal-to-noise ratio (SNR), and other
 * relevant packet metrics.
 * 
 * @param[out] status Pointer to sx1262_packet_status_t structure where the
 *                    packet status will be stored. Must be non-NULL.
 * 
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if status pointer is NULL
 * @return ESP_FAIL on communication error or other failure
 * 
 * @note The status structure should be allocated by the caller.
 * @note **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 */
esp_err_t sx1262_get_packet_status(sx1262_packet_status_t *status);

/**
 * @brief Retrieve SX1262 transceiver chip information.
 *
 * Query the attached SX1262 radio over the driver's transport (e.g. SPI)
 * to read identification, status registers, and configuration. The retrieved
 * information is logged to the console via ESP_LOGI. This is primarily a
 * diagnostic/debug function.
 *
 * This function takes no parameters and must be called after the SX1262
 * driver has been initialized. It may perform blocking I/O and therefore
 * should not be called from an interrupt context.
 *
 * @note Calling this function when the device or driver is uninitialized
 * may return an error. Output is sent to the ESP logging system.
 * @note **NOT thread-safe**: Serialize concurrent calls or ensure exclusive access to the radio.
 *
 * @return esp_err_t
 * @retval ESP_OK               Chip information successfully read and displayed
 * @retval ESP_ERR_TIMEOUT      Communication with the SX1262 timed out
 * @retval ESP_ERR_INVALID_STATE Driver or device is not initialized
 * @retval ESP_FAIL             Generic failure (communication/protocol error)
 */
esp_err_t sx1262_get_chip_info(void);

#endif // SX1262_H