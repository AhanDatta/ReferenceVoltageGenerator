-- I2C writing master
-- Sasha C. Guerrero
-- 2026 July 14

LIBRARY ieee;
USE ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

ENTITY i2c_writing_master IS
	PORT(
	--inputs
	clk : in std_logic;
	rst : in std_logic;
	str : in std_logic;
	data_in : in std_logic_vector(7 downto 0);
	limit : in std_logic_vector(2 downto 0);
	
	--outputs
	SDA : inout std_logic; -- bidir
	SCL : out std_logic;
	ack_cnt : out std_logic_vector(2 downto 0);
	done_byte_out : out std_logic;
	data_select : out std_logic_vector(2 downto 0);
	done : out std_logic;
	rdy : out std_logic
	);
END ENTITY;

ARCHITECTURE behave OF i2c_writing_master IS

-- internal signals denoted by _i
signal rst_byte_i : std_logic;
signal en_byte_i : std_logic;
signal byte_ctr_i : std_logic_vector(2 downto 0);
signal byte_lim_i : std_logic;
signal ack_i : std_logic;
signal rdy_i : std_logic;
signal done_i : std_logic;
signal en_ack_i : std_logic;
signal rst_ack_i : std_logic;

BEGIN

data_select <= byte_ctr_i;

-- block for writing a single byte
i2c_writing : entity work.i2c_writing
	port map(
		clk => clk,
		rst => rst,
		str => str,
		D => data_in,
		done => done_i, --signal
		rdy => rdy_i, --signal
		ACK => ack_i, --signal
		SDA => SDA,
		SCL => SCL
	);

-- block for writing multiple bytes
fsm_i2c_writing_master : entity work.fsm_i2c_writing_master
	port map(
		clk => clk,
		rst => rst,
		str => str,
		
		EN_byte_ctr => en_byte_i, --signal
		rst_byte_ctr => rst_byte_i, --signal
		
		done_lim => byte_lim_i, --signal
		ACK => ack_i, --signal
		rdy_i => rdy_i, --signal
		done_byte => done_i, --signal
		
		EN_ack_ctr => en_ack_i, --signal
		rst_ack_ctr => rst_ack_i, --signal
	
		done_byte_out => done_byte_out,
		done => done,
		rdy => rdy
	);

ack_ctr : entity work.counter_w_EN
	generic map (n => 3)
	port map(
		clk => clk,
		rst => rst_ack_i, --signal
		EN => en_ack_i, --signal
		cnt => ack_cnt
	);

byte_ctr : entity work.counter_w_EN
	generic map (n => 3)
	port map(
		clk => clk,
		rst => rst_byte_i, --signal
		EN => en_byte_i, --signal
		cnt => byte_ctr_i --signal
	);
	
byte_lim_logic : entity work.LimitLogic
	generic map (n => 3)
	port map(
		A => limit,
		B => byte_ctr_i, -- feed count into limlogic
		Zout => byte_lim_i
	);

END ARCHITECTURE behave;