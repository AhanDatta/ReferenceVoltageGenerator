-- I2C writing
-- Sasha C. Guerrero
-- 2026 July 2

LIBRARY ieee;
USE ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

ENTITY i2c_writing IS
	PORT(
		-- inputs
		clk : in std_logic; -- system clock 100 Mhz
		rst : in std_logic; -- pushbutton reset
		str : in std_logic; -- pushbutton start
		-- pushbuttons are 0 when pushed & 1 when released, so need to invert
		-- them so they behave intuitively (1 when pushed, 0 when released)
		D : in std_logic_vector(7 downto 0); -- 1 byte data input via switches
		
		-- outputs
		done : out std_logic; -- LED9
		rdy : out std_logic; -- LED8
		SDA : inout std_logic; -- bidirectional serial data line
		SCL : out std_logic; -- serial clock line open
		ACK : out std_logic -- LED7 acknowledgement flag
	);
END ENTITY i2c_writing;

ARCHITECTURE behave OF i2c_writing IS

-- internal connections
signal SH : std_logic; -- shift
signal L : std_logic; -- load
signal shift_reg_to_mux : std_logic_vector(7 downto 0); -- parallel-load between data_in_shift_reg and mux0
signal sel : std_logic; -- select mux0
signal CE : std_logic; -- clock enable mux1
signal mux_to_ff : std_logic; -- between mux1 Q and D_Flipflop D
signal ff_to_mux : std_logic; -- between D_Flipflop Q and mux1 D0

signal EN_Buffer : std_logic; -- enable tristate buffer
signal mux0_out : std_logic; -- mux0 output feeds tristate buffer
signal EN_SCL_Ctr : std_logic; -- enable serial clock counter
signal EN_Bit_Ctr : std_logic; -- enable bit counter
signal SCL_Cnt : std_logic_vector(7 downto 0); -- SCL pulse count
signal Bit_Cnt : std_logic_vector(3 downto 0); -- received bits count
signal Done_SCL_Ctr : std_logic := '0'; -- limit logic
signal Done_Bit_Ctr : std_logic := '0'; -- limit logic

signal Rst_SCL_Ctr : std_logic; -- reset SCL pulse count to 0
signal Rst_Bit_Ctr : std_logic; -- reset bit count to 0

BEGIN

	ACK <= ff_to_mux;

	Data_In_Shift_Reg : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst,
			clk => clk,
			SH => SH, -- signal
			L => L, -- signal
			SI => '0', -- 'open' caused error: formal port or parameter 'SI' must have actual or default value
			D => D,
			Q => shift_reg_to_mux
		);
	
	Mux0 : entity work.MUX2_1BIT
		port map (
			sel => sel, -- signal
			D0 => '0',
			D1 => shift_reg_to_mux(0), -- signal: R-shift register shifts out the LSB or rightmost bit
			Q => mux0_out -- signal
		);
	
	-- Tristate buffer gives high impedence unless buffer is enabled
	SDA <= mux0_out when EN_Buffer = '1' else 'Z';
	Mux1 : entity work.MUX2_1BIT
		port map (
			sel => sel,
			D0 => ff_to_mux,
			D1 => SDA,
			Q => mux_to_ff
		);
	
	D_Flipflop : entity work.D_FLIPFLOP
		port map (
			rst => rst,
			clk => clk,
			D => mux_to_ff,
			Q => ff_to_mux
		);
	
	SCL_Ctr : entity work.counter_w_EN
		generic map (n => 8)
		port map(
			rst => Rst_SCL_Ctr,
			clk => clk,
			EN => EN_SCL_Ctr,
			cnt => SCL_Cnt
		);
	
	SCL_Lim_Logic : entity work.LimitLogic
		generic map(n => 8)
		port map(
			A => "11111010", -- 250
			B => SCL_Cnt,
			Zout => Done_SCL_Ctr
		);
	
	Bit_Ctr : entity work.counter_w_EN
		generic map (n => 4)
		port map(
			rst => Rst_Bit_Ctr,
			clk => clk,
			EN => EN_Bit_Ctr,
			cnt => Bit_Cnt
		);
	
	Bit_Lim_Logic : entity work.LimitLogic
		generic map(n => 4)
		port map(
			A => "1000", -- 8
			B => Bit_Cnt,
			Zout => Done_Bit_Ctr
		);
	
	FSM : entity work.fsm_i2c_writing
		port map(
			clk => clk,
			str => str,
			rst => rst,
			Done_SCL_Ctr => Done_SCL_Ctr,
			Done_Bit_Ctr => Done_Bit_Ctr,
			
			L => L,
			SH => SH,
			SCL => SCL, -- open caused FSM to be stuck at S2
			EN_Buffer => EN_Buffer,
			EN_SCL_Ctr => EN_SCL_Ctr,
			Rst_SCL_Ctr => Rst_SCL_Ctr,
			EN_Bit_Ctr => EN_Bit_Ctr,
			Rst_Bit_Ctr => Rst_Bit_Ctr,
			Sel => sel,
			CE => CE,
			Done => done,
			rdy =>rdy
		);

END ARCHITECTURE behave;