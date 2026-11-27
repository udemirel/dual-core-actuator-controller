#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#define TOTAL_MESSAGES 100000U
#define RING_BUFFER_SIZE 64U
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1U)

// Payload buffer shared between Core 1 (producer) and Core 0 (consumer).
static uint32_t g_ring_buffer[RING_BUFFER_SIZE];

// Only Core 1 updates head. It is loaded with acquire by Core 0.
static atomic_uint g_producer_head;

// Only Core 0 updates tail. It is loaded with acquire by Core 1.
static atomic_uint g_consumer_tail;

// Core 1 is the single producer.
void core1_producer(void) {
    for (uint32_t i = 1; i <= TOTAL_MESSAGES; i++) {
        for (;;) {
            uint32_t head = atomic_load_explicit(&g_producer_head, memory_order_relaxed);
            uint32_t tail = atomic_load_explicit(&g_consumer_tail, memory_order_acquire);
            uint32_t next = (head + 1U) & RING_BUFFER_MASK;

            // Stop when the ring buffer is full.
            if (next != tail) {
                g_ring_buffer[head] = i;
                atomic_store_explicit(&g_producer_head, next, memory_order_release);
                break;
            }

            tight_loop_contents();
        }
    }
}

// Core 0 is the single consumer and program entry point.
int main(void) {
    stdio_init_all();

    // Initialize the ring buffer indices before starting Core 1.
    atomic_init(&g_producer_head, 0U);
    atomic_init(&g_consumer_tail, 0U);

    while (!stdio_usb_connected()) {
        sleep_ms(50);
    }

    printf("\n[SPSC TEST] Starting 64-slot ring buffer transfer (%lu messages)...\n",
           (unsigned long)TOTAL_MESSAGES);

    // Start the producer on Core 1.
    multicore_launch_core1(core1_producer);

    uint32_t received_count = 0;
    uint32_t sequence_error_count = 0;

    for (uint32_t expected = 1; expected <= TOTAL_MESSAGES; expected++) {
        uint32_t tail = atomic_load_explicit(&g_consumer_tail, memory_order_relaxed);
        uint32_t head;

        // Wait until Core 1 publishes the next message.
        for (;;) {
            head = atomic_load_explicit(&g_producer_head, memory_order_acquire);
            if (tail != head) {
                break;
            }

            tight_loop_contents();
        }

        // Read and validate the payload published in this slot.
        uint32_t value = g_ring_buffer[tail];
        if (value != expected) {
            sequence_error_count++;
        }
        received_count++;

        // Publish that this slot is available again.
        atomic_store_explicit(&g_consumer_tail,
                              (tail + 1U) & RING_BUFFER_MASK,
                              memory_order_release);
    }

    printf("[RESULT] Received: %lu | Sequence Errors: %lu -> %s\n",
           (unsigned long)received_count,
           (unsigned long)sequence_error_count,
           (sequence_error_count == 0U) ? "PASS (C11 ATOMIC SPSC RING)" : "FAIL");

    while (true) {
        tight_loop_contents();
    }
}
