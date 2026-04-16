#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "bme280_sensor_driver.h"

#define NVS_NAMESPACE "SENSOR"
#define MAC_VALUE "MAC"
#define CHANNEL_VALUE "CHANNEL"

#define ESPNOW_MAXDELAY 200
#define ESPNOW_QUEUE_SIZE 16
#define ESPNOW_SEND_SUCCESSFUL_BIT BIT0
#define ESPNOW_SEND_FAIL_BIT BIT1

#define ESPNOW_FIXED_CHANNEL 13

#define NO_OF_SAMPLES 16 

#define LED GPIO_NUM_2

#define SENSOR_NR_2 GPIO_NUM_25
#define SENSOR_NR_1 GPIO_NUM_33
#define SENSOR_NR_0 GPIO_NUM_32

#define ADC_CHANNEL ADC_CHANNEL_7
#define ADC_UNIT ADC_UNIT_1
#define ADC_ATTEN ADC_ATTEN_DB_12

#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ 100000

#define SENSOR_SLEEPTIME 600

enum MessageType {
    PAIRING_REQ,
    PAIRING_RESP,
    DATA,
};

enum SensorType {
    SENSOR_TYPE_BME280 = 0,
    SENSOR_TYPE_HDC1080,
    SENSOR_TYPE_DHT22,
    SENSOR_TYPE_CUSTOM,
};

RTC_DATA_ATTR static int boot_count = 0;

static const char* TAG = "main";

static uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static i2c_master_bus_handle_t i2c_master_bus = NULL;

static QueueHandle_t s_espnow_queue;
static EventGroupHandle_t s_espnow_event_group;

typedef struct __attribute__((packed)) struct_data {
    uint8_t msg_type;
    uint8_t sensor_nr;
    uint8_t sensor_type;
    uint32_t voltage;
    float pressure;
    float temperature;
    float humidity;
} struct_data;

typedef struct __attribute__((packed)) struct_pairing_response {
    uint8_t msg_type;       // 1 byte
    uint8_t sensor_nr;      // 1 byte
    uint8_t macAddr[ESP_NOW_ETH_ALEN];  // 6 bytes
    uint8_t channel;        // 1 byte
} struct_pairing_response;

typedef struct __attribute__((packed)) struct_pairing_request {
    uint8_t msg_type;
    uint8_t sensor_nr;
} struct_pairing_request;


/**
* @brief Blink an LED connected to a GPIO pin
* 
* This function blinks an LED connected to the specified GPIO pin.
*
* @note This function assumes that an LED is connected to GPIO pin GPIO_NUM_2.
*/
static void blink() {
    gpio_reset_pin(LED);
    gpio_set_pull_mode(LED, GPIO_PULLDOWN_ONLY);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(LED, 0);
    gpio_set_direction(LED, GPIO_MODE_INPUT);
}

/**
* @brief Get sensor number from GPIO pins
*
* This function reads the sensor number from the specified GPIO pins and stores the result in the provided
* pointer to a uint8_t variable.
* @param[in,out] nr Pointer to a uint8_t variable to store the sensor number
*
* @return esp_err_t Returns ESP_OK on success, otherwise an error code indicating the cause of failure
*
* @note This function assumes that the sensor number is encoded on three GPIO pins, SENSOR_NR_0, SENSOR_NR_1, and SENSOR_NR_2.
  The sensor number is encoded as a 3-bit binary value, with the least significant bit on SENSOR_NR_0 and the most
  significant bit on SENSOR_NR_2.
*/
static esp_err_t get_sensor_number(uint8_t *nr) 
{
    gpio_set_direction(SENSOR_NR_0, GPIO_MODE_INPUT);   
    gpio_set_direction(SENSOR_NR_1, GPIO_MODE_INPUT);  
    gpio_set_direction(SENSOR_NR_2, GPIO_MODE_INPUT);  

    gpio_set_pull_mode(SENSOR_NR_0, GPIO_PULLUP_PULLDOWN);
    gpio_set_pull_mode(SENSOR_NR_1, GPIO_PULLUP_PULLDOWN);
    gpio_set_pull_mode(SENSOR_NR_2, GPIO_PULLUP_PULLDOWN);

    int bit_0 = gpio_get_level(SENSOR_NR_0);
    int bit_1 = gpio_get_level(SENSOR_NR_1);
    int bit_2 = gpio_get_level(SENSOR_NR_2);

    *nr = bit_2 << 2 | bit_1 << 1 | bit_0;

    return ESP_OK;
}

/**
*
* @brief Get voltage value from ADC reading
*
* This function reads the voltage from the specified ADC channel and calculates the actual voltage value
* using a voltage divider of 2.2:4.7. The voltage value is stored in the provided pointer to a uint32_t variable.
*
* @param[in,out] voltage Pointer to a uint32_t variable to store the calculated voltage value
*
* @return esp_err_t Returns ESP_OK on success, otherwise an error code indicating the cause of failure
*/
static esp_err_t get_voltage(uint32_t *voltage) 
{
    static int adc_raw;

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    adc_cali_handle_t adc1_cali_chan0_handle = NULL;

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_chan0_handle));

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT + 1, ADC_CHANNEL, adc_raw);
  
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, (int*)voltage));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %lu mV", ADC_UNIT + 1, ADC_CHANNEL, *voltage);

    *voltage = *voltage * ((4.7 + 2.2) / 4.7); // Voltage divider is 2.2 : 4.7
    ESP_LOGI(TAG, "ADC%d Channel[%d] Korr Voltage: %lu mV", ADC_UNIT + 1, ADC_CHANNEL, *voltage);

    return ESP_OK;
}

/**
 * @brief Initialize I2C communication using given configuration
 *
 * This function initializes the I2C communication by setting the I2C parameters and creating the I2C master bus
 * for the specified I2C peripheral number. The configuration is passed as a pointer to an i2c_master_bus_config_t structure.
 *
 * @param[in] bus_config Pointer to an i2c_master_bus_config_t structure containing the I2C bus configuration
 *
 * @return None
 */
static void i2c_init(i2c_master_bus_config_t *bus_config)
{
	ESP_ERROR_CHECK(i2c_new_master_bus(bus_config, &i2c_master_bus));
}

/**
* @brief Initialize the Non-Volatile Storage (NVS) module
*
* This function initializes the ESP32 Non-Volatile Storage (NVS) module.
* If the NVS module has already been initialized and there are no free pages or a new version has been found,
* the function will erase the existing data and reinitialize the module.
*
* @note This function should be called before using the NVS module.
*
* @note If the NVS module has already been initialized and there are no issues, this function does nothing.
*/
static void nvs_init(void)
{
    //nvs_flash_erase(); // JUST FOR TEST

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

/**
* @brief Initialize the Wi-Fi module in station mode
*
* This function initializes the ESP32 Wi-Fi module in station mode, 
* which allows the device to connect to an existing Wi-Fi network.
*
* @note This function should be called before using any Wi-Fi related functions.
*/
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
    ESP_ERROR_CHECK( esp_wifi_set_max_tx_power(84) );
}


/**
* @brief Callback function to handle ESP-NOW send events.
*
* This function is used as a callback function to handle ESP-NOW send events.
* It sets bits in the event group based on the status of the send operation.
*
* @param mac_addr The MAC address of the recipient device.
* @param status The status of the send operation.
*          ESP_NOW_SEND_SUCCESS if the send operation was successful.
*          ESP_NOW_SEND_FAIL if the send operation failed.
* @return void
*/
static void espnow_send_cb(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        xEventGroupSetBits(s_espnow_event_group, ESPNOW_SEND_SUCCESSFUL_BIT);
    } else {
        xEventGroupSetBits(s_espnow_event_group, ESPNOW_SEND_FAIL_BIT);
    }
}

/**
* @brief ESP-NOW receive callback function.
*
* This function is called when ESP-NOW data is received by the ESP32.
* It handles the data received from the paired ESP32 device and forwards it to a queue.
*
* @param[in] mac_addr MAC address of the device that sent the data.
* @param[in] data The received data payload.
* @param[in] len Length of the received data payload.
* @return None
*/
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "esp-now receive callback");
    struct_pairing_response pairingResponse;

    uint8_t type = data[0];
    switch (type) {
    case PAIRING_RESP:    // pairing response from server
        memcpy(&pairingResponse, data, sizeof(struct_pairing_response));

        if (xQueueSend(s_espnow_queue, &pairingResponse, ESPNOW_MAXDELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Send send queue fail");
        }

        break;
  }
}


/**
* @brief Initialize ESP-NOW module
*
* This function initializes the ESP-NOW module by calling esp_now_init() and registering
* the send and receive callback functions.
* 
* @return None
*/
void espnow_init(void)
{
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );
}

/**
* @brief Read peer device MAC address and channel from NVS storage
*
* This function reads the MAC address and channel number of the paired device from the
* non-volatile storage (NVS).
*
* @param[in] mac_addr Pointer to the buffer where the MAC address will be stored.
*
* @param[in] chan Pointer to the variable where the channel number will be stored.
* 
* @return ESP_OK on success, ESP_FAIL otherwise.
*/
esp_err_t read_peer_nvs(uint8_t *mac_addr, uint8_t *chan) 
{
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return ESP_FAIL;
    }
    size_t required_size = ESP_NOW_ETH_ALEN;
    err = nvs_get_blob(my_handle, MAC_VALUE, mac_addr, &required_size);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return ESP_FAIL;
    }
    err = nvs_get_u8(my_handle, CHANNEL_VALUE, chan);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return ESP_FAIL;
    }

    nvs_close(my_handle);

    return ESP_OK;
}

/**
* @brief Store peer device MAC address and channel in NVS storage
*
* This function stores the MAC address and channel number of the paired device in the
* non-volatile storage (NVS).
*
* @param[in] mac_addr Pointer to the buffer containing the MAC address.
*
* @param[in] chan Channel number to be stored.
* 
* @return None.
*/
void store_peer_nvs(const uint8_t * mac_addr, uint8_t chan) 
{
    nvs_handle_t my_handle;

    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle));
    ESP_ERROR_CHECK(nvs_set_blob(my_handle, MAC_VALUE, mac_addr, ESP_NOW_ETH_ALEN));
    ESP_ERROR_CHECK(nvs_set_u8(my_handle, CHANNEL_VALUE, chan));
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    
    nvs_close(my_handle);
}

/**
 * @brief Delete peer device MAC address and channel from NVS storage
 *
 * This function erases the MAC address and channel number of the paired device from
 * the non-volatile storage (NVS).
 *
 * @return None.
 */
void delete_peer_nvs() 
{
    nvs_handle_t my_handle;

    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle));
    ESP_ERROR_CHECK(nvs_erase_key(my_handle, MAC_VALUE));
    ESP_ERROR_CHECK(nvs_erase_key(my_handle, CHANNEL_VALUE));
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    
    nvs_close(my_handle);
}

/**
 * @brief Add a new peer device to the ESP-NOW peer list
 *
 * This function adds a new peer device to the ESP-NOW peer list with the given MAC address
 * and channel number.
 *
 * @param[in] mac_addr Pointer to the buffer containing the MAC address of the peer device.
 * @param[in] chan Channel number to be used for communication with the peer device.
 *
 * @return None.
 */
void add_peer(const uint8_t * mac_addr, uint8_t chan){
  esp_now_peer_info_t peer;

  esp_now_del_peer(mac_addr);

  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.ifidx = ESP_IF_WIFI_STA;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

  ESP_ERROR_CHECK( esp_now_add_peer(&peer) );
}


/**
 * @brief Start the pairing process by sending a broadcast message and waiting for a response
 *
 * This function sends a broadcast message on a fixed channel and waits for a response from a sensor.
 * If a response is received, the function returns the MAC address and channel of the paired sensor.
 *
 * @param[in] mac_addr Pointer to a uint8_t array to store the MAC address of the paired sensor.
 * @param[in] chan Pointer to a uint8_t variable to store the channel of the paired sensor.
 * @param[in] sensor_nr Sensor number to match for pairing.
 *
 * @return - ESP_OK if pairing is successful and MAC address and channel are stored in mac_addr and chan, respectively.
 *         - ESP_FAIL if pairing is not successful.
 *
 * @note This function assumes that esp_now has been initialized and add_peer function has been called to add the broadcast MAC address.
 * @note This function uses a fixed channel defined by ESPNOW_FIXED_CHANNEL.
 */
esp_err_t start_pairing(uint8_t *mac_addr, uint8_t *chan, uint8_t sensor_nr)
{
    struct_pairing_request pair_req;
    struct_pairing_response pair_resp;

    pair_req.msg_type = PAIRING_REQ;
    pair_req.sensor_nr = sensor_nr;

    ESP_LOGI(TAG, "Sending broadcast to fixed channel: %d", ESPNOW_FIXED_CHANNEL);
    esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);

    add_peer(s_broadcast_mac, ESPNOW_FIXED_CHANNEL);

    ESP_ERROR_CHECK(esp_now_send(s_broadcast_mac, (uint8_t *) &pair_req, sizeof(struct_pairing_request)));

    if (xQueueReceive(s_espnow_queue, &pair_resp, ESPNOW_MAXDELAY / portTICK_PERIOD_MS) == pdTRUE) {
        ESP_LOGI(TAG, "Get Message");
        ESP_LOGI(TAG, "Received Sensor Nr: %d", pair_resp.sensor_nr);
        ESP_LOGI(TAG, "Received Channel: %d", pair_resp.channel);
        //ESP_LOGI(TAG, "Received MAC "MACSTR"", MAC2STR(pair_resp.macAddr));

        memcpy(mac_addr, pair_resp.macAddr, ESP_NOW_ETH_ALEN);
        *chan = pair_resp.channel;

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Pairing not successful");
    return ESP_FAIL;
}

/**
 * @brief Start the deep sleep mode for the specified duration
 *
 * This function configures the power domains to be turned off during deep sleep and puts the ESP32 into deep sleep for the specified duration.
 * Upon wake up, the program will restart.
 *
 * @return none
 *
 * @note This function assumes that ESP_LOGI and esp_sleep_pd_config functions are available.
 * @note SENSOR_SLEEPTIME is a macro defined in a header file and represents the duration of sleep time in seconds.
 */
void start_deep_sleep() 
{
    ESP_LOGI(TAG, "Start Deep Sleep");
    esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);

    // https://electronics.stackexchange.com/questions/530151/esp32-wroom32-consuming-77-%c2%b5a-much-too-high-in-deep-sleep
    gpio_reset_pin(SENSOR_NR_0);
    gpio_reset_pin(SENSOR_NR_1);
    gpio_reset_pin(SENSOR_NR_2);

    // https://www.esp32.com/viewtopic.php?t=3634
    // gpio_pad_select_gpio(GPIO_NUM_32);
    // gpio_set_direction(GPIO_NUM_32, GPIO_MODE_INPUT);
    // gpio_set_pull_mode(GPIO_NUM_32, GPIO_PULLUP_ONLY);

    // gpio_pad_select_gpio(GPIO_NUM_33);
    // gpio_set_direction(GPIO_NUM_33, GPIO_MODE_INPUT);
    // gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);

    esp_deep_sleep(1000000L * SENSOR_SLEEPTIME);
}


void app_main(){

    //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);

    esp_err_t ret;

    // ------- Blink LED -------
    blink();

    // ------- Read DIP switch value -------
    uint8_t sensor_nr;    
    ret = get_sensor_number(&sensor_nr);
    ESP_LOGI(TAG, "sensor: %d", sensor_nr);

    // ------- Read voltage value -------
    uint32_t voltage = 0;    
    ret = get_voltage(&voltage);
    ESP_LOGI(TAG, "voltage: %lu", voltage);


    // ------- Read sensors -------
    ESP_LOGI(TAG, "Initializing I2C bus...");
    gpio_reset_pin(SDA_PIN);
    gpio_reset_pin(SCL_PIN);

    i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = SCL_PIN,
        .sda_io_num = SDA_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
    };

    i2c_init(&i2c_config);
    ESP_LOGI(TAG, "I2C bus initialized successfully");

    // Set the I2C bus handle for the BME280 driver
    bme280_set_i2c_bus(i2c_master_bus);
    ESP_LOGI(TAG, "BME280 I2C bus handle set");

    sensor_data_t values_bme280;
    const sensor_driver_bme280_conf_t bme280_config = {
        .osr_p = BME280_OVERSAMPLING_1X,
        .osr_t = BME280_OVERSAMPLING_1X,
        .osr_h = BME280_OVERSAMPLING_1X,
        .filter = BME280_FILTER_COEFF_OFF,
        .dev_id = BME280_I2C_ADDR_PRIM
    };
    ESP_LOGI(TAG, "BME280 config: dev_id=0x%02x, osr_p=%d, osr_t=%d, osr_h=%d, filter=%d",
             bme280_config.dev_id, bme280_config.osr_p, bme280_config.osr_t, bme280_config.osr_h, bme280_config.filter);

    sensor_driver_t *bme280_driver = sensor_driver_new_bme280(&bme280_config);
    if (bme280_driver == NULL) {
        ESP_LOGE(TAG, "Failed to create BME280 driver");
    } else {
        ESP_LOGI(TAG, "BME280 driver created successfully");
    }

    ESP_LOGI(TAG, "Initializing BME280 sensor...");
    ret = sensor_driver_init_sensor(bme280_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME280 initialization failed with error: 0x%x", ret);
    } else {
        ESP_LOGI(TAG, "BME280 sensor initialized successfully");

        ESP_LOGI(TAG, "Reading BME280 sensor values...");
        ret = sensor_driver_read_values(bme280_driver, &values_bme280);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BME280 read failed with error: 0x%x", ret);
        } else {
            ESP_LOGI(TAG, "BME280 sensor read successful");
            ESP_LOGI(TAG, "bme280 %0.2f C / %.2f %% / %.2f hPa", values_bme280.temperature, values_bme280.humidity, values_bme280.pressure);
        }
    }

    // ------- Transmit data -------
    uint8_t peer_mac[ESP_NOW_ETH_ALEN];
    uint8_t chan;

    s_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(struct_pairing_response));
    if (s_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
    }
    s_espnow_event_group = xEventGroupCreate();
      if (s_espnow_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
    }

    nvs_init();
    wifi_init();
    espnow_init();

    // Try to read channel and MAC from NVS
    if (read_peer_nvs(peer_mac, &chan) != ESP_OK) {
        // If the values are not available, start a pairing
        if (start_pairing(peer_mac, &chan, sensor_nr) == ESP_OK) {
            store_peer_nvs(peer_mac, chan);
        }
        else {
            start_deep_sleep(); // Try later again
        }
    }

    ESP_LOGI(TAG, "NVS Channel: %d", chan);
    //ESP_LOGI(TAG, "NVS MAC "MACSTR"", MAC2STR(peer_mac));

    add_peer(peer_mac, chan);
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);

    // Create message to send
    struct_data msg;
    msg.msg_type = DATA;
    msg.sensor_nr = sensor_nr;
    msg.sensor_type = SENSOR_TYPE_BME280;
    msg.voltage = voltage;
    msg.temperature = values_bme280.temperature;
    msg.humidity = values_bme280.humidity;
    msg.pressure = values_bme280.pressure;

    ESP_ERROR_CHECK(esp_now_send(peer_mac, (uint8_t *) &msg, sizeof(struct_data)));

    EventBits_t bits = xEventGroupWaitBits(s_espnow_event_group,
            ESPNOW_SEND_SUCCESSFUL_BIT | ESPNOW_SEND_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            ESPNOW_MAXDELAY);
    if (bits & ESPNOW_SEND_SUCCESSFUL_BIT) {
        ESP_LOGI(TAG, "Message sent successful");
    } else if (bits & ESPNOW_SEND_FAIL_BIT) {
        ESP_LOGI(TAG, "Message sent failed");
        delete_peer_nvs(); // There was a problem. WIFI down? Channel changed? Whatever, delete pairing info and start again at the next wake up from deep sleep
    }      

    start_deep_sleep();
   
}
