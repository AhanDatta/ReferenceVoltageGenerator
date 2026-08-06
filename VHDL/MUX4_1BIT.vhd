-- Mux4_1bit
-- hdricoaniles
-- 2023 24 01
-- 4 1bit inputs mux with 


LIBRARY IEEE;
use ieee.std_logic_1164.all;


ENTITY MUX4_1BIT IS
	
	port(
		sel 					: in std_logic_vector(1 downto 0);
		D0,D1,D2,D3	 		: in std_logic_vector(7 downto 0);
		Q	 					: out std_logic_vector(7 downto 0)
		
	);
END ENTITY;

ARCHITECTURE behave OF MUX4_1BIT IS
BEGIN

	process(seL, D0,D1,D2,D3)
	begin 
		CASE sel IS
			WHEN "00" => Q <=D0;
			WHEN "01" => Q <=D1;
			WHEN "10" => Q <=D2;
			WHEN "11" => Q <=D3;
			WHEN OTHERS => Q <=D0;
		END CASE;
	end process;
END ARCHITECTURE;