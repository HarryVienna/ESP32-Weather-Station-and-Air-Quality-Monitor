#ifndef GUI_H
#define GUI_H

#include "lvgl.h"
#include "ui/ui.h"
#include "display/display.h"
#include "weather/open_meteo.h"
#include "../../common/packet_format.h"

/* Anzahl der fest verdrahteten Sensor-Karten im Weatherstation-Screen (siehe
 * sensor_slots[] in gui.c). sensor_nr aus packet_header_t muss < diesem Wert
 * sein, um einer Karte zugeordnet zu werden - wird auch vom Receiver-Watchdog
 * genutzt, um die Anzahl der zu ueberwachenden Slots zu kennen. */
#define SENSOR_SLOT_COUNT 6

void disp_wifi_status(bool status, int8_t rssi_dbm);
void disp_date_time(char* date_time);
void disp_sensor_link_quality(uint8_t sensor_nr, uint32_t voltage_mv, int16_t rssi_dbm);
void disp_sensor_values(uint8_t sensor_nr, sensor_type_t type, const void *payload);
void disp_sensor_offline(uint8_t sensor_nr, bool offline);
void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2);
void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2);
void disp_weather(current_weather_data_t *current_weather, hourly_weather_data_t *hourly_weather, daily_weather_data_t *daily_weather);
void set_brightness(uint16_t brightness);

void disp_wifi_networks(char* allNetworks);
void disp_connect_status(bool is_connected);
void disp_show_setup_spinner(bool show);

void init_charts(void);

//void set_cities(const char* region);



#endif /* GUI_H */