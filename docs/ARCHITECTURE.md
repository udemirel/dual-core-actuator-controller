# Hardware & Multi-Core Architecture Deep-Dive (RP2350)

## 1. Asymmetric Boot Sequence & Hardware Handshake (`multicore_launch_core1`)

At power-on reset (POR), the RP2350 BootROM initializes and wakes **only Core 0**. **Core 1** remains halted in a low-power sleep state executing a `WFE` (Wait For Event) instruction inside the BootROM, consuming zero CPU cycles and executing no user code until explicitly released by Core 0.

This enforces a deterministic **Master-Slave Boot** sequence: Core 0 configures system clocks, memory partitions, and shared peripherals before waking Core 1 via a 5-step hardware handshake over the **Inter-Core FIFO**:

1. **Flush & Wake (`SEV`):** Core 0 flushes the FIFO, pushes `0`, and asserts a `SEV` (Send Event) hardware signal to wake Core 1 from its `WFE` state.
2. **Handshake Echo:** Core 1 wakes up and echoes `0` back to confirm readiness for initialization parameters.
3. **Vector Table (`VTOR`) Configuration:** Core 0 transmits the interrupt vector table address; Core 1 writes it to its own `VTOR` register and echoes confirmation.
4. **Stack Pointer (`MSP`) Isolation:** Core 0 transmits the top address of Core 1's dedicated stack (`__StackOneTop` in `SCRATCH_Y`); Core 1 loads its Main Stack Pointer (`SP`) register and echoes confirmation.
5. **Entry Point (`PC`) Execution:** Core 0 transmits the target function pointer (`core1_realtime_entry`); Core 1 echoes confirmation and branches its Program Counter (`PC`) to begin real-time execution.

---

## 2. Inter-Core Hardware FIFO (SIO Mailbox Architecture)

The Inter-Core FIFO is **not** Flash storage or general-purpose SRAM; it is a dedicated, silicon-level hardware peripheral integrated into the **SIO (Single-cycle I/O)** block directly adjacent to both cores:

* **Dual Unidirectional Pipelines:** Consists of two independent, 32-bit wide, 4-entry deep hardware register queues (`Core 0 -> Core 1` and `Core 1 -> Core 0`).
* **Lock-Free Atomic Access:** Because the write port (`FIFO_WR`) is physically wired exclusively to the transmitting core and the read port (`FIFO_RD`) exclusively to the receiving core, 32-bit words are transferred in a single clock cycle without bus contention or spinlocks.
* **Hardware Doorbell (`SIO_IRQ_FIFO`):** Pushing a word into the FIFO automatically asserts an interrupt (`SIO_IRQ_FIFO`) or event signal on the receiving core, eliminating wasteful RAM polling loops.
* **Hybrid Payload Pattern:** Because the hardware queue depth is limited to `4 x 32-bit` words, large telemetry payloads are written to Spinlock-protected **Shared SRAM**, while the **FIFO** is used strictly as a low-latency doorbell to signal data availability or pass buffer indices.

---

## 3. Hardware Spinlocks vs. Interrupt Disabling

On a single-core MCU, disabling interrupts (`__disable_irq()`) suffices to protect a critical section. On a multi-core MCU, disabling interrupts on Core 0 **does not stop Core 1** from concurrently accessing the same SRAM address:

* **Atomic Hardware Lock:** `spin_lock_blocking()` performs an atomic, single-cycle hardware test-and-set on an SIO spinlock register **and** disables local interrupts on the calling core to prevent self-deadlock from an ISR.
* **Limitations of `volatile`:** Using `volatile` alone enforces compiler memory visibility/ordering but does **not** prevent *data tearing* when updating multi-field structures across concurrent cores.

---

## 4. Spatial Memory & Stack Isolation

To prevent stack collisions and satisfy **Freedom From Interference (FFI)** principles (cf. CAST-32A / AMC 20-193):

* **Core 0 Stack:** Mapped to `SCRATCH_X` (`4 KB` dedicated SRAM bank, `0x20080xxx`).
* **Core 1 Stack:** Mapped to `SCRATCH_Y` (`4 KB` dedicated SRAM bank, `0x20081xxx`).
* **Static Allocation:** Zero dynamic heap allocation (`malloc`/`free`) is permitted anywhere in the firmware.