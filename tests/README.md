# Engine tests

Run the test suite from the project root:

```bash
./tests/run_tests.sh
```

Or with custom engine path / timeout:

```bash
ENGINE=./engine TIMEOUT=90 ./tests/run_tests.sh
```

## What is tested

1. **FEN mode (start position)** – Engine with no args prints one valid UCI move or `(none)`.
2. **FEN mode (custom position)** – Engine with a FEN string prints one valid UCI move.
3. **Invalid move rejected** – Feeding an invalid UCI move (e.g. `notamove`) produces an "invalid" message on stderr.
4. **Interactive game** – Engine plays white; feeding two black moves yields valid UCI move lines.
5. **Castling position** – Engine from a FEN where castling is legal returns a valid move.
6. **No crash on empty stdin** – Engine exits cleanly when stdin is closed (e.g. no hang).

Exit code 0 means all tests passed; non-zero means at least one failed.
