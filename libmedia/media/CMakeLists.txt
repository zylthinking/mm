project(media)
cmake_minimum_required(VERSION 2.8)

add_definitions(-std=gnu99)

file(GLOB_RECURSE HEADERS "*.h")
set(INCLUDE_DIRS "")
foreach(_headerFile ${HEADERS})
    get_filename_component(_dir ${_headerFile} PATH)
    list(APPEND INCLUDE_DIRS ${_dir})
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

include_directories(${INCLUDE_DIRS})
file(GLOB_RECURSE SRC_LIST "*.c")
add_library(${PROJECT_NAME} SHARED ${SRC_LIST})
