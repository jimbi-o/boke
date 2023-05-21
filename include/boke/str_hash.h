#pragma once
namespace boke {
struct AllocatorData;
using StrHash = foonathan::string_id::hash_type;
using namespace foonathan::string_id::literals;
void InitStrHashSystem(AllocatorData*);
void TermStrHashSystem(AllocatorData*);
StrHash GetStrHash(const char* const str);
const char* GetStr(const StrHash);
constexpr StrHash kEmptyStr{};
}
