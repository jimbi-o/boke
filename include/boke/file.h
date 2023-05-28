#pragma once
namespace boke {
char* LoadFileToBuffer(const char* const filepath);
char* LoadFileToBuffer(const char* const filepath, uint32_t* bytes_read);
}
