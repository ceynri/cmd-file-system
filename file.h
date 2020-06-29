typedef struct
{
    unsigned short year;
    unsigned short month;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
} Datetime;

typedef struct
{
    // 32 Bytes
    char name[11]; // 名称
    char ext[3]; // 扩展名
    Datetime datetime; // 创建时间
    short block_number; // 起始盘块号
    unsigned short size; // 长度
    char is_directory; // 是目录还是文件
    char is_existed; // 表示目录项是否存在
} Fcb;
