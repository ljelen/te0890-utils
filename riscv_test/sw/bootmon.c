/*
 * Boot monitor program for RISC-V system.
 *
 * This program accepts text commands via the serial port.
 * It can be used to test a few simple things in the RISC-V system.
 * It also supports a command that can be used to upload
 * a program file into the RISC-V memory and start executing it.
 *
 * This program can be useful as a default program image
 * to execute when the RISC-V system starts up.
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
#include "rvlib_time.h"
#include "rvlib_gpio.h"
#include "rvlib_uart.h"
#include "rvlib_spiflash.h"


/* Hexboot helper function (written in assembler). */
extern void bootmon_hexboot_helper(uint32_t uart_base_addr);


static char scratchbuf[40];


/*
 * Print a string to the console.
 */
static void print_str(const char *msg)
{
    while (*msg != '\0') {
        rvlib_putchar(*msg);
        msg++;
    }
}


/*
 * Print an unsigned integer to the console as a decimal number.
 */
static void print_uint(unsigned int val)
{
    char *p = scratchbuf + sizeof(scratchbuf) - 1;
    *p = '\0';
    do {
        p--;
        *p = '0' + val % 10;
        val /= 10;
    } while (val != 0);
    print_str(p);
}


/*
 * Print an unsigned integer to the console as a hexadecimal number.
 */
static void print_uint_hex(unsigned int val, unsigned int width)
{
    static const char hexdigits[16] = "0123456789abcdef";
    while (width < 8 && (val >> (width << 2)) > 0) {
        width++;
    }
    while (width > 0) {
        width--;
        unsigned int d = (val >> (width << 2)) & 0xf;
        rvlib_putchar(hexdigits[d]);
    }
}


/*
 * Print a 64-bit unsigned integer to the console as a decimal number.
 */
static void print_uint64(uint64_t val)
{
    char *p = scratchbuf + sizeof(scratchbuf) - 1;
    *p = '\0';
    do {
        p--;
        *p = '0' + val % 10;
        val /= 10;
    } while (val != 0);
    print_str(p);
}


/* Print end-of-line characters to the console. */
static void print_endln(void)
{
    rvlib_putchar('\r');
    rvlib_putchar('\n');
}


/* Parse decimal or hexadecimal number. */
static int parse_uint(const char *s, uint32_t *val)
{
    uint32_t v = 0;
    int good = 0;
    size_t p = 0;

    while (s[p] == ' ') {
        p++;
    }

    if (s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X')) {
        p += 2;
        while (1) {
            char c = s[p];
            uint32_t d;
            if (c >= '0' && c <= '9') {
                d = s[p] - '0';
            } else if (c >= 'a' && c <= 'f') {
                d = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                d = c - 'A' + 10;
            } else {
                break;
            }
            if (v > UINT32_MAX / 16) {
                return -1;
            }
            v = (v << 4) + d;
            good = 1;
            p++;
        }
    } else {
        while (1) {
            char c = s[p];
            if (c < '0' || c > '9') {
                break;
            }
            if (v > UINT32_MAX / 10) {
                return -1;
            }
            v = 10 * v  + c - '0';
            good = 1;
            p++;
        }
    }

    if (good) {
        *val = v;
        return p;
    } else {
        return -1;
    }
}


/* Read one line of text from the serial port. */
static void read_command(char *cmdbuf, size_t maxlen, int cmd_echo)
{
    size_t pos = 0;

    while (1) {

        // Read next character.
        int c;
        do {
            c = rvlib_uart_recv_byte(RVLIB_DEFAULT_UART_ADDR);
        } while (c == -1);

        // Check for CR or LF to end the command.
        if (c == '\r' || c == '\n') {
            break;
        }

        // Replace TAB by space.
        if (c == '\t') {
            c = ' ';
        }

        if (c == '\b') {
            // Handle backspace.
            if (pos > 0) {
                pos--;
            }
        } else if (pos + 1 < maxlen) {
            // Add character to command buffer.
            cmdbuf[pos] = c;
            pos++;
        } else {
            // Ignore characters while command buffer is full.
            continue;
        }

        // Echo the command character.
        if (cmd_echo) {
            rvlib_putchar(c);
        }
    }

    // Mark end of command string.
    cmdbuf[pos] = '\0';
}


/* Simplify a received command:
   convert to lower case and strip redundant white space. */
void simplify_command(char *cmdbuf)
{
    char *pdst = cmdbuf;
    char *psrc = cmdbuf;
    int got_ws = 0;

    while (*psrc == ' ') {
        psrc++;
    }

    while (*psrc != '\0') {
        char c = *psrc;
        psrc++;
        if (c == ' ') {
            got_ws = 1;
        } else {
            if (got_ws) {
                *pdst = ' ';
                pdst++;
                got_ws = 0;
            }
            if (c >= 'A' && c <= 'Z') {
                c |= 0x20;
            }
            *pdst = c;
            pdst++;
        }
    }

    *pdst = '\0';
}


/* Show instruction cycle counter. */
void show_rdcycle(void)
{
    uint64_t cycles = get_cycle_counter();
    print_str("RDCYCLE = ");
    print_uint64(cycles);
    print_endln();
}


/* Show GPIO input state. */
void show_gpio_input(void)
{
    for (int gpio_index = 0; gpio_index < 2; gpio_index++) {
        print_str("GPIO");
        print_uint(gpio_index + 1);
        rvlib_putchar('=');

        uint32_t base_addr =
            (gpio_index == 0) ? RVSYS_ADDR_GPIO1 : RVSYS_ADDR_GPIO2;
        rvlib_gpio_set_drive(base_addr, 0);
        usleep(1000);
        
        for (int i = 0; i < 32; i++) {
            int v = rvlib_gpio_get_channel_input(base_addr, i);
            rvlib_putchar('0' + v);
        }
        rvlib_putchar(' ');
    }
    print_endln();
}


/* Repeatedly show GPIO input state until Enter received from console. */
void watch_gpio_input(void)
{
    print_str("Watching GPIO, press Enter to stop ...\r\n");
    while (1) {
        show_gpio_input();
        usleep(100000);
        int c = rvlib_uart_recv_byte(RVLIB_DEFAULT_UART_ADDR);
        if (c == '\r' || c == '\n') {
            break;
        }
    }
}


/*
 * Test GPIO input/output.
 * This test assumes that the actual FPGA I/O pins are floating.
 */
static void test_gpio_inout(void)
{
    for (int gpio_index = 0; gpio_index < 2; gpio_index++) {

        print_str("Testing GPIO");
        print_uint(gpio_index + 1);
        rvlib_putchar(' ');

        uint32_t base_addr =
            (gpio_index == 0) ? RVSYS_ADDR_GPIO1 : RVSYS_ADDR_GPIO2;
        rvlib_gpio_set_drive(base_addr, 0xffffffff);

        int ok = 1;

        for (int invert = 0; invert <= 1; invert++) {

            rvlib_putchar('.');

            uint32_t bgpattern = (invert) ? 0xffffffff : 0;
            rvlib_gpio_set_output(base_addr, bgpattern);

            for (int i = 0; i < 32; i++) {

                rvlib_gpio_set_channel_output(base_addr, i, !invert);
                usleep(100);

                uint32_t v = rvlib_gpio_get_input(base_addr);
                if (v != (bgpattern ^ (1 << i))) {
                    ok = 0;
                }

                rvlib_gpio_set_channel_output(base_addr, i, invert);
                usleep(100);

                v = rvlib_gpio_get_input(base_addr);
                if (v != bgpattern) {
                    ok = 0;
                }
            }
        }

        rvlib_putchar('.');

        rvlib_gpio_set_drive(base_addr, 0);

        if (ok) {
            print_str(" OK\r\n");
        } else {
            print_str(" FAIL\r\n");
        }
    }
}


/*
 * Very simple memory access test.
 *
 * This could potentially catch issues where write transactions
 * are incorrectly mapped to byte-enable signals.
 */
void test_mem_access(void)
{
    static volatile uint32_t testbuf[2];
    int ok = 1;

    print_str("Testing memory access ... ");

    // Init test buffer.
    memcpy((uint32_t*)testbuf, "abcd0123", 8);

    // Check word size read access.
    int w0 = 0x64636261;
    int w1 = 0x33323130;
    if (testbuf[0] != w0 || testbuf[1] != w1) {
        ok = 0;
    }

    for (int i = 0; i < 8; i++) {

        // Check byte size read access.
        unsigned char c = ((volatile unsigned char *)testbuf)[i];
        unsigned char x = (i < 4) ? (0x61 + i) : (0x30 + i - 4);
        if (c != x) {
            ok = 0;
        }

        // Byte size write access.
        ((volatile unsigned char *)testbuf)[i] = ~c;

        // Check effect of byte write on word read.
        if (i < 4) {
            w0 ^= (0xff << (8 * i));
        } else {
            w1 ^= (0xff << (8 * i - 32));
        }
        if (testbuf[0] != w0 || testbuf[1] != w1) {
            ok = 0;
        }
        
    }

    for (int i = 0; i < 4; i++) {

        // Check uint16 read access.
        uint16_t v = ((volatile uint16_t *)testbuf)[i];
        uint16_t x = (i == 0) ? 0x6261 :
                     (i == 1) ? 0x6463 :
                     (i == 2) ? 0x3130 : 0x3332;
        if (v != (x ^ 0xffff)) {
            ok = 0;
        }

        // uint16 write access.
        ((volatile uint16_t *)testbuf)[i] = ~v;

        // Check effect of uint16 write on word read.
        if (i < 2) {
            w0 ^= (0xffff << (16 * i));
        } else {
            w1 ^= (0xffff << (16 * i - 32));
        }
        if (testbuf[0] != w0 || testbuf[1] != w1) {
            ok = 0;
        }
        
    }

    if (ok) {
        print_str("OK\r\n");
    } else {
        print_str("FAIL\r\n");
    }
}


/* Load and execute HEX file. */
void do_hexboot(void)
{
    print_str("Reading HEX data ... ");
    bootmon_hexboot_helper(RVSYS_ADDR_UART);
}


/* Handle "led ..." subcommand. */
static int set_led_subcommand(const char *cmdbuf)
{
    const char *pcmd = cmdbuf;
    int led_channel;
    int led_state;

    if (strncmp(pcmd, "red ", 4) == 0) {
        led_channel = RVLIB_LED_RED_CHANNEL;
        pcmd += 4;
    } else if (strncmp(pcmd, "green ", 6) == 0) {
        led_channel = RVLIB_LED_GREEN_CHANNEL;
        pcmd += 6;
    } else {
        return -1;
    }

    if (strncmp(pcmd, "on", 3) == 0) {
        led_state = 1;
    } else if (strncmp(pcmd, "off", 4) == 0) {
        led_state = 0;
    } else {
        return -1;
    }

    rvlib_set_led(led_channel, led_state);
    return 1;
}


/* Handle "setgpioN ..." subcommand. */
static int set_gpio_subcommand(const char *cmdbuf)
{
    const char *pcmd = cmdbuf;
    uint32_t base_addr;
    int gpio_channel = 0;
    int gpio_state;

    while (*pcmd == ' ') {
        pcmd++;
    }

    if (*pcmd == '1') {
        base_addr = RVSYS_ADDR_GPIO1;
    } else if (*pcmd == '2') {
        base_addr = RVSYS_ADDR_GPIO2;
    } else {
        return -1;
    }
    pcmd++;

    if (*pcmd != ' ') {
        return -1;
    }
    pcmd++;

    do {
        char c = *pcmd;
        if (c < '0' || c > '9') {
            return -1;
        }
        gpio_channel = 10 * gpio_channel + c - '0';
        pcmd++;
    } while (*pcmd != ' ');
    pcmd++;

    if (*pcmd == '0') {
        gpio_state = 0;
    } else if (*pcmd == '1') {
        gpio_state = 1;
    } else if (*pcmd == 'z') {
        gpio_state = -1;
    } else {
        return -1;
    }
    pcmd++;

    if (*pcmd != '\0') {
        return -1;
    }

    if (gpio_channel > 31) {
        return -1;
    }

    int drive = (gpio_state >= 0);
    if (drive) {
        rvlib_gpio_set_channel_output(base_addr, gpio_channel, gpio_state);
    }
    rvlib_gpio_set_channel_drive(base_addr, gpio_channel, drive);

    return 1;
}


/* Read SPI flash device ID. */
static void spiflash_readid(void)
{
    struct rvlib_spiflash_device_id devid;

    print_str("SPI flash identification:\r\n");

    rvlib_spiflash_init();
    rvlib_spiflash_read_id(&devid);

    print_str("  manufacturer ID = 0x");
    print_uint_hex(devid.manufacturer_id, 2);
    print_endln();
    print_str("  device ID       = 0x");
    print_uint_hex(devid.device_id, 4);
    print_endln();
}


/* Test program/erase functions. */
static void spiflash_writetest(void)
{
    const uint32_t page_size = 256;
    const uint32_t sector_size = 64 * 1024;
    const uint32_t flash_size = 8 * 1024 * 1024;
    unsigned char buf[32];

    print_str("Test SPI flash program/erase functions:\r\n");

    rvlib_spiflash_init();

    /* Erase the last sector of the memory. */
    uint32_t sector_addr = flash_size - sector_size;
    print_str("  Erasing sector at 0x");
    print_uint_hex(sector_addr, 6);
    print_str(" ... ");

    int status = rvlib_spiflash_sector_erase(sector_addr);
    if (status < 0) {
        print_str("ERROR code -");
        print_uint(-status);
        print_endln();
    } else {
        print_str("OK\r\n");
    }

    /* Read back to check that the sector was erased. */
    print_str("  Read back erased sector ... ");
    int good = 1;
    for (unsigned int p = 0; p + sizeof(buf) <= sector_size; p += sizeof(buf)) {
        rvlib_spiflash_read_mem(sector_addr + p, buf, sizeof(buf));
        for (unsigned int i = 0; i < sizeof(buf); i++) {
            if (buf[i] != 0xff) {
                good = 0;
            }
        }
    }
    if (good) {
        print_str("OK\r\n");
    } else {
        print_str("FAILED!\r\n");
    }

    /* Program the first two pages of the erased sector. */
    static const char test_message[2][16] = {
        "Flash write test",
        "Another testpage"
    };
    uint64_t testdata[2];
    for (unsigned int page = 0; page < 2; page++) {
        testdata[page] = get_cycle_counter();
        memcpy(buf, test_message[page], 16);
        for (unsigned int i = 0; i < 8; i++) {
            buf[16 + i] = testdata[page] >> (i << 3);
        }
        uint32_t page_addr = sector_addr + page * page_size;
        print_str("  Programming page at 0x");
        print_uint_hex(page_addr, 6);
        print_str(" ... ");
        status = rvlib_spiflash_page_program(page_addr, buf, 24);
        if (status < 0) {
            print_str("ERROR code -");
            print_uint(-status);
            print_endln();
        } else {
            print_str("OK\r\n");
        }
    }

    /* Read back the programmed pages. */
    for (unsigned int page = 0; page < 2; page++) {
        uint32_t page_addr = sector_addr + page * page_size;
        print_str("  Reading back page at 0x");
        print_uint_hex(page_addr, 6);
        print_str(" ... ");
        rvlib_spiflash_read_mem(page_addr, buf, sizeof(buf));
        good = 1;
        for (unsigned int i = 0; i < 16; i++) {
            if (buf[i] != test_message[page][i]) {
                good = 0;
            }
        }
        for (unsigned int i = 0; i < 8; i++) {
            if (buf[16 + i] != ((testdata[page] >> (i << 3)) & 0xff)) {
                good = 0;
            }
        }
        for (unsigned int i = 24; i < sizeof(buf); i++) {
            if (buf[i] != 0xff) {
                good = 0;
            }
        }
        if (good) {
            print_str("OK\r\n");
        } else {
            print_str("FAILED!\r\n");
        }
    }
}


/* Read data from SPI flash. */
static int spiflash_read(uint32_t addr, uint32_t len)
{
    unsigned char buf[16];

    print_str("Reading from SPI flash:\r\n");

    rvlib_spiflash_init();

    while (len > 0) {
        size_t nbytes = (len > 16) ? 16 : len;
        rvlib_spiflash_read_mem(addr, buf, nbytes);
        print_uint_hex(addr, 8);
        rvlib_putchar(':');
        for (unsigned int i = 0; i < nbytes; i++) {
            rvlib_putchar(' ');
            print_uint_hex(buf[i], 2);
        }
        print_endln();
        addr += nbytes;
        len -= nbytes;
    }

    return 0;
}


/* Handle "spiflash ..." subcommand. */
static int spiflash_subcommand(const char *cmdbuf)
{
    const char *pcmd = cmdbuf;

    while (*pcmd == ' ') {
        pcmd++;
    }

    if (*pcmd == '\0' || strncmp(pcmd, "help", 5) == 0) {
        print_str(
            "spiflash subcommands:\r\n"
            "  spiflash readid             - Read flash device ID\r\n"
            "  spiflash read <addr> <len>  - Read bytes from flash memory\r\n"
            "  spiflash writetest          - Test program/erase functions\r\n"
            "\r\n");
        return 0;
    }

    if (strncmp(pcmd, "readid", 7) == 0) {
        spiflash_readid();
        return 0;
    } else if (strncmp(pcmd, "read", 4) == 0) {
        uint32_t addr, len;
        pcmd += 4;
        int ret = parse_uint(pcmd, &addr);
        if (ret < 0) {
            return ret;
        }
        pcmd += ret;
        ret = parse_uint(pcmd, &len);
        if (ret < 0) {
            return ret;
        }
        return spiflash_read(addr, len);
    } else if (strncmp(pcmd, "writetest", 10) == 0) {
        spiflash_writetest();
        return 0;
    } else {
        return -1;
    }
}


void show_help(void)
{
    print_str(
        "Commands:\r\n"
        "  help                     - Show this text\r\n"
        "  echo {on|off}            - Enable or disable command echo\r\n"
        "  led {red|green} {on|off} - Turn LED on or off\r\n"
        "  rdcycle                  - Show instruction cycle counter\r\n"
        "  getgpio                  - Show GPIO input state\r\n"
        "  watchgpio                - Watch GPIO input state\r\n"
        "  setgpio{1|2} {0..31} {0|1|Z} - Set GPIO output pin state\r\n"
        "  testgpio                 - Test GPIO input/output\r\n"
        "  testmem                  - Test simple memory access\r\n"
        "  spiflash ...             - SPI flash command\r\n"
        "  hexboot                  - Load and execute HEX file\r\n"
        "\r\n");
}


void command_loop(void)
{
    static char cmdbuf[80];
    int cmd_echo = 1;

    while (1) {

        // Show prompt.
        print_str(">> ");

        // Read command.
        read_command(cmdbuf, sizeof(cmdbuf), cmd_echo);
        if (cmd_echo) {
            print_endln();
        }

        // Process command.
        simplify_command(cmdbuf);

        int ret = 0;

        if (strncmp(cmdbuf, "help", sizeof(cmdbuf)) == 0) {
            show_help();
        } else if (strncmp(cmdbuf, "echo on", sizeof(cmdbuf)) == 0) {
            cmd_echo = 1;
            ret = 1;
        } else if (strncmp(cmdbuf, "echo off", sizeof(cmdbuf)) == 0) {
            cmd_echo = 0;
            ret = 1;
        } else if (strncmp(cmdbuf, "led ", 4) == 0) {
            ret = set_led_subcommand(cmdbuf + 4);
        } else if (strncmp(cmdbuf, "rdcycle", sizeof(cmdbuf)) == 0) {
            show_rdcycle();
        } else if (strncmp(cmdbuf, "getgpio", sizeof(cmdbuf)) == 0) {
            show_gpio_input();
        } else if (strncmp(cmdbuf, "watchgpio", sizeof(cmdbuf)) == 0) {
            watch_gpio_input();
        } else if (strncmp(cmdbuf, "setgpio", 7) == 0) {
            ret = set_gpio_subcommand(cmdbuf + 7);
        } else if (strncmp(cmdbuf, "testgpio", sizeof(cmdbuf)) == 0) {
            test_gpio_inout();
        } else if (strncmp(cmdbuf, "testmem", sizeof(cmdbuf)) == 0) {
            test_mem_access();
        } else if (strncmp(cmdbuf, "spiflash", 8) == 0) {
            ret = spiflash_subcommand(cmdbuf + 8);
        } else if (strncmp(cmdbuf, "hexboot", sizeof(cmdbuf)) == 0) {
            do_hexboot();
        } else if (cmdbuf[0] != '\0') {
            ret = -1;
        }

        if (ret < 0) {
            print_str("ERROR: unknown command\r\n");
        } else if (ret > 0) {
            print_str("OK\r\n");
        }
    }
}


int main(void)
{
    rvlib_set_red_led(1);
    print_str("TE0890 RISC-V boot monitor\r\n\r\n");
    usleep(10000);
    rvlib_set_red_led(0);

    show_help();
    command_loop();

    return 0;
}
