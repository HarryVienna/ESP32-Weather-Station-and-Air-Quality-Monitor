#include <string.h>

#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Project modules
#include "sensor_stack.h"
#include "display/display.h"


#define MAC_STR_LEN 18
#define ESPNOW_FIXED_CHANNEL 13

static const char* TAG = "WIFI";

/* ============================================================================
 * ESP-NOW Packet Format
 *
 * Uses common packet_format.h (via sensor_stack.h)
 * Link metadata (RSSI/SNR/timestamp) is added by the receiver
 * ============================================================================ */

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

enum MessageType {
    PAIRING_REQ,
    PAIRING_RESP,
    DATA,
};

/**
 * @brief     Print MAC address to Serial monitor
 *
 * @param     mac_addr  Pointer to the MAC address array
 *
 * @details   Formats the MAC address provided as an array of uint8_t into a string.
 *            Prints the formatted MAC address to the Serial monitor.
 */
char* get_mac_string(const uint8_t *mac_addr, char *macStrBuffer) {
    snprintf(macStrBuffer, MAC_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return macStrBuffer;
}

/**
 * @brief     Add a peer for ESP-NOW communication
 *
 * @param     mac_addr  Pointer to the MAC address of the peer
 * @param     chan      Channel to communicate with the peer
 *
 * @details   Prepares and adds a peer for ESP-NOW communication.
 *            Deletes the existing peer with the provided MAC address if present.
 *            Initializes peer information, sets the channel and encryption settings,
 *            and adds the peer using ESP-NOW API.
 *
 * @note      The function will print an error message if adding the peer fails.
 */
void add_peer(const uint8_t * mac_addr, uint8_t chan){
  esp_now_peer_info_t peer;

  esp_now_del_peer(mac_addr);

  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.encrypt = false;
  peer.ifidx = ESP_IF_WIFI_STA;
  memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

  if (esp_now_add_peer(&peer) != ESP_OK){
    ESP_LOGE(TAG, "Failed to add peer");
  }
}

/**
 * @brief     Callback function for handling data transmission status
 *
 * @param     mac_addr  Pointer to the MAC address of the recipient
 * @param     status    Status of the data transmission (success or failure)
 *
 * @details   Prints the status of the last packet transmission to the specified MAC address.
 *            Displays whether the delivery was successful or failed via the Serial monitor.
 */
void on_data_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    
    // Sicherheitsprüfung (hier heißt es nun des_addr!)
    if (tx_info == NULL || tx_info->des_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    char mac[MAC_STR_LEN];
    ESP_LOGI(TAG, "Last Packet Send to %s with status: %s ", 
             get_mac_string(tx_info->des_addr, mac), 
             status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

/**
 * @brief     Callback function for handling received data
 *
 * @param     recv_info     ESPNOW packet information
 * @param     incomingData  Pointer to the received data
 * @param     len           Length of the received data
 *
 * @details   Processes received data based on its type, such as sensor data or pairing requests.
 *            Displays the received data details, including sensor information and time, if applicable.
 *            Handles pairing requests by responding and adding peers for communication.
 */
void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming_data, int len) {

    uint8_t * mac_addr = recv_info->src_addr;

    if (mac_addr == NULL || incoming_data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    char mac[MAC_STR_LEN];
    get_mac_string(mac_addr, mac);
  
    uint8_t type = incoming_data[0];
    switch (type) {
        case DATA : {
            ESP_LOGI(TAG, "DATA packet from %s (%d bytes)", mac, len);
            
            // Validate minimum size: header (4 bytes) only, no payload
            if (len < sizeof(packet_header_t)) {
                ESP_LOGE(TAG, "Packet too small! Expected >= %zu, got %d",
                         sizeof(packet_header_t), len);
                break;
            }

            // Parse header to get payload length
            packet_header_t *header = (packet_header_t *)incoming_data;
            int expected_total = sizeof(packet_header_t) + header->payload_len;
            
            if (len != expected_total) {
                ESP_LOGE(TAG, "Packet size mismatch! Expected %d, got %d",
                         expected_total, len);
                break;
            }

            if (header->payload_len > MAX_PAYLOAD_SIZE) {
                ESP_LOGE(TAG, "Payload too large: %d bytes (max %d)",
                         header->payload_len, MAX_PAYLOAD_SIZE);
                break;
            }

            // Build sensor_packet_t with transparent payload
            sensor_packet_t packet;
            memset(&packet, 0, sizeof(packet));
            
            // Copy header (msg_type = original sender value, e.g. DATA=2)
            packet.header = *header;
            
            // Link metadata - msg_source indicates how data reached receiver
            packet.link.msg_source = SENSOR_SOURCE_ESPNOW;
            packet.link.rssi = recv_info->rx_ctrl->rssi;
            packet.link.snr = -1.0f;  // SNR not available for ESP-NOW/WiFi
            packet.link.timestamp = xTaskGetTickCount();
            
            // Copy payload (receiver doesn't interpret it)
            memcpy(packet.payload, incoming_data + sizeof(packet_header_t),
                   header->payload_len);
            packet.header.payload_len = header->payload_len;
            
            // Push to stack
            esp_err_t ret = sensor_stack_push(&packet, SENSOR_SOURCE_ESPNOW);
            if (ret == ESP_OK) {
                // Update display (only needs header + link metadata)
                display_update(&packet);
                
                ESP_LOGI(TAG, "  Sensor %d [Type=%d] payload=%d bytes",
                         packet.header.sensor_nr,
                         packet.header.sensor_type,
                         packet.header.payload_len);
            } else {
                ESP_LOGW(TAG, "Stack full, ESP-NOW packet dropped");
            }

            break;
        }
        case PAIRING_REQ: {
            ESP_LOGI(TAG, "PAIRING_REQ from %s", mac);
            
            if (len != sizeof(struct_pairing_request)) {
                ESP_LOGW(TAG, "Pairing request size mismatch! Expected %zu, got %d",
                         sizeof(struct_pairing_request), len);
                break;
            }

            struct_pairing_request pairingRequest;
            memcpy(&pairingRequest, incoming_data, sizeof(struct_pairing_request));

            ESP_LOGI(TAG, "  Sensor Nr: %d", pairingRequest.sensor_nr);

            // Peer zuerst hinzufügen, DANN senden
            add_peer(mac_addr, ESPNOW_FIXED_CHANNEL);

            struct_pairing_response pairingResponse;
            pairingResponse.msg_type = PAIRING_RESP;
            pairingResponse.sensor_nr = pairingRequest.sensor_nr;
            pairingResponse.channel = ESPNOW_FIXED_CHANNEL;
            esp_wifi_get_mac(ESP_IF_WIFI_STA, pairingResponse.macAddr);

            esp_err_t result = esp_now_send(mac_addr, (uint8_t *) &pairingResponse, sizeof(struct_pairing_response));
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Error sending pairing response (err=%d)", result);
                break;
            }

            ESP_LOGI(TAG, "  PAIRING_RESP sent successfully");

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unknown message type: %d from %s", type, mac);
            break;
        }
    }
}

/* @brief Initialize WIFI strictly for ESP-NOW
 *
 * This function initializes the WIFI driver without full TCP/IP stack overhead.
 */
esp_err_t init_wifi(void) {
    ESP_LOGI(TAG, "Initialize WIFI for ESP-NOW");

    esp_err_t ret;

    // NVS wird vom WiFi-Treiber zwingend für die Kalibrierungsdaten (MAC/PHY) benötigt
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
      if (ret != ESP_OK) {
          ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
          return ret;
      }
    }

    // Grundlegende Netzwerk- und Event-Initialisierung
    if ((ret = esp_netif_init()) != ESP_OK) {
        ESP_LOGE(TAG, "ESP netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if ((ret = esp_event_loop_create_default()) != ESP_OK) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((ret = esp_wifi_init(&cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Speicher auf RAM setzen, um Flash-Abnutzung zu vermeiden
    if ((ret = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set storage failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // WiFi in den Station-Mode versetzen (Standard für ESP-NOW Endpunkte)
    if ((ret = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // WICHTIG: Das Starten des Wi-Fi-Treibers ist zwingend erforderlich!
    if ((ret = esp_wifi_start()) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Fester Channel für ESP-NOW (muss mit Sender übereinstimmen)
    // Channel muss NACH dem Start gesetzt werden
    if ((ret = esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE)) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}


/**
 * @brief     Start ESP-NOW communication
 *
 * @details   Initializes ESP-NOW communication protocol on the ESP32.
 *            Registers callbacks for sending and receiving data.
 *
 */
esp_err_t esp_now_start(void){

    if (esp_now_init() != ESP_OK) {
      ESP_LOGE(TAG, "Error initializing ESP-NOW");
      return ESP_FAIL;
    }
    esp_now_register_send_cb(on_data_sent);
    esp_now_register_recv_cb(on_data_recv);
    return ESP_OK;
}
