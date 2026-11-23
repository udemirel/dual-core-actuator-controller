/**
 * @file main.c
 * @brief Asymmetric Multi-Processing (AMP) Architecture Demo on RP2350 (Dual Cortex-M33).
 *
 * Architecture Overview:
 *  - Core 1: Executes a deterministic, bare-metal real-time control loop (100 Hz)
 *            isolated from RTOS scheduling jitter and serial I/O overhead.
 *  - Core 0: Handles system supervision, fault diagnostics, and USB telemetry.
 *  - IPC:    Synchronized via RP2350 hardware spinlocks and inter-core hardware FIFOs.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "shared_ipc.h"

#define CONTROL_LOOP_PERIOD_US  10000U  /* 10 ms -> 100 Hz deterministic cycle */
#define TELEMETRY_DECIMATION    100U    /* Notify Core 0 every 100 cycles (1 sec) */
#define SENSOR_FAULT_THRESHOLD  950U

volatile SharedActuatorState_t g_shared_state = {0};
spin_lock_t *g_ipc_spinlock = NULL;

/**
 * @brief Core 1 entry point: Deterministic real-time control loop (Bare-Metal).
 *        Must remain free of blocking I/O (e.g., printf) to guarantee WCET bounds.
 */
void core1_realtime_entry(void) {
    uint32_t cycle_counter = 0;
    absolute_time_t next_wake_time = get_absolute_time();

    while (true) {
        next_wake_time = delayed_by_us(next_wake_time, CONTROL_LOOP_PERIOD_US);
        cycle_counter++;

        /* Simulate deterministic sensor acquisition and actuator control law */
        uint32_t simulated_adc = (cycle_counter * 17U) % 1024U;
        bool fault_flag = (simulated_adc > SENSOR_FAULT_THRESHOLD);

        /* Enter critical section: Acquire hardware spinlock & disable local IRQs */
        uint32_t irq_state = spin_lock_blocking(g_ipc_spinlock);

        g_shared_state.raw_sensor_adc      = simulated_adc;
        g_shared_state.control_loop_cycles = cycle_counter;
        g_shared_state.overlimit_fault     = fault_flag;

        /* Exit critical section: Release hardware spinlock & restore IRQs */
        spin_unlock(g_ipc_spinlock, irq_state);

        /* Trigger non-blocking Inter-Core FIFO notification at 1 Hz */
        if ((cycle_counter % TELEMETRY_DECIMATION) == 0U) {
            if (multicore_fifo_wready()) {
                multicore_fifo_push_blocking(cycle_counter);
            }
        }

        /* Check for timing overrun before sleeping until next deterministic period */
        if (time_reached(next_wake_time)) {
            uint32_t irq_err = spin_lock_blocking(g_ipc_spinlock);
            g_shared_state.missed_deadlines++;
            spin_unlock(g_ipc_spinlock, irq_err);
        } else {
            sleep_until(next_wake_time);
        }
    }
}

/**
 * @brief Core 0 entry point: System initialization, fault supervision, and telemetry.
 */
int main(void) {
    stdio_init_all();

    /* Claim and initialize an unused hardware spinlock instance */
    int spinlock_id = spin_lock_claim_unused(true);
    g_ipc_spinlock = spin_lock_init((uint)spinlock_id);

    /* Launch Core 1 real-time execution domain */
    multicore_launch_core1(core1_realtime_entry);

    while (true) {
        /* Block until Core 1 pushes a heartbeat event via hardware FIFO */
        uint32_t core1_heartbeat = multicore_fifo_pop_blocking();

        /* Take an atomic snapshot of the shared actuator state */
        uint32_t irq_state = spin_lock_blocking(g_ipc_spinlock);
        SharedActuatorState_t local_snapshot = g_shared_state;
        spin_unlock(g_ipc_spinlock, irq_state);

        printf("[CORE0_SUPERVISOR] Heartbeat: %lu | ADC: %4lu | MissedDeadlines: %lu | Status: %s\n",
               (unsigned long)core1_heartbeat,
               (unsigned long)local_snapshot.raw_sensor_adc,
               (unsigned long)local_snapshot.missed_deadlines,
               local_snapshot.overlimit_fault ? "FAULT_OVERLIMIT" : "NOMINAL");
    }

    return 0;
}