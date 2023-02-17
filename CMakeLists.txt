cmake_minimum_required(VERSION 2.8.8)

project(rules CXX)
set(VERSION 0.1)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE MATCHES "(Release|Debug)")
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall") # debug, no optimisation
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall") # debug, no optimisation

if(COVERALLS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall") # debug, no optimisation
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall") # debug, no optimisation
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage") # enabling coverage
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage") # enabling coverage
endif()

string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)
add_definitions(-D${BUILD_TYPE})

if(NOT WIN32)
  string(ASCII 27 Esc)
  set(ColorReset "${Esc}[m")
  set(ColorBold  "${Esc}[1m")
  set(Red         "${Esc}[31m")
  set(Green       "${Esc}[32m")
  set(Yellow      "${Esc}[33m")
  set(Blue        "${Esc}[34m")
  set(Magenta     "${Esc}[35m")
  set(Cyan        "${Esc}[36m")
  set(White       "${Esc}[37m")
  set(BoldRed     "${Esc}[1;31m")
  set(BoldGreen   "${Esc}[1;32m")
  set(BoldYellow  "${Esc}[1;33m")
  set(BoldBlue    "${Esc}[1;34m")
  set(BoldMagenta "${Esc}[1;35m")
  set(BoldCyan    "${Esc}[1;36m")
  set(BoldWhite   "${Esc}[1;37m")
endif()

include_directories(${PROJECT_SOURCE_DIR})

add_subdirectory(src)

set(${PROJECT_NAME}_files ${src_files} CACHE INTERNAL "")

add_executable(start main.cpp ${${PROJECT_NAME}_files} )

