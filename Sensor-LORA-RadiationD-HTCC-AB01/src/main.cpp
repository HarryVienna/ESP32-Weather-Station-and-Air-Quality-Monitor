/**
 * @file main.cpp
 * @brief LoRa Geigerzähler Sender - Heltec CubeCell HTCC-AB01 V2 (ASR6502 / SX1262)
 *
 * Zählt Geiger-Pulse via GPIO-Interrupt, berechnet einen 60-Sekunden
 * gleitenden Mittelwert und sendet CPM + µSv/h jede Minute via LoRa P2P.
 *
 * Anschluss Geigerzähler:
 *   Pulse-Ausgang (aktiv LOW) → GPIO5 (GPIO4 = RGB LED belegt)
 *   GND                       → GND  (J2 Pin 1 oder 5)
 *   VCC                       → 3V3  (J2 Pin 3) oder Ve (J2 Pin 4)
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "packet_format.h"

/* ============================================================================
 * Konfiguration
 * ============================================================================ */

#define TX_INTERVAL_MIN       1          // Sendung alle N Minuten
#define TX_INTERVAL_MS        (TX_INTERVAL_MIN * 60000UL)
#define GEIGER_PIN            GPIO5      // Pulse-Eingang (FALLING edge)
#define CONVERSION_FACTOR     151.0f     // CPM → µSv/h  (SBM-20 / J305 / J321)
#define ROLLING_WINDOW_MIN    10         // Gleitfenster in Minuten
#define ROLLING_WINDOW_SEC    (ROLLING_WINDOW_MIN * 60)

/* DIP-Switch: 4 Bit Sensor-Nr (0..15), Schalter zwischen Pin und GND */
/* DIP0=GPIO0, DIP1=GPIO3, DIP2=GPIO2, DIP3=GPIO1 (Verlötung) */
static const uint8_t PIN_SENSOR_BITS[4] = { GPIO0, GPIO3, GPIO2, GPIO1 };

/* TX-Power */
#define TX_POWER_DBM          0

/* LoRa P2P – identisch mit Receiver */
#define RF_FREQUENCY          869525000UL
#define LORA_BANDWIDTH        0
#define LORA_SPREADING_FACTOR 9
#define LORA_CODINGRATE       1
#define LORA_PREAMBLE_LENGTH  8
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON  true

#define MSG_TYPE_DATA         2

/* ============================================================================
 * Globals
 * ============================================================================ */

static RadioEvents_t radioEvents;

/* Pulse-Zähler aus ISR */
static volatile uint32_t isrPulseCount = 0;

/* Rolling-Average Buffer */
static uint32_t rollingBuffer[ROLLING_WINDOW_SEC];
static int      rollingIndex = 0;
static uint32_t rollingTotal = 0;

/* Aktuelle Messwerte */
static float    currentUsvh  = 0.0f;
static float    currentCpm   = 0.0f;

/* TX-Steuerung */
static volatile bool txDone  = false;

static uint8_t  sensorNr     = 0;

/* 1-Sekunden-Timer für Rolling Average + TX-Zähler */
static TimerEvent_t        secTimer;
static volatile bool       secTick   = false;
static volatile uint32_t   tickCount = 0;

static void onSecTimer(void) {
    secTick = true;
    TimerStart(&secTimer);
}

/* ============================================================================
 * DIP-Switch lesen
 * ============================================================================ */

static uint8_t readBits(const uint8_t *pins, uint8_t n) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        pinMode(pins[i], INPUT_PULLUP);
    }
    delayMicroseconds(50);
    for (uint8_t i = 0; i < n; i++) {
        if (digitalRead(pins[i]) == LOW) v |= (1 << i);
    }
    for (uint8_t i = 0; i < n; i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }
    return v;
}

/* ============================================================================
 * ISR – Geiger Puls (FALLING edge)
 * ============================================================================ */

static void geigerIsr(void) {
    isrPulseCount++;
}

/* ============================================================================
 * Rolling Average – einmal pro Sekunde aufrufen
 * ============================================================================ */

static void updateRollingAverage(void) {
    noInterrupts();
    uint32_t counts = isrPulseCount;
    isrPulseCount   = 0;
    interrupts();

    rollingTotal -= rollingBuffer[rollingIndex];
    rollingTotal += counts;
    rollingBuffer[rollingIndex] = counts;
    rollingIndex = (rollingIndex + 1) % ROLLING_WINDOW_SEC;

    currentCpm  = (float)rollingTotal * (60.0f / ROLLING_WINDOW_SEC);
    currentUsvh = currentCpm / CONVERSION_FACTOR;
}

/* ============================================================================
 * Radio Callbacks
 * ============================================================================ */

static void onTxDone(void) {
    Radio.Sleep();
    txDone = true;
}

static void onTxTimeout(void) {
    Serial.println("LoRa TX Timeout");
    Radio.Sleep();
    txDone = true;
}

/* ============================================================================
 * Packet senden
 * ============================================================================ */

static void sendPacket(void) {
    uint32_t voltage_mv = getBatteryVoltage();

    Serial.printf("Geiger: %.4f µSv/h  %.1f CPM  VBat: %lu mV\n",
                  currentUsvh, currentCpm, voltage_mv);

    lora_sensor_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.msg_type    = MSG_TYPE_DATA;
    packet.header.sensor_nr   = sensorNr;
    packet.header.sensor_type = SENSOR_TYPE_GEIGER;

    geiger_payload_t payload = {
        .voltage = voltage_mv,
        .usvh    = currentUsvh,
        .cpm     = currentCpm,
    };
    memcpy(packet.payload, &payload, sizeof(geiger_payload_t));
    packet.header.payload_len = sizeof(geiger_payload_t);

    uint32_t len = sizeof(packet_header_t) + packet.header.payload_len;
    Serial.printf("TX: sensor=%u type=%u len=%lu\n",
                  packet.header.sensor_nr, packet.header.sensor_type, len);

    txDone = false;
    Radio.Send((uint8_t *)&packet, len);

    while (!txDone) {
        Radio.IrqProcess();
    }
}

/* ============================================================================
 * Setup
 * ============================================================================ */

void setup(void) {
    boardInitMcu();
    Serial.begin(115200);
    delay(50);

    Serial.println("\nCubeCell Geigerzähler LoRa Sender");

    sensorNr = readBits(PIN_SENSOR_BITS, 4);

    Serial.printf("Sensor #%u, %lu Hz, SF%d, %d dBm\n",
                  sensorNr, RF_FREQUENCY, LORA_SPREADING_FACTOR, TX_POWER_DBM);

    memset(rollingBuffer, 0, sizeof(rollingBuffer));

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);  // LOW = Vext aktiv (Geigerzähler VCC)

    pinMode(GEIGER_PIN, INPUT);
    attachInterrupt(GEIGER_PIN, geigerIsr, FALLING);

    TimerInit(&secTimer, onSecTimer);
    TimerSetValue(&secTimer, 1000);
    TimerStart(&secTimer);

    /* LoRa */
    radioEvents.TxDone    = onTxDone;
    radioEvents.TxTimeout = onTxTimeout;
    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetPublicNetwork(false);
    Radio.SetTxConfig(MODEM_LORA, TX_POWER_DBM, 0,
                      LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, LORA_PREAMBLE_LENGTH,
                      LORA_FIX_LENGTH_PAYLOAD_ON, true,
                      0, 0, LORA_IQ_INVERSION_ON, 3000);
    Radio.Sleep();

    Serial.printf("Bereit – erste Sendung in %d s\n", TX_INTERVAL_MIN * 60);
}

/* ============================================================================
 * Loop – Timer-basiertes Timing mit Low Power Sleep
 * ============================================================================ */

void loop(void) {
    if (secTick) {
        secTick = false;
        tickCount++;
        updateRollingAverage();

        if (tickCount >= (uint32_t)TX_INTERVAL_MIN * 60) {
            tickCount = 0;
            sendPacket();
        }
    }

    lowPowerHandler();
}
