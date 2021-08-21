/*
 * SPI Flash Memory driver.
 *
 * Written in 2021 by Joris van Rantwijk.
 *
 * To the extent possible under law, the author has dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * See <http://creativecommons.org/publicdomain/zero/1.0/>
 */

#include "rvlib_hardware.h"
#include "rvlib_time.h"
#include "rvlib_spiflash.h"


/* SPI controller interface. */
#define RVLIB_SPIFLASH_REG_STATUS           0
#define RVLIB_SPIFLASH_REG_SLAVESEL         4
#define RVLIB_SPIFLASH_REG_DATA             8
#define RVLIB_SPIFLASH_BIT_STATUS_BUSY      0
#define RVLIB_SPIFLASH_BIT_STATUS_CMDRDY    1
#define RVLIB_SPIFLASH_BIT_STATUS_READRDY   2

/* Properties of the flash device. */
#define SPIFLASH_PROGRAM_TIMEOUT_US         5000
#define SPIFLASH_ERASE_TIMEOUT_US           (3 * 1000 * 1000UL)

/* SPI flash commands. */
#define SPIFLASH_CMD_READ_ID                0x9f
#define SPIFLASH_CMD_READ                   0x03
#define SPIFLASH_CMD_WRITE_ENABLE           0x06
#define SPIFLASH_CMD_READ_FLAGS             0x70
#define SPIFLASH_CMD_CLEAR_FLAGS            0x50
#define SPIFLASH_CMD_PAGE_PROGRAM           0x02
#define SPIFLASH_CMD_SECTOR_ERASE           0xd8
#define SPIFLASH_BIT_FLAGS_PROGRAM_ERROR    4
#define SPIFLASH_BIT_FLAGS_ERASE_ERROR      5
#define SPIFLASH_BIT_FLAGS_READY            7


/* Send a byte to the SPI slave. */
static void spi_send_byte(uint8_t b)
{
    /* Wait until the SPI controller is ready for a new command byte. */
    while (1) {
        uint32_t status = rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_STATUS);
        if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_CMDRDY)) != 0) {
            break;
        }
    }

    /* Send the command. */
    rvlib_hw_write_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_DATA, b);
}


/* Read data bytes from the SPI slave. */
static void spi_read_bytes(unsigned char *buf, size_t nbytes)
{
    size_t p = 0;
    size_t ncmd = nbytes;
    while (p < nbytes) {
        uint32_t status = rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_STATUS);
        if (ncmd > 0) {
            if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_CMDRDY)) != 0) {
                /* Capture next byte. */
                rvlib_hw_write_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_DATA, 0x100);
                ncmd--;
            }
        }
        if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_READRDY)) != 0) {
            /* Got next data byte. */
            uint32_t data = rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_DATA);
            buf[p] = data;
            p++;
        }
    }
}


/* Wait until the SPI controller is idle, then deselect the slave. */
static void spi_end_transaction(void){

    /* Wait until the SPI controller is idle. */
    while (1) {
        uint32_t status = rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_STATUS);
        if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_BUSY)) == 0) {
            break;
        }
    }

    /* Deselect the SPI slave. */
    rvlib_hw_write_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_SLAVESEL, 0);
}


/* Send a one-byte command. */
static void spi_command_simple(uint8_t cmd)
{
    spi_send_byte(cmd);
    spi_end_transaction();
}


/* Send a one-byte command, then read reply data. */
static void spi_command_read(uint8_t cmd, unsigned char *buf, size_t nbytes)
{
    spi_send_byte(cmd);
    spi_read_bytes(buf, nbytes);
    spi_end_transaction();
}


/* Send a command and 24-bit address, then read reply data. */
static void spi_command_addr_read(uint8_t cmd,
                                  uint32_t addr,
                                  unsigned char *buf,
                                  size_t nbytes)
{
    spi_send_byte(cmd);
    spi_send_byte(addr >> 16);
    spi_send_byte(addr >> 8);
    spi_send_byte(addr);
    spi_read_bytes(buf, nbytes);
    spi_end_transaction();
}


/* Send a command and 24-bit address, then write data bytes. */
static void spi_command_addr_write(uint8_t cmd,
                                   uint32_t addr,
                                   const unsigned char *data,
                                   size_t nbytes)
{
    spi_send_byte(cmd);
    spi_send_byte(addr >> 16);
    spi_send_byte(addr >> 8);
    spi_send_byte(addr);
    for (size_t p = 0; p < nbytes; p++) {
        spi_send_byte(data[p]);
    }
    spi_end_transaction();
}


/* Wait until a program/erase operation completes. */
static uint8_t spiflash_poll_completion(uint32_t timeout_us)
{
    unsigned char flags;

    uint64_t end_time = get_cycle_counter();
    end_time += RVLIB_CPU_FREQ_MHZ * (uint64_t)timeout_us;

    while (1) {
        spi_command_read(SPIFLASH_CMD_READ_FLAGS, &flags, 1);
        if ((flags & (1 << SPIFLASH_BIT_FLAGS_READY)) != 0) {
            break;
        }

        uint64_t time_now = get_cycle_counter();
        uint64_t tremain = end_time - time_now;
        if (tremain >= (((uint64_t)1) << 63)) {
            break;
        }
    }

    return flags;
}


/* Initialize communication to the flash memory. */
void rvlib_spiflash_init(void)
{
    /* Wait until the SPI controller is idle and drain the read buffer. */
    while (1) {
        uint32_t status = rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_STATUS);
        if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_READRDY)) != 0) {
            rvlib_hw_read_reg(RVSYS_ADDR_SPIFLASH + RVLIB_SPIFLASH_REG_DATA);
        } else if ((status & (1 << RVLIB_SPIFLASH_BIT_STATUS_BUSY)) == 0) {
            break;
        }
    }

    /* The first few SPI clock cycles after power-up may not get through,
       so we have to assume this first command may fail. */
    spi_command_simple(0xff);

    /* Send 0xFF to return to extended SPI mode (from dual SPI mode). */
    spi_command_simple(0xff);

    /* Clear flag status register. */
    spi_command_simple(SPIFLASH_CMD_CLEAR_FLAGS);

    /* Wait until current operation ends. */
    spiflash_poll_completion(SPIFLASH_ERASE_TIMEOUT_US);
}


/* Read flash memory device ID. */
void rvlib_spiflash_read_id(struct rvlib_spiflash_device_id *devid)
{
    unsigned char buf[3];
    spi_command_read(SPIFLASH_CMD_READ_ID, buf, 3);
    devid->manufacturer_id = buf[0];
    devid->device_id = (((uint16_t)buf[1]) << 8) | buf[2];
}


/* Read data from the flash memory into the specified buffer. */
void rvlib_spiflash_read_mem(uint32_t address,
                             unsigned char *buf,
                             size_t nbytes)
{
    spi_command_addr_read(SPIFLASH_CMD_READ, address, buf, nbytes);
}


/* Program bytes to the flash memory. */
int rvlib_spiflash_page_program(uint32_t address,
                                const unsigned char *data,
                                size_t nbytes)
{
    unsigned char flags;

    /* Check if the device is ready. */
    spi_command_read(SPIFLASH_CMD_READ_FLAGS, &flags, 1);
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_READY)) == 0) {
        return RVLIB_SPIFLASH_ERR_NOTREADY;
    }

    /* Clear previous errors. */
    spi_command_simple(SPIFLASH_CMD_CLEAR_FLAGS);

    /* Enable write access. */
    spi_command_simple(SPIFLASH_CMD_WRITE_ENABLE);

    /* Start the PAGE PROGRAM operation. */
    spi_command_addr_write(SPIFLASH_CMD_PAGE_PROGRAM, address, data, nbytes);

    /* Wait until the operation completes. */
    flags = spiflash_poll_completion(SPIFLASH_PROGRAM_TIMEOUT_US);

    /* Report result. */
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_READY)) == 0) {
        return RVLIB_SPIFLASH_ERR_TIMEOUT;
    }
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_PROGRAM_ERROR)) != 0) {
        spi_command_simple(SPIFLASH_CMD_CLEAR_FLAGS);
        return RVLIB_SPIFLASH_ERR_FAILED;
    }
    return 0;
}


/* Erase a single sector. */
int rvlib_spiflash_sector_erase(uint32_t address)
{
    unsigned char flags;

    /* Check if the device is ready. */
    spi_command_read(SPIFLASH_CMD_READ_FLAGS, &flags, 1);
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_READY)) == 0) {
        return RVLIB_SPIFLASH_ERR_NOTREADY;
    }

    /* Clear previous errors. */
    spi_command_simple(SPIFLASH_CMD_CLEAR_FLAGS);

    /* Enable write access. */
    spi_command_simple(SPIFLASH_CMD_WRITE_ENABLE);

    /* Start the SECTOR ERASE operation. */
    spi_command_addr_write(SPIFLASH_CMD_SECTOR_ERASE, address, 0, 0);

    /* Wait until the operation completes. */
    flags = spiflash_poll_completion(SPIFLASH_ERASE_TIMEOUT_US);

    /* Report result. */
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_READY)) == 0) {
        return RVLIB_SPIFLASH_ERR_TIMEOUT;
    }
    if ((flags & (1 << SPIFLASH_BIT_FLAGS_ERASE_ERROR)) != 0) {
        spi_command_simple(SPIFLASH_CMD_CLEAR_FLAGS);
        return RVLIB_SPIFLASH_ERR_FAILED;
    }
    return 0;
}

/* end */
