#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h" /* __dmb() makrosunu içerir */

#define TOTAL_MESSAGES 100000U

/* SPSC Paylaşımlı Alan (Spinlock YOK) */
static volatile uint32_t g_shared_payload = 0;   /* Sadece Core 1 yazar, Core 0 okur */
static volatile bool     g_data_ready     = false; /* Core 1 true yapar, Core 0 false yapar */

/* Core 1: SINGLE PRODUCER (Tek Üretici) */
void core1_producer(void) {
    for (uint32_t i = 1; i <= TOTAL_MESSAGES; i++) {
        /* 1. Core 0 bir önceki veriyi okuyup bayrağı indirene kadar bekle */
        while (g_data_ready) {
            tight_loop_contents();
        }

        /* 2. Önce veriyi RAM'e yaz */
        g_shared_payload = i;

        /* 3. MEMORY BARRIER: Verinin RAM'e yazıldığından emin ol, sonra bayrağı kaldır! */
        __dmb();

        /* 4. Core 0'a "veri hazır" sinyali ver */
        g_data_ready = true;
    }
}

/* Core 0: SINGLE CONSUMER (Tek Tüketici) */
int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n[SPSC TEST] Starting Lock-Free transfer (%lu messages)...\n",
           (unsigned long)TOTAL_MESSAGES);

    /* Core 1'i üretici olarak başlat */
    multicore_launch_core1(core1_producer);

    uint32_t received_count = 0;
    uint32_t error_count = 0;

    for (uint32_t expected = 1; expected <= TOTAL_MESSAGES; expected++) {
        /* 1. Core 1 veriyi hazırlayana kadar bekle */
        while (!g_data_ready) {
            tight_loop_contents();
        }

        /* 2. MEMORY BARRIER: Bayrağı okuduktan sonra taze veriyi RAM'den çek! */
        __dmb();

        /* 3. Veriyi oku ve doğrula */
        uint32_t val = g_shared_payload;
        if (val != expected) {
            error_count++;
        }
        received_count++;

        /* 4. Core 1 yeni veri yazabilsin diye bayrağı indir */
        g_data_ready = false;
    }

    printf("[RESULT] Received: %lu | Sequence Errors: %lu -> %s\n",
           (unsigned long)received_count,
           (unsigned long)error_count,
           (error_count == 0U) ? "PASS (LOCK-FREE SPSC)" : "FAIL (ORDERING VIOLATION)");

    while (true) {
        tight_loop_contents();
    }
}