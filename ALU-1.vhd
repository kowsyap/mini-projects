----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 2024/12/01 11:50:59
-- Design Name: 
-- Module Name: ALU - Behavioral
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
use IEEE.NUMERIC_STD.all;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity alu is
Port ( A: in std_logic_vector (2 downto 0);
B: in std_logic_vector (2 downto 0);
sel: in STD_LOGIC_VECTOR (1 downto 0);
ALU_out: out STD_LOGIC_VECTOR (2 downto 0);
N: out STD_LOGIC;
Z: out STD_LOGIC
);
-- Insert your code here.
end alu;

architecture Behavioral of alu is

signal temp: STD_LOGIC_VECTOR(2 DOWNTO 0);
signal temp2: STD_LOGIC_VECTOR(2 DOWNTO 0);
signal temp3: STD_LOGIC_VECTOR(2 DOWNTO 0);

begin

temp2 <= STD_LOGIC_VECTOR(signed(A) + signed(A) + signed(B));
temp3 <= STD_LOGIC_VECTOR(signed(A) - (signed(A) XOR signed(B)));

with sel select
temp <= NOT(B) NAND A when "00",
        temp2 when "01",
        A XOR B when "10",
        temp3 when "11",
        "000" when others;

N <= temp(2);
Z <= '1' when temp = "000" else '0';
ALU_out <= temp;

end Behavioral;