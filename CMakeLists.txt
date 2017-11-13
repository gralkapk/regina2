cmake_minimum_required(VERSION 3.4)

# Project.
project(regina)

# HACK: Test for our one and only supported generator.
if(NOT ${CMAKE_GENERATOR} STREQUAL "Visual Studio 12 2013 Win64")
	message(FATAL_ERROR "Use the right generator (see README.md)!")
endif()

# Update DynamoRIO submodule.
set(DYNAMORIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dynamorio")
execute_process(
	COMMAND git submodule update --init ${DYNAMORIO_DIR}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Add DynamoRIO.
include(ExternalProject)
ExternalProject_Add(dynamorio-module
	PREFIX dr
	SOURCE_DIR ${DYNAMORIO_DIR}
	CMAKE_ARGS
		-Wno-dev
		-DCMAKE_CXX_STANDARD=98
		-DDISABLE_WARNINGS=ON
		-DDEBUG=OFF
		-DBUILD_DOCS=OFF
		-DBUILD_DRSTATS=OFF
		-DBUILD_SAMPLES=OFF
		-DBUILD_TESTS=OFF)

# Try to find DynamoRIO, if it has been build.
list(APPEND CMAKE_PREFIX_PATH "${DYNAMORIO_DIR}/exports/cmake")
find_package(DynamoRIO QUIET)

# Test for DynamoRIO build.
if(NOT DynamoRIO_FOUND)
	# Add a fake target to build DynamoRIO.
	add_custom_target(regina ALL 
		${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR}
		COMMENT "Bootstrap build. Issue another build if this one has succeeded."
		DEPENDS dynamorio-module)
else()
	# Add our real target.
	file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
		"src/*.cpp" "src/*.h")
	add_library(regina SHARED ${SOURCES})
	add_dependencies(regina dynamorio-module)
	configure_DynamoRIO_client(regina)
	use_DynamoRIO_extension(regina drmgr)
	use_DynamoRIO_extension(regina drreg)
	use_DynamoRIO_extension(regina drutil)
	use_DynamoRIO_extension(regina drsyms)
endif()

# Add test targets.
add_executable(test_dijkstra EXCLUDE_FROM_ALL test/dijkstra.cpp)
add_executable(test_matrix EXCLUDE_FROM_ALL test/matrix.cpp)
add_executable(test_sorting EXCLUDE_FROM_ALL test/sorting.cpp)
