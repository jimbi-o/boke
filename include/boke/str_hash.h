#pragma once
namespace boke {
using StrHash = foonathan::string_id::hash_type;
using namespace foonathan::string_id::literals;
void InitStrHashSystem();
void TermStrHashSystem();
StrHash GetStrHash(const char* const str);
const char* GetStr(const StrHash);
constexpr StrHash kEmptyStr{};
}
