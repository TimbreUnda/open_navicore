#pragma once

// Link between the navigation plugin and the voice daemon (navi_voiced).
// Speech runs in its own process: the TTS engine is large, loading it in
// nv_navigation would grow that process by tens of MB and tie its stability to
// the engine, and the daemon owns the audio focus helper. The plugin sends one
// small message per prompt through a POSIX message queue and never blocks or waits.
// Message queues are in libc on QNX (served by the `mq` manager that already
// carries all PAS traffic), so the plugin's reviewed dependency set stays exact;
// AF_UNIX sockets would need libsocket. The queue is /dev/mq/<name> on the radio.
//
// Message (UTF-8, tab separated, at most kMaxDatagram bytes; also the mq priority):
//   NVP1 <TAB> prompt <TAB> <priority 1..3> <TAB> <language code> <TAB> <text>
//   NVP1 <TAB> cancel
// Language codes are those of ui::loc::language_code(): "uk", "en", ...

#include "voice_worker.h"

#include <atomic>
#include <string>

namespace navicore_osmand {

constexpr std::size_t kMaxDatagram = 512;   // queue message size
constexpr long kQueueDepth = 8;              // queued messages; a full queue drops new ones
constexpr const char* kDefaultQueue = "/navicore-voice";

std::string encode_prompt(const SpokenPrompt& prompt, ui::loc::Language language);
std::string encode_cancel();

enum class Message { Invalid, Prompt, Cancel };
// Parses one datagram; `prompt` and `language` are set for Message::Prompt.
Message decode_message(const char* data, std::size_t size, SpokenPrompt& prompt,
                       ui::loc::Language& language);

// Plugin side. Fire and forget: a missing daemon or a full socket drops the
// prompt (old directions are worthless a second later).
class VoiceClient {
public:
    explicit VoiceClient(std::string queue_name = kDefaultQueue) : name_(std::move(queue_name)) {}
    ~VoiceClient();
    VoiceClient(const VoiceClient&) = delete;
    VoiceClient& operator=(const VoiceClient&) = delete;

    bool send(const SpokenPrompt& prompt, ui::loc::Language language);  // mq priority = prompt priority
    bool cancel();
    unsigned sent() const { return sent_; }
    unsigned dropped() const { return dropped_; }

private:
    bool send_raw(const std::string& message, unsigned priority);
    std::string name_;
    int fd_ = -1;   // mqd_t
    unsigned sent_ = 0;
    unsigned dropped_ = 0;
};

// Daemon side: receives messages and feeds the worker. run() returns after stop().
class VoiceServer {
public:
    VoiceServer(std::string queue_name, VoiceWorker& worker)
        : name_(std::move(queue_name)), worker_(worker) {}
    ~VoiceServer();
    VoiceServer(const VoiceServer&) = delete;
    VoiceServer& operator=(const VoiceServer&) = delete;

    bool bind();              // creates the queue (replacing a stale one)
    void run();               // blocking receive loop (wakes every 200 ms to check stop())
    void stop();              // thread-safe
    unsigned received() const { return received_; }
    unsigned invalid() const { return invalid_; }

private:
    std::string name_;
    VoiceWorker& worker_;
    int fd_ = -1;   // mqd_t
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> received_{0};
    std::atomic<unsigned> invalid_{0};
};

} // namespace navicore_osmand
