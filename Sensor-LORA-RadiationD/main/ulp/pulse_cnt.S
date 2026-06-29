/*
 * ULP-FSM Impulszähler für Geigerzähler (Dauerlauf-Modus optimiert)
 * * - Keine Wakeup-Logik (wird vom Timer der main.c übernommen)
 * - Micro-Sleeps (wait) integriert, um im Dauerlauf Strom zu sparen
 */

#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"
#include "soc/sens_reg.h"

    .bss

    .global next_edge
next_edge:
    .long 0

    .global debounce_counter
debounce_counter:
    .long 0

    .global debounce_max_count
debounce_max_count:
    .long 0

    .global edge_count
edge_count:
    .long 0

    .global io_number
io_number:
    .long 0

    .text
    .global entry
entry:
    /* 12.5 µs Pause bei 8 MHz: spart Strom, Impuls (200 µs) wird trotzdem sicher erkannt */
    wait 100

    move r3, io_number
    ld r3, r3, 0

#if CONFIG_IDF_TARGET_ESP32S2
    WRITE_RTC_FIELD(SENS_SAR_IO_MUX_CONF_REG, SENS_IOMUX_CLK_GATE_EN, 1)
#elif CONFIG_IDF_TARGET_ESP32S3
    WRITE_RTC_FIELD(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_IOMUX_CLK_EN, 1)
#endif

    move r0, r3
    jumpr read_io_high, 16, ge

    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
    rsh r0, r0, r3
    jump read_done

read_io_high:
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 16, 2)
    sub r3, r3, 16
    rsh r0, r0, r3

read_done:
    and r0, r0, 1
    move r3, next_edge
    ld r3, r3, 0
    add r3, r0, r3
    and r3, r3, 1
    jump changed, eq

    /* Debounce-Zähler zurücksetzen */
    move r3, debounce_max_count
    move r2, debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    jump entry      /* Endlosschleife */

    .global changed
changed:
    move r3, debounce_counter
    ld r2, r3, 0
    add r2, r2, 0
    jump edge_detected, eq
    sub r2, r2, 1
    st r2, r3, 0
    jump entry      /* Endlosschleife */

    .global edge_detected
edge_detected:
    /* Debounce-Zähler zurücksetzen */
    move r3, debounce_max_count
    move r2, debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    
    /* next_edge umschalten */
    move r3, next_edge
    ld r2, r3, 0
    add r2, r2, 1
    and r2, r2, 1
    st r2, r3, 0
    
    /* edge_count inkrementieren */
    move r3, edge_count
    ld r2, r3, 0
    add r2, r2, 1
    st r2, r3, 0
    
    /* springt direkt zurück an den Anfang */
    jump entry      /* Endlosschleife */