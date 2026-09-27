# Working on pddnative (for every agent)

Two agents work on this repository at the same time, each in its own
checkout. Read README.md (what this is) and docs/HANDOFF.md (the state)
first; this file is the rules.

## Who works where

| | checkout | branches | does |
|---|---|---|---|
| Claude (lead) | `C:\Users\xbox\source\pddnative` (main) | `master` | the engine's core, reviews, merges into `master` |
| Muse | `C:\Users\xbox\source\pddnative-muse` (git worktree) | `muse/<task>` | the tasks in docs/TASKS.md marked for Muse |

Both checkouts share one `.git`. Every change reaches `master` by a merge
in the main checkout after review, never otherwise.

## Rules

1. Work only in your own checkout. Do not open, edit or build files in
   the other one (the tools reading the unpacked CD from the main
   checkout's `game/` is fine). To bring master's newer state into your
   branch, `git merge master` on your branch.
2. Muse: commit on `muse/<task>` branches only. Never `git push`,
   `git reset --hard`, `git rebase` of anything but your own unmerged
   branch, `--force` of any kind, `git worktree remove`, deleting branches
   you did not make, or `git stash` in a way that throws work away. When
   git refuses something, stop and write it in your task's notes.
3. No game data in the repository: nothing from `game/` or `build/`, no
   program or data files, no bytes of them pasted into sources or docs
   (hints hold addresses, names and comments). The pre-commit hook
   refuses the usual cases; the rule holds beyond them.
4. `python tools/check.py` must say `all ok` before every commit (the hook
   runs it when `src/` or `tools/` changed). Do not skip the hook
   (`--no-verify`).
5. Files you own are the ones your task names, plus your task's notes
   file. Changing anything else, in particular `tools/*.py`,
   `src/PD.hints`, `src/PD2.hints`, `docs/TASKS.md`, `docs/HANDOFF.md`,
   `README.md` and this file: ask first, in your notes. The block of
   `src/PD2.hints` below `; ==== carried over` is written by
   `tools/xfer.py` only.
6. Say what you did not verify. A guess in a hints comment says it is a
   guess ("presumably", "not checked"). Numbers and addresses are copied
   from tool output, not from memory.
7. When unsure what is wanted, write the question into your notes, set
   your status to `question` and stop working on that point.

## Where things are written

| file | written by | on | holds |
|---|---|---|---|
| `docs/TASKS.md` | Claude / the user | master | the tasks: what, for whom, done when, status |
| `docs/tasks/<id>.md` | whoever works on it | the task's branch | status, notes, findings, questions |
| `docs/reviews/<id>.md` | Claude | master | review findings for a task |

So no two agents edit the same file on different branches. Read master's
files from your worktree with `git show master:docs/TASKS.md` (your
branch may be older).

## A task, start to end (Muse)

1. Look in `git show master:docs/TASKS.md` for the first task with status
   `open` marked for Muse. Make its branch from the current master:
   `git switch -c muse/<id>-<word> master`.
2. Create `docs/tasks/<id>.md` with a first line `Status: working`, commit
   it as the branch's first commit.
3. Work; commit in small steps with messages like the ones in `git log`
   (what changed and why, in plain words; English).
4. Done means: what the task lists under "done when" holds,
   `tools/check.py` says `all ok`, and your notes say what was found, what
   is left and what is uncertain. Set `Status: review`, commit, and tell
   the user the branch is ready.
5. Claude reviews. Either the branch is merged (done), or there is
   `docs/reviews/<id>.md` on master: read it with
   `git show master:docs/reviews/<id>.md`, set `Status: working`, fix on
   the same branch, and back to step 4. Answer each point of the review in
   your notes.

## Technical conventions

- Python 3, standard library plus `capstone`. The style of the existing
  tools: a module docstring saying what the tool is for and how to call
  it, short functions, comments where the reason is not obvious, no
  frameworks.
- Hints files: syntax in the docstring of `tools/disasm.py`. The code
  segment must be named `CODE` and the one DS normally holds `DATA`
  (the generated ASSUME uses these names). Addresses are
  `SEG:OFFSET` in hex, four digits.
- Adding a program (stage 1): find its segments (relocation values,
  header SS:SP, `python tools/build.py` output), write `src/NAME.hints`,
  then repeat `tools/build.py` and `tools/gaps.py`: every gap is either
  code reached through a pointer (find the pointer; add `code`/`ptr`/
  `words`/`coderange` hints with a comment saying where it comes from)
  or data (leave it). `tools/ptrscan.py` finds immediates that are
  addresses (check each by eye before adding a `dptr`).
- Everything the tools write goes to `build/` (ignored). The unpacked CD
  is found automatically (`$PDD_GAME`, else `game/` here, else `game/` of
  the main checkout).
- Write in English in the repository.
