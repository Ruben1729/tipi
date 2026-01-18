cmake_minimum_required(VERSION 3.10)
project(tipi_protocol)

add_library(tipi STATIC tipi.c)

target_include_directories(tipi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_tipi tests/test_main.c)

target_link_libraries(test_tipi PRIVATE tipi)

set_target_properties(tipi PROPERTIES C_STANDARD 99)
set_target_properties(test_tipi PROPERTIES C_STANDARD 99)

enable_testing()
add_test(NAME TipiUnitTests COMMAND test_tipi)