-- Created by IP Generator (Version 2021.4-SP1.2 build 96435)
-- Instantiation Template
--
-- Insert the following codes into your VHDL file.
--   * Change the_instance_name to your own instance name.
--   * Change the net names in the port map.


COMPONENT pllclk
  PORT (
    pll_rst : IN STD_LOGIC;
    clkin1 : IN STD_LOGIC;
    pll_lock : OUT STD_LOGIC;
    clkout0 : OUT STD_LOGIC;
    clkout1 : OUT STD_LOGIC;
    clkout2 : OUT STD_LOGIC;
    clkout3 : OUT STD_LOGIC;
    clkout4 : OUT STD_LOGIC
  );
END COMPONENT;


the_instance_name : pllclk
  PORT MAP (
    pll_rst => pll_rst,
    clkin1 => clkin1,
    pll_lock => pll_lock,
    clkout0 => clkout0,
    clkout1 => clkout1,
    clkout2 => clkout2,
    clkout3 => clkout3,
    clkout4 => clkout4
  );
