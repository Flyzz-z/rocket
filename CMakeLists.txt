cmake_minimum_required(VERSION 3.10)
project(rocket VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译器设置
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 编译选项
# 添加 -fno-omit-frame-pointer 保留栈帧指针,使perf能正确回溯调用栈
# 添加 -fno-inline 避免函数内联,使符号更清晰
add_compile_options(-Wall -Wno-deprecated -Wno-unused-but-set-variable)

# Debug模式编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in Debug mode with Sanitizers enabled")
    add_compile_options(-g -O0 -fno-omit-frame-pointer -fno-inline)

    # ThreadSanitizer: 检测数据竞争
    # add_compile_options(-fsanitize=thread)
    # add_link_options(-fsanitize=thread)

    # AddressSanitizer: 检测内存泄漏、use-after-free等
    # 注意: TSan和ASan不能同时使用，优先使用TSan检测并发问题
    # 如需使用ASan，注释掉上面的TSan行，取消下面两行注释:
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)

# Release模式编译选项
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Building in Release mode with optimizations enabled")
    add_compile_options(-O3 -DNDEBUG)
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/rocket
    ${CMAKE_SOURCE_DIR}/rocket/common
    ${CMAKE_SOURCE_DIR}/rocket/logger
    ${CMAKE_SOURCE_DIR}/rocket/net
    ${CMAKE_SOURCE_DIR}/rocket/net/tcp
    ${CMAKE_SOURCE_DIR}/rocket/net/coder
    ${CMAKE_SOURCE_DIR}/rocket/net/rpc
    /usr/local/include
)

# 查找依赖库
find_library(PROTOBUF_LIB libprotobuf.a /usr/local/lib)
find_library(TINYXML_LIB libtinyxml.a /usr/local/lib)
find_library(ETCD_CPP_LIB etcd-cpp-api /usr/local/lib)


# rocket库源文件
file(GLOB_RECURSE ROCKET_SOURCES
    "rocket/common/*.cc"
    "rocket/logger/*.cc"
    "rocket/net/*.cc"
    "rocket/net/tcp/*.cc"
    "rocket/net/coder/*.cc"
    "rocket/net/rpc/*.cc"
)

# 创建rocket静态库
add_library(rocket STATIC ${ROCKET_SOURCES})

# 链接依赖库
target_link_libraries(rocket 
    ${PROTOBUF_LIB}
    ${TINYXML_LIB}
    dl
    pthread
)

# protobuf生成文件
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/testcases/proto)

# 添加测试可执行文件
add_executable(test_tcp testcases/test_tcp.cc)
target_link_libraries(test_tcp rocket)

add_executable(test_client testcases/test_client.cc)
target_link_libraries(test_client rocket)

add_executable(test_rpc_client testcases/test_rpc_client.cc ${PROTO_DIR}/order.pb.cc  ${PROTO_DIR}/co_stub/co_order_stub.cc)
target_link_libraries(test_rpc_client rocket ${ETCD_CPP_LIB})

add_executable(test_rpc_server testcases/test_rpc_server.cc ${PROTO_DIR}/order.pb.cc ${PROTO_DIR}/co_stub/co_order_stub.cc)
target_link_libraries(test_rpc_server rocket ${ETCD_CPP_LIB})

add_executable(test_rpc_bench testcases/test_rpc_bench.cc ${PROTO_DIR}/order.pb.cc ${PROTO_DIR}/co_stub/co_order_stub.cc)
target_link_libraries(test_rpc_bench rocket ${ETCD_CPP_LIB})

# 安装规则
install(TARGETS rocket
    ARCHIVE DESTINATION /usr/local/lib
)

install(DIRECTORY rocket/common/
    DESTINATION /usr/local/include/rocket/common
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY rocket/net/
    DESTINATION /usr/local/include/rocket/net
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY rocket/net/tcp/
    DESTINATION /usr/local/include/rocket/net/tcp
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY rocket/net/coder/
    DESTINATION /usr/local/include/rocket/net/coder
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY rocket/net/rpc/
    DESTINATION /usr/local/include/rocket/net/rpc
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY rocket/logger/
    DESTINATION /usr/local/include/rocket/logger
    FILES_MATCHING PATTERN "*.h"
)

# 创建一个自定义目标来生成compile_commands.json
add_custom_target(
    compile_commands.json
    COMMAND ${CMAKE_COMMAND} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)