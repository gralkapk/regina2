cmake_minimum_required(VERSION 3.4)

# Project.
project(regina)

find_package(DynamoRIO CONFIG REQUIRED)

# Add our real target.
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
	"src/*.cpp" "src/*.h")
add_library(regina SHARED ${SOURCES})
# add_dependencies(regina dynamorio-module)
configure_DynamoRIO_client(regina)
use_DynamoRIO_extension(regina drmgr)
use_DynamoRIO_extension(regina drreg)
use_DynamoRIO_extension(regina drutil)
use_DynamoRIO_extension(regina drsyms)
use_DynamoRIO_extension(regina drx)

# Add test targets.
add_executable(test_dijkstra EXCLUDE_FROM_ALL test/dijkstra.cpp)
add_executable(test_matrix EXCLUDE_FROM_ALL test/matrix.cpp)
add_executable(test_sorting EXCLUDE_FROM_ALL test/sorting.cpp)
add_subdirectory(test/pv)
