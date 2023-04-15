#pragma once
namespace boke {
struct AllocatorData;
using namespace foonathan::string_id::literals;
foonathan::string_id::string_id GetSid(const char* const, foonathan::string_id::default_database*);
foonathan::string_id::string_id GetSid(const char* const, const size_t, foonathan::string_id::default_database* database);
const char* GetStringFromHash(foonathan::string_id::hash_type, foonathan::string_id::default_database*);
foonathan::string_id::default_database* CreateStringHashDatabase(AllocatorData*);
}
