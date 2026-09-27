cmake_minimum_required(VERSION 3.16)
project(mecs_tests C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
enable_testing()
add_library(mecs_portable
    ../components/mecs_core/mecs_protocol.c
    ../components/mecs_core/mecs_io.c
    ../components/mecs_core/mecs_io_wire.c
    ../components/mecs_client/mecs_client.c
    ../components/mecs_client/MECS.cpp)
target_include_directories(mecs_portable PUBLIC
    ../components/mecs_core/include
    ../components/mecs_client/include)
target_compile_options(mecs_portable PRIVATE -Wall -Wextra -Werror -pedantic)
foreach(test client recovery)
  if(test STREQUAL "client")
    set(source test_mecs_client.c)
  else()
    set(source test_recovery.cpp)
  endif()
  add_executable(test_${test} ${source})
  target_link_libraries(test_${test} PRIVATE mecs_portable)
  target_compile_options(test_${test} PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  add_test(NAME ${test} COMMAND test_${test})
endforeach()
