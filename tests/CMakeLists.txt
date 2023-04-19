add_executable(test
  test_main.cpp
  test_example.cpp
  test_str_hash.cpp
)
target_link_libraries(test PRIVATE boke)
