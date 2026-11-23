#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#define ITERATIONS 100000U

volatile uint32_t g_counter = 0;
spin_lock_t *g_lock = NULL;

/* Core 1 bu fonksiyonu çalıştıracak */
void core1_worker(void) {
    for (uint32_t i = 0; i < ITERATIONS; i++) {
        /* 1. Kapıyı kilitle ve kendi kesmelerini kapat */
        uint32_t irq_status = spin_lock_blocking(g_lock);

        /* --- KRİTİK BÖLGE (Aynı anda sadece tek çekirdek girebilir) --- */
        g_counter++;
        /* ------------------------------------------------------------- */

        /* 2. Kilidi aç ve kesmeleri eski haline getir */
        spin_unlock(g_lock, irq_status);
    }
}

/* Core 0 bu fonksiyonu çalıştıracak */
int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    /* Boşta olan donanımsal spinlock'lardan birini al (RP2350'de 32 adet vardır) */
    int lock_num = spin_lock_claim_unused(true);
    g_lock = spin_lock_init((uint)lock_num);

    /* Core 1'i uyandır ve core1_worker fonksiyonuna gönder */
    multicore_launch_core1(core1_worker);

    /* Core 0 da kendi 100.000 artışını yapsın */
    for (uint32_t i = 0; i < ITERATIONS; i++) {
        uint32_t irq_status = spin_lock_blocking(g_lock);
        g_counter++;
        spin_unlock(g_lock, irq_status);
    }

    /* Core 1'in işini bitirmesini kısa bir süre bekle */
    sleep_ms(500);

    /* Sonuç her zaman tam olarak 200000 çıkacaktır */
    printf("Final Counter Value: %lu (Expected: %lu)\n",
           (unsigned long)g_counter, (unsigned long)(ITERATIONS * 2));

    while (true) {
        tight_loop_contents();
    }
}