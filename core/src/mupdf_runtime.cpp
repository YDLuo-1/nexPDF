#include "mupdf_runtime.h"

#include <stdexcept>

namespace nexpdf::detail {

MuPdfRuntime::MuPdfRuntime(const std::size_t storeBytes)
{
    locks_.user = this;
    locks_.lock = &MuPdfRuntime::lock;
    locks_.unlock = &MuPdfRuntime::unlock;

    context_ = fz_new_context(nullptr, &locks_, storeBytes);
    if (context_ == nullptr) {
        throw std::runtime_error("MuPDF context allocation failed");
    }
    fz_register_document_handlers(context_);
}

MuPdfRuntime::~MuPdfRuntime()
{
    fz_drop_context(context_);
}

void MuPdfRuntime::lock(void *user, const int index)
{
    auto *runtime = static_cast<MuPdfRuntime *>(user);
    runtime->mutexes_.at(static_cast<std::size_t>(index)).lock();
}

void MuPdfRuntime::unlock(void *user, const int index)
{
    auto *runtime = static_cast<MuPdfRuntime *>(user);
    runtime->mutexes_.at(static_cast<std::size_t>(index)).unlock();
}

} // namespace nexpdf::detail
