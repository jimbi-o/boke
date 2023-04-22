cmake_minimum_required(VERSION 3.15)
project(boke
  VERSION 1.0
  DESCRIPTION "A real-time renderer for personal experiments using d3d12."
  LANGUAGES CXX
)

option(BOKE_BUILD_TESTING "Build test" ON)

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
CPMAddPackage(
  NAME foonathan_string_id
  GITHUB_REPOSITORY "foonathan/string_id"
  GIT_TAG b5a21bb9e1f8cebc64a3daa2cba16ffce8d0a334
  OPTIONS "FOONATHAN_STRING_ID_MULTITHREADED OFF"
)
CPMAddPackage("gh:foonathan/debug_assert#7ea47091830eec9a9c6a338c8a29da70494692a5")
CPMAddPackage(
  NAME sebbbi_OffsetAllocator
  GITHUB_REPOSITORY "sebbbi/OffsetAllocator"
  GIT_TAG 3610a7377088b1e8c8f1525f458c96038a4e6fc0
  DOWNLOAD_ONLY YES
)
CPMAddPackage("gh:Tencent/rapidjson#949c771b03de448bdedea80c44a4a5f65284bfeb")
CPMAddPackage("gh:jimbi-o/tote#4780f32aaa425e3f2d9dd5927a4ef50463680255")

add_subdirectory(src)
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
  if (BOKE_BUILD_TESTING)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
    add_subdirectory(tests)
    set_directory_properties(PROPERTIES VS_STARTUP_PROJECT test)
    set_target_properties(test PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DOCTEST_CONFIG_DISABLE)
    add_subdirectory(apps)
    set_directory_properties(PROPERTIES VS_STARTUP_PROJECT app)
    set_target_properties(app PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  endif()
endif()

target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE spdlog)
set_target_properties(spdlog PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:spdlog,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog cglm foonathan_string_id tote debug_assert)

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
  "${foonathan_string_id_BINARY_DIR}"
  "${foonathan_string_id_SOURCE_DIR}"
  "${sebbbi_OffsetAllocator_SOURCE_DIR}"
  "${RapidJSON_SOURCE_DIR}/include"
  "${tote_SOURCE_DIR}/include"
  "${debug_assert_SOURCE_DIR}"
  "${spdlog_SOURCE_DIR}/include"
  "${doctest_SOURCE_DIR}"
)
target_precompile_headers(${PROJECT_NAME}
  PUBLIC
  <stdint.h>
  "${cglm_SOURCE_DIR}/include/cglm/call.h"
  "${spdlog_SOURCE_DIR}/include/spdlog/spdlog.h"
  "${foonathan_string_id_SOURCE_DIR}/database.hpp"
  "${foonathan_string_id_SOURCE_DIR}/string_id.hpp"
  "${RapidJSON_SOURCE_DIR}/include/rapidjson/document.h"
)
