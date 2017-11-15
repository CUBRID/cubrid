project(test_string_buffer)

INCLUDE_DIRECTORIES(
    "src"
)

set(SRC
    ${BASE_DIR}/string_buffer.hpp
    ${BASE_DIR}/string_buffer.cpp
    src/allocator_affix.hpp
    src/allocator_stack.hpp
    src/test_string_buffer.cpp
)

add_executable(test_string_buffer ${SRC})
#install(TARGETS StrBuf_test DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/inst/")
