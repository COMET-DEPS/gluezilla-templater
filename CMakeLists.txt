cmake_minimum_required(VERSION 3.12.0)
project(gluezilla-templater)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_GENERATOR_PLATFORM x64)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

find_package(SQLite3)
include_directories(${SQLite3_INCLUDE_DIRS})

add_custom_target(version ALL DEPENDS version_output)
add_custom_command(
    OUTPUT version_output
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
    DEPENDS always_rebuild
)
add_custom_command(OUTPUT always_rebuild COMMAND cmake -E echo)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOURCES src/bit_flipper.cpp src/phys_page_finder.cpp src/info.cpp src/config.cpp src/dram_address.cpp src/dram_layout.cpp src/dmidecode_parser.cpp src/hammer_pattern.cpp src/temperature_controller.cpp)
add_compile_options(-Wall -Wextra -pedantic -march=native)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-frelaxed-template-template-args)
endif()

file(GLOB FLIP_FINDERS src/*flip_finder.cpp)
add_executable(tester ${SOURCES} ${FLIP_FINDERS} src/tester.cpp)
add_dependencies(tester version)
target_link_libraries(tester pthread)

add_executable(validator ${SOURCES} src/validator.cpp)
add_dependencies(validator version)

option(USE_DB "whether bit flips should be stored in a database file" ${SQLite3_FOUND})
option(MEASURE_HAMMER_DURATION "whether the hammer duration should be measured and logged to the console" ${USE_DB})
option(USE_ASMJIT "whether the AsmJit library needed for Blacksmith patterns should be used" OFF)
set(LOG_LEVEL "3" CACHE STRING "one of 0 (no logging), 1 (ERROR), 2 (WARN), 3 (INFO), 4 (DEBUG), 5 (TRACE)")
if (USE_DB)
    option(LOG_FLIPS "whether to log individual bit flips (useful if the database backend is not used; LOG_LEVEL must be >= 3)" OFF)
else()
    option(LOG_FLIPS "whether to log individual bit flips (useful if the database backend is not used; LOG_LEVEL must be >= 3)" ON)
endif()
option(LOG_COLOR "whether to color ERROR, WARN and DEBUG messages" ON)

if(USE_DB)
    target_include_directories(tester PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sql)
    target_sources(tester PRIVATE src/db.cpp)
    target_compile_definitions(tester PRIVATE USE_DB MEASURE_HAMMER_DURATION)
    target_link_libraries(tester sqlite3)
elseif(MEASURE_HAMMER_DURATION)
    target_compile_definitions(tester PRIVATE MEASURE_HAMMER_DURATION)
endif()

if(USE_ASMJIT)
    set(CMAKE_GENERATOR_PLATFORM)
    set(ASMJIT_STATIC TRUE)
    add_subdirectory(libs/asmjit)
    target_compile_definitions(tester PRIVATE USE_ASMJIT)
    target_compile_definitions(validator PRIVATE USE_ASMJIT)
    target_link_libraries(tester asmjit::asmjit)
    target_link_libraries(validator asmjit::asmjit)
endif()

target_compile_definitions(tester PRIVATE LOG_LEVEL=${LOG_LEVEL})
target_compile_definitions(validator PRIVATE LOG_LEVEL=${LOG_LEVEL})
if(LOG_FLIPS)
    target_compile_definitions(tester PRIVATE LOG_FLIPS)
    target_compile_definitions(validator PRIVATE LOG_FLIPS)
endif()
if(LOG_COLOR)
    target_compile_definitions(tester PRIVATE LOG_COLOR)
    target_compile_definitions(validator PRIVATE LOG_COLOR)
endif()

find_package(Doxygen COMPONENTS dot)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    add_custom_target(docs ALL
        COMMAND Doxygen::doxygen ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM)
    set_property(TARGET docs
        APPEND
        PROPERTY ADDITIONAL_CLEAN_FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/html)
endif()
