# GraceOS Audio

## Current Status

GraceOS currently supports multiple audio backends through VoiceBox:

- SB16: usable and currently the default practical backend.
- AC97: integrated and usable, but still under tuning for smoothness.
- ISA speaker: fallback only.
- HDA: not usable in the current implementation state.

## Backend Summary

| Backend | State | Notes |
|---|---|---|
| SB16 | Working (with minor jitter) | Best current option for real playback in QEMU (`-device sb16`). |
| AC97 | Working (experimental quality) | Available via `-device AC97` and `mplayer --switch ac97`; still prone to host/path-dependent jitter. |
| ISA speaker | Working (degraded) | Emergency fallback only, low quality. |
| Intel HDA | Broken / Disabled path | Transport work landed, but practical audio output path is not production-ready. |

## Runtime Model

VoiceBox selects an audio backend and streams PCM from decoded media.

- Decode path: MP3/WAV decode in VoiceBox server.
- Output path: `audio_out_write_raw` -> selected backend driver.
- Timing path: SB16 uses a dedicated background audio clock service (`audio_clock`) spawned through minit.

Backend preference behavior:

- Default preference is SB16.
- In AUTO mode, fallback order is SB16 -> AC97 -> HDA -> ISA.
- If ISA low-memory DMA cannot be allocated (<16MB page unavailable), SB16 is disabled for that boot and fallback backends are used.

## Why SB16 Is Preferred Right Now

- Stable bring-up in QEMU.
- Dedicated DMA page strategy keeps ISA DMA constraints manageable.
- Background clock pacing gives much smoother playback than earlier blocking timing loops.

## Known Issues

- SB16 still has slight jitter / artifacts on longer tracks.
- AC97 still has jitter risk on some host audio backends.
- Playback timing is close, but not mathematically perfect in all host setups.
- HDA path still needs major rework before it can be called stable.

## CLI Usage

Examples:

- `mplayer --switch sb16 --sample`
- `mplayer --switch ac97 --sample`
- `mplayer --switch sb16 <file>.mp3`
- `mplayer --switch isa --sample`

## Recommended QEMU Audio Device

Use SB16 as primary while AC97 is available for testing/fallback:

- `-device sb16`
- `-device AC97`

Typical dual-device QEMU setup:

- `-audiodev dsound,id=snd_sb16`
- `-audiodev dsound,id=snd_ac97`
- `-device sb16,audiodev=snd_sb16`
- `-device AC97,audiodev=snd_ac97`

## Next Steps

1. Add IRQ-driven SB16 completion path (reduce chunk gaps further).
2. Move toward non-blocking playback worker for full shell responsiveness.
3. Revisit HDA only after a clean stream lifecycle redesign.
