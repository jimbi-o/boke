#include "boke/str_hash.h"
#include "boke/allocator.h"
#include "boke/util.h"
namespace boke {
foonathan::string_id::default_database* CreateStringHashDatabase(AllocatorData* allocator_data) {
  auto ptr = Allocate(GetUint32(sizeof(foonathan::string_id::default_database)), alignof(foonathan::string_id::default_database), allocator_data);
  return new (ptr) foonathan::string_id::default_database();
}
foonathan::string_id::string_id GetSid(const char* const str, foonathan::string_id::default_database* database) {
  return foonathan::string_id::string_id(foonathan::string_id::string_info(str), *database);
}
foonathan::string_id::string_id GetSid(const char* const str, const size_t str_len, foonathan::string_id::default_database* database) {
  return foonathan::string_id::string_id(foonathan::string_id::string_info(str, str_len), *database);
}
const char* GetStringFromHash(foonathan::string_id::hash_type hash, foonathan::string_id::default_database* database) {
  return database->lookup(hash);
}
} // namespace boke
