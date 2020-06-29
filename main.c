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
char* disk;
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
    disk = (char*)disk_space;
    return disk;
}

void initBootBlock()
{
    BootBlock* boot_block = (BootBlock*)disk;
    strcpy(boot_block->disk_name, "yangruichen's Disk");
    boot_block->disk_size = sizeof(Block) * BLOCK_NUM;
    boot_block->fat_block = (Block*)(disk + sizeof(Block) * FAT_BLOCK);
    boot_block->data_block = (Block*)(disk + sizeof(Block) * DATA_BLOCK);
}

void initFat()
{
    fat = (char*)(disk + sizeof(Block) * FAT_BLOCK);

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
    for (int i = 2; i < (int)(sizeof(Block) / sizeof(Fcb)); i++) {
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
    int block_num = getFreeBlock((int)((size - 1) / sizeof(Block)) + 1);
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

    Fcb* root = (Fcb*)(disk + sizeof(Block) * DATA_BLOCK);
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
    for (int i = 0; i < (int)(sizeof(Block) / sizeof(Fcb)); i++) {
        if (p->is_existed == 1) {
            p++;
        } else {
            newFcb(p, name, 1, sizeof(Fcb) * 2);
            path[current]->size = (i + 1) * sizeof(Fcb);
            break;
        }
    }
}

void doLs()
{
    Fcb* p = path[current];
    for (int i = 0; i < (int)(path[current]->size / sizeof(Fcb)); i++) {
        printf("%s ", p->name);
        p++;
    }
    printf("\n");
}

// void doCd(char* path)
// {
//     int tag = -1;
//     int fd;
//     char* buf = (char*)malloc(10000);
//     openfilelist[currfd].count = 0;
//     do_read(currfd, openfilelist[currfd].length, buf);

//     fcb* fcbptr = (fcb*)buf;
//     // 查找目标 fcb
//     for (i = 0; i < (int)(openfilelist[currfd].length / sizeof(fcb)); i++, fcbptr++) {
//         if (strcmp(fcbptr->filename, dirname) == 0 && fcbptr->attribute == 0) {
//             tag = 1;
//             break;
//         }
//     }
//     if (tag != 1) {
//         printf("my_cd: no such dir\n");
//         return;
//     } else {
//         // . 和 .. 检查
//         if (strcmp(fcbptr->filename, ".") == 0) {
//             return;
//         } else if (strcmp(fcbptr->filename, "..") == 0) {
//             if (currfd == 0) {
//                 // root
//                 return;
//             } else {
//                 currfd = my_close(currfd);
//                 return;
//             }
//         } else {
//             // 其他目录
//             fd = get_free_openfilelist();
//             if (fd == -1) {
//                 return;
//             }
//             openfilelist[fd].attribute = fcbptr->attribute;
//             openfilelist[fd].count = 0;
//             openfilelist[fd].date = fcbptr->date;
//             openfilelist[fd].time = fcbptr->time;
//             strcpy(openfilelist[fd].filename, fcbptr->filename);
//             strcpy(openfilelist[fd].exname, fcbptr->exname);
//             openfilelist[fd].first = fcbptr->first;
//             openfilelist[fd].free = fcbptr->free;

//             openfilelist[fd].fcbstate = 0;
//             openfilelist[fd].length = fcbptr->length;
//             strcat(strcat(strcpy(openfilelist[fd].dir, (char*)(openfilelist[currfd].dir)), dirname), "/");
//             openfilelist[fd].topenfile = 1;
//             openfilelist[fd].dirno = openfilelist[currfd].first;
//             openfilelist[fd].diroff = i;
//             currfd = fd;
//         }
//     }
// }

int main()
{
    getDisk();
    initDisk();

    doMkdir("123");
    doLs();

    getchar();

    releaseDisk();
    return 0;
    
    // 成功退出
    // exit(EXIT_SUCCESS);
}
