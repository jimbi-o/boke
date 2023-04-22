#pragma once
namespace boke {
rapidjson::Document GetJson(const char* const json_path, boke::AllocatorData* allocator_data);
}
