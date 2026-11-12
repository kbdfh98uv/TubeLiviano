#ifndef TL_PLAYER_H
#define TL_PLAYER_H
#include <windows.h>
#include <stddef.h>
typedef struct {
    int read_bytes; float input_bitrate;
    int demux_read_bytes; float demux_bitrate;
    int demux_corrupted, demux_discontinuity;
    int decoded_video, decoded_audio, displayed_pictures, lost_pictures;
    int played_abuffers, lost_abuffers, sent_packets, sent_bytes;
    float send_bitrate;
} PlayerStats; /* libvlc_media_stats_t, VLC 3 ABI */
typedef struct {
    int cache_ms, prefetch_kib, decoder_threads, volume;
    BOOL muted;
} PlayerOptions;
/* Decoder/cache changes take effect on the next open. Audio changes also apply
 * to the current player when its controller is idle. NULL restores defaults. */
void player_configure(const PlayerOptions *options);
BOOL player_init(const WCHAR *base, HWND video, BOOL test, WCHAR *err, size_t cap);
BOOL player_open(const char *video, const char *audio, BOOL local, WCHAR *err, size_t cap);
void player_stop(void);
void player_pause(void);
void player_close(void);
BOOL player_closed(void);
int player_state(void);
BOOL player_stats(PlayerStats *stats);
#endif
