#ifndef UNSIGNED_HASH_DIRECTORY_HPP
#define UNSIGNED_HASH_DIRECTORY_HPP

#include "unsigned_hash/unsigned_hash.hpp"

namespace unsigned_hash {
namespace directory {

HashResult hash(const std::string &dir_path, DirectoryHashOptions opts);
HashResult hash_set(const std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> &files, DirectoryHashOptions opts);

} // namespace directory
} // namespace unsigned_hash

#endif // UNSIGNED_HASH_DIRECTORY_HPP
