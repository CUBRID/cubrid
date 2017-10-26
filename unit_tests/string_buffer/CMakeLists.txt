project(test_string_buffer)

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
    "../../src/base/string_buffer.hpp"
    "../../src/base/string_buffer.cpp"
    "src/allocator_affix.hpp"
    "src/allocator_blk.hpp"
    "src/allocator_stack.hpp"
    "src/test_string_buffer.cpp"
)

#Visual Studio file filters
source_group(""    FILES ${SRC})

add_executable(test_string_buffer ${SRC})
#install(TARGETS StrBuf_test DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/inst/")
