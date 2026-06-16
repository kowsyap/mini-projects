library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use work.mayo_pkg.all;

entity mayo is
   generic (
        --ROUND 1
--        round : positive := 1;
        --ROUND 2
         round : positive := 2;

        --mayo1
--        set : positive := 1
        --mayo2
          set : positive := 2
        --mayo3
        -- set : positive := 3
        --mayo5
--        set : positive := 5

   );
    port (
        clk_i : in  std_logic;
        reset_i : in std_logic;

        msg_bytes_i : in std_logic_vector(2*MAYO_W-1 downto 0); -- message size in bytes
        mode_i : in std_logic; -- mode selection ("0": sign, "1": verify)
        
        msg_data_i : in  std_logic_vector(16*MAYO_NIBBLE-1 downto 0); -- message
        msg_valid_i : in std_logic; -- '1' when message input data is valid
        msg_ready_o : out std_logic; -- make it high to get message input data when ready to accept

        sk_data_i : in  std_logic_vector(16*MAYO_NIBBLE-1 downto 0); -- secret/private key
        sk_valid_i : in std_logic; -- '1' when secret key input data is valid
        sk_ready_o : out std_logic; -- make it high to get secret key input data when ready to accept
        
        sig_verify_data_i : in std_logic_vector(16*MAYO_NIBBLE-1 downto 0); -- Input: signature to verify
        sig_verify_valid_i : in std_logic; -- '1' when signature to verify input data is valid
        sig_verify_ready_o : out std_logic; -- make it high to get signature to verify input data when ready to accept

        sig_gen_data_o : out std_logic_vector(16*MAYO_NIBBLE-1 downto 0); -- Output: signature generated
        sig_gen_valid_o : out std_logic; -- '1' when generated signature output data is valid
        sig_gen_ready_i: in std_logic; -- make it high to get signature output when done='1'
        sig_gen_last_o : out std_logic; -- '1' with the final generated signature output beat
        verify_ok_o : out std_logic;  -- '1' if signature is valid, '0' otherwise

        seq_i : in std_logic := '0'; -- '1' if all the inputs are not used in parallel (for axis dma), by default '0' 
        calc_i: in std_logic; -- generate signature when calc is '1'
        done_o: out std_logic; -- '1' if signature generation completed
        -- module_status_o(21 downto 11): calc bits
        -- module_status_o(10 downto 0): done bits
        -- bit order in each half: esk, esk_v, m_init, a_init, qa, cfl, rref, solution, sig, t, sig_dec
        module_status_o : out std_logic_vector(21 downto 0)
    );
end entity mayo;


architecture structural of mayo is

-- status signals
signal t_done : std_logic;
signal store_done : std_logic;
signal cfl_done : std_logic;
signal init_done : std_logic;
signal qa_done : std_logic;
signal rref_done : std_logic;
signal solution_done : std_logic;
signal solution_valid : std_logic;
signal sig_done : std_logic;
signal sig_dec_done : std_logic;
signal done_t : std_logic;

--control signals
signal esk_calc : std_logic;
signal esk_vcalc : std_logic;
signal m_init_calc : std_logic;
signal a_init_calc : std_logic;
signal qa_calc : std_logic;
signal cfl_calc : std_logic;
signal solution_calc : std_logic;
signal sig_calc : std_logic;
signal t_calc : std_logic;
signal t_decode_calc : std_logic;
signal sig_dec_calc : std_logic;
signal rref_calc : std_logic;
signal rref_reset : std_logic;

signal zc, zi, zr : std_logic;
signal Lc, Li, Lr : std_logic;
signal Ec, Ei, Er : std_logic;
signal wr_perm, rd_en, reg_load : std_logic;

signal verify, signing: std_logic;

signal reset_meta : std_logic := '1';
signal reset_sync : std_logic := '1';
signal reset_r    : std_logic := '1';
attribute ASYNC_REG : string;
attribute ASYNC_REG of reset_meta : signal is "TRUE";
attribute ASYNC_REG of reset_sync : signal is "TRUE";
attribute MAX_FANOUT : integer;
attribute MAX_FANOUT of reset_r : signal is 256;

begin

    reset_sync_proc : process(clk_i)
    begin
        if rising_edge(clk_i) then
            reset_meta <= reset_i;
            reset_sync <= reset_meta;
            reset_r    <= reset_sync;
        end if;
    end process;

    datapath: entity work.mayo_datapath
    generic map (
        w => MAYO_W,
        nibble => MAYO_NIBBLE,
        round => round,
        set => set
    )
    port map(
        clk => clk_i,
        reset => reset_r,
        sig_ready => sig_gen_ready_i,

        sk => sk_data_i,
        msg => msg_data_i,
        msg_bytes => msg_bytes_i,

        sk_input_ready => sk_ready_o,
        msg_input_ready => msg_ready_o,
        sig_input_ready => sig_verify_ready_o,
        sk_input_valid => sk_valid_i,
        msg_input_valid => msg_valid_i,
        sig_input_valid => sig_verify_valid_i,
        signature => sig_gen_data_o,
        signature_i => sig_verify_data_i,

        esk_calc => esk_calc,
        esk_vcalc => esk_vcalc,
        m_init_calc => m_init_calc,
        a_init_calc => a_init_calc,
        qa_calc => qa_calc,
        cfl_calc => cfl_calc,
        solution_calc => solution_calc,
        sig_calc => sig_calc,
        sig_dec_calc => sig_dec_calc,
        t_calc => t_calc,
        t_decode_calc => t_decode_calc,
        rref_reset => rref_reset,
        reg_load => reg_load,

        Lc => Lc,
        Li => Li,
        Lr => Lr,
        Ec => Ec,
        Ei => Ei,
        Er => Er,
        wr_perm => wr_perm,
        rd_en => rd_en,

        t_done => t_done,
        cfl_done => cfl_done,
        store_done => store_done,
        init_done => init_done,
        qa_done => qa_done,
        rref_done => rref_done,
        solution_done => solution_done,
        sig_done => sig_done,
        sig_dec_done => sig_dec_done,
        solution_valid => solution_valid,
        done => done_t,
        valid => verify_ok_o,

        verify => verify,
        signing => signing,
        seq_input => seq_i,

        zc => zc,
        zi => zi,
        zr => zr
    );
    
    controller: entity work.mayo_controller
    port map(
        clk => clk_i,
        reset => reset_r,
        seq_input => seq_i,
        calc => calc_i,
        mode => mode_i,
        sig_ready => sig_gen_ready_i,

        t_done => t_done,
        cfl_done => cfl_done,
        store_done => store_done,
        init_done => init_done,
        qa_done => qa_done,
        rref_done => rref_done,
        solution_done => solution_done,
        sig_done => sig_done,
        sig_dec_done => sig_dec_done,
        solution_valid => solution_valid,

        zc => zc,
        zi => zi,
        zr => zr,

        esk_calc => esk_calc,
        esk_vcalc => esk_vcalc,
        m_init_calc => m_init_calc,
        a_init_calc => a_init_calc,
        qa_calc => qa_calc,
        cfl_calc => cfl_calc,
        solution_calc => solution_calc,
        sig_calc => sig_calc,
        t_calc => t_calc,
        sig_dec_calc => sig_dec_calc,
        rref_calc => rref_calc,
        rref_reset => rref_reset,
        reg_load => reg_load,

        Lc => Lc,
        Li => Li,
        Lr => Lr,
        Ec => Ec,
        Ei => Ei,
        Er => Er,
        wr_perm => wr_perm,
        rd_en => rd_en,

        verify => verify,
        signing => signing,

        sig_valid => sig_gen_valid_o,
        sig_last => sig_gen_last_o,
        done => done_t
    );

    done_o <= done_t;
    module_status_o <= esk_calc & esk_vcalc & m_init_calc & a_init_calc &
                       qa_calc & cfl_calc & rref_calc & solution_calc &
                       sig_calc & t_decode_calc & sig_dec_calc &
                       store_done & store_done & init_done & init_done &
                       qa_done & cfl_done & rref_done & solution_done &
                       sig_done & t_done & sig_dec_done;

end structural;
