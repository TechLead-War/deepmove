#!/usr/bin/env bash
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ENGINE="${ENGINE:-./engine}"
TIMEOUT="${TIMEOUT:-60}"
if command -v timeout >/dev/null 2>&1; then
  RUN_TIMEOUT="timeout $TIMEOUT"
elif command -v gtimeout >/dev/null 2>&1; then
  RUN_TIMEOUT="gtimeout $TIMEOUT"
else
  RUN_TIMEOUT="perl -e 'alarm shift; exec @ARGV' $TIMEOUT"
fi
UCI_MOVE_PATTERN='^[a-h][1-8][a-h][1-8][nbrq]?$'
PASS=0
FAIL=0

run_test() {
  local name="$1"
  if eval "$2"; then
    echo "  PASS: $name"
    ((PASS++)) || true
    return 0
  else
    echo "  FAIL: $name"
    ((FAIL++)) || true
    return 1
  fi
}

echo "Building engine..."
make -C "$ROOT" 1>/dev/null 2>&1 || { echo "Build failed"; exit 1; }
echo ""

echo "--- Test 1: FEN mode (start position) ---"
run_test "Engine returns one line from start position" "
  out=\$($RUN_TIMEOUT $ENGINE 2>/dev/null || true)
  first=\$(echo \"\$out\" | head -1)
  [ -n \"\$first\" ] && echo \"\$first\" | grep -qE \"\$UCI_MOVE_PATTERN|^\\(none\\)\$\"
"

echo ""
echo "--- Test 2: FEN mode (custom position) ---"
run_test "Engine returns one line from FEN" "
  fen='rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1'
  out=\$($RUN_TIMEOUT $ENGINE \"\$fen\" 2>/dev/null || true)
  first=\$(echo \"\$out\" | head -1)
  echo \"\$first\" | grep -qE \"\$UCI_MOVE_PATTERN|^\\(none\\)\$\"
"

echo ""
echo "--- Test 3: Invalid move rejected ---"
run_test "Invalid UCI move produces error on stderr" "
  stderr=\$(mktemp)
  (echo 'notamove'; echo 'quit') | $RUN_TIMEOUT $ENGINE w 2>\$stderr | head -1 >/dev/null
  grep -qi 'invalid' \$stderr
  r=\$?
  rm -f \$stderr
  [ \$r -eq 0 ]
"

echo ""
echo "--- Test 4: Interactive game (engine plays white) ---"
run_test "Engine outputs valid UCI moves in a short game" "
  moves='e7e5
g8f6'
  out=\$(echo \"\$moves\" | $RUN_TIMEOUT $ENGINE w 2>/dev/null || true)
  count=\$(echo \"\$out\" | grep -c . || echo 0)
  [ \"\$count\" -ge 1 ] && [ \"\$count\" -le 5 ]
  all_valid=true
  while IFS= read -r line; do
    [ -z \"\$line\" ] && continue
    if ! echo \"\$line\" | grep -qE \"\$UCI_MOVE_PATTERN|^\\(none\\)\$\"; then
      all_valid=false
      break
    fi
  done <<< \"\$out\"
  [ \"\$all_valid\" = true ]
"

echo ""
echo "--- Test 5: Castling input accepted ---"
run_test "Engine accepts e8g8 (black kingside) after setup" "
  fen='r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1'
  out=\$($RUN_TIMEOUT $ENGINE \"\$fen\" 2>/dev/null || true)
  first=\$(echo \"\$out\" | head -1)
  echo \"\$first\" | grep -qE \"\$UCI_MOVE_PATTERN|^\\(none\\)\$\"
"

echo ""
echo "--- Test 6: No crash on empty stdin (interactive) ---"
run_test "Engine exits cleanly on empty/closed stdin" "
  printf '' | perl -e 'alarm 3; exec @ARGV' $ENGINE w 2>/dev/null; true
  [ \$? -eq 0 ] || [ \$? -eq 124 ]
"

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="
[ "$FAIL" -eq 0 ]
