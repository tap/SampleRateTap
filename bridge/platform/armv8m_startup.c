/* Minimal bare-metal startup for Armv8-M targets under QEMU (Cortex-M55
 * on mps3-an547, Cortex-M33 on mps2-an505),
 * replacing the toolchain crt0 (linked with -nostartfiles):
 *   - vector table with initial stack pointer and Reset_Handler
 *   - FPU/MVE coprocessor enable before any FP instruction executes
 *   - .bss zeroing (QEMU's ELF loader already placed .data)
 *   - semihosting stdio (librdimon), C++ static constructors, main, exit
 *   - deterministic _sbrk over the linker-defined heap region (overrides
 *     librdimon's weak version, whose limit depends on the semihosting
 *     SYS_HEAPINFO call returning sensible values for this board)
 *   - 64-bit __atomic_* helpers: M-profile has no 64-bit exclusives and the
 *     bare-metal toolchain ships no libatomic; PRIMASK critical sections
 *     are sufficient on a single-core part. RatioTap itself is atomics-free;
 *     these serve the SampleRateTap dev dependency (the cross-validation
 *     test composes against its ring buffer).
 *
 * Ported from SampleRateTap's platform/armv8m_startup.c (the family's
 * embedded targets share one startup story; divergences land there first).
 *
 * The toolchain file passes this to the link line with `-x c`: under C the
 * vector table's address-constant initializers are guaranteed link-time
 * constants (a C++ compile could legally lower them to dynamic
 * initialization, leaving the table zeroed at reset). The extern "C"
 * guards keep the file safe if it is ever compiled as C++ anyway.
 *
 * Only the __atomic_* helpers the linked code currently needs are
 * provided; any future use of others (e.g. compare-exchange) fails loudly
 * at link time.
 */
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t __bss_start__, __bss_end__;
extern uint32_t __stack_top;
extern uint32_t __stack_limit;
extern char     __heap_start__, __heap_end__;

extern void __libc_init_array(void);
extern void initialise_monitor_handles(void);
extern int  main(int argc, char** argv);
extern void exit(int) __attribute__((noreturn));

void* __dso_handle;

/* Referenced by newlib's fini machinery; nothing to do without crti/crtn. */
void _init(void) {}
void _fini(void) {}

void* _sbrk(ptrdiff_t increment) {
    static char* brk = &__heap_start__;
    if (brk + increment > &__heap_end__) {
        errno = ENOMEM;
        return (void*)-1;
    }
    char* prev = brk;
    brk += increment;
    return prev;
}

static inline uint32_t irqLock(void) {
    uint32_t primask;
    __asm volatile("mrs %0, PRIMASK\n cpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void irqRestore(uint32_t primask) {
    __asm volatile("msr PRIMASK, %0" ::"r"(primask) : "memory");
}

uint64_t __atomic_load_8(const volatile void* ptr, int memorder) {
    (void)memorder;
    const uint32_t m = irqLock();
    const uint64_t v = *(const volatile uint64_t*)ptr;
    irqRestore(m);
    return v;
}

void __atomic_store_8(volatile void* ptr, uint64_t value, int memorder) {
    (void)memorder;
    const uint32_t m         = irqLock();
    *(volatile uint64_t*)ptr = value;
    irqRestore(m);
}

uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t value, int memorder) {
    (void)memorder;
    const uint32_t m         = irqLock();
    const uint64_t prev      = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = prev + value;
    irqRestore(m);
    return prev;
}

uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t value, int memorder) {
    (void)memorder;
    const uint32_t m         = irqLock();
    const uint64_t prev      = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = value;
    irqRestore(m);
    return prev;
}

void Reset_Handler(void) {
    /* MSPLIM exists on Armv8-M Mainline only (both targets are M33/M55
     * class): a main-stack overflow past __stack_limit raises a fault
     * instead of silently corrupting whatever sits below the stack. */
    __asm volatile("msr msplim, %0" ::"r"(&__stack_limit));

    /* Grant full access to CP10/CP11 (scalar FPU + MVE) first: code below
     * may legitimately use FP registers once newlib is involved. */
    volatile uint32_t* const cpacr = (volatile uint32_t*)0xE000ED88u;
    *cpacr |= 0xFu << 20;
    __asm volatile("dsb\n isb" ::: "memory");

    memset(&__bss_start__, 0, (size_t)((char*)&__bss_end__ - (char*)&__bss_start__));

    initialise_monitor_handles(); /* semihosting stdin/stdout/stderr */
    __libc_init_array();          /* C++ static constructors */
    exit(main(0, (char**)0));
}

void Default_Handler(void) {
    for (;;) {
        /* Faults park here; the test harness times out and fails the run. */
    }
}

void HardFault_Handler(void) {
    /* Distinct park loop so a HardFault (e.g. MSPLIM violation escalation)
     * is distinguishable from other parked vectors under a debugger. */
    __asm volatile("bkpt #0");
    for (;;) {
    }
}

__attribute__((section(".vectors"), used)) static const uintptr_t vectors[16] = {
    (uintptr_t)&__stack_top,
    (uintptr_t)&Reset_Handler,
    (uintptr_t)&Default_Handler,   /* NMI */
    (uintptr_t)&HardFault_Handler, /* HardFault */
    (uintptr_t)&Default_Handler,   /* MemManage */
    (uintptr_t)&Default_Handler,   /* BusFault */
    (uintptr_t)&Default_Handler,   /* UsageFault */
    (uintptr_t)&Default_Handler,   /* SecureFault */
    0,
    0,
    0,
    (uintptr_t)&Default_Handler, /* SVCall */
    (uintptr_t)&Default_Handler, /* DebugMonitor */
    0,
    (uintptr_t)&Default_Handler, /* PendSV */
    (uintptr_t)&Default_Handler, /* SysTick */
};

#ifdef __cplusplus
} /* extern "C" */
#endif
