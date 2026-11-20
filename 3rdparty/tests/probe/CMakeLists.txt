cmake_minimum_required(VERSION 3.21)
project(CubridThirdPartyProbe LANGUAGES C CXX)
find_package(CubridThirdParty CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Curses REQUIRED)
add_executable(probe main.cpp)
target_compile_features(probe PRIVATE cxx_std_17)
target_link_libraries(probe PRIVATE
  CubridThirdParty::expat CubridThirdParty::edit CubridThirdParty::lz4
  CubridThirdParty::ssl CubridThirdParty::odbc CubridThirdParty::rapidjson
  CubridThirdParty::re2 CubridThirdParty::tbb Threads::Threads
  ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS})
enable_testing()
add_test(NAME thirdparty-link-and-run COMMAND probe)
