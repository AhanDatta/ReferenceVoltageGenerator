-- Testbench for i2c_writing_master entity
-- Sasha C. Guerrero
-- 2026 July 30

LIBRARY ieee;
USE ieee.std_logic_1164.all;

ENTITY tb_i2c_master IS
END ENTITY;

ARCHITECTURE behave OF tb_i2c_master IS

SIGNAL str, rst, clk, done_rx, SDA, SCL, done, rdy : std_logic;
SIGNAL data_in : std_logic_vector(7 downto 0);

CONSTANT CLK_PERIOD : time := 10 ns; -- halfperiod is 10ns

BEGIN

DUT : entity work.i2c_master
	port map(
		str => str,
		rst => rst,
		clk => clk,
		data_in => data_in,
		done_rx => done_rx,
		SDA => SDA,
		SCL => SCL,
		done => done,
		rdy => rdy
	);

clk_gen : process
begin
	clk <= '0';
	wait for CLK_PERIOD;
	clk <= '1';
	wait for CLK_PERIOD;
end process;

str <= '0', '1' after 40 ns; -- S0

data_in <= "00000010",
	"01001010" after 120 ns,
	"00001000" after 220 ns,
	"00010100" after 320 ns,
	"11100000" after 420 ns;

done_rx <= '0', '1' after 80 ns, '0' after 100 ns, -- S1
	'1' after 180 ns, '0' after 200 ns, -- S3
	'1' after 280 ns, '0' after 300 ns, -- S5
	'1' after 380 ns, '0' after 400 ns, -- S7
	'1' after 480 ns, '0' after 500 ns; -- S9
	
SDA <= 'Z', '0' after 95446 ns, 'Z' after 97856 ns, --  ack 1
	'0' after 202560 ns, 'Z' after 204000 ns,
	'0' after 308000 ns, 'Z' after 309000 ns,
	'0' after 413250 ns, 'Z' after 417000 ns;
	
END behave;
