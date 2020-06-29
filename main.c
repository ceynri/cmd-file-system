#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

#include "disk.h"
#include "file.h"

// 硬盘指针
Block* disk;
// 硬盘空间
void* disk_space = (void*)0;
// 共享内存id
int shmid;

// fat表
char* fat;

// 当前打开的目录的绝对路径数组
Fcb* path[16];
// 当前打开的目录深度
short current;

char* getDisk()
{
    // 获取共享内存区
    shmid = shmget((key_t)1110, sizeof(Disk), 0666 | IPC_CREAT);
    printf("Shmid: %d\n", shmid);
    if (shmid == -1) {
        // 获取失败
        fprintf(stderr, "Shmget failed\n");
        exit(EXIT_FAILURE);
    }

    // 启用进程对该共享内存区的访问
    disk_space = shmat(shmid, (void*)0, 0);
    if (disk_space == (void*)-1) {
        // 启用失败
        fprintf(stderr, "Shmat failed\n");
        exit(EXIT_FAILURE);
    }
    disk = (Block*)disk_space;
    return disk;
}

void initBootBlock()
{
    BootBlock* boot_block = (BootBlock*)disk;
    strcpy(boot_block->disk_name, "yangruichen's Disk");
    boot_block->disk_size = sizeof(Block) * BLOCK_NUM;
    boot_block->fat_block = disk + FAT_BLOCK;
    boot_block->data_block = disk + DATA_BLOCK;
}

void initFat()
{
    fat = (char*)(disk + FAT_BLOCK);

    for (int i = 0; i < FAT_BLOCK; i++) {
        fat[i] = USED; // Full sign
    }
    for (int i = FAT_BLOCK; i < BLOCK_NUM; i++) {
        fat[i] = FREE; // Empty sign
    }
}

void setCurrentTime(Datetime* datetime)
{
    time_t rawtime;
    time(&rawtime);
    struct tm* time = localtime(&rawtime);
    datetime->year = (time->tm_year + 1900);
    datetime->month = (time->tm_mon + 1);
    datetime->day = time->tm_mday;
    datetime->hour = time->tm_hour;
    datetime->minute = time->tm_min;
    datetime->second = time->tm_sec;
}

void initDirFcb(Fcb* fcb, short block_number, short parent_number)
{
    // 标记该空间以被使用
    fat[block_number] = USED;
    // 目录名
    strcpy(fcb->name, ".");
    fcb->is_directory = 1;
    // 时间
    setCurrentTime(&fcb->datetime);
    // 文件
    fcb->block_number = block_number;
    fcb->size = 2 * sizeof(Fcb);
    fcb->is_existed = 1;

    // ..目录
    Fcb* p = fcb + 1;
    memcpy(p, fcb, sizeof(Fcb));
    strcpy(p->name, "..");
    p->block_number = parent_number;
    p->size = -1;

    // 初始化目录表
    for (int i = 2; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        p++;
        strcpy(p->name, "");
        p->is_existed = 0;
    }
}

int getFreeBlock(int size)
{
    int count = 0;
    for (int i = DATA_BLOCK; i < DATA_NUM; i++) {
        if (fat[i] == FREE) {
            count++;
        } else {
            count = 0;
        }
        if (count == size) {
            for (int j = 0; j < size; j++) {
                fat[i - j] = USED;
            }
            return i;
        }
    }
    return -1;
}

void newFcb(Fcb* fcb, char* name, char is_dir, int size)
{
    // 目录名
    strcpy(fcb->name, name);
    fcb->is_directory = is_dir;
    // 时间
    setCurrentTime(&fcb->datetime);
    // 文件
    int block_num = getFreeBlock(((size - 1) / sizeof(Block)) + 1);
    if (block_num == -1) {
        printf("[newFcb] Disk has Fulled\n");
        exit(EXIT_FAILURE);
    }
    fcb->block_number = block_num;
    fcb->size = size;
    fcb->is_existed = 1;
}

void initDisk()
{
    initBootBlock();
    initFat();

    Fcb* root = (Fcb*)(disk + DATA_BLOCK);
    initDirFcb(root, DATA_BLOCK, DATA_BLOCK);
    current = 0;
    path[current] = root;
}

void releaseDisk()
{
    // 停止引用共享内存
    if (shmdt(disk_space) == -1) {
        // 停止失败
        fprintf(stderr, "Shmdt failed\n");
        exit(EXIT_FAILURE);
    }
    // 删除共享内存
    if (shmctl(shmid, IPC_RMID, 0) == -1) {
        // 删除失败
        fprintf(stderr, "Shmctl failed\n");
        exit(EXIT_FAILURE);
    }
}

void doMkdir(char* name)
{
    // 找一个空fcb
    Fcb* p = path[current];
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (p->is_existed == 1) {
            p++;
        } else {
            newFcb(p, name, 1, sizeof(Fcb) * 2);
            path[current]->size += sizeof(Fcb);
            break;
        }
    }
}

Fcb* searchFcb(char* name)
{
    char is_existed = 0;
    Fcb* p = path[current];
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (p->is_existed == 1 && strcmp(p->name, name) == 0) {
            return p;
        }
        p++;
    }
    return NULL;
}

int doRmdir(char* name)
{
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        printf("[doRmdir] You can't delete %s\n", name);
        return -1;
    }
    Fcb* p = searchFcb(name);
    if (p && p->is_directory == 1) {
        // 释放fat标记
        for (int i = 0; i < ((p->size - 1) / sizeof(Block)) + 1; i++) {
            fat[p->block_number + i] = FREE;
        }
        // 删除索引记录
        p->is_existed = 0;
    } else {
        printf("[doRmdir] Not found %s\n", name);
        return -1;
    }
    return 0;
}

int doRename(char* src, char* dst)
{
    if (strcmp(src, ".") == 0 || strcmp(src, "..") == 0) {
        printf("[doRename] You can't rename %s\n", src);
        return -1;
    }
    Fcb* p = searchFcb(src);
    if (p) {
        strcpy(p->name, dst);
    } else {
        printf("[doRename] Not found %s\n", src);
        return -1;
    }
    return 0;
}

int doOpen(char* name)
{
    // Fcb* p = searchFcb(name);
    // if (p) {
    //     if (p->is_directory != 0) {
    //         printf("[doOpen] %s is not file\n", name);
    //         return -1;
    //     }
    //     // 存在该文件，即读取文件内容
    //     disk[p->block_number]
    // } else {
    //     // 不存在该文件，则创建文件
    // }
}

void doLs()
{
    Fcb* p = path[current];
    int num = path[current]->size / sizeof(Fcb);
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (p->is_existed) {
            printf("%s\t", p->name);
        }
        p++;
    }
    printf("\n");
}

void doCd(char* path)
{
    // if (strcmp(fcbptr->filename, dirname) == 0 && fcbptr->attribute == 0){}
}

int main()
{
    getDisk();
    initDisk();

    doMkdir("123");
    doLs();
    doRename("123", "345");
    doLs();

    getchar();

    releaseDisk();
    return 0;
}
