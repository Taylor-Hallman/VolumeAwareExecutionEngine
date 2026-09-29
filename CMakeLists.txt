cmake_minimum_required(VERSION 4.3)
project(VolumeAwareExecutionEngine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(VAEE_lib
        src/Parser.cpp
        src/serialization/Decoder.cpp
        src/serialization/Encoder.cpp
        src/NetworkHelper.cpp
)

add_executable(VolumeAwareExecutionEngine main.cpp)

target_link_libraries(VolumeAwareExecutionEngine PRIVATE VAEE_lib)

target_include_directories(VAEE_lib PUBLIC src)

add_subdirectory(clients/md_client)
add_subdirectory(clients/oe_client)
add_subdirectory(server)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_subdirectory(tests)

FetchContent_Declare(
        cli11
        URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.zip
)
FetchContent_MakeAvailable(cli11)
