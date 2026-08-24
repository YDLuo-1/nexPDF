#pragma once

#include <mupdf/fitz.h>

#include <array>
#include <cstddef>
#include <mutex>

namespace nexpdf::detail {

class MuPdfRuntime final {
public:
    explicit MuPdfRuntime(std::size_t storeBytes);
    ~MuPdfRuntime();

    MuPdfRuntime(const MuPdfRuntime &) = delete;
    MuPdfRuntime &operator=(const MuPdfRuntime &) = delete;

    [[nodiscard]] fz_context *context() const noexcept { return context_; }

private:
    static void lock(void *user, int index);
    static void unlock(void *user, int index);

    std::array<std::mutex, FZ_LOCK_MAX> mutexes_;
    fz_locks_context locks_{};
    fz_context *context_ = nullptr;
};

} // namespace nexpdf::detail
