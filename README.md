# tnes

tnes is a fork of [Tines](https://github.com/larrykollar/tines)
(which is itself a fork of the hnb outliner) with a few
quality-of-life features added. It is drop-in compatible with Tines:
it reads and writes the same database (`~/.tines`), the same
configuration file (`~/.tinesrc`), and the same file formats, so the
data is shared between both programs.

## What tnes adds

* **Autosave on Enter.** Every time you commit an entry (finishing a
  new node or editing an existing one with return), tnes runs the same
  `save` command that the F2 key is bound to, so your database is
  written to disk after every edit.

* **Session restore.** tnes remembers where your cursor was and which
  nodes were expanded the last time you left, and puts everything back
  the way it was on the next launch. The state is stored in a small
  sidecar file, `<database>_tnes_session` (e.g. `~/.tines_tnes_session`),
  next to the database itself. It is updated on every save, every
  quit, on tines' own timed autosave, and after every keypress (only
  rewritten when something actually changed). A stale session file --
  for example after editing the database with another tool -- is
  simply ignored rather than restoring bogus positions.

* **`tnes` instead of `tines`.** The binary is renamed so both can
  coexist (the repo/package name is tnes; the in-program strings
  attribute the original: "tnes, based on Tines by Larry Kollar").

The session file is plain text (`cursor <n>` and `expanded <n>` lines
numbered by depth-first walk from the first visible node), so it is
easy to inspect or delete. Deleting it just disables restore until it
is recreated.

## Building and installing

```
autoreconf -fi
./configure --prefix=$HOME/.local
make
make install
```

That puts the `tnes` binary in `~/.local/bin` and libcli in
`~/.local/lib`, and installs this manpage as `man tnes`. (Note that
the shared data files from `doc/`, such as the starter rc, are not
installed by the build; on a fresh machine tnes generates default
preferences instead, and `doc/tines.1` remains the upstream
manpage.)

## Testing

With tmux installed and `src/tnes` built, run the headless smoke
test:

```
tests/session-smoke.sh
```

It exercises autosave-on-enter, hostile session files, and the
cursor/expansion round-trip inside a throwaway `HOME` (exit 0 = pass,
77 = skipped, 1 = failure). It only touches the tmux session named
`smoke`; other tmux sessions are left alone.

---

# tines

Tines is a console-based outliner/planner/notebook.
It is a fork of the hnb outliner,
which has not been updated in over 10 years.

The current version is 1.11.1. See the [Changelog](https://github.com/larrykollar/tines/wiki/Changelog.md) for a list of new features.

See the ROADMAP file for further planned updates
along the way to version 2.0 and beyond.

The [wiki](https://github.com/larrykollar/tines/wiki) has a
fairly complete and updated set of documentation.
The doc directory provides manpages, sample files,
and general documentation.
