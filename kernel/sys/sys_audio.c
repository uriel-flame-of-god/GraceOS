// ============================
// GraceOS Audio Syscall Bridge
// INT 0x80 syscalls 130-135
// ============================

#include "../audio/voicebox.h"
#include "../../include/grace/audio_syscalls.h"
#include "../mm/sasy/handle.h"

/* ============================
   Syscall Implementations
   These match the signature:
   long fn(long a1, long a2, long a3, long a4, long a5, long a6)
   ============================ */

/* SYS_AUDIO_OPEN (130)
   a1 = sample_rate, a2 = channels, a3 = bits_per_sample
   Returns stream handle (> 0) or -1 */
long sys_audio_open(long a1, long a2, long a3,
                    long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    uint32_t sample_rate = (uint32_t)a1;
    uint32_t channels    = (uint32_t)a2;
    uint32_t bits        = (uint32_t)a3;

    /* Validate */
    if (sample_rate == 0) sample_rate = 44100;
    if (channels    == 0) channels    = 2;
    if (bits        == 0) bits        = 16;
    if (channels > 8 || (bits != 8 && bits != 16 && bits != 24 && bits != 32))
        return -1;

    return (long)audio_out_open(sample_rate, channels, bits);
}

/* SYS_AUDIO_WRITE (131)
   a1 = stream handle, a2 = SASY seg_handle, a3 = byte_count
   Returns bytes written or -1 */
long sys_audio_write(long a1, long a2, long a3,
                     long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    int          handle     = (int)a1;
    seg_handle_t seg_handle = (seg_handle_t)a2;
    uint32_t     byte_count = (uint32_t)a3;

    if (byte_count == 0) return 0;
    return (long)audio_out_write(handle, seg_handle, byte_count);
}

/* SYS_AUDIO_START (132)
   a1 = stream handle
   Returns 0 or -1 */
long sys_audio_start(long a1, long a2, long a3,
                     long unused1, long unused2, long unused3)
{
    (void)a2; (void)a3;
    (void)unused1; (void)unused2; (void)unused3;
    return (long)audio_out_start((int)a1);
}

/* SYS_AUDIO_STOP (133)
   a1 = stream handle
   Returns 0 or -1 */
long sys_audio_stop(long a1, long a2, long a3,
                    long unused1, long unused2, long unused3)
{
    (void)a2; (void)a3;
    (void)unused1; (void)unused2; (void)unused3;
    return (long)audio_out_stop((int)a1);
}

/* SYS_AUDIO_CLOSE (134)
   a1 = stream handle
   Returns 0 or -1 */
long sys_audio_close(long a1, long a2, long a3,
                     long unused1, long unused2, long unused3)
{
    (void)a2; (void)a3;
    (void)unused1; (void)unused2; (void)unused3;
    return (long)audio_out_close((int)a1);
}

/* SYS_AUDIO_CTL (135)
   a1 = stream handle, a2 = cmd (AUDIO_CTL_*), a3 = value
   Returns 0 or -1 */
long sys_audio_ctl(long a1, long a2, long a3,
                   long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    return (long)audio_out_ctl((int)a1, (uint32_t)a2, (uint32_t)a3);
}

/* ============================
   Audio Syscall Init
   Called from syscall_init()
   ============================ */

void audio_syscalls_init(void)
{
    /* VoiceBox Core is already initialised by kmain before this is called */
}
