#ifndef SHARED_IPC_H
#define SHARED_IPC_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

/**
 * @brief Safety-critical telemetry and actuator state shared between Core 0 and Core 1.
 *        Protected by a hardware spinlock to prevent data tearing and race conditions.
 */
typedef struct {
    uint32_t raw_sensor_adc;
    uint32_t control_loop_cycles;
    uint32_t missed_deadlines;
    bool     overlimit_fault;
} SharedActuatorState_t;

/* Global IPC synchronization primitives */
extern volatile SharedActuatorState_t g_shared_state;
extern spin_lock_t *g_ipc_spinlock;

#endif /* SHARED_IPC_H */