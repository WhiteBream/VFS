/*  __      __ _   _  _  _____  ____   ____  ____  ____   ___   ___  ___
    \ \_/\_/ /| |_| || ||_   _|| ___| | __ \| __ \| ___| / _ \ |   \/   |
     \      / |  _  || |  | |  | __|  | __ <|    /| __| |  _  || |\  /| |
      \_/\_/  |_| |_||_|  |_|  |____| |____/|_|\_\|____||_| |_||_| \/ |_|
*/
/*! \copyright Copyright (c) 2015-2024, White Bream, https://whitebream.nl
*************************************************************************//*!
 \file      vfs.h
 \brief     Filesystem wrapper
 \version   1.0.0.0
 \since     June 6, 2015
 \date      April 15, 2024

 File system wrapper for a mix of FatFS and LittleFS filesystems.
****************************************************************************/

#ifndef _VFS_H
#define _VFS_H

#ifdef __cplusplus
extern "C" {
#else
#define nullptr (void*)0
#endif


#include <stdint.h>
#include <stdio.h> // SEEK_SET et al
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include "vfs_conf.h"


#ifndef EDOOFUS
#define EDOOFUS    88     /* Programming error */
#endif


#ifdef USE_FATFS
//#define DIR DIR_FATFS   // 'Rename' FatFs DIR struct to allow for same-named posix DIR
#define DIR_FATFS DIR  // 'Rename' FatFs DIR struct to allow for same-named posix DIR
#include "ff.h"
#undef DIR
#include "ff_gen_drv.h"
#endif

#ifdef USE_LITTLEFS
#include "lfs.h"
#endif

#ifdef USE_JESFS
#include "jesfs.h"
#endif


//#if defined(USE_FATFS) && defined(USE_LITTLEFS)
//#define HAVE_MULTI_FS
//#endif


#ifndef VFS_POSIX
#define VFS_POSIX   1
#endif
#ifndef VFS_CLI
#define VFS_CLI     1
#endif


#if (VFS_POSIX == 1)
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#endif


#define MAX_PATH    64

#ifndef INODE_STORAGE_BITS
#define INODE_STORAGE_BITS  2	// Allows for 4 drives
#endif
#ifndef INODE_FOLDER_BITS
#define INODE_FOLDER_BITS   10	// Allows for root + 1023 'active' directories
#endif
#define INODE_ITEM_BITS     (32 - INODE_FOLDER_BITS - INODE_STORAGE_BITS)
#define INODE_ITEM_MASK     (UINT32_MAX >> (INODE_FOLDER_BITS + INODE_STORAGE_BITS))
#define INODE_STORAGE_MASK	(UINT32_MAX << (INODE_ITEM_BITS + INODE_FOLDER_BITS))
#define INODE_FOLDER_MASK	((UINT32_MAX >> INODE_STORAGE_BITS) & (UINT32_MAX << INODE_ITEM_BITS))
#define INODE_STORAGE(x)    (x >> (32 - INODE_STORAGE_BITS))
#define INODE_FOLDER(x)     ((x & INODE_FOLDER_MASK) >> (32 - INODE_FOLDER_BITS - INODE_STORAGE_BITS))


// open flags
#define VFS_RDONLY    0x01    // Open a file as read only
#define VFS_WRONLY    0x02    // Open a file as write only
#define VFS_RDWR      0x03    // Open a file as read and write
#define VFS_CREAT     0x10    // Create a file if it does not exist
#define VFS_EXCL      0x20    // Fail if a file already exists
#define VFS_TRUNC     0x40    // Truncate the existing file to zero size
#define VFS_APPEND    0x80    // Move to end of file on every write

#define	ATR_IREAD     0x01    // read permission, owner
#define	ATR_IWRITE 	  0x02    // write permission, owner
#define	ATR_IEXEC	  0x04    // execute/search permission, owner
#define ATR_HID       0x08
#define ATR_SYS       0x10
#define ATR_REG	      0x40	  // regular
#define	ATR_DIR       0x80	  // directory
#define ATR_REMOVABLE_DISK	0x10
#define ATR_FLAT_FILESYSTEM	0x20


#ifdef xUSE_FATFS
typedef struct FATFS FATFS;
typedef struct FIL FIL;
typedef struct DIR_FATFS DIR_FATFS;
typedef struct Diskio_drvTypeDef Diskio_drvTypeDef;
#endif

#ifdef xUSE_LITTLEFS
struct lfs_config;
struct lfs_attr;
struct lfs_file_config;
typedef struct _lfs lfs_t;
typedef struct _lfs_file lfs_file_t;
typedef struct _lfs_dir lfs_dir_t;
#endif


typedef enum Fs_e
{
    FS_NONE,
    FS_ROOT,
    FS_FATFS,
    FS_LITTLEFS,
    FS_JESFS,
    FS_ANYFS,
    FS_FIXED = 0x80
}
Fs_t;

typedef enum VfsEvent_e
{
    EVT_MOUNT,
    EVT_UNMOUNT,
	EVT_MOUNT_FAIL
}
VfsEvent_t;

typedef struct FileSystem_s
{
    char* drive;
    union
    {
        struct
        {
            void* a;
            void* b;
            void* c;
        }
        dummy;
    #ifdef USE_FATFS
        struct
        {
            FATFS* fs;
            Diskio_drvTypeDef* drv;
        }
        fatfs;
    #endif
    #ifdef USE_LITTLEFS
        struct
        {
            lfs_t* fs;
            struct lfs_config* cfg;
            int (*ioctl)(const struct lfs_config* c, uint8_t cmd, void* buff);
        }
        lfs;
    #endif
	#ifdef USE_JESFS
		struct
		{
        	const char* label;
		}
		jesfs;
	#endif
    };
    void (*eventcb)(struct FileSystem_s* filesys, VfsEvent_t event);
    Fs_t type;
    int8_t index;
    uint8_t namelen;
}
FileSystem_t;


typedef struct VfsFile_s
{
    FileSystem_t* filesys;
#ifdef MTP_EVENTS
    uint32_t handle;
    enum VfsHandleFlags_e
	{
    	FILE_CREATED = 1,
		FILE_WRITTEN = 2,
	} flags;
#endif
    union
    {
    #ifdef USE_FATFS
        FIL ff;
    #endif
    #ifdef USE_LITTLEFS
        struct
        {
            lfs_file_t lfs;
            time_t modified;
            struct lfs_attr lfsattrs[1];
            struct lfs_file_config lfscfg;
        };
    #endif
	#ifdef USE_JESFS
        FS_DESC jes;
	#endif
    };
}
VfsFile_t;


typedef struct VfsDir_s
{
    FileSystem_t* filesys;
    union
    {
        int root;
    #ifdef USE_FATFS
        DIR_FATFS ff;
    #endif
    #ifdef USE_LITTLEFS
        struct
        {
            lfs_dir_t lfs;
            char lfspath[MAX_PATH];
        };
    #endif
	#ifdef USE_JESFS
		uint16_t fno;
	#endif
    };
    const char* pattern;
}
VfsDir_t;


typedef struct VfsInfo_s
{
	char     name[MAX_PATH];
	uint64_t size;
	time_t   created;
	time_t   modified;
	uint8_t  attrib;
    uint8_t  device;
    uint32_t inode;
    uint32_t blocks;
    uint32_t blocksize;
}
VfsInfo_t;


extern FileSystem_t vRootSystem[1];
extern FileSystem_t vFileSystem[];


extern int vfs_file_open(VfsFile_t* file, const char* path, int flags);
#if 0
extern int vfs_file_open_fromdir(VfsFile_t* file, VfsDir_t* dir);
#endif
extern int vfs_file_close(VfsFile_t* file);
extern size_t vfs_file_read(VfsFile_t* file, void* buffer, size_t size);
extern size_t vfs_file_write(VfsFile_t* file, const void* buffer, size_t size);
extern int vfs_putc(char c, VfsFile_t* file);
extern int vfs_puts(const char* str, VfsFile_t* file);
extern int vfs_printf(VfsFile_t* file, const char* str, ...);
extern int vfs_getc(VfsFile_t* file);
extern char* vfs_gets(char* buff, int len, VfsFile_t* file);
extern size_t vfs_file_seek(VfsFile_t* file, size_t offset, int whence);
extern size_t vfs_file_sync(VfsFile_t* file);
extern int vfs_file_truncate(VfsFile_t* file, size_t size);
extern size_t vfs_file_tell(VfsFile_t* file);
extern size_t vfs_file_size(VfsFile_t* file);
extern int vfs_file_eof(VfsFile_t* file);
extern int vfs_file_rewind(VfsFile_t* file);
extern int vfs_dir_open(VfsDir_t* dir, const char* path);
extern int vfs_dir_close(VfsDir_t* dir);
extern int vfs_dir_read(VfsDir_t* dir, VfsInfo_t* info);
extern int vfs_findfirst(VfsDir_t* dir, VfsInfo_t* info, const char* path, const char* pattern);
extern int vfs_findnext(VfsDir_t* dir, VfsInfo_t* info);
extern int vfs_mkdir(const char* path);
extern int vfs_remove(const char* path);
extern int vfs_rename(const char* oldpath, const char* newpath);
extern int vfs_copy(const char* source, const char* dest);
extern int vfs_stat(const char* path, VfsInfo_t* info);
extern int vfs_touch(const char* path, const VfsInfo_t* info);
extern int vfs_crc(const char* path, uint32_t* crc);
extern int64_t vfs_fs_size(const char* path);
extern int64_t vfs_fs_free(const char* path);
extern const char* vfs_fs_type(const char* path);
extern int vfs_getlabel(const char* path, char* label);
extern int vfs_setlabel(const char* label);
extern int vfs_mount(const char* path, bool mount);
extern int vfs_format(const char* path);
extern char* vfs_volume(int num);
extern int vfs_check_fs_mutex(const char* path);


#if (VFS_POSIX == 1)

#undef feof
#undef getc

#define fclose          _fclose
#define fflush          _fflush
#define fstat           _fstat

extern FILE* fopen(const char* restrict pathname, const char* restrict mode);
extern int _fclose(FILE* stream);
extern int _fflush(FILE* stream);
extern int fseek(FILE* stream, long offset, int whence);
extern long ftell(FILE* stream);
extern int feof(FILE* stream);
extern void rewind(FILE* stream);
extern size_t fread(void* restrict ptr, size_t size, size_t nmemb, FILE* restrict stream);
extern size_t fwrite(const void* restrict ptr, size_t size, size_t nmemb, FILE* stream);
extern char* fgets(char* str, int n, FILE* stream);
extern int getc(FILE* stream);
extern int fputs(const char* str, FILE* stream);
extern int fputc(int ch, FILE* stream);
extern int _fstat(FILE* stream, struct stat* restrict buf);

extern int ftruncate(int fildes, off_t length);
extern int stat(const char* restrict path, struct stat* restrict buf);
//extern int chmod(const char* path, mode_t mode);
//extern int fchmod(int fd, mode_t mode);
//extern mode_t umask(mode_t mask);
extern int mkdir(const char* path, mode_t mode);
extern int remove(const char* filename);
extern int rename(const char* oldname, const char* newname);

extern DIR* opendir(const char* name);
extern int closedir(DIR* dir);
extern struct dirent* readdir(DIR* dir);

#endif // VFS_POSIX

#ifndef CRC_FUNC
extern uint32_t CRC_FUNC(uint32_t* pCrc, uint32_t* pUint32, uint32_t vLen, bool vInit);
#endif

extern int vfs_init(void);
extern void vfs_deinit(void);


#ifdef __cplusplus
}
#endif

#endif /*_VFS_H */
