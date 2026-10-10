# Contributing to Open NaviCore

Thanks for helping make the stock navigation boundary useful to open navigation
engines. Changes are welcome when they preserve the distinction between proven
ABI compatibility and still-unknown product semantics.

## Before opening a change

- Do not submit proprietary firmware, SDK files, map data, credentials, keys,
  or logs containing personal/location data.
- Keep live testing limited to authorized, stationary bench hardware.
- Open an issue before changing a public backend ABI, recovered vtable layout,
  callback prototype, or evidence classification.
- Keep refactors separate from new ABI or semantic claims when practical.

## Development workflow

1. Create a branch from `main`.
2. Make the smallest coherent change.
3. Add or update tests and checked evidence.
4. Run the relevant focused test, followed by:

   ```sh
   make test-host
   make check-contracts
   ```

   Changes to target ABI code additionally require maintainer-recorded
   `make -B -j2 verify-qnx`; changes to replay fixtures require
   `make replay-stock` with the identified local stock image.

5. Explain the evidence class, ownership/lifetime behavior, and any remaining
   limitations in the pull request.

## ABI evidence rules

A symbol name or plausible decompilation is not enough to change a native
contract. ABI changes should identify concrete machine instructions, vtable
address points, register/stack placement, pointer access extent, and ownership.
Use an independent producer, consumer, compiler oracle, or bounded replay when
the contract involves aggregates, hidden returns, alignment, or pointer data.

Keep these evidence classes separate in code and documentation:

- static machine evidence;
- compiler-oracle evidence;
- synthetic/replay evidence;
- native runtime evidence;
- inferred semantics.

## Generated and checked files

The following are intentionally tracked because verification treats them as
reviewable contracts:

- `src/capture/generated/mock_raw_callback_slot_table.c`
- `src/capture/generated/mock_raw_callback_slots_arm.S`
- `src/compat/qr39/mock_static_provider_slots.def`
- root-level ABI/evidence CSV files
- replay fixtures under `fixtures/`

Regenerate them with the repository tools and commit generator and output
changes together. Do not hand-edit generated tables to make a check pass.

## Style

- C code targets C99 and builds with warnings as errors.
- Keep public ABI declarations explicit and fixed-width.
- Avoid variadic or generic catch-all functions at recovered boundaries.
- Prefer bounded copies and document whether pointer data is borrowed, copied,
  retained, or returned.
- Preserve the existing error and lifecycle conventions.

By participating, you agree to follow [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
