-- Finite state machine for I2C master
-- Sasha C. Guerrero
-- 2026 July 29

LIBRARY ieee;
USE ieee.std_logic_1164.all;

ENTITY fsm_i2c_master IS
	PORT(
		clk : in std_logic;
		rst : in std_logic;
		str : in std_logic;
		done_rx : in std_logic;
		w_done : in std_logic;
		w_rdy : in std_logic;
		
		L0 : out std_logic;
		L1 : out std_logic;
		L2 : out std_logic;
		L3 : out std_logic;
		L4 : out std_logic;
		w_str : out std_logic;
		done : out std_logic;
		rdy : out std_logic;
		rst_all : out std_logic -- internal rst
	);
END ENTITY;

ARCHITECTURE behave OF fsm_i2c_master IS

signal currentState, nextState : std_logic_vector(4 downto 0);

BEGIN

process(clk, rst, str, done_rx, w_done, w_rdy, currentState)
begin
	CASE currentState IS
		WHEN "00000" => -- S0: Wait for str=1
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '1';
			rst_all <= '1';
			
			if str='1' then
				nextState <= "00001";
			else
				nextState <= currentState;
			end if;
		
		WHEN "00001" => -- S1: Wait for 1st byte of data
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if done_rx = '1' then
				nextState <= "00010"; -- S2
			else
				nextState <= currentState;
			end if;
		
		WHEN "00010" => -- S2: Load 1st byte of data into 1st register
			L0    <= '1';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "00011"; -- S3
		
		WHEN "00011" => -- S3: Wait for 2nd byte of data
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if done_rx = '1' then
				nextState <= "00100"; -- S4
			else
				nextState <= currentState;
			end if;
		
		WHEN "00100" => -- S4: Load 2nd byte of data into register dac_select
			L0    <= '0';
			L1    <= '1';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "00101"; -- S5
			
		WHEN "00101" => -- S5: Wait for 3rd byte of data
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if done_rx = '1' then
				nextState <= "00110"; -- S6
			else
				nextState <= currentState;
			end if;
		
		WHEN "00110" => -- S6: Load 3rd byte of data into register reg_select
			L0    <= '0';
			L1    <= '0';
			L2    <= '1';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "00111"; -- S7
		
		WHEN "00111" => -- S7: Wait for 4th byte of data
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if done_rx = '1' then
				nextState <= "01000"; -- S8
			else
				nextState <= currentState;
			end if;
		
		WHEN "01000" => -- S8: Load 4th byte of data into register v_high
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '1';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "01001"; -- S9
		
		WHEN "01001" => -- S9: Wait for 5th byte of data
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if done_rx = '1' then
				nextState <= "01010"; -- S10
			else
				nextState <= currentState;
			end if;
		
		WHEN "01010" => -- S10: Load 5th byte of data into register v_low
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '1';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "01011"; -- S11
		
		WHEN "01011" => -- S11: Write dac_select to SDA
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '1';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "01100"; -- S12
		
		WHEN "01100" => -- S12: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if w_done = '1' then
				nextState <= "01101"; -- S13
			else
				nextState <= currentState;
			end if;
		
		WHEN "01101" => -- S13: Write reg_select to SDA
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '1';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "01110"; -- S14
		
		WHEN "01110" => -- S14: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if w_done = '1' then
				nextState <= "01111"; -- S15
			else
				nextState <= currentState;
			end if;
		
		WHEN "01111" => -- S15: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '1';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "10000"; -- S16
		
		WHEN "10000" => -- S16: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if w_done = '1' then
				nextState <= "10001"; -- S17
			else
				nextState <= currentState;
			end if;
		
		WHEN "10001" => -- S17: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '1';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			nextState <= "10010"; -- S18
		
		WHEN "10010" => -- S18: Wait for byte to be written
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			
			if w_done = '1' then
				nextState <= "10011"; -- S19
			else
				nextState <= currentState;
			end if;
		
		WHEN "10011" => -- S19: Clear registers and clear writing block
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '1';
			nextState <= "00000";
		
		WHEN OTHERS => -- fallback to S0
			L0    <= '0';
			L1    <= '0';
			L2    <= '0';
			L3    <= '0';
			L4    <= '0';
			w_str <= '0';
			done  <= '0';
			rdy   <= '0';
			rst_all <= '0';
			nextState <= "00000";
			
	END CASE;
end process;

process(clk, nextState, rst)
begin
	if rst = '1' then
		currentState <= "00000";
	elsif rising_edge(clk) then
		currentState <= nextState;
	end if;
end process;

END ARCHITECTURE;
