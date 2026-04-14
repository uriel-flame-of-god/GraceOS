/*
 * audio_syscalls.h — VoiceBoxSystem syscall numbers
 *
 * Syscall numbers 130–135 are reserved for the GraceOS VoiceBox audio subsystem.
 * Shared between kernel (kernel/audio/voicebox.h) and userland (lib/libgrace/audio.h).
 *
 * ABI: INT 0x80 / RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5
 */

#ifndef GRACE_AUDIO_SYSCALLS_H
#define GRACE_AUDIO_SYSCALLS_H

/* ============================
   VoiceBox Syscall Numbers
   ============================ */

#define SYS_AUDIO_OPEN   130  /* (sample_rate, channels, bits) → stream_handle / -1 */
#define SYS_AUDIO_WRITE  131  /* (handle, seg_handle, byte_count) → bytes_written / -1 */
#define SYS_AUDIO_START  132  /* (handle) → 0 / -1 */
#define SYS_AUDIO_STOP   133  /* (handle) → 0 / -1 */
#define SYS_AUDIO_CLOSE  134  /* (handle) → 0 / -1 */
#define SYS_AUDIO_CTL    135  /* (handle, cmd, value) → 0 / -1 */

/* ============================
   SYS_AUDIO_CTL commands
   ============================ */

#define AUDIO_CTL_SET_VOLUME  1  /* value = 0–100 */
#define AUDIO_CTL_MUTE        2  /* value = 1 (mute) / 0 (unmute) */
#define AUDIO_CTL_SET_RATE    3  /* value = sample rate in Hz */

/* ============================
   Audio open flags (channels)
   ============================ */

#define AUDIO_MONO   1
#define AUDIO_STEREO 2

/* ============================
   Common sample rates
   ============================ */

#define AUDIO_RATE_44100 44100
#define AUDIO_RATE_48000 48000
#define AUDIO_RATE_22050 22050

#endif /* GRACE_AUDIO_SYSCALLS_H */
