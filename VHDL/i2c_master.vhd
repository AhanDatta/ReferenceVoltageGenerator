-- I2C master (loading data from register memory; reading and writing)
-- Sasha C. Guerrero
-- 2026 July 14

LIBRARY ieee;
USE ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

ENTITY i2c_master IS
	PORT(
		str : in std_logic;
		rst : in std_logic;
		clk : in std_logic;
		data_in : in std_logic_vector(7 downto 0);
		
		done_rx : in std_logic; -- temporarily here until rs232 module is given
		
		SDA : inout std_logic;
		SCL : out std_logic;
		done : out std_logic;
		rdy : out std_logic
	);
END ENTITY;

ARCHITECTURE behave OF i2c_master IS

--internal signals
signal rst_i, str_i, done_byte_i, done_i, rdy_i : std_logic;
signal Linternal0, Linternal1, Linternal2, Linternal3, Linternal4 : std_logic;
signal Qinternal0, Qinternal1, Qinternal2, Qinternal3, Qinternal4 : std_logic_vector(7 downto 0); 
signal data_select : std_logic_vector(2 downto 0);
signal mux0_Q : std_logic_vector(7 downto 0);
signal mux1_Q : std_logic_vector(2 downto 0);

BEGIN

	bank_select : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst_i,
			clk => clk,
			SH => '0',
			L => Linternal0,
			SI => '0',
			D => data_in,
			Q => Qinternal0
		);
	
	dac_select : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst_i,
			clk => clk,
			SH => '0',
			L => Linternal1,
			SI => '0',
			D => data_in,
			Q => Qinternal1
		);
	
	reg_select : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst_i,
			clk => clk,
			SH => '0',
			L => Linternal2,
			SI => '0',
			D => data_in,
			Q => Qinternal2
		);
		
	v_high : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst_i,
			clk => clk,
			SH => '0',
			L => Linternal3,
			SI => '0',
			D => data_in,
			Q => Qinternal3
		);
	
	v_low : entity work.R_SHIFT_REG
		generic map(n => 8)
		port map(
			rst => rst_i,
			clk => clk,
			SH => '0',
			L => Linternal4,
			SI => '0',
			D => data_in,
			Q => Qinternal4
		);
		
	mux0 : entity work.MUX4_1BIT
		port map(
			sel => data_select(1 downto 0),
			D0 => Qinternal4,
			D1 => Qinternal3,
			D2 => Qinternal2,
			D3 => Qinternal1,
			Q => mux0_Q
		);
	
	mux1 : entity work.MUX2_3BIT
		port map(
			sel => Qinternal1(0), -- LSB of dac_select is the R/W bit
			D0 => "100", -- 0, writing mode, write all 4 bytes
			D1 => "001", -- 1, reading mode, write only 1 byte (dac address)
			Q => mux1_Q
		);
	
	writing_master : entity work.i2c_writing_master
		port map(
			clk => clk,
			rst => rst_i,
			str => str_i,
			data_in => mux0_Q,
			limit => mux1_Q,
			SDA => SDA,
			SCL => SCL,
			ack_cnt => open,
			data_select => data_select,
			done_byte_out => done_byte_i,
			done => done_i,
			rdy => rdy_i
		);
		
	fsm_master : entity work.fsm_i2c_master
		port map(
			clk => clk,
			rst => rst,
			str => str,
			done_rx => done_rx, -- need rs232 module
			w_done => done_byte_i, --done_i,
			w_rdy => rdy_i,
			
			L0 => Linternal0,
			L1 => Linternal1,
			L2 => Linternal2,
			L3 => Linternal3,
			L4 => Linternal4,
			w_str => str_i,
			done => done,
			rdy => rdy,
			rst_all => rst_i
		);

END ARCHITECTURE;