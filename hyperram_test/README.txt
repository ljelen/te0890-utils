
  WARNING: Data corruption
  ------------------------

This HyperRAM controller shows occasional data corruption.

On my TE0890 board, a burst of errors occurs about once every 10 hours
during intensive memory testing. The cause of these errors is unknown.

For ISSI HyperRAM rev D devices, very high error rates have been reported
that only occur for single-word bursts (burst length 1 or 2 bytes).
These are most likely caused by silicon errata.

ISSI HyperRAM revision D devices have errata describing malfunctioning
single-word burst access in combination with variable latency.
A possible workaround is to enable fixed latency by changing bit 3 of
parameter "config0_data" from 0 to 1 in entity "hyperram_ctrl".
I have not tested this.
(based on errata document ISSI AN66WX001)

Cypress HyperRAM revision B devices have errata describing intermittent
errors in case of variable latency. A possible workaround is to enable
fixed latency. I have not tested this.


  HyperRAM interface
  ==================

This design contains an interface core for the HyperRAM memory
on the TE0890 board. It also contains a test driver which performs
a simple memory test of the HyperRAM.

The HyperRAM interface core can be used as a component to use
the HyperRAM in larger designs. The memory test design is mostly useful
as an example, and to check that the HyperRAM interface is correctly
implemented.

The HyperRAM interface core is designed to run only at 100 MHz.
It will probably not work at other clock frequencies, or on other
boards than the TE0890. The test design runs at 100 MHz from
the on-board oscillator.


  Test method
  -----------

The test is based on a moving-inversions march test. A test sequence
consists of the following steps:
 * Fill the memory with a fixed test pattern.
 * Read the memory to check the pattern while simultaneously writing
   the bit-inverted pattern.
 * Read the memory to check the inverted pattern, while simultaneously
   writing the original pattern.

The test is performed with different test patterns and at different
burst sizes. Testing with different burst sizes is important because
the burst logic of the HyperRAM interface core is quite complicated.

In addition to fixed 16-bit test patterns, the test is also done
with a random sequence of data words.


  Usage
  -----

Use Vivado to open the project file hyperram_test/vivado/hyperram_test.xpr.
Then simply click "Generate Bitstream".
Program the bitfile on the TE0890 board via JTAG.

The memory test shows its status on the two LEDs:
 * Red LED:    Turns on when the test has detected at least one error.
 * Green LED:  Turns on when the test has detected zero errors and
               has completed at least one full round of the test.

If everything works well, the green LED should turn on after
running for about 70 seconds.

The test progress can be monitored by attaching an FTDI-USB cable
to the FTDI header of the TE0890 board. The test driver outputs
data at 115200 bps.

The output should look something like this:

    RAM Test
  R=0000 F=00000000
  P=0000 B=001 B=002 B=003 B=004 B=005 B=010 B=03f B=200 F=00000000
  P=ff00 B=001 B=002 B=003 B=004 B=005 B=010 B=03f B=200 F=00000000
  ...
  R=0001 F=00000000

All numbers in the output are hexadecimal.
The meaning of the messages is as follows:
  R=nnnn     : Start a new round and display the round counter.
  F=nnnnnnnn : Total number of faults detected up to this point.
  P=nnnn     : Start testing with the displayed test pattern.
  B=nnn      : Start testing with the displayed burst length.
  E=a-bbbbbb-c-dddd-eeee : Describes the first few errors detected at the
               current burst length.
               a      = 0 or 1 to indicate error in march element 2 or 3
               bbbbbb = approximate word address where error occured
               c      = byte mask
               dddd   = expected data
               eeee   = actual read data


  Simulation
  ----------

I have done successful simulations of this design, using a VHDL model
of a HyperRAM device that I downloaded from Cypress. It is a model for
the Cypress S27KL0641. They call it a "Verilog model", but the download
also contains a VHDL version.

The file "sim_top.vhd" connects the memory test design to the HyperRAM model.
Some changes to the memory test design are needed to make the simulation
practical: reducing the size of the memory space under test, and avoiding
waiting for the RS-232 driver to send each output character.


  License
  -------

This VHDL code is licensed under the CC0 1.0 license.

To the extent possible under law, the author(s) have dedicated all
copyright and related and neighboring rights to this software to
the public domain worldwide. This software is distributed without
any warranty.

See <http://creativecommons.org/publicdomain/zero/1.0/>.

----
