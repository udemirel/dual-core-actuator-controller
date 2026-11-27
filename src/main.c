#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#define TOTAL_MESSAGES 100000U

/* One-slot SPSC channel: Core 1 writes, Core 0 reads. */
static uint32_t g_shared_payload = 0;
static atomic_bool g_data_ready;

/* Core 1 is the single producer. */
void core1_producer(void) {
    for (uint32_t i = 1; i <= TOTAL_MESSAGES; i++) {
        /* Wait until Core 0 releases the slot. */
        while (atomic_load_explicit(&g_data_ready, memory_order_acquire)) {
            tight_loop_contents();
        }

        /* Write the payload before publishing that it is ready. */
        g_shared_payload = i;
        atomic_store_explicit(&g_data_ready, true, memory_order_release);
    }
}

/* Core 0 is the single consumer and program entry point. */
int main(void) {
    stdio_init_all();

    /* Initialize the atomic flag before starting the producer core. */
    atomic_init(&g_data_ready, false);

    while (!stdio_usb_connected()) {
        sleep_ms(50);
    }

    printf("\n[SPSC TEST] Starting C11 atomic transfer (%lu messages)...\n",
           (unsigned long)TOTAL_MESSAGES);

    /* Start the producer on Core 1. */
    multicore_launch_core1(core1_producer);

    uint32_t received_count = 0;
    uint32_t error_count = 0;

    for (uint32_t expected = 1; expected <= TOTAL_MESSAGES; expected++) {
        /* Wait until Core 1 publishes a message. */
        while (!atomic_load_explicit(&g_data_ready, memory_order_acquire)) {
            tight_loop_contents();
        }

        /* The acquire load makes the published payload visible here. */
        uint32_t value = g_shared_payload;
        if (value != expected) {
            error_count++;
        }
        received_count++;

        /* Release the slot so Core 1 can write the next message. */
        atomic_store_explicit(&g_data_ready, false, memory_order_release);
    }

    printf("[RESULT] Received: %lu | Sequence Errors: %lu -> %s\n",
           (unsigned long)received_count,
           (unsigned long)error_count,
           (error_count == 0U) ? "PASS (C11 ATOMIC SPSC)" : "FAIL");

    while (true) {
        tight_loop_contents();
    }
}
