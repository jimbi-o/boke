set(BOKE_EXTERNAL_FILES
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.hpp"
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.cpp"
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
  gfx/d3d12_agility_sdk_version.cpp
  "${D3D12MemoryAllocator_SOURCE_DIR}/include/D3D12MemAlloc.h"
  "${D3D12MemoryAllocator_SOURCE_DIR}/src/D3D12MemAlloc.cpp"
)
if (BOKE_BUILD_TESTING)
  list(APPEND BOKE_EXTERNAL_FILES "${imgui_SOURCE_DIR}/imgui_demo.cpp")
endif()
source_group("External Files" FILES ${BOKE_EXTERNAL_FILES})

set(BOKE_SRC_FILES
  gfx/gfx.cpp
  gfx/core.cpp
  gfx/barrier_config.cpp
  gfx/descriptors.cpp
  gfx/resources.cpp
  gfx/descriptors_shader_visible.cpp
  gfx/imgui_util.cpp
)
source_group("Source Files (gfx)" FILES ${BOKE_SRC_FILES})

if (BOKE_BUILD_TESTING)
  add_library(${PROJECT_NAME} OBJECT)
else()
  add_library(${PROJECT_NAME})
endif()
target_sources(${PROJECT_NAME}
  PRIVATE
  ${BOKE_EXTERNAL_FILES}
  ${BOKE_SRC_FILES}
  allocator.cpp
  container.cpp
  util.cpp
  str_hash.cpp
  framework.cpp
  json.cpp
  string_util.cpp
)
