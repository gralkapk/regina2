cmake_minimum_required(VERSION 3.4)

project(regina)

set(SOURCES src/regina.cpp)

add_library(regina SHARED ${SOURCES})

find_package(DynamoRIO)
if (NOT DynamoRIO_FOUND)
  message(FATAL_ERROR "DynamoRIOpackage required to build")
endif(NOT DynamoRIO_FOUND)

configure_DynamoRIO_client(regina)
use_DynamoRIO_extension(regina drmgr)
use_DynamoRIO_extension(regina drreg)