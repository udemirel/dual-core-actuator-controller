# RP2350 Safety-Critical Dual-Core AMP Architecture

A deterministic **Asymmetric Multi-Processing (AMP)** embedded software reference implementation targeting the **Raspberry Pi RP2350 (Dual ARM Cortex-M33)** microcontroller. Designed with safety-critical and aerospace software engineering principles in mind (freedom from interference, deterministic execution, and hardware-enforced synchronization).

## System Architecture

The workload is strictly partitioned across the two Cortex-M33 cores to isolate real-time control loops from non-deterministic system supervision and I/O overhead:

* **Core 1 (Real-Time Execution Domain — Bare-Metal):**
  * Runs a deterministic `100 Hz` (`10 ms`) control loop free of RTOS scheduling jitter or blocking I/O.
  * Acquires simulated sensor data, evaluates fault thresholds, and tracks execution deadlines.
  * Uses an isolated stack located in `SCRATCH_Y` SRAM (`0x20081xxx`).
* **Core 0 (System Supervision Domain — Host/RTOS):**
  * Manages boot sequencing, hardware initialization, fault diagnostics, and USB CDC telemetry.
  * Uses an isolated stack located in `SCRATCH_X` SRAM (`0x20080xxx`).
* **Inter-Processor Communication (IPC):**
  * **Shared Memory:** Statically allocated state structure (`SharedActuatorState_t`) in SRAM (zero dynamic memory allocation / no `malloc`).
  * **Hardware Spinlocks (`SIO`):** Guarantees atomic read-modify-write access and prevents data tearing across cores.
  * **Inter-Core Hardware FIFO:** Acts as a low-latency, lock-free hardware doorbell for event signaling and boot handshaking.

## Repository Structure

```text
├── CMakeLists.txt              # Out-of-source CMake build configuration for RP2350
├── pico_sdk_import.cmake       # Pico SDK CMake integration
├── include/
│   └── shared_ipc.h            # Shared memory data structures and IPC primitives
├── src/
│   └── main.c                  # Core 0 supervisor and Core 1 real-time implementation
├── docs/
│   └── ARCHITECTURE.md         # Deep-dive hardware notes (Boot handshake, FIFO, Spinlocks)
└── external/
    ├── pico-sdk/               # Official Raspberry Pi Pico SDK (Git Submodule)
    └── FreeRTOS-Kernel/        # FreeRTOS Kernel (Git Submodule)