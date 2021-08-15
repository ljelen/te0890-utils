
# Main clock input from 100 MHz on-board oscillator.
create_clock -period 10.000 -name clk_100m -waveform {0.000 5.000} [get_ports clk_100m_pin]
set_input_jitter clk_100m 0.200

# JTAG clock, assume max 50 MHz.
create_clock -period 20.0 -name jtag_clk -waveform {0.0 10.0} [get_pins inst_bscane2/DRCK]
set_max_delay -from [get_clocks -include_generated_clocks clk_100m] -to [get_clocks jtag_clk] -datapath_only 10.0

# SPI timing
#
# This assumes SPI_CLK runs at 25 MHz and the system clock at 100 MHz
# (4 system clock cycles per SPI clock cycle).
#
# SPI_CLK timing
#
#   The SPI_CLK output is routed through STARTUP2E/USRCCLKO which has
#   uncertain delay.
#
#   Spartan-7 datasheet:
#     delay from USRCCLKO to CCLK = min 0.5 ns, max 7.5 ns
#
#   We can choose how we constrain the clock, but this choice impacts
#   the constraints for MISO and MOSI. The following seems reasonable.
#
#   Min delay:
#     delay sysclk to USRCCLKO:      2 ns
#     delay USRCCLKO to SPI_CLK:   + 0.5 ns
#     margin                       - 0.5 ns
#     min sysclk to SPI_CLK delay: = 2 ns
set_min_delay -to [get_pins inst_startupe2/USRCCLKO] 2

#   Max delay:
#     delay sysclk to USRCCLKO:     10 ns
#     delay USRCCLKO to SPI_CLK:   + 7.5 ns
#     margin                       + 0.5 ns
#     max sysclk to SPI_CLK delay: = 18 ns
set_max_delay -to [get_pins inst_startupe2/USRCCLKO] 10

# SPI_MOSI timing:
#
#   N25Q datasheet:
#     setup MOSI before rising SPI_CLK: min 2 ns
#     hold MOSI after rising SPI_CLK:   min 3 ns
#
#   SPI_CS can be constrained the same as MOSI.
#   The N25Q requires slightly stricter setup/hold times for SPI_CS,
#   but the controller keeps an additional sysclk cycle slack.
#
#   Setup analysis:
#     at  0 ns: sysclk rising edge that initiates MOSI transition
#     at 10 ns: reference sysclk edge for output delay timing
#     at 20 ns: sysclk rising edge that initiates SPI_CLK rising edge
#     at 22 ns: SPI_CLK rising edge  (20 ns + min delay sysclk to SPI_CLK)
#     at 20 ns: MOSI valid at N25Q   (22 ns - MOSI setup requirement)
#     at 17 ns: MOSI output valid    (20 ns - trace delay - margin)
#
#   The most accurate description would be through a multicycle path,
#   but it seems unnecessarily complicated. Without a multicycle path,
#   Vivado will run its output delay analysis relative to the rising
#   sysclk edge at 10 ns, following the launching edge at 0 ns.
#   The MOSI signal does not need to be valid for another 7 ns after
#   the reference edge.
#   So we can specify a small negative maximum output delay.
set_output_delay -max -2.0 [get_ports {spi_mosi spi_cs_l}]

#   Hold analysis:
#     at 20 ns: sysclk rising edge that initiates SPI_CLK rising edge
#     at 38 ns: SPI_CLK rising edge  (20 ns + max delay sysclk to SPI_CLK)
#     at 41 ns: MOSI hold at N25Q    (38 ns + MOSI hold requirement)
#     at 42 ns: MOSI output hold     (41 ns + margin)
#     at 40 ns: sysclk rising edge that initiates MOSI transition
#
#   Vivado will runs its output delay analysis relative to the rising
#   sysclk edge at 40 ns that initiates the MOSI transition. Following
#   this edge, the output must hold its previous state for another 2 ns.
#   To express this, we need to specify a negative minimum output delay.
set_output_delay -min -2.0 [get_ports {spi_mosi spi_cs_l}]

# SPI_MISO timing:
#
#   N25Q datasheet:
#     delay from SPI_CLK to MISO valid   = max 7 ns
#     delay from SPI_CLK to MISO invalid = min 1 ns (hold time)
#
#   The sysclk rising edge at  0 ns initiates a falling SPI_CLK edge.
#   The sysclk rising edge at 40 ns initiates the capturing of MISO input.
#
#   Setup analysis:
#     at  0 ns: sysclk rising edge
#     at 18 ns: SPI_CLK falling edge (0 ns + max delay sysclk to SPI_CLK)
#     at 25 ns: MISO output valid    (18 ns + max SPI_CLK to MISO delay)
#     at 27 ns: MISO input valid     (25 ns + trace delay + margin)
#     at 30 ns: reference sysclk edge for input delay timing
#     at 40 ns: sysclk rising edge that captures MISO
#
#   The most accurate description would be through a multicycle path,
#   but it seems unnecessarily complicated. Without a multicycle path,
#   Vivado will run its input delay analysis relative to the rising
#   sysclk edge at 30 ns, preceding the capturing edge at 40 ns.
#   The MISO input signal is already valid at the time of the reference edge.
#   So even 0 ns input delay will be fine, but we set 2 ns to balance
#   the hold timing.
set_input_delay -max 2.0 [get_ports spi_miso]

#   Hold analysis:
#     at  0 ns: sysclk rising edge
#     at  2 ns: SPI_CLK falling edge (0 ns + min delay sysclk to SPI_CLK)
#     at  3 ns: MISO output invalid  (2 ns + min delay SPI_CLK to MISO invalid)
#     at  2 ns: MISO input invalid   (3 ns - margin)
#
#   So we have a guaranteed min 2 ns delay from sysclk to MISO transition.
set_input_delay -min 2.0 [get_ports spi_miso]

