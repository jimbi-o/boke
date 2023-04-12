cmake_minimum_required(VERSION 3.15)
project(boke
  VERSION 1.0
  DESCRIPTION "A real-time renderer for personal experiments using d3d12."
  LANGUAGES CXX
)

# Define a function to download and include CPM
function(download_cpm)
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
    message(STATUS "Downloading CPM.cmake...")
    file(DOWNLOAD "https://github.com/TheLartians/CPM.cmake/releases/latest/download/cpm.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
  endif()
  include("${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
endfunction()

download_cpm()

CPMAddPackage("gh:onqtam/doctest#v2.4.11")
CPMAddPackage("gh:gabime/spdlog@1.11.0")
CPMAddPackage("gh:recp/cglm#v0.8.9")
CPMAddPackage("gh:foonathan/string_id#b5a21bb9e1f8cebc64a3daa2cba16ffce8d0a334")
CPMAddPackage("gh:foonathan/debug_assert#v1.3.3")
CPMAddPackage(
  NAME sebbbi_OffsetAllocator
  GITHUB_REPOSITORY "sebbbi/OffsetAllocator"
  GIT_TAG 3610a7377088b1e8c8f1525f458c96038a4e6fc0
  DOWNLOAD_ONLY YES
)
CPMAddPackage("gh:Tencent/rapidjson@1.1.0")
CPMAddPackage("gh:jimbi-o/tote#7af765e708e3e575f221ed4b62614af0e022524a")

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_executable(${CMAKE_PROJECT_NAME})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${doctest_SOURCE_DIR}")
  add_subdirectory(tests)
  if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT {$CMAKE_PROJECT_NAME})
  endif()
else()
  add_library(${PROJECT_NAME})
endif()

target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE spdlog)
set_target_properties(spdlog PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:spdlog,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog cglm foonathan_string_id tote)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:Clang>:-Weverything -Wno-c++98-c++11-c++14-compat -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++20-compat>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /MP>
)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)

target_include_directories(${PROJECT_NAME}
  PUBLIC
  "include"
  PRIVATE
  "${string_id_BINARY_DIR}"
  "${sebbbi_OffsetAllocator_SOURCE_DIR}"
  "${RapidJSON_SOURCE_DIR}"
  "${tote_SOURCE_DIR}"
)
target_precompile_headers(${PROJECT_NAME}
  PRIVATE
  "${cglm_SOURCE_DIR}/include/cglm/call.h"
  "${spdlog_SOURCE_DIR}/include/spdlog/spdlog.h"
  "${string_id_SOURCE_DIR}/database.hpp"
  "${string_id_SOURCE_DIR}/string_id.hpp"
  "${RapidJSON_SOURCE_DIR}/include/rapidjson/document.h"
)
add_subdirectory(src)
