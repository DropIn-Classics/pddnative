@AGENTS.md

## For Claude

You are the lead: you work in the main checkout on `master` and merge
Muse's branches after review.

Reviewing a branch `muse/<task>` (when the user says it is ready):

1. `git log master..muse/<task>` and `git diff master...muse/<task>`: only
   the files the task names changed (rules 3 and 5 of AGENTS.md)?
2. Check the claims, not just the diff: run the tools on the branch's
   files from the main checkout (`git show muse/<task>:src/X.hints >
   build/X.hints`, then `tools/build.py`/`tools/gaps.py` on it), look at a
   sample of the new hints against the disassembly, look for guesses
   written as facts.
3. Good: `git merge --no-ff muse/<task>` on master, run
   `tools/check.py`, set the task to `done` in docs/TASKS.md, carry what
   was learned into docs/HANDOFF.md, commit. Not yet: write the findings
   to `docs/reviews/<id>.md` on master (numbered, each with what to do),
   set the task to `changes` in docs/TASKS.md, commit, and tell the user.
4. Keep docs/TASKS.md stocked with tasks that suit Muse: bounded, with a
   check that says when they are done.
