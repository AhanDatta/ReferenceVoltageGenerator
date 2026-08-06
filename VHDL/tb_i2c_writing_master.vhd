-- Testbench for i2c_writing_master entity
-- Sasha C. Guerrero
-- 2026 July 15

LIBRARY ieee;
USE ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

ENTITY tb_i2c_writing_master IS
END ENTITY;

ARCHITECTURE behave OF tb_i2c_writing_master IS

SIGNAL clk, rst, str, done, rdy, SDA, SCL : std_logic;
SIGNAL limit, ack_cnt : std_logic_vector(2 downto 0);
SIGNAL data_in, received_data : std_logic_vector(7 downto 0);

CONSTANT CLK_PERIOD : time := 10 ns; -- halfperiod is 10ns

BEGIN

DUT : entity work.i2c_writing_master
	port map(
		clk => clk,
		rst => rst,
		str => str,
		data_in => data_in,
		limit => limit,
		done => done,
		rdy => rdy,
		SDA => SDA,
		SCL => SCL,
		ack_cnt => ack_cnt
	);

clk_gen : process
begin
	clk <= '0';
	wait for CLK_PERIOD;
	clk <= '1';
	wait for CLK_PERIOD;
end process;

-- Uncomment this section to send 2 bytes
------------------------------------
--rst <= '1', '0' after 40 ns;
--str <= '0', '1' after 40 ns, '0' after 70 ns, '1' after 123200 ns, '0' after 123230 ns;

--limit <= "010";
--data_in <= "01010110", "00010001" after 120100 ns;

-- to send ACK, pull down SDA
--SDA <= 'Z', '0' after 94400 ns, 'Z' after 106050 ns, '0' after 218000 ns, 'Z' after 221000 ns;


-- Uncomment this section to send 4 bytes
-----------------------------------
rst <= '1', '0' after 40 ns;
str <= '0', '1' after 40 ns, '0' after 70 ns, '1' after 123200 ns, '0' after 123230 ns,
	'1' after 231950 ns, '0' after 232000 ns, '1' after 342900 ns, '0' after 342950 ns;
limit <= "100";
data_in <= "01010110", "00010001" after 120100 ns, "11001111" after 231900 ns, "01101001" after 342900 ns;
SDA <= 'Z', '0' after 94400 ns, 'Z' after 106050 ns, '0' after 218000 ns, 'Z' after 221000 ns,
	'0' after 325500 ns, 'Z' after 329000 ns, '0' after 438244 ns, 'Z' after 439300 ns;

END behave;
