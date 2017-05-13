cmake_minimum_required(VERSION 2.8.7)

project(NCodeWeb)

option(NCODE_WEB_DISABLE_TESTS "If tests should be compiled or not" ON)
option(NCODE_WEB_DEBUG "A debug build" OFF)
option(NCODE_WEB_ASAN "Compile with ASAN on" OFF)
option(NCODE_WEB_TSAN "Compile with TSAN on" OFF)

set(NCODE_WEB_BASE_FLAGS "-g -std=c++11 -pedantic-errors -Winit-self -Woverloaded-virtual -Wuninitialized -Wall -Wextra -fno-exceptions")
set(NCODE_WEB_BASE_LD_FLAGS "")
if (NCODE_WEB_ASAN)
   set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls")
   set(NCODE_WEB_BASE_LD_FLAGS "${NCODE_WEB_BASE_LD_FLAGS} -fsanitize=address")
endif()
if (NCODE_WEB_TSAN)
   set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls")
   set(NCODE_WEB_BASE_LD_FLAGS "${NCODE_WEB_BASE_LD_FLAGS} -fsanitize=thread")
endif()

if (NCODE_WEB_DEBUG)
  set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -O0 -fno-omit-frame-pointer --coverage")
else()
  set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -O3 -march=native -DNDEBUG")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NCODE_WEB_BASE_FLAGS}")
set(CMAKE_C_FLAGS "-O3 -march=native -DNDEBUG")
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${NCODE_WEB_BASE_LD_FLAGS} --coverage")

if (NOT NCODE_WEB_DISABLE_TESTS)
   include(CTest)
   add_subdirectory(external/googletest)
   macro(add_test_exec name src_file deps)
     add_executable(${name} ${src_file})
     target_link_libraries(${name} gtest gmock_main ${deps} ${ARGN})
     add_test(NAME ${name} COMMAND ${name})
   endmacro(add_test_exec)
endif()

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_extensions)

add_subdirectory(external/ncode_common)
add_subdirectory(external/ncode_net)
include_directories(${CMAKE_SOURCE_DIR}/external)

# Creates C resources file from files in given directory
function(create_resources dir output prefix)
  # Create empty output file
  file(WRITE ${output} "")
  # Collect input files
  file(GLOB bins ${dir}/*)
  # Iterate through input files
  foreach(bin ${bins})
    # Get short filename
    string(REGEX MATCH "([^/]+)$" filename ${bin})
    # Replace filename spaces & extension separator for C compatibility
    string(REGEX REPLACE "\\.| |-" "_" filename ${filename})
    # Read hex data from file
    file(READ ${bin} filedata HEX)
    # Convert hex data for C compatibility
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
    # Append data to output file
    file(APPEND ${output} "const unsigned char ${prefix}_${filename}[] = {${filedata}};\nconst unsigned ${prefix}_${filename}_size = sizeof(${prefix}_${filename});\n")
  endforeach()
endfunction()

create_resources("${PROJECT_SOURCE_DIR}/data/www" "${PROJECT_BINARY_DIR}/www_resources.c" "www")
create_resources("${PROJECT_SOURCE_DIR}/data/grapher_templates" "${PROJECT_BINARY_DIR}/grapher_resources.c" "grapher")

# Code "stolen" from Google's ctemplates
add_library(ctemplate STATIC src/ctemplate/template.cc
            src/ctemplate/template_dictionary.cc
            src/ctemplate/template_modifiers.cc
            src/ctemplate/template_annotator.cc
            src/ctemplate/template_pathops.cc
            src/ctemplate/per_expand_data.cc
            src/ctemplate/template_string.cc
            src/ctemplate/template_cache.cc
            src/ctemplate/htmlparser/htmlparser.cc
            src/ctemplate/htmlparser/jsparser.cc
            src/ctemplate/htmlparser/statemachine.cc
            src/ctemplate/base/arena.cc)

# Silence warnings about unused stuff in ctemplates
set_target_properties(ctemplate PROPERTIES COMPILE_FLAGS
                      "-Wno-unused-parameter -Wno-unused-const-variable -Wno-sign-compare -Wno-unused-private-field")

set(WEB_HEADER_FILES src/web_page.h src/graph.h src/grapher.h src/server.h src/mongoose.h)
add_library(ncode_web STATIC src/web_page.cc src/graph.cc src/grapher.cc src/server.cc src/mongoose.c ${PROJECT_BINARY_DIR}/www_resources.c ${PROJECT_BINARY_DIR}/grapher_resources.c ${WEB_HEADER_FILES})
target_link_libraries(ncode_web ncode_common ncode_net ctemplate)

if (NOT NCODE_WEB_DISABLE_TESTS)
  add_test_exec(web_page_test src/web_page_test.cc ncode_web)
  add_test_exec(graph_test src/graph_test.cc ncode_web)
  add_test_exec(grapher_test src/grapher_test.cc ncode_web)
  add_test_exec(server_test src/server_test.cc ncode_web)
endif()
