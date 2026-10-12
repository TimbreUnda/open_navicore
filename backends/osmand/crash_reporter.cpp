#include "crash_reporter.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

namespace navicore_osmand {
namespace {
int crash_fd = -1;
// Shared libraries are mapped in this window on the radio.
constexpr std::uintptr_t library_low = 0x70000000u;
constexpr std::uintptr_t library_high = 0x80000000u;
constexpr int stack_words = 256;

void put(const char* text) noexcept
{
    if (crash_fd >= 0)
        (void)::write(crash_fd, text, std::strlen(text));
}

void put_hex(std::uintptr_t value) noexcept
{
    char text[11] = {'0', 'x'};
    for (int i = 0; i < 8; ++i) {
        const unsigned nibble = (value >> ((7 - i) * 4)) & 0xfu;
        text[2 + i] = static_cast<char>(nibble < 10 ? '0' + nibble
                                                    : 'a' + nibble - 10);
    }
    text[10] = '\0';
    put(text);
}

void handler(int signal, siginfo_t* info, void* context) noexcept
{
    put("crash signal=");
    put_hex(static_cast<std::uintptr_t>(signal));
    put(" tid=");
    put_hex(static_cast<std::uintptr_t>(pthread_self()));
    put(" addr=");
    put_hex(reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr));
#if defined(__arm__) && defined(__QNX__)
    const auto* uc = static_cast<const ucontext_t*>(context);
    if (uc) {
        const auto& gpr = uc->uc_mcontext.cpu.gpr;
        put(" pc=");
        put_hex(gpr[ARM_REG_PC]);
        put(" lr=");
        put_hex(gpr[ARM_REG_LR]);
        put(" sp=");
        put_hex(gpr[ARM_REG_SP]);
        put("\nstack");
        const auto* stack =
            reinterpret_cast<const std::uint32_t*>(gpr[ARM_REG_SP]);
        for (int i = 0; stack && i < stack_words; ++i) {
            const std::uint32_t word = stack[i];
            if (word >= library_low && word < library_high) {
                put(" ");
                put_hex(word);
            }
        }
    }
#else
    (void)context;
#endif
    put("\n");
    // Die normally: restore the default action and return to re-fault.
    ::signal(signal, SIG_DFL);
}
} // namespace

void install_crash_reporter(const char* path) noexcept
{
    if (crash_fd < 0)
        crash_fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    struct sigaction action {};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    (void)::sigaction(SIGSEGV, &action, nullptr);
    (void)::sigaction(SIGBUS, &action, nullptr);
    (void)::sigaction(SIGILL, &action, nullptr);
}

} // namespace navicore_osmand
