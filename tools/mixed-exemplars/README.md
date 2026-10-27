# Mixed R + C exemplars

`make_mixed.py` makes R + C mixed copies of building exemplars (`.SC4Desc` files with text exemplars, `EQZT1###`)
for the SC4SIM MixedOccupancy module. It was written for the MN Mediterranean packs
(Mediterranean Pack 2, Medit Pack 2 and Medit Pack 3).

```
python make_mixed.py <original packs folder> <output folder> <report.json>
```

It copies the folder and edits only the `.SC4Desc` files. Lots and models are left untouched. In each exemplar it:
- adds `Allow Joint Occupancy` (0x27812836) = True;
- appends one secondary entry to `Capacity Satisfied`, at the building's own wealth:
  - an R building gets CS (CO if the same model ships as CO) = 25% of the commercial version's capacity;
  - a CS/CO building gets R = 25% of the residential version's capacity;
  - a model without the other version uses CS = 0.6 × R and CO = 1.0 × R.

The share and ratios are `SHARE` and `RATIO` at the top of the script. `dbpf.py` is a minimal DBPF 1.0 reader and
writer for uncompressed files.

Third-party packs are not included: run the script on your own copy, and ask the author before you publish
modified files.
