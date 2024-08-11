cmake_minimum_required(VERSION 3.12)

project(ac LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer"
)
set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE INTERNAL "")

add_executable(${PROJECT_NAME} ac.cc ac_test.cc)
target_link_libraries(${PROJECT_NAME} asan)
