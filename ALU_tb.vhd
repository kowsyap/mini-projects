----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 2024/12/01 12:12:38
-- Design Name: 
-- Module Name: ALU_tb - Behavioral
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
use ieee.std_logic_signed.all;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity ALU_tb is
end ALU_tb;

architecture Behavioral of ALU_tb is

component alu is 
    Port (  A: in std_logic_vector (2 downto 0);
            B : in std_logic_vector (2 downto 0);
            sel : in STD_LOGIC_VECTOR (1 downto 0);
            ALU_out : out STD_LOGIC_VECTOR (2 downto 0);
            N : out STD_LOGIC;
            Z : out STD_LOGIC);
end component;

signal A_tb : std_logic_vector (2 downto 0) := "000";
signal B_tb : std_logic_vector (2 downto 0) := "000";
signal S_tb : std_logic_vector (1 downto 0) := "00";
signal F_tb : std_logic_vector (2 downto 0) := "000";
signal Z_tb : std_logic;
signal N_tb : std_logic;
begin

uut: alu
    Port map  ( A => A_tb,
               B => B_tb,
               sel => S_tb,
               ALU_out => F_tb,
               Z => Z_tb,
               N => N_tb);

stimuli: process
begin

S_tb <= "00";
A_tb <= "101";
B_tb <= "000";
for k in 0 to 3 loop
        wait for 2 ns;
          S_tb <= S_tb + 1;
		end loop;
      
A_tb <= "010";
B_tb <= "100";
for k in 0 to 3 loop
        wait for 2 ns;
          S_tb <= S_tb + 1;
        end loop;

A_tb <= "011";
B_tb <= "111";
for k in 0 to 3 loop
        wait for 2 ns;
          S_tb <= S_tb + 1;
       end loop;

A_tb <= "110";
B_tb <= "001";
for k in 0 to 3 loop
        wait for 2 ns;
          S_tb <= S_tb + 1;
       end loop;
wait;

end process stimuli;

end Behavioral;
