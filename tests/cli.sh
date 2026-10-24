#!/bin/sh
# Runs every command-line command against the built binary: cli.sh <binary>
set -eu

CYTO="$1"
FIXTURES="$(CDPATH= cd -- "$(dirname -- "$0")/fixtures" && pwd)"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
export HOME="$DIR/home" XDG_DATA_HOME="$DIR/home/.local/share"
mkdir -p "$HOME/.claude"
cp "$FIXTURES"/* "$DIR/"
FAILED=0

pass() { echo "PASS   : $1"; }
fail() { echo "FAIL!  : $1"; FAILED=1; }
check() { # name, command...
    name="$1"; shift
    if "$@" >"$DIR/out" 2>"$DIR/err"; then pass "$name"; else fail "$name"; cat "$DIR/err"; fi
}
refuses() { # name, command...: must exit 1 with a message on stderr
    name="$1"; shift
    if "$@" >"$DIR/out" 2>"$DIR/err"; then fail "$name (succeeded)"; elif [ -s "$DIR/err" ]; then pass "$name"; else fail "$name (no message)"; fi
}
has() { # name, pattern: the last output contains it
    if grep -q -- "$2" "$DIR/out"; then pass "$1"; else fail "$1"; cat "$DIR/out"; fi
}

check "bare cyto prints help" "$CYTO"
has "help lists stats" "stats <workspace.cyto>"
check "help for a command" "$CYTO" help stats
has "stats help names --out" "out <out>"

check "info" "$CYTO" info "$DIR/a.fcs" --json
has "info counts the events" '"events": 300'
has "info lists the stain" '"label": "CD4"'
has "info finds the spillover" '"FL2-A"'
refuses "info refuses a missing file" "$CYTO" info "$DIR/missing.fcs"
printf 'not an fcs file, but long enough to have a header of sorts......' >"$DIR/bad.fcs"
refuses "info refuses a non-FCS file" "$CYTO" info "$DIR/bad.fcs"

check "check a valid workspace" "$CYTO" check "$DIR/small.cyto" --json
has "check says valid" '"valid": true'
has "check lists the populations" '"path": "Small/CD4"'
sed 's/"parent": 1/"parent": 9/' "$DIR/small.cyto" >"$DIR/orphan.cyto"
refuses "check reports a missing parent" "$CYTO" check "$DIR/orphan.cyto"
sed 's/"FSC-A"/"FSC-W"/' "$DIR/small.cyto" >"$DIR/channel.cyto"
refuses "check reports a channel the samples lack" "$CYTO" check "$DIR/channel.cyto"
sed 's/"a.fcs", //' "$DIR/small.cyto" >"$DIR/one.cyto"
sed 's/"b.fcs"/"b-missing.fcs"/' "$DIR/small.cyto" >"$DIR/missing.cyto"
refuses "check reports a missing sample" "$CYTO" check "$DIR/missing.cyto"

check "stats as CSV" "$CYTO" stats "$DIR/small.cyto"
has "stats counts CD4 in a" "^a,Small/CD4,Small,90,50.00,30.00,"
has "stats counts CD4 in b" "^b,Small/CD4,Small,30,25.00,10.00,"
check "stats as JSON" "$CYTO" stats "$DIR/small.cyto" --json
has "stats JSON has percentages" '"percentOfParent": 50'
check "stats to a file" "$CYTO" stats "$DIR/small.cyto" -o "$DIR/stats.csv" --json
[ "$(wc -l <"$DIR/stats.csv")" -eq 7 ] && pass "stats file has a row per sample and population" || fail "stats file rows"
check "stats over other samples" "$CYTO" stats "$DIR/one.cyto" "$DIR/a.fcs"
has "stats gates the named sample" "^a,Small,All events,180,"
refuses "stats refuses a missing sample" "$CYTO" stats "$DIR/missing.cyto"
refuses "stats refuses a missing workspace" "$CYTO" stats "$DIR/nope.cyto"

check "skill prints" "$CYTO" skill
has "skill has front matter" "^name: cyto"
check "skill install" "$CYTO" skill install --json
[ -f "$HOME/.agents/skills/cyto/SKILL.md" ] && pass "skill installed" || fail "skill installed"
[ -L "$HOME/.claude/skills/cyto" ] && pass "skill linked for Claude Code" || fail "skill linked"
check "skill install again" "$CYTO" skill install

[ "$FAILED" -eq 0 ] && echo "Totals: all command-line checks passed" || { echo "Totals: some command-line checks failed"; exit 1; }
