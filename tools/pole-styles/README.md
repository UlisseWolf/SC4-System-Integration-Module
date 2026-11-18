# Pole styles as exemplars

`make_pole_styles.py styles.ini OUT.dat` writes one pole-style exemplar (type 0x6534284A, group 0x5C4DB000) per
`[PowerPoles.<Name>]` section, with the same keys as `SC4SIM.ini` (`REG.*`, `FAR-*`, `InterPoleDistance`) plus:

- `Instance = 0x........` - exemplar instance (default: CRC32 of the lower-case name);
- `Order = n` - position in the Tab cycle (default 10, 20, 30... in file order).

`[PowerPoles.FAR]` becomes the FAR-default exemplar. SC4SIM.dll 1.12 and later reads these exemplars from every
plugin; see docs/power-pole-customization.md ("Pole styles in a .dat").

`p3_styles.ini` is the source of `CAM_PowerPoleStyles.dat` (P3Wood 0x5C4DB101, P3Steel 0x5C4DB102):

    python make_pole_styles.py p3_styles.ini CAM_PowerPoleStyles.dat
