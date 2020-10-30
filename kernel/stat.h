#define T_DIR 1    // Directory
#define T_FILE 2   // File
#define T_DEVICE 3 // Device

struct stat
{
  int dev;     // File system's disk device
  uint ino;    // Inode number,文件的唯一标志
  short type;  // Type of file
  short nlink; // Number of links to file，有几个文件名指向该文件的inode
  uint64 size; // Size of file in bytes
};
