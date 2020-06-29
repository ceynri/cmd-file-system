#include <fcntl.h>
// #include <semaphore.h>
#include <sys/stat.h>

#define USED -1
#define FREE 0

#define FAT_BLOCK 1
#define DATA_BLOCK 14

const unsigned int BLOCK_NUM = 25600;
const unsigned int DATA_NUM = 25600 - DATA_BLOCK;
const unsigned int BLOCK_SIZE = 4096;
const unsigned int DISK_SIZE = 104857600;

typedef struct
{
    char space[4096];
} Block;

typedef struct
{
    Block data[25600];
} Disk;

typedef struct
{
    char disk_name[32]; // 盘名
    short disk_size;    // 盘大小
    Block *fat_block;   // fat块起始位置
    Block *data_block;  // 数据块起始位置
} BootBlock;

typedef struct
{
    // 13
    short id;
} Fat;
