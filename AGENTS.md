# Agent notes for open_navicore

## Merging pull requests

- Merge with **rebase**, not squash (owner preference, 2026-10-20):
  `gh pr merge <PR#> --rebase`. Keep the branch's commits as they are, so each
  small refactoring step stays a separate commit in `main`.
- Write commits so that each one builds and passes the host tests on its own.
- Agents do not merge to `main` unless the operator says so in that session.
- Local `main` and `origin/main` stay identical (they diverged by 30+ commits once and needed a
  sync merge, 2026-10-24). Before changing local `main`: `git fetch` and
  `git merge --ff-only origin/main`; right after: `git push origin main`. After `gh pr merge`,
  fast-forward local `main` the same way. Never leave a merge on local `main` unpushed.
