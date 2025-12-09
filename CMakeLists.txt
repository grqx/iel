cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
project(iel VERSION 0.0.0 LANGUAGES C)

if (MSVC OR WIN32)
    message(FATAL_ERROR "msvc/win32 is unsupported")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -pedantic)
endif()

file(GLOB_RECURSE srcfiles CONFIGURE_DEPENDS src/*.c include/*.h)
add_library(iel STATIC ${srcfiles})
target_include_directories(iel PUBLIC include/)

# various dev scripts to test stuff
file(GLOB_RECURSE dev_srcfiles CONFIGURE_DEPENDS dev/*.c)

foreach(dev_srcfile IN LISTS dev_srcfiles)
    get_filename_component(dev_name "${dev_srcfile}" NAME_WE)
    set(dev_tgt dev_exec_${dev_name})
    add_executable(${dev_tgt} ${dev_srcfile})
    target_link_libraries(${dev_tgt} PRIVATE iel)
endforeach()

