/**
 * @file main.cpp
 * @brief LoRa Sensor Sender - Heltec CubeCell HTCC-AB01 V2 (ASR6502 / SX1262)
 *
 * Sendet BME280 Messdaten via LoRa P2P im selben Packet-Format wie der
 * ESP32-S3 Sender, damit der bestehende Receiver beide Sensoren parsen kann.
 *
 * Boot-Path:
 *   Power-on / Reset -> setup() -> Messung -> Sendung -> lowPowerHandler()
 *   Timer-Wakeup     -> loop()  -> Messung -> Sendung -> lowPowerHandler()
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include "packet_format.h"

/* ============================================================================
 * Konfiguration
 * ============================================================================ */

#define TX_INTERVAL_MS         600000UL     // 10 Minuten

/* DIP-Switch Pinbelegung
 *   4 Bit Sensor-Nr  (0..15)
 *   2 Bit TX-Power-Index in POWER_LEVELS[]
 * Schalter zwischen Pin und GND, interne Pullups, nach dem Lesen werden
 * die Pins als OUTPUT LOW gehalten, damit im Sleep kein Strom fliesst. */
static const uint8_t PIN_SENSOR_BITS[4] = { GPIO0, GPIO1, GPIO2, GPIO3 };
static const uint8_t PIN_POWER_BITS[2]  = { GPIO5, GPIO6 };
static const int8_t  POWER_LEVELS[4]    = { 14, 17, 20, 22 };  // dBm, anpassbar

/* Onboard RGB-LED, leuchtet kurz beim Senden */
#define LED_COLOR              0x000800     // dunkles Gruen, sparsam
#define LED_BLINK_MS           10

/* LoRa - identisch mit ESP32-Sender */
#define RF_FREQUENCY           869525000UL  // 869.525 MHz, EU G3
#define LORA_BANDWIDTH         0            // 0 = 125 kHz
#define LORA_SPREADING_FACTOR  9            // SF9
#define LORA_CODINGRATE        1            // 1 = 4/5
#define LORA_PREAMBLE_LENGTH   8
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON   true

/* MessageType-Werte, identisch zur Enum im ESP32-Sender (main.c) */
#define MSG_TYPE_PAIRING_REQ   0
#define MSG_TYPE_PAIRING_RESP  1
#define MSG_TYPE_DATA          2

/* BME280 */
#define BME280_I2C_ADDR        0x76         // oder 0x77, je nach SDO-Pin

/* ============================================================================
 * Globals
 * ============================================================================ */

static RadioEvents_t   radioEvents;
static TimerEvent_t    wakeUpTimer;
static Adafruit_BME280 bme;
static bool            bmeOk = false;

/* Aus DIP-Schaltern beim Power-on gelesen */
static uint8_t         sensorNr = 0;
static int8_t          txPower  = 14;

/* Flags fuer die Wait-Schleifen im loop() */
static volatile bool   txDone    = false;
static volatile bool   wakeFired = false;

/* ============================================================================
 * DIP-Switch lesen
 *   Pullups einschalten, Pegel lesen, danach Pins als OUTPUT LOW stilllegen
 *   damit im Deep-Sleep kein Pullup-Strom fliesst.
 * ============================================================================ */

static uint8_t readBits(const uint8_t *pins, uint8_t n) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        pinMode(pins[i], INPUT_PULLUP);
    }
    delayMicroseconds(50);
    for (uint8_t i = 0; i < n; i++) {
        // Schalter geschlossen = LOW = Bit gesetzt
        if (digitalRead(pins[i]) == LOW) v |= (1 << i);
    }
    for (uint8_t i = 0; i < n; i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }
    return v;
}

static void readDipConfig(void) {
    sensorNr = readBits(PIN_SENSOR_BITS, 4);
    uint8_t pw = readBits(PIN_POWER_BITS, 2);
    txPower = POWER_LEVELS[pw];
    Serial.printf("DIP: sensor=%u, txPower=%d dBm\n", sensorNr, txPower);
}

/* ============================================================================
 * Sensor read - Forced Mode, dann Werte abholen
 * ============================================================================ */

static void readSensors(float *pressure, float *temperature, float *humidity) {
    if (!bmeOk) {
        *pressure = *temperature = *humidity = 0.0f;
        return;
    }
    bme.takeForcedMeasurement();
    *temperature = bme.readTemperature();
    *humidity    = bme.readHumidity();
    *pressure    = bme.readPressure() / 100.0f;   // Pa -> hPa
}

/* ============================================================================
 * Packet bauen und senden - exakt gleicher Layout wie im ESP32-Sender
 * ============================================================================ */

static void buildAndSend(void) {
    /* Kurzer LED-Blink als visuelle Bestaetigung dass gesendet wird */
    turnOnRGB(LED_COLOR, 0);
    delay(LED_BLINK_MS);
    turnOffRGB();

    float    pressure, temperature, humidity;
    readSensors(&pressure, &temperature, &humidity);

    uint32_t voltage_mv = getBatteryVoltage();   // CubeCell built-in

    Serial.printf("BME280: %.2f C / %.2f %% / %.2f hPa, VBat: %lu mV\n",
                  temperature, humidity, pressure, voltage_mv);

    lora_sensor_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.msg_type    = MSG_TYPE_DATA;
    packet.header.sensor_nr   = sensorNr;
    packet.header.sensor_type = SENSOR_TYPE_BME280;

    bme280_payload_t bme280_payload = {
        .voltage     = voltage_mv,
        .pressure    = pressure,
        .temperature = temperature,
        .humidity    = humidity,
    };
    memcpy(packet.payload, &bme280_payload, sizeof(bme280_payload_t));
    packet.header.payload_len = sizeof(bme280_payload_t);

    uint32_t total_len = sizeof(packet_header_t) + packet.header.payload_len;
    Serial.printf("TX: sensor=%u type=%u len=%lu\n",
                  packet.header.sensor_nr, packet.header.sensor_type, total_len);

    Radio.Send((uint8_t *)&packet, total_len);
}

/* ============================================================================
 * Radio callbacks und Wake callback
 *   Setzen nur Flags, die im loop() abgefragt werden.
 * ============================================================================ */

static void onTxDone(void) {
    Serial.println("LoRa send OK");
    Radio.Sleep();
    txDone = true;
}

static void onTxTimeout(void) {
    Serial.println("LoRa send TIMEOUT");
    Radio.Sleep();
    txDone = true;
}

static void onWake(void) {
    wakeFired = true;
}

/* ============================================================================
 * Setup
 * ============================================================================ */

void setup(void) {
    boardInitMcu();
    Serial.begin(115200);
    delay(50);

    Serial.println("\nCubeCell BME280 LoRa Sender");

    /* Erst DIP-Konfiguration lesen, dann den Rest initialisieren */
    readDipConfig();
    Serial.printf("Sensor #%u, %lu Hz, SF%d, %d dBm\n",
                  sensorNr, RF_FREQUENCY, LORA_SPREADING_FACTOR, txPower);

    /* BME280 in Forced Mode, Filter off, Oversampling 1x */
    Wire.begin();
    if (bme.begin(BME280_I2C_ADDR)) {
        bme.setSampling(Adafruit_BME280::MODE_FORCED,
                        Adafruit_BME280::SAMPLING_X1,   // Temperature
                        Adafruit_BME280::SAMPLING_X1,   // Pressure
                        Adafruit_BME280::SAMPLING_X1,   // Humidity
                        Adafruit_BME280::FILTER_OFF);
        bmeOk = true;
        Serial.println("BME280 ok");
    } else {
        Serial.printf("BME280 not found at 0x%02X\n", BME280_I2C_ADDR);
    }

    /* LoRa */
    radioEvents.TxDone    = onTxDone;
    radioEvents.TxTimeout = onTxTimeout;
    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetPublicNetwork(false);   // Sync Word 0x1424 (private)
    Radio.SetTxConfig(MODEM_LORA, txPower, 0,
                      LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, LORA_PREAMBLE_LENGTH,
                      LORA_FIX_LENGTH_PAYLOAD_ON, true,
                      0, 0, LORA_IQ_INVERSION_ON, 3000);

    TimerInit(&wakeUpTimer, onWake);
}

/* ============================================================================
 * Loop - linear: senden, warten bis TX fertig, dann zehn Minuten schlafen
 * ============================================================================ */

void loop(void) {
    // 1. Messen und senden, dann auf TxDone/TxTimeout warten
    txDone = false;
    buildAndSend();
    while (!txDone) {
        lowPowerHandler();
        Radio.IrqProcess();
    }

    // 2. Schlafen bis der Timer feuert
    wakeFired = false;
    TimerSetValue(&wakeUpTimer, TX_INTERVAL_MS);
    TimerStart(&wakeUpTimer);
    while (!wakeFired) {
        lowPowerHandler();
    }
}
