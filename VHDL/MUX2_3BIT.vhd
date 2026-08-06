-- Mux2_1bit
-- hdricoaniles
-- 2023 24 01
-- 4 1bit inputs mux with 
-- modified to have 3-bit input/output

LIBRARY IEEE;
use ieee.std_logic_1164.all;


ENTITY MUX2_3BIT IS
	
	port(
		sel 					: in std_logic;
		D0,D1	 		: in std_logic_vector(2 downto 0);
		Q	 					: out std_logic_vector(2 downto 0)
		
	);
END ENTITY;

ARCHITECTURE behave OF MUX2_3BIT IS
BEGIN

	process(seL, D0,D1)
	begin 
		CASE sel IS
			WHEN '0' => Q <=D0;
			WHEN '1' => Q <=D1;
			WHEN OTHERS => Q <= D0;
		END CASE;
	end process;
END ARCHITECTURE;