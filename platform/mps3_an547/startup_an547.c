/* Minimal bare-metal startup for Cortex-M55 on the MPS3 AN547 (QEMU),
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
 *     are sufficient on a single-core part
 *
 * Note: this file may be compiled as C or C++ (the g++ driver treats .c
 * sources passed on its link line as C++), hence the extern "C" guards.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t __bss_start__, __bss_end__;
extern uint32_t __stack_top;
extern char __heap_start__, __heap_end__;

extern void __libc_init_array(void);
extern void initialise_monitor_handles(void);
extern int main(int argc, char** argv);
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
    const uint32_t m = irqLock();
    *(volatile uint64_t*)ptr = value;
    irqRestore(m);
}

uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t value, int memorder) {
    (void)memorder;
    const uint32_t m = irqLock();
    const uint64_t prev = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = prev + value;
    irqRestore(m);
    return prev;
}

uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t value, int memorder) {
    (void)memorder;
    const uint32_t m = irqLock();
    const uint64_t prev = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = value;
    irqRestore(m);
    return prev;
}

void Reset_Handler(void) {
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

__attribute__((section(".vectors"), used)) static const uintptr_t vectors[16] = {
    (uintptr_t)&__stack_top,
    (uintptr_t)&Reset_Handler,
    (uintptr_t)&Default_Handler, /* NMI */
    (uintptr_t)&Default_Handler, /* HardFault */
    (uintptr_t)&Default_Handler, /* MemManage */
    (uintptr_t)&Default_Handler, /* BusFault */
    (uintptr_t)&Default_Handler, /* UsageFault */
    (uintptr_t)&Default_Handler, /* SecureFault */
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
