cmake_minimum_required(VERSION 3.14)
project(pass_manager VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(pass_manager
    src/main.cpp
    src/crypto.cpp
    src/database.cpp
    src/cli.cpp
)

target_include_directories(pass_manager PRIVATE src)

target_link_libraries(pass_manager PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
)

target_compile_options(pass_manager PRIVATE
    -Wall -Wextra -Wpedantic
)
