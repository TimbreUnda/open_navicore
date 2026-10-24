# Voice guidance: wiring Ukrainian TTS into Open NaviCore

2026-10-20. Offline analysis, nothing was run on the radio for this note. It answers: how do
spoken guidance prompts get from the OsmAnd backend to the speakers, given what the TTS work
found about sound (`contracts/native/as_audio_service_allocate_channel.md` in the radio workspace, outside this repository)?
Roadmap item: Milestone 3, "Add voice guidance and stock audio-focus integration".

Labels follow `api-ui-restrictions.md`: **recovered** (address or file cited), **inferred**,
**not recovered**.

## Where the pieces are

| Piece | State | Source |
| --- | --- | --- |
| Guidance state (distance to maneuver, next and following maneuver, off-route, recalculating, arrived) | exists, renderer thread | `backends/osmand/ui/navigation_model.h` `GuidanceState`, `guidance_tracker.cpp` |
| Ukrainian UI strings | exists | `backends/osmand/ui/localization.cpp` |
| Ukrainian speech engine (eSpeak NG, RHVoice Anatol/Natalia/Volodymyr/Marianna), plays through io-audio | works on the radio, 0.4-0.5 RTF, ~1.8 s engine load per call | `uk-voice/tts/`, `tts_play` |
| Sound reaching the speakers | io-audio alone is silent; a DSP channel must be allocated by `as_audio_service` | contract md above |
| Backend ABI audio | inbound only: `audio_changed` (source, volume, request handle); outbound `publish_audio_availability` (states 1..4) | `include/open_navicore/backend.h` |
| Outbound audio/TTS requests | typed but **dormant**, no backend API yet | `contracts/qr39/callbacks/proxy-slot-manifest.csv` |

Dormant outbound seams (NaviCore-to-PAS, open_navicore is in the NaviCore role, `nv_navigation` is the provider):

| Interface/slot | Name | Prototype | Evidence |
| --- | --- | --- | --- |
| SI_AudioHandling 6 | `requestAudioSourceAllocation` | `(self, handle, source)` | concrete receiver `0812fee8` |
| SI_AudioHandling 7 | `requestAudioSourcePreparation` | `(self, handle, source)` | `0812e868` |
| SI_AudioHandling 8 | `signalDeallocateAudioSource` | `(self, source)` | `0812db5c` |
| SI_AudioHandling 9 | `replyAudioSourceDeactivation` | `(self, handle, source, result)` | `0812d8e4` |
| SI_AudioHandling 11/14 | `requestAudioSourceStatus` / `EnableStatus` | `(self, handle, source)` | `0812e3d4`, `0812e350` |
| SI_TTS 24/3 | `requestPlayTTS` | `(self, handle, source, text_type, const char *text)`, text 1..1027 bytes | `080de278` |

Stock facts that matter: `nv_navigation` logs `[SI_AUDIO] Audio channel access is not
available. [AllocStatus: %d]` and opens `/dev/pcmC7D0p` for its audio source (the NAVI source,
id 8). So stock navigation already does allocation then PCM; it does not need a custom client.

## Three routes

### A. Stock TTS (SI_TTS requestPlayTTS)

Emit `requestPlayTTS(handle, NAVI, type, text)`; the stock side synthesizes and plays through
the Nuance natp engine. Pros: zero audio plumbing, stock focus and ducking. Cons:
the contract md records that natp has **no Ukrainian voice**, so Ukrainian text would be
mispronounced or rejected. Usable only for English and other installed languages. Whether
`text_type` selects phonetic input or a prompt id is **not recovered**.

### B. Own TTS plus stock audio focus (recommended)

The OsmAnd backend produces the text, our engine renders PCM, and stock code allocates the
channel for us:

1. `requestAudioSourceAllocation(handle, NAVI)` (slot 6), optionally `...Preparation` (slot 7).
2. Wait for the provider reply that reports the allocation status. Provider slots 5, 7, 8 and 18
   of adapter 10 are reachable but their names are **not recovered**
   (`contracts/qr39/provider/slot-manifest.csv`); the stock `AllocStatus` value is one word.
3. Open `pcmC7D0p` (as `nv_navigation` does) and write S16 PCM. Rate: our voices are 22/24 kHz,
   io-audio resamples (the Bluetooth notes saw RATE_POLY/SRC creation for 44.1 to 48 kHz).
4. When done, `signalDeallocateAudioSource(NAVI)` (slot 8).

Why this over route C: stock keeps arbitration (it mutes or ducks entertainment, knows the
zone and channel) and we never construct PAS messages by hand inside `nv_navigation`.
Open question: does the stock allocation actually produce the AllocateChannel to
`as_audio_service` with the combination that reaches the speakers? Inferred yes; confirm
with one live stock-source test (see below).

### C. Own TTS plus own AllocateChannel (what `as_alloc` does)

The backend, running inside `nv_navigation`, is already inside the PAS framework (it is
loaded as the NaviCore replacement), so it could open a session to `as_audio_service` and send
request 0x20 as `uk-voice/tts/test/as_alloc.cc` does. Cons: duplicates the stock source
bookkeeping, may conflict with the stock NAVI allocation (two owners of one source), and the
response and payload remainder are not recovered. Keep as a fallback and as the bench tool.

## Recommended structure (route B)

```
GuidanceTracker::update()              renderer thread, GuidanceState
  -> PromptPlanner                     pure function: (previous, current state, language) -> text
       "Через двісті метрів поверніть праворуч", "Ви прибули", "Прокладаємо новий маршрут"
       rules: announce at fixed distances per speed band, once per maneuver, repeat suppression
  -> VoiceQueue (new, own worker)      bounded queue, drop-oldest, priority (arrival/reroute first)
  -> TtsEngine (persistent)            eSpeak/RHVoice loaded once, so no 1.8 s per prompt
  -> AudioFocus (new, backend API)     allocate(NAVI) / release(NAVI) via the dormant seams
  -> PcmSink                           snd_pcm on pcmC7D0p, drains, then release
```

New backend ABI needed (V4, additive per the V1..V3 rules in `backend.h`):

- outbound services: `request_audio_source(runtime_context, source)` and
  `release_audio_source(...)` wrapping slots 6 and 8, and an inbound
  `audio_source_state(backend_context, source, status)` once the provider reply slot is named;
- no TTS slot is required for route B; route A stays optional per language.

Process and thread constraints (from `docs/technical.md`): no host callback while the callback
mutex is held; outbound calls only between `start_v2` and stop; names are bounded ASCII. The
voice worker must be a real joined thread reported through the thread-status service. Speech
must never run on the render thread.

## Offline work that can proceed without the radio

1. `PromptPlanner` with host unit tests (Ukrainian numerals and plural forms: 200 метрів,
   1 кілометр, 2 кілометри, 5 кілометрів; maneuver names from `Maneuver`).
2. Persistent TTS library: build eSpeak/RHVoice as a static library or shared object with a
   `synthesize(text) -> PCM` call (`uk-voice/tts/build.sh`); measure memory against the 71 MB
   `nv_navigation` footprint seen on the radio (`private=71284K`).
3. ABI V4 header and host smoke tests for the two outbound seams, following
   `tests/backend_seam_smoke.c`.
4. Name the provider audio slots 5/7/8/18 by decompiling `nv_navigation` concrete provider
   targets (`nv_navigation:0x0812e560` is the provider vtable): which one carries the
   allocation result, and which asks us to stop speaking (focus loss).
5. Decompile the `nv_navigation` caller that logs `AllocStatus` to confirm the reply layout.

## Live checks that need the radio (later)

- one stock-source observation: trigger a stock nav prompt and watch which PAS requests flow
  (libvpspy-calls on the observer side) to confirm slot 6 leads to AllocateChannel 0x20;
- `as_alloc` audibility (channel 2, source 8, zone 1 on `pcmC7D0p`; fallback channel 1 /
  source 15 / zone 1 on `pcmC1D0p`);
- focus loss during a phone call: provider reply on `replyAudioSourceDeactivation`.

Not claimed: audibility of any path, the meaning of `text_type`, the provider reply slot for
allocation status, the response payload of AllocateChannel.

## Implemented (branch `feat/voice-guidance`, 2026-10-20, nothing run on the radio)

Architecture change from the sketch above: speech runs in its own process,
`navi_voiced`, not inside `nv_navigation`. Reasons: the RHVoice engine and voice data are
large next to the 71 MB the plugin process already holds; an engine crash must not take the
navigation down; the plugin's dependency set is pinned by `tests/check_osmand_elf.sh`
(`libc`, `libm`, EGL, GLES, OsmAnd core) so it cannot take `libasound` or `libsocket`.
The plugin therefore only plans prompts and posts them to a POSIX message queue, which is in
`libc` and served by the `mq` manager that already carries all PAS traffic.

```
OsmAndEngine (plugin, render thread)
  GuidanceState -> PromptPlanner (voice_prompts)  -> VoiceClient --mq_send, non-blocking-->
                                                       /dev/mq/navicore-voice
navi_voiced (own process)
  VoiceServer (mq_timedreceive) -> VoiceWorker thread -> RhVoiceEngine (kept loaded)
                                        -> QnxPcmOut (io-audio, pcmC7D0p) + ProcessFocus
ProcessFocus -> as_alloc 2 8 1  (holds AllocateChannel while prompts are being spoken;
                                 stopped 8 s after the last prompt)
```

| File | Role | Tests |
| --- | --- | --- |
| `backends/osmand/voice_prompts.*` | what to say: stages at 1000/500/200/50 m, one prompt per stage, nearest stage only when joining late, reroute and arrival, Ukrainian plurals/ordinals, English; other languages stay silent | `voice_prompts_test` |
| `backends/osmand/voice_worker.*` | thread, bounded queue, newer manoeuvre prompt replaces older, Reroute/Arrival outrank and cut, cancel, stats | `voice_worker_test` (fakes) |
| `backends/osmand/voice_link.*` | message format, plugin `VoiceClient`, daemon `VoiceServer` | `voice_link_test` (real mq) |
| `backends/osmand/process_focus.*` | audio focus held by a helper process, shared between prompts, idle stop | `process_focus_test` |
| `backends/osmand/qnx_pcm_out.*` | io-audio output, device opened lazily at the synthesized rate | QNX compile only |
| `backends/osmand/rhvoice_engine.*` | RHVoice `TtsEngine`, engine loaded once | QNX compile only |
| `backends/osmand/navi_voiced.cpp` | daemon `main`, `--selftest TEXT` | QNX compile and link only |
| `osmand_engine.cpp`, `config.*` | plugin hook, on unless `$VP4R_HOME/flags/navicore.d/no-voice` or `OPEN_NAVICORE_OSMAND_VOICE=0` | compile (`-Werror`) only |

Verified offline: host tests under ASan/UBSan (ThreadSanitizer cannot map memory on this
WSL kernel); all new sources compile for QNX with the project's GCC 8.5 flags and `-Werror`;
`navi_voiced` links (NEEDED `libasound.so.2`, `libm.so.2`, `libc.so.3`, the same set
`tts_play` already runs with). Not verified: the CMake QNX build end to end (LVGL is not
checked out in this tree and an unrelated asset step fails on `-march=armv7-a`), any radio
behaviour, audibility.

### Why the daemon takes the bench focus route first

`as_alloc` sends the AllocateChannel request by hand (route C above). The stock route B
needs the V4 backend ABI and the unnamed provider slots. The assumption that makes route C
safe to try first, **not verified**: while Open NaviCore replaces NaviCore, nothing else
allocates the NAVI source, so there is one owner. Stock route B stays the target.

### First live session (needs the radio and someone listening)

```sh
# host: build the daemon (recipe in uk-voice/tts/test/BUILD_NAVI_VOICED.md) and deploy
bash uk-voice/tts/test/deploy_tts.sh          # kit, as_alloc, navi_voiced, alloc_say.sh
# radio:
export LD_LIBRARY_PATH=/pas/lib/dll:/pas/lib:/lib:/usr/lib:/proc/boot
/tmp/tts/navi_voiced --rhvoice-data /tmp/tts/rhvoice --voice Natalia --focus alloc \
    --alloc-cmd /tmp/tts/as_alloc --selftest "Через двісті метрів поверніть праворуч."
/tmp/tts/navi_voiced --rhvoice-data /tmp/tts/rhvoice --focus alloc &    # then enable-voice
```

Order of checks: (1) `--selftest --focus none` against a stock source that is already
playing; (2) `--selftest --focus alloc`; (3) the daemon plus `enable-voice` with the
route simulation marker. If (2) is silent try `--alloc-route 1 15 1 --device pcmC1D0p`.

### Still open

- Stock focus (route B): V4 ABI with `request_audio_source` / `release_audio_source`
  (slots 6 and 8) and the provider slots that report the result and a focus loss.
- Ducking of entertainment is whatever AllocateChannel does; not observed.
- Prompt wording review by a native speaker; stage distances per speed band.
- Packaging: done as the vp4r package `55-voice-uk.ifs` (repo `voice-uk-ifs`): `navi_voiced`,
  `as_alloc` and the RHVoice data under `/voice-uk`, started by `55-voice-uk.daemon`; off with
  `flags/voice-uk.d/disabled`, voice name in `flags/voice-uk.d/voice`.
