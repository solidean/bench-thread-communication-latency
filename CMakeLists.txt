cmake_minimum_required(VERSION 3.25)
project(bench-thread-communication-latency CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Put all build products into predictable folders inside the build dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Threads REQUIRED)

# vcpkg-provided, header-only
find_package(readerwriterqueue CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS lockfree)

add_executable(bench-thread-communication-latency src/main.cc)
target_include_directories(bench-thread-communication-latency PRIVATE src)
target_link_libraries(bench-thread-communication-latency PRIVATE
    Threads::Threads
    readerwriterqueue::readerwriterqueue
    concurrentqueue::concurrentqueue
    Boost::lockfree
)
