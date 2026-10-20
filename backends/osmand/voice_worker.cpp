#include "voice_worker.h"

#include <algorithm>

namespace navicore_osmand {

bool VoiceWorker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return false;
    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void VoiceWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        stopping_ = true;
        cut_ = true;
        stats_.dropped += static_cast<unsigned>(queue_.size());
        queue_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    idle_.notify_all();
}

void VoiceWorker::set_language(ui::loc::Language language) {
    std::lock_guard<std::mutex> lock(mutex_);
    language_ = language;
}

void VoiceWorker::enqueue(SpokenPrompt prompt) {
    if (prompt.text.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_) return;
        if (prompt.priority == PromptPriority::Maneuver) {
            // Newer directions replace queued older ones.
            auto stale = std::remove_if(queue_.begin(), queue_.end(), [](const SpokenPrompt& q) {
                return q.priority == PromptPriority::Maneuver;
            });
            stats_.dropped += static_cast<unsigned>(queue_.end() - stale);
            queue_.erase(stale, queue_.end());
        }
        if (queue_.size() >= kCapacity) {
            // Evict the oldest entry of the lowest priority present.
            auto victim = std::min_element(queue_.begin(), queue_.end(),
                [](const SpokenPrompt& a, const SpokenPrompt& b) { return a.priority < b.priority; });
            if (victim->priority > prompt.priority) return;  // new one is the least important
            queue_.erase(victim);
            ++stats_.dropped;
        }
        // Higher priority first, arrival order within a priority.
        auto at = std::find_if(queue_.begin(), queue_.end(), [&](const SpokenPrompt& q) {
            return q.priority < prompt.priority;
        });
        const PromptPriority incoming = prompt.priority;
        queue_.insert(at, std::move(prompt));
        if (speaking_ && incoming > speaking_priority_) cut_ = true;
        // A newer Maneuver prompt also supersedes the one being spoken: the vehicle
        // has moved on, the old distance is already wrong.
        if (speaking_ && incoming == PromptPriority::Maneuver &&
            speaking_priority_ == PromptPriority::Maneuver)
            cut_ = true;
    }
    wake_.notify_one();
}

void VoiceWorker::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.dropped += static_cast<unsigned>(queue_.size());
    queue_.clear();
    if (speaking_) cut_ = true;
    if (!speaking_) idle_.notify_all();
}

void VoiceWorker::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !speaking_; });
}

VoiceStats VoiceWorker::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void VoiceWorker::run() {
    for (;;) {
        SpokenPrompt prompt;
        ui::loc::Language language;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            prompt = std::move(queue_.front());
            queue_.pop_front();
            language = language_;
            speaking_ = true;
            speaking_priority_ = prompt.priority;
            cut_ = false;
        }

        std::vector<short> pcm;
        unsigned rate = 0;
        bool ok = engine_.synthesize(prompt.text, language, pcm, rate);
        bool acquired = false;
        bool write_ok = true;
        bool completed = false;
        if (ok && !pcm.empty() && rate != 0) {
            // A newer prompt may have arrived while synthesizing.
            bool skip;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                skip = cut_;
            }
            if (!skip) {
                acquired = audio_.acquire();
                if (acquired) {
                    std::size_t done = 0;
                    completed = true;
                    while (done < pcm.size()) {
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (cut_ || stopping_) {
                                completed = false;
                                break;
                            }
                        }
                        std::size_t n = std::min(kChunkSamples, pcm.size() - done);
                        if (!audio_.write(pcm.data() + done, n, rate)) {
                            write_ok = false;
                            completed = false;
                            break;
                        }
                        done += n;
                    }
                    audio_.release();
                }
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        // One outcome per prompt.
        if (!ok || pcm.empty() || rate == 0) ++stats_.synth_failed;
        else if (cut_) ++stats_.dropped;
        else if (!acquired) ++stats_.acquire_failed;
        else if (!write_ok) ++stats_.write_failed;
        else if (completed) ++stats_.spoken;
        speaking_ = false;
        cut_ = false;
        if (queue_.empty()) idle_.notify_all();
    }
}

} // namespace navicore_osmand
