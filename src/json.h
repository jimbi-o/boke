#pragma once
namespace boke {
struct AllocatorData;
rapidjson::Document GetJson(const char* const json_path, boke::AllocatorData* allocator_data);
}
