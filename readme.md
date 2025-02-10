
```
create_clock -period 10.000 -name clk -waveform {0.000 5.000} [get_ports clk]

```

```
LIBRARY ieee;
USE ieee.std_logic_1164.all;
USE ieee.numeric_std.all;
USE ieee.std_logic_textio.all;

LIBRARY std;
use std.textio.all;

ENTITY sequential_divider_tb IS
END sequential_divider_tb;

architecture behavioral of sequential_divider_tb is


CONSTANT W : INTEGER := 4;
CONSTANT CLK_PERIOD : TIME := 10 ns;
CONSTANT NUM_TESTS : INTEGER := 5;
CONSTANT RESET_WIDTH : TIME := 100 ns;

TYPE test_vector IS RECORD
    dividend : INTEGER;
    divisor : INTEGER;
END RECORD;

TYPE tests IS ARRAY(0 TO NUM_TESTS - 1) OF test_vector;

CONSTANT TEST_DATA : tests := (
    (117, 10),
    (10, 5),
    (92, 12),
    (157, 11),
    (228, 15)
);

SIGNAL z_tb : STD_LOGIC_VECTOR(2*W - 1 DOWNTO 0);
SIGNAL d_tb : STD_LOGIC_VECTOR(W - 1 DOWNTO 0);
SIGNAL init_tb, run_tb : STD_LOGIC;
SIGNAL clk_tb : STD_LOGIC := '0';
SIGNAL q_tb : STD_LOGIC_VECTOR(W - 1 DOWNTO 0);
SIGNAL r_tb : STD_LOGIC_VECTOR(W - 1 DOWNTO 0);
-- interface to component

SIGNAL expected_q : STD_LOGIC_VECTOR(2*W - 1 DOWNTO 0);
SIGNAL expected_r : STD_LOGIC_VECTOR(W - 1 DOWNTO 0);

begin

    -- instantiate sequential restoring divider
    DUT : entity work.sequential_divider--(behavioral)
    --GENERIC MAP(W => W)
    PORT MAP(
        z => z_tb,
        d => d_tb,
        init => init_tb,
        run => run_tb,
        clk => clk_tb,
        q => q_tb,
        r => r_tb
    );

    -- insert input data
    input_data : PROCESS
    BEGIN
         WAIT FOR RESET_WIDTH;
        FOR i IN 0 TO NUM_TESTS - 1 LOOP
            z_tb <= STD_LOGIC_VECTOR(to_unsigned(TEST_DATA(i).dividend, 2*W));
            d_tb <= STD_LOGIC_VECTOR(to_unsigned(TEST_DATA(i).divisor, W));
            WAIT FOR 5 * CLK_PERIOD;
        END LOOP;
    END PROCESS;
    
    expected_q <= STD_LOGIC_VECTOR(UNSIGNED(z_tb) / UNSIGNED(d_tb));
    expected_r <= STD_LOGIC_VECTOR(UNSIGNED(z_tb) mod UNSIGNED(d_tb));
    -- calculated expected result    
    
    -- set init signal
    init_data : PROCESS
    BEGIN
         WAIT FOR RESET_WIDTH;
        run_tb <= '1';
        init_tb <= '1';
        WAIT FOR CLK_PERIOD;
        init_tb <= '0';
        WAIT FOR W * CLK_PERIOD;
    END PROCESS;
    
    -- drive clock
    clk_tb <= not clk_tb after CLK_PERIOD / 2;

    -- check if results are correct
    check_results : PROCESS
    VARIABLE error_msg : LINE;
    VARIABLE failure_count : INTEGER := 0;
    BEGIN
         WAIT FOR RESET_WIDTH;
        FOR i IN 0 TO NUM_TESTS - 1 LOOP
            
            WAIT FOR (W+1) * CLK_PERIOD;
            
            IF expected_q(W - 1 DOWNTO 0) /= q_tb THEN
                write(error_msg, STRING'("Incorrect quotient!"));
                writeline(output, error_msg);
                failure_count := failure_count + 1;
            END IF;
            
            IF expected_r /= r_tb THEN
                write(error_msg, STRING'("Incorrect remainder!"));
                writeline(output, error_msg);
                failure_count := failure_count + 1;
            END IF;
        END LOOP;
        
        write(error_msg, STRING'("Total failures: "));
        write(error_msg, failure_count);
        writeline(output, error_msg);
    END PROCESS;

end behavioral;

```
