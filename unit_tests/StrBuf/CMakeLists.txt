#cmake_minimum_required(VERSION 3.5)
project(StrBuf_test)

if (WIN32)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
endif (WIN32)

if (UNIX)
    SET(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -fPIC -m64 -fvisibility=hidden")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -m64 -fvisibility=hidden -std=c++0x")
endif (UNIX)

INCLUDE_DIRECTORIES(
    "src"
    "../../src/base"
)

set(SRC
    "../../src/base/StrBuf.hpp"
    "../../src/base/StrBuf.cpp"
    "src/Al_AffixAllocator.hpp"
    "src/Al_Blk.hpp"
    "src/Al_StackAllocator.hpp"
    "src/StrBuf_test.cpp"
)

#Visual Studio file filters
source_group(""    FILES ${SRC})

add_executable(StrBuf_test ${SRC})
#install(TARGETS StrBuf_test DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/inst/")
