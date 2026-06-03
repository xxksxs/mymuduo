#!/bin/bash

# 发生错误时结束脚本执行
set -e

# 如果没有build目录，创建该目录
if [ ! -d `pwd`/build ]; then
    mkdir `pwd`/build
fi

# 清理现有的build内文件并重新编译
rm -rf `pwd`/build/*

cd `pwd`/build &&
    cmake .. &&
    make

# 回到项目根目录
cd ..

# 把头文件拷贝到 /usr/include/mymuduo ，so库拷贝到 /usr/lib
# 这样其他程序就可以直接包含头文件并链接该动态库了
if [ ! -d /usr/include/mymuduo ]; then 
    mkdir /usr/include/mymuduo
fi

# 拷贝头文件到系统路径下
for header in `ls *.h`
do
    cp $header /usr/include/mymuduo
done

# 拷贝生成的动态库到系统默认路径
cp `pwd`/lib/libmymuduo.so /usr/lib

# 刷新动态链接库缓存
ldconfig

echo "mymuduo build and install sequence completed successfully!"
