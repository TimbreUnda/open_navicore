#include "voice_link.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace navicore_osmand {

namespace {

const char kMagic[] = "NVP1";

// The radio runs the mq server (/dev/mq, used by PAS), not mqueue: libc's
// mq_* fail there with ENOSYS and libmq's reach mq. libmq is looked up at run
// time so the plugin's reviewed NEEDED set stays libc-only; without it (host,
// mqueue systems) libc's functions are used.
struct MqApi {
    mqd_t (*open)(const char*, int, ...) = ::mq_open;
    int (*close)(mqd_t) = ::mq_close;
    int (*unlink)(const char*) = ::mq_unlink;
    int (*send)(mqd_t, const char*, size_t, unsigned) = ::mq_send;
    ssize_t (*timedreceive)(mqd_t, char*, size_t, unsigned*, const struct timespec*) =
        ::mq_timedreceive;

    MqApi() {
        void* lib = dlopen("libmq.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return;
        void* o = dlsym(lib, "mq_open");
        void* c = dlsym(lib, "mq_close");
        void* u = dlsym(lib, "mq_unlink");
        void* s = dlsym(lib, "mq_send");
        void* r = dlsym(lib, "mq_timedreceive");
        if (!o || !c || !u || !s || !r) return;  // all from one library or none
        open = reinterpret_cast<decltype(open)>(o);
        close = reinterpret_cast<decltype(close)>(c);
        unlink = reinterpret_cast<decltype(unlink)>(u);
        send = reinterpret_cast<decltype(send)>(s);
        timedreceive = reinterpret_cast<decltype(timedreceive)>(r);
    }
};

const MqApi& mq() {
    static const MqApi api;
    return api;
}

std::string single_line(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return out;
}

} // namespace

std::string encode_prompt(const SpokenPrompt& prompt, ui::loc::Language language) {
    std::string text = single_line(prompt.text);
    std::string head = std::string(kMagic) + "\tprompt\t" +
                       std::to_string(static_cast<int>(prompt.priority)) + "\t" +
                       ui::loc::language_code(language) + "\t";
    if (head.size() + text.size() > kMaxDatagram) {
        // Cut on a UTF-8 boundary.
        std::size_t keep = kMaxDatagram - head.size();
        while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
        text.resize(keep);
    }
    return head + text;
}

std::string encode_cancel() { return std::string(kMagic) + "\tcancel"; }

Message decode_message(const char* data, std::size_t size, SpokenPrompt& prompt,
                       ui::loc::Language& language) {
    std::string s(data, size);
    std::size_t p0 = s.find('\t');
    if (p0 == std::string::npos || s.compare(0, p0, kMagic) != 0) return Message::Invalid;
    std::size_t p1 = s.find('\t', p0 + 1);
    std::string kind = s.substr(p0 + 1, p1 == std::string::npos ? std::string::npos : p1 - p0 - 1);
    if (kind == "cancel") return p1 == std::string::npos ? Message::Cancel : Message::Invalid;
    if (kind != "prompt" || p1 == std::string::npos) return Message::Invalid;
    std::size_t p2 = s.find('\t', p1 + 1);
    if (p2 == std::string::npos) return Message::Invalid;
    std::size_t p3 = s.find('\t', p2 + 1);
    if (p3 == std::string::npos) return Message::Invalid;
    std::string prio = s.substr(p1 + 1, p2 - p1 - 1);
    if (prio.size() != 1 || prio[0] < '1' || prio[0] > '3') return Message::Invalid;
    ui::loc::Language lang;
    if (!ui::loc::parse_language(s.substr(p2 + 1, p3 - p2 - 1), lang)) return Message::Invalid;
    std::string text = s.substr(p3 + 1);
    if (text.empty()) return Message::Invalid;
    prompt.text = text;
    prompt.priority = static_cast<PromptPriority>(prio[0] - '0');
    prompt.key.clear();
    language = lang;
    return Message::Prompt;
}

// ---- client ---------------------------------------------------------------

VoiceClient::~VoiceClient() {
    if (fd_ >= 0) mq().close(static_cast<mqd_t>(fd_));
}

bool VoiceClient::send_raw(const std::string& message, unsigned priority) {
    if (message.size() > kMaxDatagram) {
        ++dropped_;
        return false;
    }
    if (fd_ < 0) {
        mqd_t q = mq().open(name_.c_str(), O_WRONLY | O_NONBLOCK);
        if (q == static_cast<mqd_t>(-1)) {  // no daemon yet
            ++dropped_;
            return false;
        }
        fd_ = static_cast<int>(q);
    }
    if (mq().send(static_cast<mqd_t>(fd_), message.data(), message.size(), priority) == 0) {
        ++sent_;
        return true;
    }
    if (errno != EAGAIN) {  // the queue went away (daemon restarted): reopen next time
        mq().close(static_cast<mqd_t>(fd_));
        fd_ = -1;
    }
    ++dropped_;
    return false;
}

bool VoiceClient::send(const SpokenPrompt& prompt, ui::loc::Language language) {
    return send_raw(encode_prompt(prompt, language), static_cast<unsigned>(prompt.priority));
}

bool VoiceClient::cancel() { return send_raw(encode_cancel(), 31); }  // jumps the queue

// ---- server ---------------------------------------------------------------

VoiceServer::~VoiceServer() {
    if (fd_ >= 0) {
        mq().close(static_cast<mqd_t>(fd_));
        mq().unlink(name_.c_str());
    }
}

bool VoiceServer::bind() {
    mq().unlink(name_.c_str());  // a stale queue from an earlier run
    struct mq_attr attr;
    std::memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg = kQueueDepth;
    attr.mq_msgsize = static_cast<long>(kMaxDatagram);
    mode_t old = umask(0);  // the plugin may run under another user than the daemon
    mqd_t q = mq().open(name_.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    umask(old);
    if (q == static_cast<mqd_t>(-1)) {
        std::fprintf(stderr, "voice: mq_open %s: %s\n", name_.c_str(), std::strerror(errno));
        return false;
    }
    fd_ = static_cast<int>(q);
    return true;
}

void VoiceServer::stop() { stopping_ = true; }

void VoiceServer::run() {
    char buffer[kMaxDatagram + 1];
    while (!stopping_) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 200 * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            ++deadline.tv_sec;
        }
        unsigned priority = 0;
        ssize_t n = mq().timedreceive(static_cast<mqd_t>(fd_), buffer, sizeof buffer - 1, &priority,
                                    &deadline);
        if (n < 0) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            break;
        }
        ++received_;
        SpokenPrompt prompt;
        ui::loc::Language language = ui::loc::Language::Ukrainian;
        switch (decode_message(buffer, static_cast<std::size_t>(n), prompt, language)) {
        case Message::Prompt:
            worker_.set_language(language);
            worker_.enqueue(std::move(prompt));
            break;
        case Message::Cancel:
            worker_.cancel_all();
            break;
        case Message::Invalid:
            ++invalid_;
            break;
        }
    }
}

} // namespace navicore_osmand
