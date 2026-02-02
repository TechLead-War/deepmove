# Chess engine

Build: `make`

**One shot** — start position or a FEN:  
`./engine` or `./engine "fen string"`  
Output: best move in UCI (e.g. `e2e4`).

**Interactive (new game)** — you pick your colour, then you type the opponent’s move and the engine replies with your best move each time.  
`./engine white` or `./engine black` (or `w` / `b`).  
- If you’re **white**: the engine prints your first move; you type the opponent’s reply (e.g. `e7e5`), enter; engine prints your next move; repeat.  
- If you’re **black**: you type the opponent’s first move (e.g. `e2e4`), enter; engine prints your reply; repeat.  
Moves are in UCI format: `e2e4`, `g1f3`, `e7e8q` (promotion). Type `quit` or press Ctrl-D to exit.

**FEN** is a single line that encodes a board (where the pieces are, who is to move, castling rights, en passant). Use it when you want the engine to think from a specific position instead of the start. Paste the line in quotes after the program name.

Example — the position after White plays 1. e4:

```
rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1
```

So: `./engine "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"` gives Black’s best reply.

## Force play last engine move to a manual one.
<br>

```force <uci> or play <uci>```<br>Replaces the last engine move with your custom move.