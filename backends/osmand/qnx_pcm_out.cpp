#include "qnx_pcm_out.h"

#include <cstdio>
#include <cstring>

#ifdef __QNX__
#include <sys/asoundlib.h>
#endif

namespace navicore_osmand {

bool QnxPcmOut::acquire() {
    if (focused_) return true;
    focused_ = focus_.request();
    return focused_;
}

void QnxPcmOut::release() {
    close_device();
    if (focused_) {
        focus_.release();
        focused_ = false;
    }
}

#ifdef __QNX__

bool QnxPcmOut::open_device(unsigned sample_rate_hz) {
    snd_pcm_t* h = nullptr;
    int rc = snd_pcm_open_name(&h, device_.c_str(), SND_PCM_OPEN_PLAYBACK);
    if (rc < 0) {
        std::fprintf(stderr, "voice: snd_pcm_open_name(%s): %s\n", device_.c_str(), snd_strerror(rc));
        return false;
    }
    snd_pcm_channel_info_t info;
    std::memset(&info, 0, sizeof info);
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    snd_pcm_channel_params_t params;
    std::memset(&params, 0, sizeof params);
    params.channel = SND_PCM_CHANNEL_PLAYBACK;
    params.mode = SND_PCM_MODE_BLOCK;
    params.start_mode = SND_PCM_START_FULL;
    params.stop_mode = SND_PCM_STOP_STOP;
    params.buf.block.frag_size = 0;
    if (snd_pcm_plugin_info(h, &info) == 0) params.buf.block.frag_size = info.max_fragment_size;
    params.buf.block.frags_min = 1;
    params.buf.block.frags_max = -1;
    params.format.interleave = 1;
    params.format.rate = static_cast<int>(sample_rate_hz);
    params.format.voices = 1;
    params.format.format = SND_PCM_SFMT_S16_LE;
    if ((rc = snd_pcm_plugin_params(h, &params)) < 0 ||
        (rc = snd_pcm_plugin_prepare(h, SND_PCM_CHANNEL_PLAYBACK)) < 0) {
        std::fprintf(stderr, "voice: pcm setup: %s\n", snd_strerror(rc));
        snd_pcm_close(h);
        return false;
    }
    snd_pcm_channel_setup_t setup;
    std::memset(&setup, 0, sizeof setup);
    setup.channel = SND_PCM_CHANNEL_PLAYBACK;
    snd_pcm_plugin_setup(h, &setup);
    fragment_ = setup.buf.block.frag_size > 0 ? static_cast<unsigned>(setup.buf.block.frag_size)
                                              : 4096u;
    handle_ = h;
    rate_ = sample_rate_hz;
    return true;
}

void QnxPcmOut::close_device() {
    if (!handle_) return;
    snd_pcm_t* h = static_cast<snd_pcm_t*>(handle_);
    if (focus_.valid()) snd_pcm_plugin_playback_drain(h);
    else snd_pcm_plugin_flush(h, SND_PCM_CHANNEL_PLAYBACK);
    snd_pcm_close(h);
    handle_ = nullptr;
    rate_ = 0;
}

bool QnxPcmOut::write(const short* samples, std::size_t count, unsigned sample_rate_hz) {
    if (!focused_ || !focus_.valid()) return false;
    if (handle_ && rate_ != sample_rate_hz) close_device();
    if (!handle_ && !open_device(sample_rate_hz)) return false;
    snd_pcm_t* h = static_cast<snd_pcm_t*>(handle_);
    const char* p = reinterpret_cast<const char*>(samples);
    std::size_t left = count * sizeof(short);
    unsigned stalled = 0;
    while (left > 0) {
        if (!focus_.valid()) return false;
        std::size_t n = left < fragment_ ? left : fragment_;
        ssize_t w = snd_pcm_plugin_write(h, p, n);
        if (w < static_cast<ssize_t>(n)) {
            snd_pcm_channel_status_t status;
            std::memset(&status, 0, sizeof status);
            status.channel = SND_PCM_CHANNEL_PLAYBACK;
            snd_pcm_plugin_status(h, &status);
            if (status.status == SND_PCM_STATUS_READY || status.status == SND_PCM_STATUS_UNDERRUN) {
                snd_pcm_plugin_prepare(h, SND_PCM_CHANNEL_PLAYBACK);
            } else if (w <= 0) {
                std::fprintf(stderr, "voice: pcm write failed, status=%d\n", status.status);
                return false;
            }
            if (w < 0) w = 0;
        }
        if (w <= 0 && ++stalled >= 3) {
            std::fprintf(stderr, "voice: pcm made no progress\n");
            return false;
        }
        if (w > 0) stalled = 0;
        p += w;
        left -= static_cast<std::size_t>(w);
    }
    return true;
}

#else  // host build: no io-audio; the type exists so the plugin code compiles.

bool QnxPcmOut::open_device(unsigned) { return false; }
void QnxPcmOut::close_device() { handle_ = nullptr; rate_ = 0; }
bool QnxPcmOut::write(const short*, std::size_t, unsigned) { return false; }

#endif

} // namespace navicore_osmand
