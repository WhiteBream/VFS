/*  __      __ _   _  _  _____  ____   ____  ____  ____   ___   ___  ___
    \ \_/\_/ /| |_| || ||_   _|| ___| | __ \| __ \| ___| / _ \ |   \/   |
     \      / |  _  || |  | |  | __|  | __ <|    /| __| |  _  || |\  /| |
      \_/\_/  |_| |_||_|  |_|  |____| |____/|_|\_\|____||_| |_||_| \/ |_|
*/
/*! \copyright Copyright (c) 2020-2024, White Bream, https://whitebream.nl
*************************************************************************//*!
 File system wrapper for a mix of FatFS and LittleFS filesystems.
****************************************************************************/

#include "vfs.h"
#include <stdlib.h>
//#include "defines.h"


#ifdef USE_FATFS

#ifndef FF_MAX_SS
#define FF_MAX_SS _MAX_SS
#endif

#endif

#ifndef DISKIO_HOOK_READ
#define DISKIO_HOOK_READ()
#endif
#ifndef DISKIO_HOOK_WRITE
#define DISKIO_HOOK_WRITE()
#endif
#ifndef DISKIO_HOOK_ERROR
#define DISKIO_HOOK_ERROR()
#endif


#ifdef USE_FATFS

#define	SZ_DIRE				32		/* Size of a directory entry */

#if _MAX_SS != _MIN_SS
#error _MAX_SS != _MIN_SS is currently unsupported
#endif

static FATFS vFatFs[_VOLUMES] = {0};
static Diskio_drvTypeDef* pDiskIo[_VOLUMES] = {0};

#endif // USE_FATFS


#ifdef USE_LITTLEFS

#define LFS_ATTR_LABEL    0x70
#define LFS_ATTR_CREATE   0x74
#define LFS_ATTR_MODIFY   0x75


int SpiFlashRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size)
{
	sflash_read(block * 4096 + off, buffer, size);
	return(0);
}


int SpiFlashProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size)
{
    return(sflash_SectorWrite(block * 4096 + off, buffer, size));
}


int SpiFlashErase(const struct lfs_config* c, lfs_block_t block)
{
	return(sflash_SectorErase(block * 4096));
}


int SpiFlashSync(const struct lfs_config* c)
{
    //return(-SPIFLASH_ioctl(CTRL_SYNC, nullptr));
	return(0);
}


static lfs_t vLittleFs[1];

// configuration of the filesystem is provided by this struct
struct lfs_config vLfsCfg =
{
    // block device operations
    .read  = SpiFlashRead,
    .prog  = SpiFlashProg,
    .erase = SpiFlashErase,
    .sync  = SpiFlashSync,

    // block device configuration
    .read_size = 16,
    .prog_size = 256,
    .block_size = 4096,
    .block_count = 256,
    .cache_size = 256,
    .lookahead_size = 16,
    .block_cycles = 100000,
};

#endif // USE_LITTLEFS


static void VfsEvent(FileSystem_t* filesys, VfsEvent_t event);


FileSystem_t vFileSystem[] =
{
#ifdef USE_FATFS
    {"SPI:", {{&vFatFs[0], &SPIFLASH_Driver}}, VfsEvent, FS_FATFS | FS_FIXED},
#endif
#ifdef USE_LITTLEFS
    //{"SPI:", {{&vLittleFs[0], &vLfsCfg, SpiFlashIoctl}}, VfsEvent, FS_LITTLEFS | FS_FIXED},
    {"SPI:", {{&vLittleFs[0], &vLfsCfg, nullptr}}, VfsEvent, FS_LITTLEFS | FS_FIXED},
#endif
#ifdef USE_JESFS
	{"SPI:", {{"SPI Flash"}}, VfsEvent, FS_JESFS | FS_FIXED},
#endif
    {nullptr, {}}
};


#ifdef USE_FATFS

DSTATUS disk_initialize(BYTE pdrv)
{
    for (int i = 0; i < sizeof(vFileSystem) / sizeof(vFileSystem[0]); i++)
    {
        // We must find the appropriate driver on first use of a specific physical drive
        if ((vFileSystem[i].index == i + 1) && ((vFileSystem[i].type & ~FS_FIXED) == FS_FATFS) && (vFileSystem[i].fatfs.fs->drv == pdrv))
        {
            pDiskIo[pdrv] = vFileSystem[i].fatfs.drv;
            break;
        }
    }
    if (pDiskIo[pdrv] != nullptr)
        return(pDiskIo[pdrv]->disk_initialize());
    else
        return(STA_NOINIT);
}


DSTATUS disk_status(BYTE pdrv)
{
    return(pDiskIo[pdrv]->disk_status());
}


DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    DRESULT res = pDiskIo[pdrv]->disk_read(buff, sector, count);

    if(res == RES_OK)
    {
        DISKIO_HOOK_READ();
    }
    else
    {
        DISKIO_HOOK_ERROR();
    }
    return(res);
}


#if _USE_WRITE == 1
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    DRESULT res = pDiskIo[pdrv]->disk_write(buff, sector, count);

    if(res == RES_OK)
    {
        DISKIO_HOOK_WRITE();
    }
    else
    {
        DISKIO_HOOK_ERROR();
    }
    return(res);
}
#endif


#if _USE_IOCTL == 1
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return(pDiskIo[pdrv]->disk_ioctl(cmd, buff));
}
#endif


#if 0
DWORD get_fattime(void)
{
    time_t t = time(nullptr);
    struct tm* pTm = localtime(&t);
    uint32_t ret;

    ret = ((pTm->tm_year - 80) & 0x7F) << 25;
    ret |= (pTm->tm_mon + 1) << 21;
    ret |= (pTm->tm_mday) << 16;
    ret |= (pTm->tm_hour) << 11;
    ret |= (pTm->tm_min) << 5;
    ret |= (pTm->tm_sec / 2) << 0;
    return(ret);
}
#endif

#endif // USE_FATFS


static void
VfsEvent(FileSystem_t* filesys, VfsEvent_t event)
{
    const char pre[] = {'k', 'M', 'G', 'T'};
    int err;

    if (event == EVT_MOUNT)
    {
        VfsInfo_t info;

        // Get disk info
        if (err = -vfs_stat(filesys->drive, &info), err == 0)
        {
            uint64_t sz = info.blocks * info.blocksize;

            uint32_t s1 = sz / 1024;
            uint8_t p1 = 0;

            uint32_t s2 = (sz - info.size) / 1024;
            uint8_t p2 = 0;

            while ((p1 < sizeof(pre) - 1) && (s1 > 9 * 1024))
            {
                p1++;
                s1 /= 1024;
            }
            while ((p2 < sizeof(pre) - 1) && (s2 > 9 * 1024))
            {
                p2++;
                s2 /= 1024;
            }

            syslog(nullptr, "Mounted %s %lu%cB, %lu%cB free (%s)\n", filesys->drive, s1, pre[p1], s2, pre[p2], vfs_fs_type(filesys->drive));

            // Check file-system
            VfsDir_t* dir = malloc(sizeof(VfsDir_t));
            if (dir != nullptr)
            {
                if (vfs_dir_open(dir, filesys->drive) == 0)
                {
                    while (vfs_dir_read(dir, &info) == 0)
                    {
                        if ((info.size == UINT32_MAX) && (info.name[0] == 0xFF) && (info.name[1] == 0xFF) && (info.name[2] == 0xFF) && (info.name[3] == 0xFF) && (info.name[4] == 0xFF))
                        {
                            syslog(nullptr, "Filesystem %s seems corrupted...\n", filesys->drive);
                            err = EDOOFUS;
                            break;
                        }
#if 0
                        else if (info.name[0] != '.')
                        {
                        	if (info.size > UINT32_MAX)
                        		info.size = UINT32_MAX;
                        	syslog(nullptr, "- %s (%lu)\n", info.name, (uint32_t)info.size);
                        }
#endif
                    }
                    vfs_dir_close(dir);
                }
                free(dir);
            }
        }
        else
        {
            syslog(nullptr, "Mounted %s (%s) but vfs_stat says %d: %s...!\n", filesys->drive, vfs_fs_type(filesys->drive), err, strerror(err));
            err = EDOOFUS;
        }
#if 0
        // Try resolve problem with corrupted SPI-Flash based volume by
        // fully erasing at chip level, then attempt to build a new filesystem
        if ((err == EDOOFUS) && (filesys->type & FS_FIXED))
        {
            // Erase disk
            // TODO map ioctl through filesys->type
            SPIFLASH_ioctl(DISK_ERASE, 0);

            // Format if mount failed
            err = -vfs_format(filesys->drive);
            syslog(nullptr, "Formatted %s result %d: %s\n", filesys->drive, err, strerror(err));

        #ifdef _BOOTAPI_H
            char label[22];
            sniprintf(label, sizeof(label), "%s%s", filesys->drive, pBootVersion->serial);
            vfs_setlabel(label);
        #endif

            if (err == 0)
                SystemReset();
        }
#endif
    }
    else if (event == EVT_UNMOUNT)
    {
        syslog(nullptr, "Unmounted %s\n", filesys->drive);
    }
    else if (event == EVT_MOUNT_FAIL)
    {
        if (filesys->type & FS_FIXED)
        {
            // Erase disk
            // TODO map ioctl through filesys->type
         //   SPIFLASH_ioctl(DISK_ERASE, 0);

            // Format if mount failed
            err = -vfs_format(filesys->drive);
            syslog(nullptr, "Formatted %s result %d: %s", filesys->drive, err, strerror(err));

        #ifdef _BOOTAPI_H
            char label[22];
            sniprintf(label, sizeof(label), "%s%s", filesys->drive, pBootVersion->serial);
            vfs_setlabel(label);
        #endif

            if (err == 0)
                SystemReset();
        }
    }
}

