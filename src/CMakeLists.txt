if (BOKE_BUILD_TESTING)
  add_library(${PROJECT_NAME} OBJECT)
else()
  add_library(${PROJECT_NAME})
endif()
target_sources(${PROJECT_NAME}
  PRIVATE
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.hpp"
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.cpp"
  allocator.cpp
  container.cpp
  util.cpp
  str_hash.cpp
  framework.cpp
)
