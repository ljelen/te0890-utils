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

#ifndef RVLIB_SPIFLASH_H_
#define RVLIB_SPIFLASH_H_

#include <stddef.h>
#include <stdint.h>


/* Error codes. */
#define RVLIB_SPIFLASH_ERR_FAILED   (-1)
#define RVLIB_SPIFLASH_ERR_TIMEOUT  (-2)
#define RVLIB_SPIFLASH_ERR_NOTREADY (-3)


/* Data structure returned by READ ID operation. */
struct rvlib_spiflash_device_id {
    uint8_t  manufacturer_id;
    uint16_t device_id;
};


/* Initialize communication to the flash memory. */
void rvlib_spiflash_init(void);

/* Read flash memory device ID. */
void rvlib_spiflash_read_id(struct rvlib_spiflash_device_id *devid);

/* Read data from the flash memory into the specified buffer. */
void rvlib_spiflash_read_mem(uint32_t address,
                             unsigned char *buf,
                             size_t nbytes);

/*
 * Program bytes to the flash memory.
 *
 * Parameters:
 *     address: Byte address of the first location to program.
 *     data:    Data to program to the flash memory.
 *     nbytes:  Number of bytes to program.
 *
 * All programmed bytes must be located in the same flash page.
 *
 * Returns:
 *     0 if programming completes successfully;
 *     a negative error code if the operation failed:
 *     RVLIB_SPIFLASH_ERR_FAILED if programming failed;
 *     RVLIB_SPIFLASH_ERR_TIMEOUT if the operation timed out;
 *     RVLIB_SPIFLASH_ERR_NOTREADY if a program/erase operation is still busy.
 */
int rvlib_spiflash_page_program(uint32_t address,
                                const unsigned char *data,
                                size_t nbytes);

/*
 * Erase a single sector.
 *
 * Parameters:
 *     address: Byte address of the sector to erase.
 *              Any address within the target sector can be used.
 *
 * Returns:
 *     0 if the erase operation completes successfully;
 *     a negative error code if the operation failed:
 *     RVLIB_SPIFLASH_ERR_FAILED if the erase operation failed;
 *     RVLIB_SPIFLASH_ERR_TIMEOUT if the operation timed out;
 *     RVLIB_SPIFLASH_ERR_NOTREADY if a program/erase operation is still busy.
 */
int rvlib_spiflash_sector_erase(uint32_t address);

#endif  // RVLIB_SPIFLASH_H_
