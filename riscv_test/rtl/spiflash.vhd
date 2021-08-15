--
-- SPI flash memory controller for simple processor system
--
-- This SPI master operates in CPOL=1, CPHA=1 mode.
-- Input (MISO) is captured on the falling edge of the SPI clock.
-- Output (MOSI) is updated simultaneous to the falling edge of the SPI clock.
-- The clock signal stays high when the clock is idle and/or slave deselected.
--
-- Register map:
--
--   address 0x00 (read-only): Status
--     bit   0    = '1' when the controller is processing commands,
--                  '0' when all previous commands have completed.
--     bit   1    = '1' when the controller is ready for a new command.
--     bit   2    = '1' when a read result byte is available.
--
--   address 0x04 (read-write): Slave select status
--     bit   0    = '1' to select slave, '0' to deselect slave.
--       Note the slave is automatically selected at the start of any transfer,
--       but must be explicitly deselected at the end of a transaction.
--       Do not write to this register before all commands have completed.
--
--   address 0x08 (when write): Start byte transfer
--     bits  7-0  = Data bits to write to MOSI.
--     bit   8    = '1' to capture MISO data, '0' to ignore MISO data.
--       Writing to this register adds an 8-bit transfer to the command queue.
--       The slave will become selected if it was deselected.
--       If bit 1 of register 0x04 is '0', writes to this register are ignored.
--
--   address 0x08 (when read): Read captured MISO data.
--     bits  7-0  = Captured MISO byte from the read FIFO.
--     bit   8    = '1' when returning valid data,
--                  '0' if the read FIFO was empty.
--       Reading from this register returns the oldest captured data byte and
--       removes it from the read FIFO.
--       If bit 2 of register 0x04 is '0', reading this register will
--       return 0x0000 and have no other effect.
--


library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.rvsys.all;


entity spiflash is

    generic (
        -- SPI clock half-period in units of system clock cycles.
        -- (SPI clock frequency) = (system clock frequency) / (2 * clk_period)
        clk_half_period: integer range 1 to 8;

        -- Minimum slave deselect time in system clock cycles.
        deselect_time: integer range 1 to 7
    );

    port (
        -- System clock.
        clk:            in  std_logic;

        -- Synchronous reset, active high.
        rst:            in  std_logic;

        -- SPI signals.
        spi_clk:        out std_logic;
        spi_cs:         out std_logic;
        spi_mosi:       out std_logic;
        spi_miso:       in  std_logic;

        -- Bus interface signals.
        slv_input:      in  bus_slv_input_type;
        slv_output:     out bus_slv_output_type
    );

end entity;

architecture spiflash_arch of spiflash is

    -- State machine.
    type state_type is (State_Idle, State_Deselect, State_Transfer, State_LastBit);

    -- Internal registers.
    type regs_type is record
        state:          state_type;
        clk_cnt:        unsigned(2 downto 0);
        clk_fall:       std_logic;
        clk_level:      std_logic;
        shift_reg:      std_logic_vector(7 downto 0);
        shift_cnt:      unsigned(2 downto 0);
        shift_capture:  std_logic;
        tx_fifo_data:   std_logic_vector(8 downto 0);
        tx_fifo_valid:  std_logic;
        rx_fifo_data:   std_logic_vector(7 downto 0);
        rx_fifo_valid:  std_logic;
        reg_slvsel:     std_logic;
        spi_clk:        std_logic;
        spi_cs:         std_logic;
        spi_mosi:       std_logic;
        rsp_valid:      std_logic;
        rsp_rdata:      std_logic_vector(31 downto 0);
    end record;

    constant regs_init: regs_type := (
        state           => State_Idle,
        clk_cnt         => (others => '0'),
        clk_fall        => '0',
        clk_level       => '1',
        shift_reg       => (others => '0'),
        shift_cnt       => (others => '0'),
        shift_capture   => '0',
        tx_fifo_data    => (others => '0'),
        tx_fifo_valid   => '0',
        rx_fifo_data    => (others => '0'),
        rx_fifo_valid   => '0',
        reg_slvsel      => '0',
        spi_clk         => '1',
        spi_cs          => '1',
        spi_mosi        => '0',
        rsp_valid       => '0',
        rsp_rdata       => (others => '0')
    );

    signal r: regs_type := regs_init;
    signal rnext: regs_type;

begin

    -- Drive outputs.
    spi_clk     <= r.spi_clk;
    spi_cs      <= r.spi_cs;
    spi_mosi    <= r.spi_mosi;
    slv_output  <= ( cmd_ready => '1',
                     rsp_valid => r.rsp_valid,
                     rsp_rdata => r.rsp_rdata );

    -- Asynchronous process.
    process (all) is
        variable v: regs_type;
    begin
        -- By default, set next registers equal to current registers.
        v := r;

        -- Update SPI clock output signal.
        v.spi_clk := r.clk_level;

        -- When not idle, update clock divider and prepare SPI clock edges.
        if r.clk_cnt = 0 then
            -- Prepare clock edge and reload counter.
            v.clk_level := not r.clk_level;
            v.clk_fall  := r.clk_level;
            v.clk_cnt   := to_unsigned(clk_half_period - 1, v.clk_cnt'length);
        else
            -- Count down until next clock edge.
            v.clk_fall  := '0';
            v.clk_cnt   := r.clk_cnt - 1;
        end if;

        -- State machine.
        case r.state is

            when State_Idle =>
                -- Wait for next transfer.

                -- Keep clock idle high.
                v.clk_level := '1';
                v.clk_fall  := '1';
                v.clk_cnt   := to_unsigned(clk_half_period - 1, v.clk_cnt'length);

                -- Offload captured data.
                if r.shift_capture = '1' and r.rx_fifo_valid = '0' then
                    v.rx_fifo_data  := r.shift_reg;
                    v.rx_fifo_valid := '1';
                    v.shift_capture := '0';
                end if;

                if r.shift_capture = '0' or r.rx_fifo_valid = '0' then
                    -- Capture buffer is empty or will be offloaded this cycle.

                    if r.tx_fifo_valid = '1' then
                        -- Start new transfer.
                        v.tx_fifo_valid := '0';
                        v.shift_reg     := r.tx_fifo_data(7 downto 0);
                        v.shift_capture := r.tx_fifo_data(8);
                        v.shift_cnt     := to_unsigned(7, v.shift_cnt'length);
                        -- Select slave.
                        v.reg_slvsel    := '1';
                        v.spi_cs        := '0';
                        -- Prepare first falling edge
                        v.clk_level     := '0';
                        v.state         := State_Transfer;
                    else
                        -- Update slave select.
                        v.spi_cs := not r.reg_slvsel;
                        if r.reg_slvsel = '0' and r.spi_cs = '0' then
                            -- Wait for minimum slave deselect time.
                            v.clk_cnt := to_unsigned(deselect_time - 1, v.clk_cnt'length);
                            v.state   := State_Deselect;
                        end if;
                    end if;

                end if;

            when State_Deselect =>
                -- Wait for minimum slave deselect time.

                -- Keep clock idle high.
                v.clk_level := '1';

                -- Go back to idle after minimum deselect time.
                if r.clk_cnt = 0 then
                    v.state := State_Idle;
                end if;

            when State_Transfer =>
                -- Transfer bits via SPI.

                if r.clk_fall = '1' then
                    -- Shift next bit.
                    -- NOTE: This works out such that the following signal assignments
                    -- all occur on the same system clock edge:
                    --     spi_clk      <= '0'
                    --     spi_mosi     <= shift_reg(7)
                    --     shift_reg(0) <= spi_miso
                    v.spi_mosi  := r.shift_reg(r.shift_reg'high);
                    v.shift_reg := r.shift_reg(r.shift_reg'high-1 downto 0) & spi_miso;
                    v.shift_cnt := r.shift_cnt - 1;

                    if r.shift_cnt = 0 then
                        -- Just started the last bit of the 8-bit transfer.
                        v.state := State_LastBit;
                    end if;
                end if;

            when State_LastBit =>
                -- Handle the last bit of the transfer.

                -- Suppress the next falling edge.
                if r.clk_level = '1' then
                    v.clk_level := '1';
                end if;

                -- Capture the last MISO bit.
                if r.clk_fall = '1' then
                    v.spi_mosi  := '0';
                    v.shift_reg := r.shift_reg(r.shift_reg'high-1 downto 0) & spi_miso;
                    v.state     := State_Idle;
                end if;

        end case;

        -- Answer read transactions after 1 clock cycle.
        v.rsp_valid := slv_input.cmd_valid and (not slv_input.cmd_write);

        -- Handle bus read transactions.
        if slv_input.cmd_valid = '1' and slv_input.cmd_write = '0' then

            -- By default return all zeros.
            v.rsp_rdata := (others => '0');

            case slv_input.cmd_addr(3 downto 2) is

                when "00" =>
                    -- address 0x00 = status register
                    if r.state /= State_Idle or r.tx_fifo_valid = '1' then
                        v.rsp_rdata(0) := '1';
                    end if;
                    v.rsp_rdata(1) := not r.tx_fifo_valid;
                    v.rsp_rdata(2) := r.rx_fifo_valid;

                when "01" =>
                    -- address 0x04 = slave select register
                    v.rsp_rdata(0) := r.reg_slvsel;

                when "10" =>
                    -- address 0x08 = read data
                    v.rsp_rdata(7 downto 0) := r.rx_fifo_data;
                    v.rsp_rdata(8) := r.rx_fifo_valid;
                    if r.rx_fifo_valid = '1' then
                        -- Pop data from read FIFO
                        v.rx_fifo_valid := '0';
                    end if;

                when others =>
                    null;

            end case;
        end if;

        -- Handle bus write transactions.
        if slv_input.cmd_valid = '1' and slv_input.cmd_write = '1' then

            case slv_input.cmd_addr(3 downto 2) is

                when "01" =>
                    -- address 0x04 = slave select register
                    v.reg_slvsel := slv_input.cmd_wdata(0);

                when "10" =>
                    -- address 0x08 = transfer command register
                    if r.tx_fifo_valid = '0' then
                        v.tx_fifo_data := slv_input.cmd_wdata(8 downto 0);
                        v.tx_fifo_valid := '1';
                    end if;

                when others =>
                    null;

            end case;
        end if;

        -- Synchronous reset.
        if rst = '1' then
            v.state         := State_Idle;
            v.clk_level     := '1';
            v.tx_fifo_valid := '0';
            v.rx_fifo_valid := '0';
            v.reg_slvsel    := '0';
            v.spi_clk       := '1';
            v.spi_cs        := '1';
            v.spi_mosi      := '0';
            v.rsp_valid     := '0';
        end if;

        -- Drive new register values to synchronous process.
        rnext <= v;

    end process;

    -- Synchronous process.
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= rnext;
        end if;
    end process;

end architecture;
