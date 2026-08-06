-- Finite state machine for I2C writing master
-- Sasha C. Guerrero
-- 2026 July 14

LIBRARY ieee;
USE ieee.std_logic_1164.all;

ENTITY fsm_i2c_writing_master IS
	PORT(
		clk : in std_logic;
		rst : in std_logic;
		str : in std_logic;
		done_byte : in std_logic;
		done_lim : in std_logic;
		ACK : in std_logic;
		rdy_i : in std_logic;
		
		EN_ack_ctr : out std_logic;
		rst_ack_ctr : out std_logic;
		EN_byte_ctr : out std_logic;
		rst_byte_ctr : out std_logic;
		done_byte_out : out std_logic;
		done : out std_logic;
		rdy : out std_logic
	);
END ENTITY;

ARCHITECTURE behave of fsm_i2c_writing_master IS

signal currentState, nextState : std_logic_vector(3 downto 0);

BEGIN

process(clk, rst, str, clk, done_byte, done_lim, currentState)
begin
	CASE currentState IS
		WHEN "0000" => -- S0: Wait for str=1
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '1';
			if str = '1' then
				nextState <= "0001";
			else
				nextState <= currentState;
			end if;

		WHEN "0001" => -- S1: Reset counters
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '1';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '1';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			nextState    <= "0010";
		
		WHEN "0010" => -- S2: Wait for done_byte=1
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			if done_byte = '1' then
				nextState <= "0011";
			else
				nextState <= currentState;
			end if;
		
		WHEN "0011" => -- S3: Check ACK. If ACK=0 go to S4, else S5.
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';

			-- Stay in S3 if SDA is Z. S4 is for ACK. S5 is for NACK.
			if ACK = '0' then
				nextState <= "0100"; -- go to S4
			elsif ACK = '1' then
				nextState <= "0101"; -- go to S5
			else
				nextState <= "0011"; -- stay in S3
			end if;
			
		WHEN "0100" => -- S4: Increment ACK counter
			EN_ack_ctr   <= '1';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done         <= '0';
			done_byte_out <= '0';
			rdy          <= '0';
			nextState    <= "0110"; -- go to S6
		
		WHEN "0101" => -- S5: Do nothing
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			nextState    <= "0110"; -- go to S6
		
		WHEN "0110" => -- S6: Increment byte counter
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '1';
			rst_byte_ctr <= '0';
			done_byte_out <= '1';
			done         <= '0';
			rdy          <= '0';
			nextState    <= "0111"; -- go to S6
		
		WHEN "0111" => -- S7: Check done_lim. If 0 go to S2, else S8.
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			
			if done_lim = '0' then
				nextState <= "0010"; -- go to S2
			else
				nextState <= "1000"; -- go to S8
			end if;
		
		WHEN "1000" => -- S8: Reset counters
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '1';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '1';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			nextState    <= "0000"; -- go to S0
		
		WHEN OTHERS => -- fallback to S0
			EN_ack_ctr   <= '0';
			rst_ack_ctr  <= '0';
			EN_byte_ctr  <= '0';
			rst_byte_ctr <= '0';
			done_byte_out <= '0';
			done         <= '0';
			rdy          <= '0';
			nextState    <= "0000"; -- go to S0
			
		END CASE;
end process;

process(clk, nextState, rst)
begin
	if rst = '1' then
		currentState <= "0000";
	elsif rising_edge(clk) then
		currentState <= nextState;
	end if;
end process;

END ARCHITECTURE;