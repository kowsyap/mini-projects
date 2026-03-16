library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vecadd is
  port (
    aclk           : in  std_logic;
    aresetn        : in  std_logic;

    -- Input AXI-Stream
    streami_tvalid : in  std_logic;
    streami_tready : out std_logic;
    streami_tdata  : in  std_logic_vector(31 downto 0);
    streami_tlast  : in  std_logic;

    -- Output AXI-Stream
    streamo_tvalid : out std_logic;
    streamo_tready : in  std_logic;
    streamo_tdata  : out std_logic_vector(31 downto 0);
    streamo_tlast  : out std_logic;
    streamo_tkeep  : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of vecadd is

  type state_t is (RECV, SEND_LO, SEND_HI);
  signal state_reg : state_t := RECV;
  signal state_next : state_t;

  signal acc_reg      : unsigned(63 downto 0) := (others => '0');
  signal acc_wr       : std_logic;
  signal acc_clr      : std_logic;

begin

    streamo_tkeep  <= "1111";

    streamo_tdata <= std_logic_vector(acc_reg(31 downto 0)) when state_reg = SEND_LO else
                    std_logic_vector(acc_reg(63 downto 32)) when state_reg = SEND_HI else
                    (others => '0');


    process(aclk)
    begin
        if rising_edge(aclk) then
            if aresetn = '0' then
                state_reg     <= RECV;
            else
                state_reg    <= state_next;
            end if;
        end if;
    end process;

    process(aclk)
    begin
        if rising_edge(aclk) then
            if aresetn = '0' then
                acc_reg <= (others => '0');
            elsif acc_clr = '1' then
                acc_reg <= (others => '0');
            elsif acc_wr = '1' then
                acc_reg <= acc_reg + resize(unsigned(streami_tdata), 64);
            end if;
        end if;
    end process;

    process(streami_tvalid, streami_tlast, streamo_tready, state_reg)
    begin
        state_next     <= state_reg;
        acc_wr     <= '0';
        acc_clr      <= '0';

        streami_tready <= '0';
        streamo_tvalid <= '0';
        streamo_tlast  <= '0';

        case state_reg is
            when RECV =>
                streami_tready <= '1';
                if (streami_tvalid = '1') then
                    acc_wr <= '1';
                    if streami_tlast = '1' then
                        state_next    <= SEND_LO;
                    end if;
                end if;

            when SEND_LO =>
                streamo_tvalid <= '1';
                if (streamo_tready = '1') then
                    state_next <= SEND_HI;
                end if;

            when SEND_HI =>
                streamo_tvalid <= '1';
                streamo_tlast  <= '1';
                if (streamo_tready = '1') then
                    acc_clr    <= '1';
                    state_next <= RECV;
                end if;

        end case;
    end process;

end architecture;
