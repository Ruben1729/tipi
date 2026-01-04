cmake_minimum_required(VERSION 4.0)
project(tipi C)

set(CMAKE_C_STANDARD 99)

add_library(tipi STATIC tipi.c)
target_include_directories(tipi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()
add_executable(tipi_tests tests/test_main.c)
target_link_libraries(tipi_tests PRIVATE tipi)
add_test(NAME UnitTests COMMAND tipi_tests)
