# Agent notes for open_navicore

## Merging pull requests

- Merge with **rebase**, not squash (owner preference, 2026-10-20):
  `gh pr merge <PR#> --rebase`. Keep the branch's commits as they are, so each
  small refactoring step stays a separate commit in `main`.
- Write commits so that each one builds and passes the host tests on its own.
- Agents do not merge to `main` unless the operator says so in that session.
