/*
 * Interrupt handling.
 *
 * Written in 2021 by Joris van Rantwijk.
 *
 * To the extent possible under law, the author has dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * See <http://creativecommons.org/publicdomain/zero/1.0/>
 */

#ifndef RVLIB_INTERRUPT_H_
#define RVLIB_INTERRUPT_H_

#include <stdint.h>


/*
 * Declarations of interrupt handling functions.
 *
 * Some or all of these functions should be implemented by the application
 * to handle the interrupts.
 */
void handle_software_interrupt(void);
void handle_timer_interrupt(void);
void handle_external_interrupt(void);
void handle_unexpected_trap(uint32_t cause, uint32_t badaddr);


/*
 * Initialize interrupt handling.
 *
 * Call this function once when the application starts.
 * It forces the linker to emit the low-level interrupt handling code.
 */
static inline void rvlib_interrupt_init(void)
{
    void __trap_vector(void);
    asm volatile ( "" : : "r" (__trap_vector) );
}


/* Enable interrupts. */
static inline void rvlib_interrupt_enable(void)
{
    /* Set MSTATUS.MIE = 1. */
    asm volatile ( "csrsi mstatus, 8" );
}


/* Disable interrupts. */
static inline void rvlib_interrupt_disable(void)
{
    /* Set MSTATUS.MIE = 0. */
    asm volatile ( "csrci mstatus, 8" );
}


/*
 * Enable or disable software interrupts.
 *
 * When enabled, the application must implement "handle_software_interrupt()".
 */
static inline void rvlib_enable_software_interrupt(int enable)
{
    // Update MIE.MSIE.
    if (enable) {
        asm volatile ( "csrs mie, 8" );
    } else {
        asm volatile ( "csrc mie, 8" );
    }
}


/*
 * Enable or disable timer interrupts.
 *
 * When enabled, the application must implement "handle_timer_interrupt()".
 */
static inline void rvlib_enable_timer_interrupt(int enable)
{
    // Update MIE.MTIE.
    const uint32_t MIE_MTIE = 0x80;
    if (enable) {
        asm volatile ( "csrs mie, %0" : : "r" (MIE_MTIE) );
    } else {
        asm volatile ( "csrs mie, %0" : : "r" (MIE_MTIE) );
    }
}


/*
 * Enable or disable external interrupts.
 *
 * When enabled, the application must implement "handle_external_interrupt()".
 */
static inline void rvlib_enable_external_interrupt(int enable)
{
    // Update MIE.MEIE.
    const uint32_t MIE_MEIE = 0x800;
    if (enable) {
        asm volatile ( "csrs mie, %0" : : "r" (MIE_MEIE) );
    } else {
        asm volatile ( "csrs mie, %0" : : "r" (MIE_MEIE) );
    }
}

#endif  // RVLIB_INTERRUPT_H_
