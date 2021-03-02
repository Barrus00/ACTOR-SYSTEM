cmake_minimum_required (VERSION 3.1)
project (CACTI)

enable_testing()

#set(CMAKE_C_STANDARD ...)
set(CMAKE_C_FLAGS "-g -Wall -Wextra -pthread")

# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
  # invoke built-in add_executable
  _add_executable(${ARGV})
  if (TARGET ${_name})
    target_link_libraries(${_name} cacti)
  endif()
endmacro()

add_library(cacti STATIC cacti.c generic_queue.c err.c)
add_executable(macierz macierz.c)
add_executable(silnia silnia.c)

add_subdirectory(test)

install(TARGETS cacti DESTINATION .)
