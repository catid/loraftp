cmake_minimum_required(VERSION 3.5)
project(loraftp)


################################################################################
# Config

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# In debug mode, add -DDEBUG
add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
else()
    # Warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-psabi")

    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -march=native -fstack-protector")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
endif()


################################################################################
# Dependencies

set(ZSTD_INCLUDE_FILES
    zstd_lib/zstd.h
)

file(GLOB ZSTD_SOURCE_FILES
    zstd_lib/common/*.c
    zstd_lib/common/*.h
    zstd_lib/compress/*.c
    zstd_lib/compress/*.h
    zstd_lib/decompress/*.c
    zstd_lib/decompress/*.h
    zstd_lib/dictBuilder/*.c
    zstd_lib/dictBuilder/*.h
)

# Zstd library
add_library(zstd ${ZSTD_SOURCE_FILES})
target_include_directories(zstd PUBLIC
    zstd_lib
)

add_subdirectory(wirehair)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


################################################################################
# Targets

enable_testing()


# Library: loraftp

add_library(loraftp STATIC
    include/loraftp.hpp
    include/waveshare.hpp
    include/linux_serial.hpp
    include/tools.hpp
    include/Counter.h
    src/loraftp.cpp
    src/waveshare.cpp
    src/linux_serial.cpp
    src/tools.cpp
)
target_include_directories(loraftp PUBLIC
    include
    .
)
target_link_libraries(loraftp
    PUBLIC
        pigpio
        wirehair
        zstd
        Threads::Threads
)

install(TARGETS loraftp DESTINATION lib)
install(FILES include/loraftp.hpp DESTINATION include)


# App: loraftp_send

add_executable(loraftp_send
    app/loraftp_send.cpp
)
target_link_libraries(loraftp_send
    PUBLIC
        loraftp
)

set_target_properties(loraftp_send PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS loraftp_send DESTINATION bin)


# App: loraftp_get

add_executable(loraftp_get
    app/loraftp_get.cpp
)
target_link_libraries(loraftp_get
    PUBLIC
        loraftp
)

set_target_properties(loraftp_get PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS loraftp_get DESTINATION bin)


# App: echo_test

add_executable(echo_test
    test/echo_test.cpp
)
target_link_libraries(echo_test
    PUBLIC
        loraftp
)

set_target_properties(echo_test PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS echo_test DESTINATION bin)
