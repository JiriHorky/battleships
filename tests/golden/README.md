`legacy_9x8_default.txt` is the stdout of the original `main.cpp` (default
9x8 board and fleet) with the non-deterministic debug lines removed: the
`Placing ship on idx N <timestamp>` progress lines and the `index hash`
lines printed by `shapes_map`. What remains is the index grid, the per-cell
frequency table and `Total hits`. `tests/test_sanity.cpp` parses the
numbers; it does not byte-compare the program's stdout.
