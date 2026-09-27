cmake_minimum_required(VERSION 3.16)
project(mio_client_tests C)

enable_testing()
add_executable(test_client
    test_client.c
    ../components/mio_core/mio.c
    ../components/mio_core/mio_io.c
    ../components/mio_core/mio_io_wire.c
    ../components/mio_core/mio_command.c
    ../components/mio_client/mio_client.c
)
target_include_directories(test_client PRIVATE
    ../components/mio_core/include
    ../components/mio_client/include
)
target_compile_options(test_client PRIVATE -Wall -Wextra -Werror -pedantic)
add_test(NAME client_lifecycle COMMAND test_client)
