#!/bin/sh
# tnes session/autosave smoke test (runs the real UI headlessly via tmux)
#
# Covers:
#   T1  autosave-on-enter: typing + return writes the database to disk
#   T2  hostile session file: garbage/negative/overflow lines are ignored
#   T3  session round-trip: cursor + expanded state restore on relaunch
#   T4  all-hostile session file: non-numeric values are not read as 0
#
# Requirements: tmux, a built src/tnes. Run from anywhere.
#
# Exit status: 0 all tests passed, 1 at least one test failed, 77 skipped
# (no tmux / no binary / no system tinesrc). Only touches the tmux session
# named "smoke" -- other tmux sessions are left alone.

set -u

tmp=$(mktemp -d)
tnes_bin="${TNES_BIN:-$(dirname "$0")/../src/tnes}"
fail=0

cleanup() { tmux kill-session -t smoke 2>/dev/null; sleep 1; rm -rf "$tmp" 2>/dev/null; }
trap cleanup EXIT

[ -x "$tnes_bin" ] || { echo "SKIP: $tnes_bin not built"; exit 77; }
command -v tmux >/dev/null || { echo "SKIP: no tmux"; exit 77; }

# sandbox HOME with default rc and a tiny database
cp /usr/share/tines/tinesrc "$tmp/.tinesrc" 2>/dev/null \
	|| { echo "SKIP: /usr/share/tines/tinesrc not found"; exit 77; }
cat > "$tmp/.tines" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<tree>
<node><data>alpha topic</data></node>
<node><data>beta topic</data>
  <node><data>child a1</data></node>
  <node><data>child b2</data></node>
</node>
</tree>
EOF

launch() {
	tmux kill-session -t smoke 2>/dev/null
	tmux new-session -d -s smoke -x 100 -y 30 \
		"bash -c 'env HOME=$tmp $tnes_bin; sleep 600'"
	sleep 2
}

# --- T1: autosave on enter ---------------------------------------------
launch
tmux send-keys -t smoke Down; sleep 0.3
tmux send-keys -t smoke "+"; sleep 0.5
tmux send-keys -t smoke "smoke typed note"; sleep 0.4
tmux send-keys -t smoke Enter; sleep 1
if grep -q "smoke typed note" "$tmp/.tines"; then
	echo "T1 PASS: enter-commit saved database"
else
	echo "T1 FAIL: enter-commit did not save database"; fail=1
fi
if [ -e "$tmp/.tines_tnes_session.tmp" ]; then
	echo "T1 FAIL: session tmp file left behind (non-atomic write)"; fail=1
fi
if [ -f "$tmp/.tines_tnes_session" ]; then
	echo "T1 PASS: session file written atomically"
else
	echo "T1 FAIL: session file not written"; fail=1
fi

# --- T2: hostile session file ------------------------------------------
tmux kill-session -t smoke 2>/dev/null
printf '# tnes session state\ndb %s/.tines\ncursor 99999999999999\nexpanded -5\nexpanded 1\nexpanded 99999999999\ncursor -3\nexpanded banana\ncursor 1\n' "$tmp" > "$tmp/.tines_tnes_session"
launch
pane=$(tmux capture-pane -t smoke -p)
case "$pane" in
	*"- beta topic"*)
		echo "T2 PASS: valid lines applied, hostile lines ignored"
		;;
	*)
		echo "T2 FAIL: session not restored from mixed file"; fail=1;;
esac
tmux kill-session -t smoke 2>/dev/null

# --- T3: write clean session (quit-save) and round-trip ----------------
launch
tmux send-keys -t smoke C-q; sleep 0.5
tmux send-keys -t smoke y; sleep 1.5
tmux kill-session -t smoke 2>/dev/null

launch
pane=$(tmux capture-pane -t smoke -p)
case "$pane" in
	*"- beta topic"*)
		echo "T3 PASS: expanded state round-trips"
		;;
	*)
		echo "T3 FAIL: expanded state not restored"; fail=1
		;;
esac
tmux kill-session -t smoke 2>/dev/null

# --- T4: all-hostile session file (runs last: replaces the clean
# sidecar T3 depends on) --------------------------------------------------
# "expanded banana" must be ignored entirely: it used to be parsed with
# strtol as 0 and silently expanded the FIRST node.
printf '# tnes session state\nexpanded banana\ncursor banana\nexpanded -5\ncursor 99999999999999\n' > "$tmp/.tines_tnes_session"
launch
pane=$(tmux capture-pane -t smoke -p)
case "$pane" in
	*"+ beta topic"*)
		echo "T4 PASS: non-numeric session lines ignored"
		;;
	*)
		echo "T4 FAIL: garbage session line changed expansion state"; fail=1
		;;
esac

exit $fail
