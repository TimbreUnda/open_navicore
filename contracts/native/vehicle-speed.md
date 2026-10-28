# VehicleData displayed-speed units

Recovered 2026-10-28 from the operator's USA QR39.5 `nv_navigation`, SHA-256
`beab7cebd6b31792e3d179712f2999a9fe9f5ed1210bb350d19c79c99310c1c8`.
Addresses below are Thumb instruction addresses in that executable, not
offsets in `libNaviCore.so`. This corrects the formerly incomplete slot-8
prototype in the provider matrix: the float was retained, its unit was omitted.

## Provider call

`SI_VehicleData::notifyClientAdapter`, entry `0x0812b230`:

- `0x0812b348` loads the cached float from `self+0x38` into `s14`.
- `0x0812b34c..0x0812b358` compares it with 325.0 (literal at `0x0812b5a8`);
  the greater-than branch does not call the provider.
- `0x0812b3b6` loads the unit byte at `self+0x3c` into **r2**.
- Unit 1 takes `0x0812b3c6..0x0812b3d0`: provider object in **r0**, float
  bits in **r1** (`vmov r1,s14`), virtual method at byte offset **0x20**,
  `blx r3`. **r2 remains 1**.
- Unit 2 takes `0x0812b50e..0x0812b520`: multiply the float by
  `0.6213709712028503` (literal `0x0812b5ac`), pass it in **r1** to the same
  method; **r2 remains 2**. This is km/h to mph conversion.
- Other unit values do not call this method.

The recovered ARM soft-float call is therefore
`uint32_t slot8(void *self, float displayed_speed, uint32_t unit)`, with
unit **1 = km/h**, **2 = mph**. Only the low byte of the unit is populated
by this caller; treating it as a core-register word preserves it exactly.
The backend divides km/h by 3.6 or multiplies mph by 0.44704.

## Input lifetime

The speed-event handler at `0x0812b92c` reads a four-byte event. At
`0x0812b996..0x0812b99e` it accepts raw values through `0xfffe`; at
`0x0812b9c8..0x0812b9dc` it converts that integer with seven fractional
bits (division by 128), stores `self+0x38`, and invokes the notifier with
mask 4. This is an event-fed retained property; this handler has no periodic
refresh timer. The read-only bench log inspection on 2026-10-28 contained
one zero-speed notification while GPS reached callback count 1024.

The code consequently retains speed for the provider lifecycle, including
constant speed and a standstill. Day/night changes must not change its unit.
The callback cadence while moving, and a separate native signal-loss
notification, are **not recovered**. A sample-age timeout is not used:
notification silence alone does not establish invalidity. On provider reset
the accumulated valid bits and unit are cleared.

## Backend extension and verification

`OpenNaviBackendVehicleV2` retains its original 32-byte prefix and appends
`uint32_t displayed_vehicle_speed_unit` at byte 32 (size 36). Older
32-byte inputs remain accepted, with the absent unit zero-initialized.
Unknown units, nonfinite/negative speed, and normalized speeds above
325 km/h disable suppression of distant instructions and produce a backend
trace; GPS is not silently substituted. Simulation explicitly supplies its
configured speed.

Host tests cover native slot dispatch, coalesced vehicle state, unit-only
changes, legacy prefix handling, metric/imperial normalization, and the
cluster visibility policy. The ARM object check verifies that slot 8 consumes
both incoming `r1` and `r2`. These checks do not constitute a road test.
