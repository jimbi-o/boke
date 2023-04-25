set(BOKE_EXTERNAL_FILES
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.hpp"
  "${sebbbi_OffsetAllocator_SOURCE_DIR}/offsetAllocator.cpp"
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
  d3d12_agility_sdk_version.cpp
)
source_group("External Files" FILES ${BOKE_EXTERNAL_FILES})
if (BOKE_BUILD_TESTING)
  add_library(${PROJECT_NAME} OBJECT)
else()
  add_library(${PROJECT_NAME})
endif()
target_sources(${PROJECT_NAME}
  PRIVATE
  ${BOKE_EXTERNAL_FILES}
  allocator.cpp
  container.cpp
  util.cpp
  str_hash.cpp
  framework.cpp
  json.cpp
  gfx.cpp
  string_util.cpp
)
