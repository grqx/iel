cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(iel VERSION 0.0.0 LANGUAGES C)

if (MSVC OR WIN32)
    message(FATAL_ERROR "msvc/win32 is unsupported")
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wc++-compat -pedantic -fvisibility=hidden)

    add_compile_options(-g -fomit-frame-pointer)

    add_compile_options(-fsanitize=address,leak,undefined,alignment -fanalyzer)
    add_link_options(-fsanitize=address,leak,undefined,alignment)
else()
    message(FATAL_ERROR "${CMAKE_C_COMPILER_ID} is unsupported")
endif()

file(GLOB_RECURSE srcfiles CONFIGURE_DEPENDS src/*.c include/*.h)
add_library(iel STATIC ${srcfiles})
set_property(
    TARGET iel
    PROPERTY
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_compile_definitions(iel PRIVATE IEL_BUILDING=2)
target_include_directories(iel PUBLIC include/)

# various dev scripts to test stuff
file(GLOB_RECURSE dev_srcfiles CONFIGURE_DEPENDS dev/*.c)

foreach(dev_srcfile IN LISTS dev_srcfiles)
    get_filename_component(dev_name "${dev_srcfile}" NAME_WE)
    set(dev_tgt dev_exec_${dev_name})
    add_executable(${dev_tgt} ${dev_srcfile})
    set_property(
        TARGET ${dev_tgt}
        PROPERTY
        C_STANDARD 23
        C_STANDARD_REQUIRED OFF
        C_EXTENSIONS ON)
    # target_compile_definitions(${dev_tgt} PRIVATE IEL_USE_STABLE=2)
    target_link_libraries(${dev_tgt} PRIVATE iel)
endforeach()

