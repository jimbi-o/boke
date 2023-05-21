add_executable(test
  test_main.cpp
  test_example.cpp
)
target_link_libraries(test PRIVATE boke)
target_compile_definitions(test PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
AddD3d12AgilitySDK(test)
