cmake_minimum_required(VERSION 3.14)
project(edit-mcp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(edit-mcp src/main.cpp)
install(TARGETS edit-mcp RUNTIME DESTINATION bin)
