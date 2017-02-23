cmake_minimum_required(VERSION 2.8.7)

project(NCodeWeb)

option(NCODE_WEB_DISABLE_TESTS "If tests should be compiled or not" ON)
option(NCODE_WEB_DISABLE_BENCHMARKS "If benchmarks should be compiled or not" ON)
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

if(APPLE)
elseif(UNIX)
  set(NOCDE_NET_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -pthread")
endif()

if (NCODE_DEBUG)
  set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -O0 -fno-omit-frame-pointer --coverage")
else()
  set(NCODE_WEB_BASE_FLAGS "${NCODE_WEB_BASE_FLAGS} -O3 -march=native -DNDEBUG")   
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NCODE_WEB_BASE_FLAGS}")
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
find_package(NCodeCommon REQUIRED)
find_package(NCodeNet REQUIRED)
find_package(Ctemplate REQUIRED)

include_directories(${CMAKE_BINARY_DIR})

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

################################
# Web and grapher
################################
set(WEB_HEADER_FILES src/web_page.h src/graph.h src/grapher.h src/fcgi.h)
add_library(ncode_web STATIC src/web_page.cc src/graph.cc src/grapher.cc src/fcgi.cc ${PROJECT_BINARY_DIR}/www_resources.c ${PROJECT_BINARY_DIR}/grapher_resources.c ${WEB_HEADER_FILES})
target_link_libraries(ncode_web ncode_common ncode_net ${CTEMPLATE_LIBRARIES})

if (NOT NCODE_WEB_DISABLE_TESTS)
  add_test_exec(web_page_test src/web_page_test.cc ncode_web)
  add_test_exec(graph_test src/graph_test.cc ncode_web)
  add_test_exec(grapher_test src/grapher_test.cc ncode_web)
endif()
