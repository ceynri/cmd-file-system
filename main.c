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

Block* getDisk()
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

Fcb* searchFcb(char* name)
{
    char is_existed = 0;
    Fcb* fcb = path[current];
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (fcb->is_existed == 1 && strcmp(fcb->name, name) == 0) {
            return fcb;
        }
        fcb++;
    }
    return NULL;
}

Fcb* getFreeFcb(Fcb* fcb)
{
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (fcb->is_existed == 0) {
            return fcb;
        }
        fcb++;
    }
    return NULL;
}

Fcb* newFcb(Fcb* fcb, char* name, char is_dir, int size)
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
    fcb->size = 0;
    fcb->is_existed = 1;
    return fcb;
}

void doMkdir(char* name)
{
    Fcb* fcb = getFreeFcb(path[current]);
    newFcb(fcb, name, 1, sizeof(Block));
    fcb->size = sizeof(Fcb) * 2;
    path[current]->size += sizeof(Fcb);
    Fcb* new_dir = (Fcb*)(disk + fcb->block_number);
    initDirFcb(new_dir, fcb->block_number, path[current]->block_number);
}

// TODO 多层级目录，需要递归删除
int doRmdir(char* name)
{
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        printf("[doRmdir] You can't delete %s\n", name);
        return -1;
    }
    Fcb* fcb = searchFcb(name);
    if (fcb && fcb->is_directory == 1) {
        // 释放fat标记
        int i;
        for (i = 0; i < ((fcb->size - 1) / sizeof(Block) + 1); i++) {
            fat[fcb->block_number + i] = FREE;
        }
        // 删除索引记录
        fcb->is_existed = 0;
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
    Fcb* fcb = searchFcb(src);
    if (fcb) {
        strcpy(fcb->name, dst);
    } else {
        printf("[doRename] Not found %s\n", src);
        return -1;
    }
    return 0;
}

int doOpen(char* name)
{
    Fcb* fcb = searchFcb(name);
    if (fcb) {
        if (fcb->is_directory != 0) {
            printf("[doOpen] %s is not readable file\n", name);
            return -1;
        }
        // 存在该文件，即读取文件内容
        char* p = (char*)(disk + fcb->block_number);
        for (int i = 0; i < fcb->size / sizeof(char); i++) {
            printf("%c", *p);
            p++;
        }
    } else {
        // 不存在该文件，则创建文件
        Fcb* fcb = getFreeFcb(path[current]);
        newFcb(fcb, name, 0, sizeof(Block));
        fcb->size = 0;
        path[current]->size += sizeof(Fcb);
    }
    return 0;
}

int doWrite(char* name)
{
    Fcb* fcb = searchFcb(name);
    if (fcb) {
        if (fcb->is_directory != 0) {
            printf("[doWrite] %s is not writable file\n", name);
            return -1;
        }
        // 存在该文件，即尝试写入文件内容
        char* p = (char*)(disk + fcb->block_number);
        scanf("%[^\n]", p);
        fcb->size = strlen(p) * sizeof(char);
    } else {
        // 不存在该文件，则创建文件
        printf("[doWrite] Not found %s\n", name);
    }
    return 0;
}

void doLs()
{
    Fcb* fcb = path[current];
    int num = path[current]->size / sizeof(Fcb);
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (fcb->is_existed) {
            printf("%s\t", fcb->name);
        }
        fcb++;
    }
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
    printf("\n");
    doLs();
    printf("\n");
    doRmdir("123");
    printf("\n");
    doLs();
    printf("\n");
    doOpen("345");
    printf("\n");
    doLs();
    printf("\n");
    doWrite("345");
    printf("\n");
    doLs();
    printf("\n");
    doOpen("345");
    printf("\n");
    doLs();
    printf("\n");

    getchar();

    releaseDisk();
    return 0;
}
