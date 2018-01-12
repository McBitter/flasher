cmake_minimum_required (VERSION 2.6)
project (Flasher)

file(GLOB_RECURSE Flasher_SOURCE "*.c")

add_executable (Flasher ${Flasher_SOURCE})
