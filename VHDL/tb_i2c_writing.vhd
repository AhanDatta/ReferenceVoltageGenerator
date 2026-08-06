-- Testbench for I2C writing entity
-- Sasha C. Guerrero
-- 2026 July 8

LIBRARY ieee;
USE ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

ENTITY tb_i2c_writing IS -- 'tb' prefix is testbench
END ENTITY;

ARCHITECTURE behave OF tb_i2c_writing IS

-- internal connections
SIGNAL clk, rst, str, done, ready, SDA, SCL, ACK : std_logic;
SIGNAL D, Q : std_logic_vector(7 downto 0);


-- oscillating clock
CONSTANT CLK_PERIOD : time := 20 ns;


BEGIN

-- device under test
DUT : entity work.i2c_writing
	port map (
		clk => clk,
		rst => rst,
		str => str,
		D => D,
		done => done,
		rdy => ready,
		SDA => SDA,
		SCL => SCL,
		ACK => ACK
	);
	
-- oscillating clock
clk_gen : process
begin
	clk <= '0';
	wait for CLK_PERIOD / 2;
	clk <= '1';
	wait for CLK_PERIOD / 2;
end process;

rst <= '1', '0' after 40 ns;
str <= '0', '1' after 40 ns, '0' after 70 ns;

D <= "01010110";	


END behave;