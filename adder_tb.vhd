----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 06/06/2018 02:11:26 PM
-- Design Name: 
-- Module Name: adder_tb - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
-- 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity adder_2bit_tb is
--  Port ( );
end adder_2bit_tb;

architecture Behavioral of adder_2bit_tb is
signal A, B : std_logic_vector(1 downto 0);
signal sum : std_logic_vector(2 downto 0);

component adder_2bit_wrapper is
  port (
    A : in STD_LOGIC_VECTOR ( 1 downto 0 );
    B : in STD_LOGIC_VECTOR ( 1 downto 0 );
    sum : out STD_LOGIC_VECTOR ( 2 downto 0 )
  );
  end component adder_2bit_wrapper;
begin
design_1_i:  adder_2bit_wrapper
     port map (
      A => A,
      B => B,
      sum => sum
    );
    
simulation : process begin --altered to cover all input combinations
    A <= "00";
    B <= "00";
    wait for 10 ns;  
    A <= "00";
    B <= "01";
    wait for 10 ns;
    A <= "00";
    B <= "10";
    wait for 10 ns;
    A <= "00";
    B <= "11";
    wait for 10 ns;
    
    A <= "01";
    B <= "00";
    wait for 10 ns;
    A <= "01";
    B <= "01";
    wait for 10 ns;
    A <= "01";
    B <= "10";
    wait for 10 ns;
    A <= "01";
    B <= "11";
    wait for 10 ns;
    
    A <= "10";
    B <= "00";
    wait for 10 ns;
    A <= "10";
    B <= "01";
    wait for 10 ns;
    A <= "10";
    B <= "10";
    wait for 10 ns;
    A <= "10";
    B <= "11";
    wait for 10 ns;
    
    A <= "11";
    B <= "00";
    wait for 10 ns;
    A <= "11";
    B <= "01";
    wait for 10 ns;
    A <= "11";
    B <= "10";
    wait for 10 ns;
    A <= "11";
    B <= "11";
    wait for 10 ns;
    
end process;    
    
end Behavioral;
