cmake_minimum_required(VERSION 3.0)
project(mymuduo)

# mymuduo最终编译成so动态库，设置动态库的路径，放在当前的lib文件夹下
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置调试信息 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -pthread")

#定义参与编译的源文件
aux_source_directory(. SRC_LIST)
#编译生成动态库，库名为mymuduo，源文件为SRC_LIST
add_library(mymuduo SHARED ${SRC_LIST})

# 链接多线程库（由于muduo网络库中使用了大量C++11的thread相关功能，底层依然依赖pthread）
target_link_libraries(mymuduo pthread)