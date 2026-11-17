// Host-only IncFsFileMap backing for libandroidfw.
//
// libandroidfw uses android::incfs::IncFsFileMap as its generic
// memory-mapped-file wrapper.  On a non-IncFS system this is just a regular
// MAP_SHARED mapping; the verification helpers in map_ptr.h are no-ops.
// Keeping this in a small host stub lets libandroidfw drop the real libincfs
// dependency without changing any asset/resource behavior.

#include "util/map_ptr.h"

#include <sys/mman.h>

namespace android::incfs {

IncFsFileMap::IncFsFileMap() noexcept = default;
IncFsFileMap::IncFsFileMap(IncFsFileMap&&) noexcept = default;
IncFsFileMap& IncFsFileMap::operator=(IncFsFileMap&&) noexcept = default;
IncFsFileMap::~IncFsFileMap() noexcept = default;

const void* IncFsFileMap::unsafe_data() const {
    return map_ ? map_->data() : nullptr;
}

size_t IncFsFileMap::length() const {
    return map_ ? map_->size() : 0;
}

off64_t IncFsFileMap::offset() const {
    return offset_;
}

const char* IncFsFileMap::file_name() const {
    return filename_.c_str();
}

bool IncFsFileMap::Create(int fd, off64_t offset, size_t length, const char* file_name) {
    return Create(fd, offset, length, file_name, true /* verify */);
}

std::optional<base::MappedFile> IncFsFileMap::take_data() && {
    if (!map_) {
        return std::nullopt;
    }
    auto ptr = this->data();
    if (!ptr.verify<uint8_t>(map_->size())) {
        return std::nullopt;
    }
    return std::move(map_);
}

bool IncFsFileMap::Create(int fd, off64_t offset, size_t length, const char* file_name,
                          bool verify) {
    return CreateForceVerification(fd, offset, length, file_name, verify);
}

bool IncFsFileMap::CreateForceVerification(int fd, off64_t offset, size_t length,
                                           const char* file_name, bool /* verify */) {
    map_ = base::MappedFile::Create(fd, offset, length, PROT_READ);
    if (!map_) {
        return false;
    }

    filename_ = file_name ? file_name : "";
    offset_ = offset;
    return true;
}

bool IncFsFileMap::Verify(const uint8_t* const& /* data_start */,
                          const uint8_t* const& /* data_end */,
                          const uint8_t** /* prev_verified_block */) const {
    return true;
}

}  // namespace android::incfs
