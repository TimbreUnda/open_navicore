# Agent notes for open_navicore

## Failures: obvious, not silent

- An obvious failure is better than a silent fallback (owner preference, 2026-10-28).
  When a component fails, make it visible (the feature visibly missing, an error
  trace line), do not quietly substitute an older or weaker path that hides it.
- A fallback is only for an explicit switch: e.g. OsmAnd's own manoeuvres run only
  when Organic Maps' turns are switched off (marker `osmand-turns`), never because
  Organic Maps' code failed on a route (that route gets no turns and
  `worker=omaps_error`).

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
