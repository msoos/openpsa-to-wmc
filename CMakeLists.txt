cmake_minimum_required(VERSION 3.16)
project(openpsa-to-wmc CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GMPXX REQUIRED IMPORTED_TARGET gmpxx)

add_executable(openpsa-to-wmc src/main.cpp src/mef.cpp src/cnf.cpp)
target_compile_options(openpsa-to-wmc PRIVATE -Wall -Wextra -Wshadow)
target_link_libraries(openpsa-to-wmc PRIVATE PkgConfig::GMPXX)
install(TARGETS openpsa-to-wmc RUNTIME DESTINATION bin)
