# 基于共享内存的简易文件管理系统

个人大三操作系统课程期末大作业

## 实验目的
1.	掌握计算机操作系统管理进程、处理机、存储器、文件系统的基本方法。
2.	了解进程的创建、撤消和运行，进程并发执行；了解线程（进程）调度方法；掌握内存空间的分配与回收的基本原理；通过模拟文件管理的工作过程，了解文件操作命令的实质。
3.	了解现代计算机操作系统工作原理，具有初步分析设计操作系统的能力。
4.	通过在计算机上编程实现操作系统中的各种管理功能，在系统程序设计能力方面得到提升。

## 实验要求

1.	创建一个100M的文件或者创建一个100M的共享内存。
2.	尝试自行设计一个C语言小程序，使用步骤1分配的100M空间（共享内存或mmap），然后假设这100M空间为一个空白磁盘，设计一个简单的文件系统管理这个空白磁盘，给出文件和目录管理的基本数据结构，并画出文件系统基本结构图，以及基本操作接口。（20分）
3.	在步骤1的基础上实现部分文件操作接口操作，创建目录mkdir，删除目录rmdir，修改名称，创建文件open，修改文件，删除文件rm，查看文件系统目录结构ls。（30分）
4.	参考进程同步的相关章节，通过信号量机制实现多个终端对上述文件系统的互斥访问，系统中的一个文件允许多个进程读，不允许写操作；或者只允许一个写操作，不允许读。（20分）
5.	实验报告书写质量（30分）

## 实现步骤

1. 获取共享内存
2. 设计文件系统结构
    1. 分析题目，选择合适的文件系统结构（FAT）
    2. 目录管理功能规划（FCB）
3. 实现常见文件操作接口
    1. 创建目录mkdir
    2. 删除目录rmdir
    3. 修改名称rename
    4. 创建文件open
    5. 写入文件write
    6. 删除文件rm
    7. 查看文件系统目录结构ls
    8. 查看文件系统目录详细信息lls
    9. 目录跳转cd
4. 基于信号量机制实现多终端互斥访问
    1. POSIX信号量
    2. open读写互斥
    3. write读写互斥
