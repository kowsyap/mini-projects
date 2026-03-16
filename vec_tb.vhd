library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vec_tb is
end entity;

architecture tb of vec_tb is
  signal aclk           : std_logic := '0';
  signal aresetn        : std_logic := '0';
  signal streami_tvalid : std_logic := '0';
  signal streami_tready : std_logic;
  signal streami_tdata  : std_logic_vector(31 downto 0) := (others => '0');
  signal streami_tlast  : std_logic := '0';
  signal streamo_tvalid : std_logic;
  signal streamo_tready : std_logic := '1';
  signal streamo_tdata  : std_logic_vector(31 downto 0);
  signal streamo_tlast  : std_logic;
  signal streamo_tkeep  : std_logic_vector(3 downto 0);
begin
  aclk <= not aclk after 5 ns;

  dut: entity work.vecadd
    port map (
      aclk           => aclk,
      aresetn        => aresetn,
      streami_tvalid => streami_tvalid,
      streami_tready => streami_tready,
      streami_tdata  => streami_tdata,
      streami_tlast  => streami_tlast,
      streamo_tvalid => streamo_tvalid,
      streamo_tready => streamo_tready,
      streamo_tdata  => streamo_tdata,
      streamo_tlast  => streamo_tlast,
      streamo_tkeep  => streamo_tkeep
    );

  stim_proc: process
    type in_data_array_t is array (0 to 4) of std_logic_vector(31 downto 0);
    type in_last_array_t is array (0 to 4) of std_logic;
    constant in_data : in_data_array_t := (
      x"00000001",
      x"00000002",
      x"00000003",
      x"FFFFFFFF",
      x"00000001"
    );
    constant in_last : in_last_array_t := ('0', '0', '1', '0', '1');
    variable i : integer := 0;
  begin
    wait for 20 ns;
    aresetn <= '1';
    wait until rising_edge(aclk);

    while i <= 4 loop
      streami_tvalid <= '1';
      streami_tdata  <= in_data(i);
      streami_tlast  <= in_last(i);

      wait until rising_edge(aclk);
      if streami_tready = '1' then
        i := i + 1;
      end if;
    end loop;

    streami_tvalid <= '0';
    streami_tdata  <= (others => '0');
    streami_tlast  <= '0';

    wait;
  end process;

  check_proc: process
    type out_data_array_t is array (0 to 3) of std_logic_vector(31 downto 0);
    type out_last_array_t is array (0 to 3) of std_logic;
    constant out_data : out_data_array_t := (
      x"00000006",
      x"00000000",
      x"00000000",
      x"00000001"
    );
    constant out_last : out_last_array_t := ('0', '1', '0', '1');
    variable i : integer := 0;
  begin
    wait until aresetn = '1';

    while i <= 3 loop
      wait until rising_edge(aclk);
      if streamo_tvalid = '1' then
        assert streamo_tdata = out_data(i)
          report "Unexpected streamo_tdata" severity failure;
        assert streamo_tlast = out_last(i)
          report "Unexpected streamo_tlast" severity failure;
        assert streamo_tkeep = "1111"
          report "Unexpected streamo_tkeep" severity failure;
        i := i + 1;
      end if;
    end loop;

    wait for 20 ns;
    assert false report "Simulation finished successfully" severity note;
    wait;
  end process;
end architecture;
