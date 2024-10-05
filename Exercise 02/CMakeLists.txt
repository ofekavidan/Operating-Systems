cmake_minimum_required(VERSION 3.22)
project(ex2)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(ex2
        uthreads.cpp
        uthreads.h
        Thread.cpp
        Thread.h)