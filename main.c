#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

#include "disk.h"
#include "file.h"

Block* disk; // 硬盘指针
void* disk_space = (void*)0; // 硬盘空间
int shmid; // 共享内存id

char* fat; // fat表

Fcb* open_path[16]; // 当前打开的目录路径fcb数组
char* open_name[16]; // 当前打开的目录路径名数组
short current; // 当前打开的目录深度

const int FCB_LIST_LEN = sizeof(Block) / sizeof(Fcb); // 一个Block可容纳的Fcb个数

Block* getDisk()
{
    // 获取共享内存区
    shmid = shmget((key_t)1127, sizeof(Disk), 0666 | IPC_CREAT);
    // printf("Shmid: %d\n", shmid);
    if (shmid == -1) {
        // 获取失败
        fprintf(stderr, "[getDisk] Shmget failed\n");
        exit(EXIT_FAILURE);
    }
    // 启用进程对该共享内存区的访问
    disk_space = shmat(shmid, (void*)0, 0);
    if (disk_space == (void*)-1) {
        // 启用失败
        fprintf(stderr, "[getDisk] Shmat failed\n");
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
    for (int i = 2; i < FCB_LIST_LEN; i++) {
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
    open_path[current] = root;
    open_name[current] = "Root";
}

void releaseDisk()
{
    // 停止引用共享内存
    if (shmdt(disk_space) == -1) {
        // 停止失败
        fprintf(stderr, "[releaseDisk] Shmdt failed\n");
        exit(EXIT_FAILURE);
    }
    // 删除共享内存
    if (shmctl(shmid, IPC_RMID, 0) == -1) {
        // 删除失败
        fprintf(stderr, "[releaseDisk] Shmctl failed\n");
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

int getBlockNum(int size)
{
    return (size - 1) / sizeof(Block) + 1;
}

Fcb* searchFcb(char* name)
{
    char is_existed = 0;
    Fcb* fcb = open_path[current];
    for (int i = 0; i < FCB_LIST_LEN; i++) {
        if (fcb->is_existed == 1 && strcmp(fcb->name, name) == 0) {
            return fcb;
        }
        fcb++;
    }
    return NULL;
}

Fcb* getFreeFcb(Fcb* fcb)
{
    for (int i = 0; i < FCB_LIST_LEN; i++) {
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
    int block_num = getFreeBlock(getBlockNum(size));
    if (block_num == -1) {
        printf("[newFcb] Disk has Fulled\n");
        exit(EXIT_FAILURE);
    }
    fcb->block_number = block_num;
    fcb->size = 0;
    fcb->is_existed = 1;
    return fcb;
}

// 字符分割函数
char** split(char** arr, char* str, const char* delims)
{
    char* s = NULL;
    s = strtok(str, delims);
    while (s != NULL) {
        *arr++ = s;
        s = strtok(NULL, delims);
    }
    return arr;
}

int doMkdir(char* name)
{
    // 查找是否已存在
    Fcb* s = searchFcb(name);
    if (s) {
        printf("[doMkdir] %s is existed\n", name);
        return -1;
    }
    
    // 在当前fcb表中插入新目录的fcb
    Fcb* fcb = getFreeFcb(open_path[current]);
    newFcb(fcb, name, 1, sizeof(Block));
    fcb->size = sizeof(Fcb) * 2;
    open_path[current]->size += sizeof(Fcb);

    // 初始化新目录的fcb
    Fcb* new_dir = (Fcb*)(disk + fcb->block_number);
    initDirFcb(new_dir, fcb->block_number, open_path[current]->block_number);
    return 0;
}

// TODO 多层级目录，需要递归删除
int doRmdir(char* name)
{
    // char* path_split[16];
    // split(path_split, path, "/");
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        printf("[doRmdir] You can't delete %s\n", name);
        return -1;
    }
    Fcb* fcb = searchFcb(name);
    if (fcb && fcb->is_directory == 1) {
        Fcb* p = (Fcb*)(disk + fcb->block_number) + 2;
        for (int i = 0; i < FCB_LIST_LEN; i++) {

        }
        // 释放fat标记
        for (int i = 0; i < getBlockNum(fcb->size); i++) {
            fat[fcb->block_number + i] = FREE;
        }
        // 删除索引记录
        fcb->is_existed = 0;
        // 减小记录的空间大小
        open_path[current] -= sizeof(fcb);
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
        for (int i = 0; i < fcb->size; i++) {
            printf("%c", *p);
            p++;
        }
        printf("\n");
    } else {
        // 不存在该文件，则创建文件
        Fcb* fcb = getFreeFcb(open_path[current]);
        newFcb(fcb, name, 0, sizeof(Block));
        fcb->size = 0;
        open_path[current]->size += sizeof(Fcb);
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
        char* head = (char*)(disk + fcb->block_number);
        char* p = head;
        // 去掉蜜汁回车
        getchar();
        while ((*p = getchar()) != 27 && *p != EOF) {
            p++;
        }
        *p = 0;
        fcb->size = strlen(head);
    } else {
        // 不存在该文件
        printf("[doWrite] Not found %s\n", name);
    }
    return 0;
}

int doRm(char* name)
{
    Fcb* fcb = searchFcb(name);
    if (fcb) {
        if (fcb->is_directory != 0) {
            printf("[doRm] %s is not file\n", name);
            return -1;
        }
        // 释放fat标记
        for (int i = 0; i < getBlockNum(fcb->size); i++) {
            fat[fcb->block_number + i] = FREE;
        }
        // 删除索引记录
        fcb->is_existed = 0;
        // 减小记录的空间大小
        open_path[current] -= sizeof(fcb);
    } else {
        printf("[doRm] Not found %s\n", name);
        return -1;
    }
    return 0;
}

void doLs()
{
    Fcb* fcb = open_path[current];
    int num = open_path[current]->size / sizeof(Fcb);
    for (int i = 0; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (fcb->is_existed) {
            printf("%s\t", fcb->name);
        }
        fcb++;
    }
    printf("\n");
}

void doLls()
{
    Fcb* fcb = open_path[current];
    int num = open_path[current]->size / sizeof(Fcb);
    for (int i = 0; i < 2; i++) {
        if (fcb->is_existed) {
            printf("%s\n", fcb->name);
        }
        fcb++;
    }
    for (int i = 2; i < (sizeof(Block) / sizeof(Fcb)); i++) {
        if (fcb->is_existed) {
            printf("%hu-%hu-%hu %hu:%hu:%hu\t", fcb->datetime.year, fcb->datetime.month, fcb->datetime.day, fcb->datetime.hour, fcb->datetime.minute, fcb->datetime.second);
            printf("Block %hd\t", fcb->block_number);
            printf("%hu B\t", fcb->size);
            printf("%s\t", fcb->is_directory ? "Dir" : "File");
            printf("%s\n", fcb->name);
        }
        fcb++;
    }
}

int doCd(char* name)
{
    if (strcmp(name, ".") == 0) {
        return 0;
    }
    if (strcmp(name, "..") == 0) {
        current = (current > 0 ? current - 1 : 0);
        return 0;
    }
    if (current == 15) {
        printf("[doCd] Depth of the directory has reached the upper limit\n");
        return -1;
    }
    // 查找是否存在
    Fcb* fcb = searchFcb(name);
    if (fcb) {
        if (fcb->is_directory != 1) {
            printf("[doCd] %s is not directory\n", name);
            return -1;
        }
        current++;
        open_name[current] = fcb->name;
        open_path[current] = (Fcb*)(disk + fcb->block_number);
        return 0;
    } else {
        printf("[doCd] %s is not existed\n", name);
        return -1;
    }
}

void printPathInfo()
{
    printf("YangRui@FileSystem:");
    for (int i = 0; i <= current; i++) {
        printf("/%s", open_name[i]);
    }
    printf("> ");
}

char* getArg(char* str)
{
    scanf("%s", str);
    return str;
}

char* doWhat(char* cmd)
{
    scanf("%s", cmd);
    return cmd;
}

int cmdLoopAdapter()
{
    char buffer[64];
    while (1) {
        printPathInfo();
        doWhat(buffer);
        if (strcmp(buffer, "mkdir") == 0) {
            doMkdir(getArg(buffer));
        } else if (strcmp(buffer, "rmdir") == 0) {
            doRmdir(getArg(buffer));
        } else if (strcmp(buffer, "rename") == 0) {
            doRename(getArg(buffer), getArg(buffer));
        } else if (strcmp(buffer, "open") == 0) {
            doOpen(getArg(buffer));
        } else if (strcmp(buffer, "write") == 0) {
            doWrite(getArg(buffer));
        } else if (strcmp(buffer, "rm") == 0) {
            doRm(getArg(buffer));
        } else if (strcmp(buffer, "ls") == 0) {
            doLs();
        } else if (strcmp(buffer, "lls") == 0) {
            doLls();
        } else if (strcmp(buffer, "cd") == 0) {
            doCd(getArg(buffer));
        } else if (strcmp(buffer, "exit") == 0) {
            return 0;
        } else if (strlen(buffer) != 0) {
            printf("[cmdLoopAdapter] Unsupported command\n");
        }
        fflush(stdin);
    }
    return -1;
}

int main()
{
    getDisk();
    initDisk();
    int ret = cmdLoopAdapter();
    releaseDisk();
    return ret;
}
