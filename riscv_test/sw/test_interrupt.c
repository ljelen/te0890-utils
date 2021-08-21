/*
 * Test interrupt handling on the RISC-V system.
 *
 * This program is designed to be compiled in freestanding mode
 * (without libc). It runs on a bare-metal RISC-V system,
 * using rvlib to access system peripherals.
 *
 * Written in 2021 by Joris van Rantwijk.
 *
 * To the extent possible under law, the author has dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * See <http://creativecommons.org/publicdomain/zero/1.0/>
 */

#include <stddef.h>
#include <stdint.h>
#include "rvlib_std.h"
#include "rvlib_hardware.h"
#include "rvlib_interrupt.h"
#include "rvlib_time.h"
#include "rvlib_gpio.h"
#include "rvlib_uart.h"


static volatile int timer_count_interrupts;
static volatile uint64_t timer_next_interrupt;
static volatile int test_state = 0;


static void print_str(const char *msg)
{
    while (*msg != '\0') {
        rvlib_putchar(*msg);
        msg++;
    }
}


static void print_hex(uint32_t val)
{
    static const char hexdigits[16] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        int t = (val >> (28 - 4*i)) & 0xf;
        rvlib_putchar(hexdigits[t]);
    }
}


static void print_uint(unsigned int val)
{
    char msg[12];
    char *p = msg + sizeof(msg) - 1;
    *p = '\0';
    do {
        p--;
        *p = '0' + val % 10;
        val /= 10;
    } while (val != 0);
    print_str(p);
}


/* Test timer and timer interrupt. */
static void test_timer(void)
{
    const int num_interrupts = 12;
    uint64_t scheduled_interrupt;
    int timer_ok = 1;

    print_str("\r\nTesting timer interrupt ...\r\n");

    // Clear timer.
    rvlib_timer_reset_counter();
    rvlib_timer_set_timecmp(UINT64_MAX);
    timer_count_interrupts = 0;

    // Enable timer interrupts.
    rvlib_enable_timer_interrupt(1);

    // Schedule an interrupt to occur in the future.
    scheduled_interrupt = RVLIB_CPU_FREQ_MHZ * 123450;
    print_str("scheduling interrupt to occur at ");
    print_uint((unsigned int)scheduled_interrupt);
    print_str("\r\n");
    rvlib_timer_set_timecmp(scheduled_interrupt);

    for (int i = 1; i <= num_interrupts; i++) {
        // Prepare to schedule the next interrupt.
        timer_next_interrupt = RVLIB_CPU_FREQ_MHZ * 12345 * (i + 1) * (i + 10);

        // Wait until the scheduled interrupt occurs.
        while (timer_count_interrupts < i) ;

        // Check timing of the interrupt.
        uint64_t timer_counter = rvlib_timer_get_counter();
        print_str("  got interrupt at ");
        print_uint((unsigned int)timer_counter);
        print_str("\r\n");
        if (timer_counter < scheduled_interrupt
            || timer_counter > scheduled_interrupt + 500) {
            timer_ok = 0;
        }

        // Remember when the next interrupt will occur.
        scheduled_interrupt = timer_next_interrupt;
        print_str("scheduled interrupt to occur at ");
        print_uint((unsigned int)scheduled_interrupt);
        print_str("\r\n");
    }

    // Cancel next interrupt.
    rvlib_timer_set_timecmp(UINT64_MAX);
    print_str("canceled next interrupt\r\n");

    // Wait and check no more interrupts.
    usleep(1000000);
    if (timer_count_interrupts != num_interrupts) {
        print_str("got spurious interrupt\r\n");
        timer_ok = 0;
    }

    // Disable timer interrupts.
    rvlib_enable_timer_interrupt(0);

    if (timer_ok) {
        print_str("timer test OK\r\n");
    } else {
        print_str("timer test FAILED\r\n");
    }
}


/* Test misaligned data access. */
static void test_misaligned_data(void)
{
    const int offset = 1;
    uint32_t buf[2] = { 0x01234567, 0x89abcdef };
    volatile uint32_t *badptr;

    // Create misaligned pointer but make sure the compiler
    // does not understand what we are doing.
    asm volatile ( "addi %0,%1,%2" : "=r" (badptr) : "r" (buf), "I" (offset) );

    test_state = 1;
    print_str("\r\nNow going to trigger misaligned data access ...\r\n");
    uint32_t val = *badptr;

    print_hex(val);

    print_str(" hmm, somehow got past misaligned data access\r\n");
    print_str("ERROR: no interrupt on misaligned data access\r\n");
}


static void helper_misaligned_branch(void)
{
    print_str(" hmm, somehow got through misaligned branch\r\n");
}


/* Test misaligned branch. */
static void test_misaligned_branch(void)
{
    const int offset = 2;
    void (*badptr)(void);

    // Create misaligned pointer but make sure the compiler
    // does not understand what we are doing.
    asm volatile ( "addi %0,%1,%2"
                   : "=r" (badptr)
                   : "r" (&helper_misaligned_branch), "I" (offset) );

    test_state = 3;
    print_str("\r\nNow going to trigger misaligned call ...\r\n");
    badptr();

    print_str("ERROR: no interrupt on misaligned branch\r\n");
}


/* Test misaligned branch. */
static void test_finished(void)
{
    print_str("\r\nTest finished.\r\n");
    _Exit(0);
}


/* Count timer interrupts. */
void handle_timer_interrupt(void)
{
    timer_count_interrupts += 1;
    rvlib_set_green_led(timer_count_interrupts & 1);
    rvlib_timer_set_timecmp(timer_next_interrupt);
}


/* Print message on unexpected trap, then halt program. */
void handle_unexpected_trap(uint32_t cause, uint32_t badaddr)
{
    rvlib_set_red_led(1);
    print_str("detected trap: cause=0x");
    print_hex(cause);
    print_str(" badaddr=0x");
    print_hex(badaddr);
    print_str("\r\n");

    switch (test_state) {
      case 1:
        /* continue testing after trap on misaligned data access */
        test_state = 2;
        test_misaligned_branch();
        break;
      case 3:
        /* finish test after trap on misaligned branch */
        test_state = 4;
        test_finished();
	break;
      default:
        print_str("ERROR: this should not happen\r\n");
    }

    _Exit(1);
}


/*
 * Main program.
 */
int main(void)
{
    rvlib_interrupt_init();
    rvlib_interrupt_enable();

    rvlib_set_red_led(0);
    print_str("Testing RISC-V interrupts\r\n");

    test_timer();
    test_misaligned_data();

    return 0;
}

