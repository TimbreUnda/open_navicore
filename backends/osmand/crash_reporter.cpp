#include "crash_reporter.h"

#include <cxxabi.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

namespace navicore_osmand {
namespace {
int crash_fd = -1;
int persistent_fd = -1;
// Shared libraries are mapped in this window on the radio.
constexpr std::uintptr_t library_low = 0x70000000u;
constexpr std::uintptr_t library_high = 0x80000000u;
// procnto maps libc.so.3 here; abort() and raise() return addresses land in it.
constexpr std::uintptr_t libc_low = 0x01000000u;
constexpr std::uintptr_t libc_high = 0x01100000u;
constexpr int stack_words = 512;
// The persistent copy survives a reboot; start it over when it grows past this.
constexpr off_t persistent_limit_bytes = 256 * 1024;

void put(const char* text) noexcept
{
    const std::size_t length = std::strlen(text);
    if (crash_fd >= 0)
        (void)::write(crash_fd, text, length);
    if (persistent_fd >= 0)
        (void)::write(persistent_fd, text, length);
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

bool code_address(std::uint32_t word) noexcept
{
    return (word >= library_low && word < library_high) ||
           (word >= libc_low && word < libc_high);
}

void handler(int signal, siginfo_t* info, void* context) noexcept
{
    put("crash signal=");
    put_hex(static_cast<std::uintptr_t>(signal));
    put(" pid=");
    put_hex(static_cast<std::uintptr_t>(::getpid()));
    put(" tid=");
    put_hex(static_cast<std::uintptr_t>(pthread_self()));
    put(" addr=");
    put_hex(reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr));
    put(" time=");
    put_hex(static_cast<std::uintptr_t>(::time(nullptr)));
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
            if (code_address(word)) {
                put(" ");
                put_hex(word);
            }
        }
    }
#else
    (void)context;
#endif
    put("\n");
    // Die normally: restore the default action. A fault re-faults on return;
    // SIGABRT was raised on purpose, so raise it again (delivered on return).
    ::signal(signal, SIG_DFL);
    if (signal == SIGABRT)
        (void)::raise(SIGABRT);
}

// std::terminate (uncaught exception, joinable std::thread destroyed, ...)
// ends in abort(); name the exception first, the SIGABRT line follows.
[[noreturn]] void terminate_handler() noexcept
{
    put("terminate pid=");
    put_hex(static_cast<std::uintptr_t>(::getpid()));
    put(" tid=");
    put_hex(static_cast<std::uintptr_t>(pthread_self()));
    const std::type_info* type = abi::__cxa_current_exception_type();
    put(" exception=");
    put(type ? type->name() : "none");
    put("\n");
    std::abort();
}

int open_persistent(const char* path) noexcept
{
    int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    struct stat status {};
    if (fd >= 0 && ::fstat(fd, &status) == 0 &&
        status.st_size > persistent_limit_bytes)
        (void)::ftruncate(fd, 0);
    return fd;
}
} // namespace

void install_crash_reporter(const char* path,
                            const char* persistent_path) noexcept
{
    if (crash_fd < 0)
        crash_fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (persistent_fd < 0 && persistent_path)
        persistent_fd = open_persistent(persistent_path);
    struct sigaction action {};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    (void)::sigaction(SIGSEGV, &action, nullptr);
    (void)::sigaction(SIGBUS, &action, nullptr);
    (void)::sigaction(SIGILL, &action, nullptr);
    (void)::sigaction(SIGABRT, &action, nullptr);
    std::set_terminate(terminate_handler);
}

void crash_report_line(const char* text) noexcept
{
    char stamp[24];
    (void)std::snprintf(stamp, sizeof stamp, "%ld ",
                        static_cast<long>(::time(nullptr)));
    put(stamp);
    put(text);
    put("\n");
}

} // namespace navicore_osmand
