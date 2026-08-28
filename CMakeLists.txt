cmake_minimum_required(VERSION 3.12)
project(mini_leveldb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 公共头根 include/，内部头根 src/（两种 include 风格都要支持）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 自动收集 src/ 下所有 .cc 编译为静态库
# CONFIGURE_DEPENDS: 新增 .cc 后 cmake 自动重新扫描，不用手动重跑
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)
add_library(mini_leveldb STATIC ${LIB_SOURCES})

# tests/ 下每个 *_test.cc 单独编译链接，生成同名可执行文件
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc
)
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE mini_leveldb)
endforeach()
