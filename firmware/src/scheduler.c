/* =============================================================================
 *  GREJEM OS / scheduler.c
 *
 *  Lekki kooperatywny scheduler oparty o monotoniczny licznik SysTick 1 kHz.
 *  Każde zadanie ma własny okres i last_run. scheduler_dispatch() wywoływane
 *  jest w superloopie głównej pętli - jeśli któryś task urosnął poza swój
 *  okres, jest uruchamiany.
 *
 *  Każde zadanie MUSI być nieblokujące (krótkie, deterministyczne).
 * ============================================================================= */
#include "scheduler.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>

#include "buttons.h"
#include "leds.h"
#include "heartbeat.h"
#include "usbhid.h"
#include "adc.h"

/* Monotoniczny licznik ms - zwiększany w sys_tick_handler() */
static volatile uint32_t systick_ms = 0;

/* ---- Tabela zadań periodycznych ---- */
typedef struct {
    uint32_t period_ms;          /* co ile ms uruchamiać                */
    uint32_t last_run;           /* czas ostatniego uruchomienia        */
    void   (*run)(void);         /* funkcja zadania (non-blocking!)     */
} task_t;

/* Kolejność ma znaczenie przy zbieżności: krótsze czasy / krytyczne wcześniej. */
static task_t tasks[] = {
    {    1, 0, buttons_poll     },   /* 1 kHz  - debouncing przycisków    */
    {    1, 0, usbhid_pump      },   /* drain kolejki TX protokołu do USB */
    {    5, 0, adc_flush_dirty  },   /* 200 Hz - wyemituj zmienione popy  */
    {   20, 0, leds_update      },   /* 50 Hz  - animacje LED (blink/pulse/breathe) */
    { 1500, 0, heartbeat_tick   },   /* co 1.5s - pakiet "żyję"           */
};
#define TASKS_COUNT   (sizeof(tasks) / sizeof(tasks[0]))

void scheduler_init(void) {
    systick_ms = 0;

    /* SysTick: źródło AHB/8 = 72MHz/8 = 9 MHz. Przeładuj co 9000 = 1 kHz. */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
    systick_set_reload(8999);                 /* 9_000_000 / 9000 = 1000 Hz */
    systick_interrupt_enable();
    systick_clear();
    systick_counter_enable();
}

uint32_t scheduler_millis(void) {
    /* Na Cortex-M3 odczyt 32-bit aligned jest atomowy - nie trzeba __disable_irq. */
    return systick_ms;
}

void scheduler_dispatch(void) {
    /* Snapshot licznika - wystarczy jeden odczyt na przebieg. */
    uint32_t now = systick_ms;

    for (uint32_t i = 0; i < TASKS_COUNT; i++) {
        /* Różnica bez znaku - poprawnie obsługuje rollover 32-bit (co 49 dni). */
        if ((uint32_t)(now - tasks[i].last_run) >= tasks[i].period_ms) {
            tasks[i].last_run = now;
            tasks[i].run();
        }
    }
}

/* ISR SysTicka - libopencm3 używa nazwy sys_tick_handler. */
void sys_tick_handler(void) {
    systick_ms++;
}
