/*  __      __ _   _  _  _____  ____   ____  ____  ____   ___   ___  ___
    \ \_/\_/ /| |_| || ||_   _|| ___| | __ \| __ \| ___| / _ \ |   \/   |
     \      / |  _  || |  | |  | __|  | __ <|    /| __| |  _  || |\  /| |
      \_/\_/  |_| |_||_|  |_|  |____| |____/|_|\_\|____||_| |_||_| \/ |_|
*/
/*! \copyright Copyright (c) 2020, White Bream, https://whitebream.nl
*************************************************************************//*!
 \file      vfs_conf.h
 \brief     Configuration of filesystem wrapper
 \version   1.0.0.0
 \since     July 21, 2020
 \date      July 21, 2020

 File system wrapper for a mix of FatFS and LittleFS filesystems.
****************************************************************************/

#ifndef _VFS_CONF_H
#define _VFS_CONF_H

#ifdef __cplusplus
extern "C" {
#endif


//#define USE_FATFS
#define USE_JESFS
//#define USE_LITTLEFS

//#define CIA_DISK	"SPI:"

#define _WHITEBREAM_H
//#include "whitebream.h"
#include "bootapi.h"
#include "main.h"
#include "project.h"


#define VFS_POSIX           0
#define VFS_CLI             0
#define VFS_NODIRS          1


#define INODE_STORAGE_BITS      1
#define INODE_FOLDER_BITS   	7	// Allows for root + 1023 'active' directories

//#define CRC_FUNC(pCrc, pUint32, vLen, vInit)	CalculateSTM32Crc(pCrc, pUint32, vLen, vInit)

// Some IOCTL commands, for time being here. pProbably not really the right place but I don't know where else
#define CTRL_SYNC			0	/* Complete pending write process (needed at _FS_READONLY == 0) */
#define GET_SECTOR_COUNT	1	/* Get media size (needed at _USE_MKFS == 1) */
#define GET_SECTOR_SIZE		2	/* Get sector size (needed at _MAX_SS != _MIN_SS) */
#define GET_BLOCK_SIZE		3	/* Get erase block size (needed at _USE_MKFS == 1) */
#define CTRL_TRIM			4	/* Inform device that the data on the block of sectors is no longer used (needed at _USE_TRIM == 1) */
#define SECTOR_ERASE    	105
#define DISK_ERASE      	106

//#define vfs_malloc              pvPortMalloc
//#define vfs_free                vPortFree
//#define vfs_malloc_usable_size  vPortMallocUsableSize

#ifndef syslog
#define syslog(x,...)
#endif

//#ifdef LED_DISK	// dont work on enum
//#define DISKIO_HOOK_READ()      LedBlink(LED_DISK, GREEN, 50)
//#define DISKIO_HOOK_WRITE()     LedBlink(LED_DISK, YELLOW, 100)
//#define DISKIO_HOOK_ERROR()     LedBlink(LED_DISK, RED, 500)
//#endif


#ifdef __cplusplus
}
#endif

#endif /*_VFS_CONF_H */
