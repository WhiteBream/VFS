/*  __      __ _   _  _  _____  ____   ____  ____  ____   ___   ___  ___
    \ \_/\_/ /| |_| || ||_   _|| ___| | __ \| __ \| ___| / _ \ |   \/   |
     \      / |  _  || |  | |  | __|  | __ <|    /| __| |  _  || |\  /| |
      \_/\_/  |_| |_||_|  |_|  |____| |____/|_|\_\|____||_| |_||_| \/ |_|
*/
/*! \copyright Copyright (c) 2015-2024, White Bream, https://whitebream.nl
*************************************************************************//*!
 File system wrapper for a mix of FatFS and LittleFS filesystems.
****************************************************************************/

#include "vfs.h"
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <malloc.h>


#ifndef __isleap
/* Nonzero if YEAR is a leap year (every 4 years, except every 100th isn't, and every 400th is).  */
#define __isleap(year)   ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif


#ifdef USE_FATFS

#ifndef FF_MAX_SS
#define FF_MAX_SS _MAX_SS
#endif
#ifndef FF_USE_LABEL
#define FF_USE_LABEL _USE_LABEL
#endif

#endif


#ifndef vfs_malloc
#define vfs_malloc              malloc
#endif
#ifndef vfs_free
#define vfs_free                free
#endif
#ifndef vfs_malloc_usable_size
#define vfs_malloc_usable_size  malloc_usable_size
#endif


FileSystem_t vRootSystem[1] = {{"", .dummy = {nullptr}, nullptr, FS_ROOT, 0, 0}};


#ifdef USE_FATFS

// Helper function to translate return values from FatFS
static int
_ff_errno(FRESULT err)
{
    int e = 0;

    switch (err)
    {
        case FR_DISK_ERR: e = EIO; break; 	/* (1) A hard error occurred in the low level disk I/O layer */
        case FR_INT_ERR: e = EDOOFUS; break; 				/* (2) Assertion failed */
        case FR_NOT_READY: e = EBUSY; break; 			/* (3) The physical drive cannot work */
        case FR_NO_FILE: e = ENOENT; break; 				/* (4) Could not find the file */
        case FR_NO_PATH: e = ENOTDIR; break; 				/* (5) Could not find the path */
        case FR_INVALID_NAME: e = EINVAL; break; 		/* (6) The path name format is invalid */
        case FR_DENIED: e = ENOSPC; break; 				/* (7) Access denied due to prohibited access or directory full */
        case FR_EXIST: e = EEXIST; break; 				/* (8) Access denied due to prohibited access */
        case FR_INVALID_OBJECT: e = EBADF; break; 		/* (9) The file/directory object is invalid */
        case FR_WRITE_PROTECTED: e = EROFS; break; 		/* (10) The physical drive is write protected */
        case FR_INVALID_DRIVE: e = ENODEV; break; 		/* (11) The logical drive number is invalid */
        case FR_NOT_ENABLED: e = ENODEV; break; 			/* (12) The volume has no work area */
        case FR_NO_FILESYSTEM: e = ENXIO; break; 		/* (13) There is no valid FAT volume */
        case FR_MKFS_ABORTED: e = EINVAL; break; 		/* (14) The f_mkfs() aborted due to any parameter error */
        case FR_TIMEOUT: e = ETIMEDOUT; break; 				/* (15) Could not get a grant to access the volume within defined period */
        case FR_LOCKED: e = ENOLCK; break; 				/* (16) The operation is rejected according to the file sharing policy */
        case FR_NOT_ENOUGH_CORE: e = ENOMEM; break; 		/* (17) LFN working buffer could not be allocated */
        case FR_TOO_MANY_OPEN_FILES: e = EMFILE; break; 	/* (18) Number of open files > _FS_SHARE */
        case FR_INVALID_PARAMETER: e = EINVAL; break; 	/* (19) Given parameter is invalid */
        default: break;
    }
    return(-e);
}


// Helper function to translate f_open parameters in FatFS
static BYTE
_ff_fopen_flag(int flags)
{
    BYTE ff = 0;

    if (flags & VFS_RDONLY)
    {
        ff |= FA_READ;
    }
    if (flags & VFS_WRONLY)
    {
        ff |= FA_WRITE;
        if (flags & VFS_TRUNC)
        {
            ff |= FA_CREATE_ALWAYS; //Creates a new file. If the file is existing, it will be truncated and overwritten.
        }
        if (flags & VFS_EXCL)
        {
            ff |= FA_CREATE_NEW;	// Creates a new file. The function fails with FR_EXIST if the file is existing.
        }
        if (flags & VFS_CREAT)
        {
            ff |= FA_OPEN_ALWAYS;	// Opens the file if it is existing. If not, a new file will be created.
        }
    }

    return(ff);
}


static time_t
_ff_timestamp(FILINFO* info, bool modified)
{
    const char vMonths[2][12] =
    {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},   // Not leap year
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}    // Leap year
    };

    int y, x;
    WORD n;
    time_t t = 0;

#if _FATFS <= 32020	/* Revision ID */
    if (modified)
    {
        n = info->fdate;
    }
    else
    {
        n = info->fcdate;
    }
#else
    n = info->fdate;
#endif

    x = ((n >> 9) & 0x7F) + 1980;
    y = 1970;
    if (x > 2018)
    {
        // Quick optimize to save 46 iterations on the majority of conversions
        t = 1514764800 / 86400;
        y = 2018;
    }
    for (; y < x; y++)
    {
        t += __isleap(y) ? 366 : 365;
    }

    for (x = 0; x < ((n >> 5) & 0xF) - 1; x++)
    {
        t += vMonths[__isleap(y)][x];
    }

    t += (n & 0x1F) - 1;
    t *= 86400;

#if _FATFS <= 32020	/* Revision ID */
    if (modified)
    {
        n = info->ftime;
    }
    else
    {
        n = info->fctime;
    }
#else
    n = info->ftime;
#endif

    t += 3600 * ((n >> 11) & 0x1F);
    t += 60 * ((n >> 5) & 0x3F);
    t += 2 * ((n >> 0) & 0x1F);

    return(t);
}


static void
_ff_fat_timedate(FILINFO* info, time_t julian, bool modified)
{
    struct tm* p = gmtime(&julian);
    WORD v = 0;

    if (p->tm_year > 80)
    {
        v = ((p->tm_year - 80) & 0x7F) << 9;
        v |= (p->tm_mon + 1) << 5;
        v |= p->tm_mday << 0;
    }
#if _FATFS <= 32020	/* Revision ID */
    if (modified)
    {
        info->fdate = v;
    }
    else
    {
        info->fcdate = v;
    }
#else
    info->fdate = v;
#endif

    v = p->tm_hour << 11;
    v |= p->tm_min << 5;
    v |= (p->tm_sec / 2) << 0;

#if _FATFS <= 32020	/* Revision ID */
    if (modified)
    {
        info->ftime = v;
    }
    else
    {
        info->fctime = v;
    }
#else
    info->ftime = v;
#endif
}

#endif // USE_FATFS


#ifdef USE_LITTLEFS

#define LFS_ATTR_LABEL    0x70
#define LFS_ATTR_CREATE   0x74
#define LFS_ATTR_MODIFY   0x75


// Helper function to translate f_open parameters in LittleFS
static int
_lfs_fopen_flag(int flags)
{
    int ff = 0;

    if (flags & VFS_RDONLY)
    {
        ff |= LFS_O_RDONLY;
    }
    if (flags & VFS_WRONLY)
    {
        ff |= LFS_O_WRONLY;
    }

    if (flags & VFS_CREAT)
    {
        ff |= LFS_O_CREAT;
    }
    if (flags & VFS_EXCL)
    {
        ff |= LFS_O_EXCL;
    }
    if (flags & VFS_TRUNC)
    {
        ff |= LFS_O_TRUNC;
    }

    return(ff);
}


static char*
_lfs_fix_path(FileSystem_t* pVfs, const char* path)
{
	char* p = (char*)path;

	while (p = strchr(p, '\\'), p != nullptr)
	{
        *p++ = '/';
    }

	if (strncmp(path, pVfs->drive, pVfs->namelen) == 0)
	{
		return((char*)&path[pVfs->namelen + 1]);
	}
	else if (path[0] == '/')
	{
		return((char*)&path[pVfs->namelen + 1]);
	}
	else
	{
		return((char*)path);
	}
}

#endif // USE_LITTLEFS


#ifdef USE_JESFS

// Helper function to translate return values from FatFS
static int
_jes_errno(int16_t err)
{
    int e = 0;

    if (err >= 0)
    {
    	return(err);
    }

    switch (err)
    {
		case -108: e = ENXIO; break; // Unknown MAGIC, this Flash is either unformated or contains other data
		case -110: e = EINVAL; break; // Filename to long/short
		case -111: e = ENOSPC; break; // Too many files, Index full! (ca. 1000 for 4k sectors)
		case -113: e = ENOSPC; break; // Flash full! No free sectors available or Flash not formatted
    	case -124: e = ENOENT; break; // File not found
    	case -129: e = EBADF; break; // File descriptor corrupted.
    	case -139: e = EINVAL; break; // Format parameter
    	case -142: e = EBADF; break; // Illegal file system structure (-> run recover, Index defect points to illegal HEAD)
    	case -143: e = EBADF; break; // Illegal file system structure (-> run recover, Index defect)
    	case -147: e = EBUSY; break; // Device Voltage too low
    	case -148: e = EBUSY; break; // Flash not accesible: Deepsleep or PowerFail

    	case -100: // SPI Init (Hardware)
    	case -101: // Flash Timeout WaitBusy
    	case -102: // SPI Can not set WriteEnableBit (Flash locked?)
    	case -103: // FlashID:Unknown/illegal, readable but unknown Flash Density (describes the size)
    	case -104: // FlashID:Unknown Flash ID, readable but unknown (eg. 0xC228 for Macronix M25xx, see docu)
    	case -105: // Illegal flash addr
    	case -106: // Block crosses sector border
    	case -107: // fs_start found problems in the filesystem structure (-> run recover)
    	case -109: // Flash-ID in the Flash Index does not match Hardware-ID (-> run recover)
    	case -112: // Sector border violated (before write)
    	case -114: // Index corrupted (-> run recover) (or FS in deepsleep (see internal flag STATE_DEEPSLEEP))
    	case -115: // Number out of range Index (fs_stat)
    	case -116: // No active file at this entry (fs_stat)
    	case -117: // Illegal descriptor or file not open
    	case -118: // File not open for writing
    	case -119: // Index out of range
    	case -120: // Illegal sector address
    	case -121: // Short circle in sector list (-> run recover)
    	case -122: // sector list contains illegal file owner (-> run recover)
    	case -123: // Illegal sector type (-> run recover)
    	case -125: // Illegal file flags (e.g. trying to delete a file opened for write)
    	case -126: // Illegal file system structure (-> run recover ((possible reason: PowerLoss, , Index defect points to HEAD 0xFFFFFFFF)))
    	case -127: // Closed files can not be continued (for writing)
    	case -128: // Sector defect ('Header with owner') (-> run recover)
    	case -130: // Try to write to (unclosed) file in RAW with unknown end position
    	case -131: // Sector corrupted: Empty marked sector not empty
    	case -132: // File is empty
    	case -133: // Rename not possible with Files open as READ or RAW
    	case -134: // Rename requires an empty File as new Filename
    	case -135: // Both files must be open for Rename
    	case -136: // Erase Sector failed
    	case -137: // Write to Flash Failed
    	case -138: // Verify Failed
    	case -140: // Command Deepsleep: Filesystem already sleeping (only informative)
    	case -141: // Other Commands: Filesystem sleeping!
    	case -144: // FlashID:ZeroSleep read as 0x000000 (Short Circuit on SPI or Flash still Sleepmode)
    	case -145: // FlashID:UnConnected read as 0xFFFFFF (SPI unconnected or Flash corrupt)
    	case -146: // Illegal MagicHeader: Invalid Value found (and not 0xFFFFFFFF)
        default: e = EIO; break;
    }
    return(-e);
}


static bool
_jes_truncate_path(const char* path)
{
	// We might be manipulating the path even though we pretend to expect a const...
	// Just don't pass ROM based strings that are longer than FNAMLEN, okay?

	char* p = (char*)path;

	if (strlen(p) > FNAMELEN)
	{
		int extlen = 0;
		char* ext = strrchr(p, '.');

		if (ext != nullptr)
		{
			extlen = strlen(ext);
		}

		memmove(p + FNAMELEN - 2 - extlen, "~1", 3);	// MSDOS style
		if (extlen > 0)
		{
			memmove(p + FNAMELEN - extlen, ext, extlen + 1);
		}
		return(true);
	}
	return(false);
}


static uint8_t
_jes_open_flag(int flags)
{
    uint8_t ff = 0;

    if (flags & VFS_RDONLY)
    {
        ff |= SF_OPEN_READ;
    }
    if (flags & VFS_WRONLY)
    {
    	ff |= SF_OPEN_WRITE | SF_OPEN_CRC;

        if (flags & VFS_TRUNC)
        {
            ff |= SF_OPEN_CREATE; //Creates a new file. If the file is existing, it will be truncated and overwritten.
        }
        if (flags & VFS_EXCL)	// Create a new file. The function fails with FR_EXIST if the file is existing.
        {
        	// TODO must test for existence before opening
        }
    }

    return(ff);
}



static char*
_jes_fix_path(FileSystem_t* pVfs, const char* path)
{
	if (strncmp(path, pVfs->drive, pVfs->namelen) == 0)
	{
		return((char*)&path[pVfs->namelen + 1]);
	}
	else if ((path[0] == '\\') || (path[0] == '/'))
	{
		return((char*)&path[pVfs->namelen + 1]);
	}
	else
	{
		return((char*)path);
	}
}

#endif // USE_JESFS


static int
_vfs_find_entry(const char* restrict name, bool force)
{
	int i;

    if (name == nullptr)
    {
        return(-ENOENT);
    }
    for (i = 0; vFileSystem[i].drive != nullptr; i++)
    {
        if (strncasecmp(name, vFileSystem[i].drive, vFileSystem[i].namelen) == 0)
        {
            if (force || (vFileSystem[i].index == i + 1))
            {
                return(i);
            }
        }
    }
    if ((i == 1) && (strchr(name, ':') == nullptr))	// Allow empty drive name if there is only one drive
    {
    	return(0);
    }
    return(-ENOENT);
}


int
vfs_file_open(VfsFile_t* file, const char* path, int flags)
{
    int i, ret = -ENOENT;
#ifdef MTP_EVENTS
    bool exists;
#endif

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(ret);
    }

    file->filesys = &vFileSystem[i];
#ifdef MTP_EVENTS
    file->flags = 0;
#endif

    // We cannot open the root entries

    //if (((path[file->filesys->namelen] != '\\') && (path[file->filesys->namelen] != '/')) || (strlen(path) < file->filesys->namelen + 2))
    //if ((strncmp(path, file->filesys->drive, file->filesys->namelen) == 0) && ((path[file->filesys->namelen] != '\\') && (path[file->filesys->namelen] != '/')) || (strlen(path) < file->filesys->namelen + 2))
    if ((strncmp(path, file->filesys->drive, file->filesys->namelen) == 0) && ((path[file->filesys->namelen] != '\\') && (path[file->filesys->namelen] != '/')))
    {
        return(-EBADF);
    }

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
        #ifdef MTP_EVENTS
            if (flags & VFS_CREAT)
            {
                // Check if file exists already
                if (f_stat(path, nullptr) == FR_OK)
                {
                    exists = true;
                }
            }
        #endif
            if (ret = _ff_errno(f_open(&file->ff, path, _ff_fopen_flag(flags))), ret == 0)
            {
            #ifdef MTP_EVENTS
                if ((flags & VFS_CREAT) && !exists)
                {
                    file->flags |= FILE_CREATED;
                }
                // TODO file->handle = xx;
            #endif
                if (flags & VFS_APPEND) // Older version of fatfs don't support append
                {
                    f_lseek(&file->ff, f_size(&file->ff));
                }
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            time_t t;

        #ifdef MTP_EVENTS
            if (flags & VFS_CREAT)
            {
                // Check if file exists already
                if (lfs_file_opencfg(file->filesys->lfs.fs, &file->lfs, path, LFS_O_RDONLY, &file->lfscfg) == 0)
                {
                    ret = lfs_file_close(file->filesys->lfs.fs, &file->lfs);
                    exists = true;
                }
            }
        #endif
            // Timestamp functionality
            file->modified = time(nullptr);
            // Set up description of timestamp attribute
            file->lfsattrs[0].type = LFS_ATTR_MODIFY;
            file->lfsattrs[0].buffer = &file->modified;
            file->lfsattrs[0].size = sizeof(&file->modified);
            // Set up config to indicate file has custom attributes
            memset(&file->lfscfg, 0, sizeof(file->lfscfg));
            file->lfscfg.attrs = file->lfsattrs;
            file->lfscfg.attr_count = 1;
            path = _lfs_fix_path(file->filesys, path);
            if (ret = lfs_file_opencfg(file->filesys->lfs.fs, &file->lfs, path, _lfs_fopen_flag(flags), &file->lfscfg), ret == 0)
            {
            #ifdef MTP_EVENTS
                if ((flags & VFS_CREAT) && !exists)
                {
                    file->flags |= FILE_CREATED;
                }
                // TODO file->handle = xx;
            #endif
                // Set created timestamp if new file...
                if (lfs_getattr(file->filesys->lfs.fs, path, LFS_ATTR_CREATE, &t, sizeof(t)) == LFS_ERR_NOATTR)
                {
                    t = time(nullptr);
                    lfs_setattr(file->filesys->lfs.fs, path, LFS_ATTR_CREATE, &t, sizeof(t)); // LFS timestamp
                }
            }
            break;
        }
    #endif
	#ifdef USE_JESFS
        case FS_JESFS:
		{
            path = _jes_fix_path(file->filesys, path);
            _jes_truncate_path(path);

		#ifdef MTP_EVENTS
			if (flags & VFS_CREAT)
			{
				// Check if file exists already
				if (fs_open(&file->jes, path, SF_OPEN_READ))
				{
					fs_close(&file->jes);
					exists = true;
				}
			}
		#endif
			ret = _jes_errno(fs_open(&file->jes, (char*)path, _jes_open_flag(flags)));
		#ifdef MTP_EVENTS
			if ((flags & VFS_CREAT) && !exists)
			{
				file->flags |= FILE_CREATED;
			}
			// TODO file->handle = xx;
		#endif
			if (flags & VFS_APPEND)
			{
				file->jes.file_pos = file->jes.file_len;
			}
			break;
		}
	#endif
    }

    if (ret < 0)	// Clear the filesystem handle so this can be used to detect an open filehandle
    {
    	file->filesys = nullptr;
    }
    return(ret);
}


#if 0
int
vfs_file_open_fromdir(VfsFile_t* file, VfsDir_t* dir)
{
    int ret = -ENOENT;

    // TODO Check if the DIR entry is a file

    file->filesys = dir->filesys;

    switch (dir->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            if (ret = _ff_errno(f_open_fromdir(&file->ff, &dir->ff)), ret == 0)
            {
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            // TODO similar solution for LFS

            // Set up config to indicate file has custom attributes
            memset(&file->lfscfg, 0, sizeof(file->lfscfg));
            file->lfscfg.attrs = file->lfsattrs;
            file->lfscfg.attr_count = 1;
            //path = _lfs_fix_path(file->filesys, path);
            //if (ret = lfs_file_opencfg(file->filesys->lfs.fs, &file->lfs, path, LFS_O_RDONLY, &file->lfscfg), ret == 0)
            //{
            //}
            break;
        }
    #endif
    }

    return(ret);
}
#endif


int
vfs_file_close(VfsFile_t* file)
{
    int ret = -EBADF;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_close(&file->ff));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_close(file->filesys->lfs.fs, &file->lfs);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = _jes_errno(fs_close(&file->jes));
			break;
		}
	#endif
    }
#ifdef MTP_EVENTS
    if (file->flags & FILE_CREATED)
    {
        PtpEvent(PTP_EVENT_OBJECT_ADDED, file->handle);
    }
    else if (file->flags & FILE_WRITTEN)
    {
        PtpEvent(PTP_EVENT_OBJECT_INFO_CHANGED, file->handle);
    }
#endif
    file->filesys = nullptr;
    return(ret);
}


size_t
vfs_file_read(VfsFile_t* file, void* buffer, size_t size)
{
    size_t ret = 0;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            UINT r;

            if (ret = _ff_errno(f_read(&file->ff, buffer, size, &r)), ret == 0)
            {
                ret = r;
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_read(file->filesys->lfs.fs, &file->lfs, buffer, size);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = _jes_errno(fs_read(&file->jes, (uint8_t*)buffer, size));
			break;
		}
	#endif
    }
    return(ret);
}


size_t
vfs_file_write(VfsFile_t* file, const void* buffer, size_t size)
{
    size_t ret = 0;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            UINT r;

            if (ret = _ff_errno(f_write(&file->ff, buffer, size, &r)), ret == 0)
            {
                ret = r;
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            file->modified = time(nullptr);
            ret = lfs_file_write(file->filesys->lfs.fs, &file->lfs, buffer, size);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = _jes_errno(fs_write(&file->jes, (uint8_t*)buffer, size));
			break;
		}
	#endif
    }
#ifdef MTP_EVENTS
    file->flags |= FILE_WRITTEN;
#endif
    return(ret);
}


int
vfs_putc(char c, VfsFile_t* file)
{
    return(vfs_file_write(file, &c, 1));
}


int
vfs_puts(const char* str, VfsFile_t* file)
{
    return(vfs_file_write(file, str, strlen(str)));
}


int
vfs_printf(VfsFile_t* file, const char* str, ...)
{
    int ret = -ENOMEM;
    size_t len;
    char* buf;
    va_list args;

    va_start(args, str);
    len = vsniprintf(nullptr, 0, str, args);
    if (buf = vfs_malloc(len + 1), buf != nullptr)
    {
        len = vsniprintf(buf, len + 1, str, args);
        ret = vfs_file_write(file, &buf, len);
        vfs_free(buf);
    }
    va_end(args);
    return(ret);
}


int
vfs_getc(VfsFile_t* file)
{
    char ch;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            f_gets(&ch, sizeof(ch), &file->ff);
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            lfs_file_read(file->filesys->lfs.fs, &file->lfs, &ch, sizeof(ch));
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			fs_read(&file->jes, (uint8_t*)&ch, sizeof(ch));
			break;
		}
	#endif
    }
    return(ch);
}


char*
vfs_gets(char* buff, int len, VfsFile_t* file)
{
    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            return(f_gets(buff, len, &file->ff));
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            int rv = 0;

            while (rv < len - 1)
            {
                if (lfs_file_read(file->filesys->lfs.fs, &file->lfs, &buff[rv], sizeof(buff[0])) > 0)
                {
                    if (buff[rv] == '\n')
                    {
                        buff[rv] = '\0';
                        break;
                    }
                    rv++;
                }
            }
            return(buff);
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
            int rv = 0;

            while (rv < len - 1)
            {
            	if (fs_read(&file->jes, (uint8_t*)&buff[rv], sizeof(buff[0])) > 0)
                {
                    if (buff[rv] == '\n')
                    {
                        buff[rv] = '\0';
                        break;
                    }
                    rv++;
                }
            }
            return(buff);
		}
	#endif
    }
    return(nullptr);
}


size_t
vfs_file_seek(VfsFile_t* file, size_t offset, int whence)
{
    int ret = -EBADF;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            DWORD start;

            if (whence == SEEK_SET)
            {
                start = 0;
            }
            else if (whence == SEEK_CUR)
            {
                start = f_tell(&file->ff);
            }
            else if (whence == SEEK_END)
            {
                start = f_size(&file->ff) + 1;
            }
            else
            {
                ret = -EINVAL;
                break;
            }

            ret = _ff_errno(f_lseek(&file->ff, start + offset));
            if ((ret == 0) && (f_tell(&file->ff) != start + offset))
            {
                ret = -ENOSPC;
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_seek(file->filesys->lfs.fs, &file->lfs, offset, whence);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
            size_t start;

            if (whence == SEEK_SET)
            {
                start = 0;
            }
            else if (whence == SEEK_CUR)
            {
                start = file->jes.file_pos;
            }
            else if (whence == SEEK_END)
            {
                start = file->jes.file_len + 1;
            }
            else
            {
                ret = -EINVAL;
                break;
            }

			file->jes.file_pos = start + offset;
			ret = 0;
			break;
		}
	#endif
    }
    return(ret);
}


size_t
vfs_file_sync(VfsFile_t* file)
{
    int ret = -EBADF;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_sync(&file->ff));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_sync(file->filesys->lfs.fs, &file->lfs);
            break;
        }
    #endif
    }
    return(ret);
}


int
vfs_file_truncate(VfsFile_t* file, size_t size)
{
    int ret = -EBADF;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            if (ret = _ff_errno(f_lseek(&file->ff, size)), ret == 0)
            {
                ret = _ff_errno(f_truncate(&file->ff));
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            file->modified = time(nullptr);
            ret = lfs_file_truncate(file->filesys->lfs.fs, &file->lfs, size);
            break;
        }
    #endif
    }
#ifdef MTP_EVENTS
    file->flags |= FILE_WRITTEN;
#endif
    return(ret);
}


size_t
vfs_file_tell(VfsFile_t* file)
{
    size_t ret = 0;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = f_tell(&file->ff);
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_tell(file->filesys->lfs.fs, &file->lfs);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			// Assumes valid file handle
			ret = file->jes.file_pos;
			break;
		}
	#endif
    }
    return(ret);
}


size_t
vfs_file_size(VfsFile_t* file)
{
    size_t ret = 0;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = f_size(&file->ff);
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_size(file->filesys->lfs.fs, &file->lfs);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			// Assumes valid file handle
			ret = file->jes.file_len;
			break;
		}
	#endif
    }
    return(ret);
}


int
vfs_file_eof(VfsFile_t* file)
{
    size_t ret = 0;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = f_eof(&file->ff);
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_tell(file->filesys->lfs.fs, &file->lfs) == lfs_file_size(file->filesys->lfs.fs, &file->lfs);
            break;
        }
    #endif
    }
    return(ret);
}


int
vfs_file_rewind(VfsFile_t* file)
{
    int ret = -EBADF;

    switch (file->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_lseek(&file->ff, 0));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_file_seek(file->filesys->lfs.fs, &file->lfs, 0, LFS_SEEK_SET);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = _jes_errno(fs_rewind(&file->jes));
			break;
		}
	#endif
    }
    return(ret);
}


int
vfs_dir_open(VfsDir_t* dir, const char* path)
{
    int i, ret = -ENOENT;

    memset(dir, 0, sizeof(VfsDir_t));

    if (i = _vfs_find_entry(path, false), i >= 0)
    {
        dir->filesys = &vFileSystem[i];

        switch (vFileSystem[i].type & ~FS_FIXED)
        {
        #ifdef USE_FATFS
            case FS_FATFS:
            {
                ret = _ff_errno(f_opendir(&dir->ff, path));
                break;
            }
        #endif
        #ifdef USE_LITTLEFS
            case FS_LITTLEFS:
            {
                strncpy(dir->lfspath, path, sizeof(dir->lfspath));
                ret = lfs_dir_open(dir->filesys->lfs.fs, &dir->lfs, _lfs_fix_path(dir->filesys, dir->lfspath));
                break;
            }
        #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
            path = _jes_fix_path(dir->filesys, path);
            if (strlen(path) == 0)
            {
            	dir->fno = 0;
            	ret = 0;
            }
			break;
		}
	#endif
        }
    }
    else if ((path == nullptr) || (strcspn(path, " \x1") <= 1)) // Deal with mountpoints...
    {
        dir->filesys = &vRootSystem[0];
        dir->root = 0;
        ret = 0;
    }
    return(ret);
}


int
vfs_dir_close(VfsDir_t* dir)
{
    int ret = -EBADF;

    switch (dir->filesys->type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_closedir(&dir->ff));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_dir_close(dir->filesys->lfs.fs, &dir->lfs);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = 0;
			break;
		}
	#endif
    }
    dir->filesys = nullptr;
    return(ret);
}


int
vfs_dir_read(VfsDir_t* dir, VfsInfo_t* info)
{
    switch (dir->filesys->type & ~FS_FIXED)
    {
        case FS_ROOT:
        {
            if (vFileSystem[dir->root].drive != nullptr)
            {
                info->inode = vFileSystem[dir->root].index << INODE_ITEM_BITS;
                info->device = vFileSystem[dir->root].index;
                info->created = info->modified = 0;
                info->attrib = ATR_DIR | ATR_IREAD | ATR_IWRITE | ATR_IEXEC;
                switch (vFileSystem[dir->root].type & ~FS_FIXED)
                {
                #ifdef USE_FATFS
                    case FS_FATFS:
                    {
                        FATFS* ff;
                        DWORD nclst;
                        f_getfree(vFileSystem[dir->root].drive, &nclst, &ff);
                        info->blocksize = ff->csize * FF_MAX_SS;
                        info->blocks = ff->n_fatent - 2;
                        info->size = (info->blocks - nclst) * (uint64_t)info->blocksize;
                        break;
                    }
                #endif
                #ifdef USE_LITTLEFS
                    case FS_LITTLEFS:
                    {
                        info->blocksize = vFileSystem[dir->root].lfs.cfg->block_size;
                        info->blocks = vFileSystem[dir->root].lfs.cfg->block_count;
                        info->size = lfs_fs_size(vFileSystem[dir->root].lfs.fs) * (uint64_t)info->blocksize;
                        lfs_getattr(vFileSystem[dir->root].lfs.fs, "", LFS_ATTR_CREATE, &info->created, sizeof(info->created));
                        break;
                    }
                #endif
				#ifdef USE_JESFS
            		case FS_JESFS:
            		{
						info->blocksize = SF_SECTOR_PH;
						info->blocks = (sflash_info.total_flash_size + SF_SECTOR_PH - 1) / SF_SECTOR_PH;
						info->size = sflash_info.total_flash_size - sflash_info.available_disk_size;
						info->created = sflash_info.creation_date;
						info->modified = sflash_info.creation_date;
		                info->attrib |= ATR_FLAT_FILESYSTEM;
						break;
            		}
				#endif
                }
                sniprintf(info->name, sizeof(info->name), "%s", vFileSystem[dir->root].drive);
                dir->root++;
                return(0);
            }
            break;
        }
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            FILINFO fno = {0};

		#if _USE_LFN && (_FFCONF != 68300)
			fno.lfname = info->name;
			fno.lfsize = sizeof(info->name);
		#endif

            if (f_readdir(&dir->ff, &fno) == FR_OK)
            {
                if (fno.fname[0] == '\0')
                {
                    break;
                }
                info->inode = fno.inode | (dir->filesys->index << INODE_ITEM_BITS);
                info->device = dir->filesys->index;
                info->size = fno.fsize;
                info->blocksize = dir->filesys->fatfs.fs->csize * FF_MAX_SS;
                info->blocks = (fno.fsize + info->blocksize - 1) / info->blocksize;
                info->created = _ff_timestamp(&fno, false);
                info->modified = _ff_timestamp(&fno, true);
                info->attrib = ATR_IREAD | ATR_IEXEC;
                info->attrib |= fno.fattrib & AM_DIR ? ATR_DIR : ATR_REG;
                if (!(fno.fattrib & AM_RDO))
                {
                    info->attrib |= ATR_IWRITE;
                }
                if (fno.fattrib & AM_HID)
                {
                    info->attrib |= ATR_HID;
                }
			#if _USE_LFN && (_FFCONF != 68300)
                if (fno.lfname[0] == '\0')
			#endif
                {
                    sniprintf(info->name, sizeof(info->name), "%s", fno.fname);
                }
                return(0);
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            struct lfs_info lfs;

            if (lfs_dir_read(dir->filesys->lfs.fs, &dir->lfs, &lfs) == true)
            {
//xxx                info->inode = _lfs_inode(dir->filesys, dir, nullptr);
                info->device = dir->filesys->index;
                info->size = lfs.size;
                info->blocksize = dir->filesys->lfs.cfg->block_size;
                info->blocks = (info->size + info->blocksize - 1) / info->blocksize;
                // Temporary abuse file name to create a full path in order to query file attributes
                sniprintf(info->name, sizeof(info->name), "%s/%s", dir->lfspath, lfs.name);
                lfs_getattr(dir->filesys->lfs.fs, info->name, LFS_ATTR_CREATE, &info->created, sizeof(info->created));
                lfs_getattr(dir->filesys->lfs.fs, info->name, LFS_ATTR_MODIFY, &info->modified, sizeof(info->modified));
                info->attrib = ATR_IREAD | ATR_IWRITE | ATR_IEXEC;
                info->attrib |= lfs.type & LFS_TYPE_DIR ? ATR_DIR : ATR_REG;
                sniprintf(info->name, sizeof(info->name), "%s", lfs.name);
                return(0);
            }
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			FS_STAT stat = {0};
			int16_t res;

			do
			{
				info->inode = dir->fno | (dir->filesys->index << INODE_ITEM_BITS);
				if (res = fs_info(&stat, dir->fno++), res & FS_STAT_ACTIVE)
				{
					strcpy(info->name, stat.fname);
					info->device = dir->filesys->index;
					info->size = stat.file_len;
					info->blocksize = SF_SECTOR_PH;
					info->blocks = (info->size + SF_SECTOR_PH - 1) / SF_SECTOR_PH;
					info->created = stat.file_ctime;
					info->modified = stat.file_ctime;
					info->attrib = ATR_IREAD | ATR_IEXEC | ATR_REG | ATR_IWRITE;
					return(0);
				}
			}
			while (res != 0);
		}
	#endif
    }
    return(-1);
}


int
vfs_findfirst(VfsDir_t* dir, VfsInfo_t* info, const char* path, const char* pattern)
{
    int ret;

    if (ret = vfs_dir_open(dir, path), ret == 0)
    {
        dir->pattern = pattern;
        if (ret = vfs_findnext(dir, info), ret != 0)
        {
            vfs_dir_close(dir);
        }
    }
    return(ret);
}


static bool
pattern_matching(const char* pat, const char* nam, int skip, int inf)
{
    const char* pp;
    const char* np;
    char pc, nc;
    int nm, nx;

    while (skip-- != 0) 				/* Pre-skip name chars */
    {
        if (!toupper(*nam++))
        {
            return 0;	/* Branch mismatched if less name chars */
        }
    }
    if ((*pat == '\0') && (inf != 0))
    {
        return(true);		/* (short circuit) */
    }

    do
    {
        pp = pat; np = nam;			/* Top of pattern and name to match */
        for (;;)
        {
            if ((*pp == '?') || (*pp == '*'))	/* Wildcard? */
            {
                nm = nx = 0;
                do 		/* Analyze the wildcard chars */
                {
                    if (*pp++ == '?')
                    {
                        nm++;
                    }
                    else
                    {
                        nx = 1;
                    }
                }
                while ((*pp == '?') || (*pp == '*'));
                if (pattern_matching(pp, np, nm, nx))	/* Test new branch (recurs upto number of wildcard blocks in the pattern) */
                {
                     return(true);
                }
                nc = *np;
                break;	/* Branch mismatched */
            }
            pc = toupper(*pp++);	/* Get a pattern char */
            nc = toupper(*np++);	/* Get a name char */
            if (pc != nc) 	/* Branch mismatched? */
            {
                break;
            }
            if (pc == 0) 		/* Branch matched? (matched at end of both strings) */
            {
                return(true);
            }
        }
        toupper(*nam++);			/* nam++ */
    }
    while ((inf != 0) && (nc != 0));			/* Retry until end of name if infinite search is specified */

    return(false);
}


int
vfs_findnext(VfsDir_t* dir, VfsInfo_t* info)
{
    int ret;

    while (ret = vfs_dir_read(dir, info), ret == 0)
    {
        if (pattern_matching(dir->pattern, info->name, 0, 0))
        {
            break;
        }
    }
    return(ret);
}


int
vfs_mkdir(const char* path)
{
    int i, ret = -ENOENT;
#ifdef MTP_EVENTS
    uint32_t handle;
#endif

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_mkdir(path));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            path = _lfs_fix_path(&vFileSystem[i], path);
            if (ret = lfs_mkdir(vFileSystem[i].lfs.fs, path), ret == 0)
            {
                // LFS timestamp
                time_t t = time(nullptr);
                lfs_setattr(vFileSystem[i].lfs.fs, path, LFS_ATTR_CREATE, &t, sizeof(t));
                lfs_setattr(vFileSystem[i].lfs.fs, path, LFS_ATTR_MODIFY, &t, sizeof(t));
            }
            break;
        }
    #endif
    }
#ifdef MTP_EVENTS
    if (ret == 0)
    {
        // TODO create handle
        PtpEvent(PTP_EVENT_OBJECT_ADDED, handle);
    }
#endif
    return(ret);
}


int
vfs_remove(const char* path)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_unlink(path));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            path = _lfs_fix_path(&vFileSystem[i], path);
            ret = lfs_remove(vFileSystem[i].lfs.fs, path);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			FS_DESC jes;

			path = _jes_fix_path(&vFileSystem[i], path);
			if (fs_open(&jes, (char*)path, SF_OPEN_RAW) == 0)
			{
				ret = _jes_errno(fs_delete(&jes));
			}
			break;
		}
	#endif
    }
#ifdef MTP_EVENTS
    if (file->created)
    {
        // TODO Create handle
        PtpEvent(PTP_EVENT_OBJECT_REMOVED, GetFileHandle());
    }
#endif
    return(ret);
}


int
vfs_rename(const char* oldpath, const char* newpath)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(oldpath, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_rename(oldpath, newpath));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            oldpath = _lfs_fix_path(&vFileSystem[i], oldpath);
            newpath = _lfs_fix_path(&vFileSystem[i], newpath);
            ret = lfs_rename(vFileSystem[i].lfs.fs, oldpath, newpath);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			FS_DESC old, new;

			oldpath = _jes_fix_path(&vFileSystem[i], oldpath);
            _jes_truncate_path(newpath);
			newpath = _jes_fix_path(&vFileSystem[i], newpath);
			if (fs_open(&old, (char*)oldpath, SF_OPEN_RAW) == 0)	//XXX probably needs a bit of path manipulation
			{
				if (fs_open(&new, (char*)newpath, SF_OPEN_CREATE) == 0)	//XXX probably needs a bit of path manipulation
				{
					ret = _jes_errno(fs_rename(&old, &new));
				}
			}
			break;
		}
	#endif
    }
    return(ret);
}


int
vfs_copy(const char* source, const char* dest)
{
    int ret;
    VfsFile_t vSrc;
    VfsFile_t vDst;
    VfsInfo_t info;
    unsigned char buf[128];

    if (ret = vfs_file_open(&vSrc, source, VFS_RDONLY), ret == 0)
    {
        vfs_stat(source, &info);

        strcpy((char*)buf, dest);
        if (dest[strlen(dest) - 1] == '/')
        {
            strcat((char*)buf, strrchr(source, '/') + 1);
        }
        else if (dest[strlen(dest) - 1] == ':')
        {
            strcat((char*)buf, strchr(source, '/'));
        }

        if (ret = vfs_file_open(&vDst, (char*)buf, VFS_WRONLY | VFS_CREAT | VFS_TRUNC), ret == 0)
        {
            // Copy file attributes
            vfs_touch((char*)buf, &info);

            while (ret = vfs_file_read(&vSrc, buf, sizeof(buf)), ret > 0)
            {
                if (ret = vfs_file_write(&vDst, buf, ret), ret < 0)
                {
                    // Abort on write error
                    break;
                }
            }
            vfs_file_close(&vDst);
        }
        vfs_file_close(&vSrc);
    }
    return(ret);
}


int
vfs_stat(const char* path, VfsInfo_t* info)
{
    int ret = -ENOENT;
    int i;

    memset(info, 0, sizeof(VfsInfo_t));

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            FILINFO fno = {0};
            DWORD nclst;

            if ((path[vFileSystem[i].namelen] == '\0') || ((path[vFileSystem[i].namelen] == '/') && (path[vFileSystem[i].namelen + 1] == '\0')) || ((path[vFileSystem[i].namelen] == '\\') && (path[vFileSystem[i].namelen + 1] == '\0')))
            {
                info->device = vFileSystem[i].index;
                info->attrib = ATR_DIR | ATR_IREAD | ATR_IWRITE;
                // Add attribute for REMOVABLE
                if (!(vFileSystem[i].type & FS_FIXED))
                {
                    info->attrib |= ATR_REMOVABLE_DISK;
                }
                // Test root
                if (ret = _ff_errno(f_getfree(path, &nclst, &vFileSystem[i].fatfs.fs)), ret == 0)
                {
                    // Use volume label for name?
                #if FF_USE_LABEL
                    f_getlabel(path, info->name, nullptr);
                #endif
                    info->blocksize = vFileSystem[i].fatfs.fs->csize * FF_MAX_SS;
                    info->blocks = vFileSystem[i].fatfs.fs->n_fatent - 2;
                    info->size = (info->blocks - nclst) * (uint64_t)info->blocksize;
                }
                if (info->name[0] == '\0')
                {
                    strcpy(info->name, path);
                }
            }
            else if (ret = _ff_errno(f_stat(path, &fno)), ret == 0)
            {
                strcpy(info->name, strrchr(path, '/') + 1);
                info->device = vFileSystem[i].index;
                info->size = fno.fsize;
                info->blocksize = vFileSystem[i].fatfs.fs->csize * FF_MAX_SS;
                info->blocks = (info->size + info->blocksize - 1) / info->blocksize;
                info->created = _ff_timestamp(&fno, false);
                info->modified = _ff_timestamp(&fno, true);

                if (fno.fattrib & AM_DIR)
                {
                    info->attrib = ATR_DIR;
                }
                else
                {
                    info->attrib = ATR_REG;
                }

                info->attrib |= ATR_IREAD;
                if (!(fno.fattrib & AM_RDO))
                {
                    info->attrib |= ATR_IWRITE;
                }
                if (fno.fattrib & AM_HID)
                {
                    info->attrib = ATR_HID;
                }
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            struct lfs_info fno;

            path = _lfs_fix_path(&vFileSystem[i], path);

            if ((path[0] == '\0') || ((path[0] == '/') && (path[1] == '\0')))
            {
                strcpy(info->name, path);
                info->device = vFileSystem[i].index;
                info->attrib = ATR_DIR | ATR_IREAD | ATR_IWRITE;
                // Add attribute for REMOVABLE
                if (!(vFileSystem[i].type & FS_FIXED))
                {
                    info->attrib |= ATR_REMOVABLE_DISK;
                }
                // Test root
                if (fno.size = lfs_fs_size(vFileSystem[i].lfs.fs), fno.size >= 0)
                {
                    info->blocksize = vFileSystem[i].lfs.cfg->block_size;
                    info->blocks = vFileSystem[i].lfs.cfg->block_count;
                    info->size = fno.size * (uint64_t)info->blocksize;
                    return(0);
                }
            }
            else if (ret = lfs_stat(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], &fno), ret >= 0)
			{
				strcpy(info->name, strrchr(path, '/') + 1);
				info->device = vFileSystem[i].index;
				info->size = fno.size;
				info->blocksize = vFileSystem[i].lfs.cfg->block_size;
				info->blocks = (info->size + info->blocksize - 1) / info->blocksize;
				lfs_getattr(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], LFS_ATTR_CREATE, &info->created, sizeof(info->created));
				lfs_getattr(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], LFS_ATTR_MODIFY, &info->modified, sizeof(info->modified));

				if (fno.type & LFS_TYPE_DIR)
				{
					info->attrib = ATR_DIR;
				}
				else
				{
					info->attrib = ATR_REG;
				}
				info->attrib |= ATR_IREAD | ATR_IWRITE;
				return(0);
			}
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			FS_DESC jes;

			path = _jes_fix_path(&vFileSystem[i], path);
            if ((path[0] == '\0') || ((path[0] == '/') && (path[1] == '\0')))
            {
            	strcpy(info->name, vFileSystem[i].jesfs.label);
				info->blocksize = SF_SECTOR_PH;
				info->blocks = (sflash_info.total_flash_size + SF_SECTOR_PH - 1) / SF_SECTOR_PH;
				info->size = sflash_info.total_flash_size - sflash_info.available_disk_size;
				info->created = sflash_info.creation_date;
				info->modified = sflash_info.creation_date;
                info->attrib = ATR_DIR | ATR_IREAD | ATR_IWRITE | ATR_FLAT_FILESYSTEM;
                return(0);
            }
			else if (ret = _jes_errno(fs_open(&jes, (char*)path, SF_OPEN_READ)), ret == 0)
			{
				// Open file and set struct values
				strcpy(info->name, path);
				info->device = vFileSystem[i].index;
				info->size = jes.file_len;
				info->blocksize = SF_SECTOR_PH;
				info->blocks = (info->size + SF_SECTOR_PH - 1) / SF_SECTOR_PH;
				info->created = jes.file_ctime;
				info->modified = jes.file_ctime;
				info->attrib = ATR_REG | ATR_IREAD | ATR_IWRITE;
				fs_close(&jes);
				return(0);
			}
			break;
		}
	#endif
    }
    return(ret);
}


int
vfs_touch(const char* path, const VfsInfo_t* info)
{
    int i, ret = 0;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(-1);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            FILINFO fno = {0};

            if (info->attrib != 0)
            {
                //TODO Update file attributes

                if (info->attrib != 0)
                {
                    BYTE attr = 0;

                    if (info->attrib & ATR_HID)
                    {
                        attr |= AM_HID;
                    }
                    if (info->attrib & ATR_SYS)
                    {
                        attr |= AM_SYS;
                    }
                    if (f_chmod(path, attr, AM_HID | AM_SYS) != FR_OK)
                    {
                        ret = -1;
                    }
                }
            }
            // Update timestamp
            _ff_fat_timedate(&fno, info->created, false);
            _ff_fat_timedate(&fno, info->modified, true);
		#ifdef FATFS_CREATE_DT
            if (fno.fcdate | fno.fctime | fno.fdate | fno.ftime)
		#else
			if (fno.fdate | fno.ftime)
		#endif
            {
                if (f_utime(path, &fno) != FR_OK)
                {
                    ret = -1;
                }
            }
            return(ret);
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            struct lfs_info fno;

            path = _lfs_fix_path(&vFileSystem[i], path);
// TODO update file attributes and time stamps
            if (lfs_stat(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], &fno) >= 0)
            {
                strcpy(info->name, strrchr(path, '/') + 1);
                ret = lfs_setattr(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], LFS_ATTR_CREATE, &info->created, sizeof(info->created));
                if ((lfs_setattr(vFileSystem[i].lfs.fs, &path[vFileSystem[i].namelen + 1], LFS_ATTR_MODIFY, &info->modified, sizeof(info->modified)) < 0) || (ret < 0))
                {
                    ret = -1;
                }
            }
            break;
        }
    #endif
	#ifdef USE_JESFS
#if 0	// Does not work when the file is already opened
		case FS_JESFS:
		{
			FS_DESC fil;

			path = _jes_fix_path(&vFileSystem[i], path);
			if (fs_open(&fil, (char*)path, SF_OPEN_WRITE) == 0)
			{
				// We can only set the creation time if created with SF_OPEN_WAIT_FOR_CTIME
				if (fil.file_ctime == 0xFFFFFFFF)
				{
					fil.file_ctime = MAX(info->created, info->modified);
				}
				ret = _jes_errno(fs_close(&fil));
			}
			break;
		}
#endif
	#endif
    }
    return(ret);
}


int
vfs_crc(const char* path, uint32_t* crc)
{
    int ret;
    union
    {
        VfsInfo_t info;
        VfsFile_t src;
    } x;
    unsigned char buf[128];

    if (ret = vfs_stat(path, &x.info), ret == 0)
    {
    	CRC_FUNC(crc, (uint32_t*)&x.info.size, sizeof(x.info.size) / sizeof(uint32_t), true);
#if 0   // TODO Copy file attributes  (in vfs_copy) before including timestamp in CRC
        CRC_FUNC(crc, (uint32_t*)&x.info.created, sizeof(x.info.created) / sizeof(uint32_t), false);
#endif
        if (ret = vfs_file_open(&x.src, path, VFS_RDONLY), ret == 0)
        {
            while (ret = vfs_file_read(&x.src, buf, sizeof(buf)), ret > 0)
            {
                if (ret < sizeof(buf))
                {
                    memset(buf + ret, 0, sizeof(buf) - ret);
                }
                CRC_FUNC(crc, (uint32_t*)&buf, ret / sizeof(uint32_t), false);
            }
            vfs_file_close(&x.src);
        }
    }
    return(ret);
}


int64_t
vfs_fs_size(const char* path)
{
    int i;
    int64_t ret = 0;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            FATFS* ff;
            DWORD nclst;
            if (f_getfree(path, &nclst, &ff) == FR_OK)
            {
                ret = (int64_t)nclst * vFileSystem[i].fatfs.fs->csize * FF_MAX_SS;
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = (int64_t)vFileSystem[i].lfs.cfg->block_size * vFileSystem[i].lfs.cfg->block_count;
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = sflash_info.total_flash_size;
			break;
		}
	#endif
    }
    return(ret);
}


int64_t
vfs_fs_free(const char* path)
{
    int i;
    int64_t ret = 0;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            FATFS* ff;
            DWORD nclst;
            if (f_getfree(path, &nclst, &ff) == FR_OK)
            {
                ret = (int64_t)nclst * vFileSystem[i].fatfs.fs->csize * FF_MAX_SS;
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
/* TODO               if (fno.size = lfs_fs_size(vFileSystem[i].lfs.fs), fno.size >= 0)
                {
                    info->inode = vFileSystem[i].index << 29;
                    info->device = vFileSystem[i].index;
                    info->blocksize = vFileSystem[i].lfs.cfg->block_size;
                    info->blocks = vFileSystem[i].lfs.cfg->block_count;
                    info->size = fno.size * info->blocksize;
                }*/
            ret = (int64_t)vFileSystem[i].lfs.cfg->block_size * vFileSystem[i].lfs.cfg->block_count;
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = sflash_info.available_disk_size;
			break;
		}
	#endif
    }
    return(ret);
}


const char*
vfs_fs_type(const char* path)
{
    int i;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return("error");
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
        	return("FatFS");
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
        	return("LittleFS");
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
        	return("JesFS");
		}
	#endif
    }
    return("FS?");
}


int
vfs_getlabel(const char* path, char* label)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(path, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_getlabel(path, label, nullptr));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_getattr(vFileSystem[i].lfs.fs, vFileSystem[i].drive, LFS_ATTR_LABEL, label, MAX_PATH);
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			strcpy(label, vFileSystem[i].jesfs.label);
			ret = 0;
			break;
		}
	#endif
    }
    return(ret);
}


int
vfs_setlabel(const char* label)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(label, false), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_setlabel(label));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            ret = lfs_setattr(vFileSystem[i].lfs.fs, vFileSystem[i].drive, LFS_ATTR_LABEL, &label, strlen(label));
            break;
        }
    #endif
    }
    return(ret);
}


int
vfs_mount(const char* path, bool mount)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(path, true), i < 0)
    {
        return(i);
    }

    // Try mounting according table entry
    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            if (mount)
            {
                if (vFileSystem[i].fatfs.fs == nullptr)
                {
                    vFileSystem[i].fatfs.fs = vfs_malloc(sizeof(FATFS));
                    ret = -ENOMEM;
                }
                if (vFileSystem[i].fatfs.fs != nullptr)
                {
                    vFileSystem[i].index = i + 1;
                    // Do full check on fixed volumes
                    if (ret = _ff_errno(f_mount(vFileSystem[i].fatfs.fs, vFileSystem[i].drive, vFileSystem[i].type & FS_FIXED ? 1 : 0)), ret != 0)
                    {
                        vFileSystem[i].index = 0;
                    }
                }
            }
            else
            {
                if (vFileSystem[i].fatfs.fs != nullptr)
                {
                    ret = _ff_errno(f_mount(nullptr, vFileSystem[i].drive, 0));
                    vFileSystem[i].index = 0;
				#ifdef vfs_malloc_usable_size
                    if (vfs_malloc_usable_size(vFileSystem[i].fatfs.fs) != 0)
				#endif
                    {
                        vfs_free(vFileSystem[i].fatfs.fs);
                        vFileSystem[i].fatfs.fs = nullptr;
                    }
                }
            }
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            if (mount)
            {
                if (vFileSystem[i].lfs.fs == nullptr)
                {
                    vFileSystem[i].lfs.fs = vfs_malloc(sizeof(lfs_t));
                    ret = -ENOMEM;
                }
                if (vFileSystem[i].lfs.fs != nullptr)
                {
                //    memset(vFileSystem[i].lfs, 0, sizeof(lfs_t));
                    vFileSystem[i].index = i + 1;
                    if (ret = lfs_mount(vFileSystem[i].lfs.fs, vFileSystem[i].lfs.cfg), ret != 0)
                    {
                        ret= -ENXIO;
                        vFileSystem[i].index = 0;
                    }
                }
            }
            else
            {
                if (vFileSystem[i].lfs.fs != nullptr)
                {
                    ret = lfs_unmount(vFileSystem[i].lfs.fs);
                    vFileSystem[i].index = 0;
                    if (vfs_malloc_usable_size(vFileSystem[i].lfs.fs) != 0)
                    {
                        vfs_free(vFileSystem[i].lfs.fs);
                        vFileSystem[i].lfs.fs = nullptr;
                    }
                }
            }
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			if (ret = _jes_errno(fs_start(FS_START_NORMAL)), ret == 0)
			{
                vFileSystem[i].index = i + 1;
			}
			break;
		}
	#endif
    }
    if (ret == 0)
    {
        if (vFileSystem[i].eventcb != nullptr)
        {
            (vFileSystem[i].eventcb)(&vFileSystem[i], vFileSystem[i].index == 0 ? EVT_UNMOUNT : EVT_MOUNT);
        }
    #ifdef MTP_EVENTS
        PtpEvent(vFileSystem[i].index == 0 ? PTP_EVENT_STORE_REMOVED : PTP_EVENT_STORE_ADDED, STORAGE_ID(i));
    #endif
    }
    else if (mount && (vFileSystem[i].eventcb != nullptr))
    {
        (vFileSystem[i].eventcb)(&vFileSystem[i], EVT_MOUNT_FAIL);
    }
    return(ret);
}


int
vfs_format(const char* path)
{
    int i, ret = -ENOENT;

    if (i = _vfs_find_entry(path, true), i < 0)
    {
        return(i);
    }

    // Try mounting according table entry
    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
            ret = _ff_errno(f_mkfs(path, 0, 0));
            break;
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            if (ret = lfs_format(vFileSystem[i].lfs.fs, vFileSystem[i].lfs.cfg), ret == 0)
            {
                time_t t = time(nullptr);
                lfs_setattr(vFileSystem[i].lfs.fs, vFileSystem[i].drive, LFS_ATTR_CREATE, &t, sizeof(t));
            }
            break;
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			ret = _jes_errno(fs_format(FS_FORMAT_SOFT));
			break;
		}
	#endif
    }
    return(ret);
}


char*
vfs_volume(int num)
{
    int i;

    for (i = 0; vFileSystem[i].drive != nullptr; i++)
    {}
    if (num < i)
    {
        return(vFileSystem[num].drive);
    }
    return(nullptr);
}


int
vfs_check_fs_mutex(const char* path)
{
    int i;

    if (i = _vfs_find_entry(path, true), i < 0)
    {
        return(i);
    }

    switch (vFileSystem[i].type & ~FS_FIXED)
    {
    #ifdef USE_FATFS
        case FS_FATFS:
        {
		#if _FS_REENTRANT
            return(uxSemaphoreGetCount(vFileSystem[i].fatfs.fs->sobj) != 0 ? 0 : -EBUSY);
		#else
            return(0);
		#endif
        }
    #endif
    #ifdef USE_LITTLEFS
        case FS_LITTLEFS:
        {
            // TODO Check for LFS filesystem mutex
            return(0);
        }
    #endif
	#ifdef USE_JESFS
		case FS_JESFS:
		{
			//TODO ret = _jes_errno(fs_format(FS_FORMAT_SOFT);
			break;
		}
	#endif
    }
    return(-ENOENT);
}


#if (VFS_POSIX == 1)
/* Posix compatible wrapper functions around FatFS and LittleFS filesystems */

FILE* fopen(const char* restrict pathname, const char* restrict mode)
{
    VfsFile_t* file = vfs_malloc(sizeof(VfsFile_t));
    int flags = 0;

    if (strchr(mode, 'r') != nullptr)
    {
        flags |= VFS_RDONLY;
    }

    if (strchr(mode, 'w') != nullptr)
    {
        flags |= VFS_WRONLY | VFS_TRUNC;
    }
    else if (strchr(mode, 'a') != nullptr)
    {
        flags |= VFS_WRONLY | VFS_APPEND;
    }

    if (file == nullptr)
    {
        errno = ENOMEM;
    }
    else
    {
        errno = -vfs_file_open(file, pathname, flags);
    }

    return((FILE*)file);
}


int _fclose(FILE* stream)
{
    errno = -vfs_file_close((VfsFile_t*)stream);
    vfs_free(stream);
    return(errno);
}


int _fflush(FILE* stream)
{
    errno = -vfs_file_sync((VfsFile_t*)stream);
    return(errno);
}


int fseek(FILE* stream, long offset, int whence)
{
    errno = -vfs_file_seek((VfsFile_t*)stream, offset, whence);
    return(errno);
}


long ftell(FILE* stream)
{
    return(vfs_file_tell((VfsFile_t*)stream));
}


int feof(FILE* stream)
{
    return(vfs_file_tell((VfsFile_t*)stream) == vfs_file_size((VfsFile_t*)stream));
}


void rewind(FILE* stream)
{
    vfs_file_rewind((VfsFile_t*)stream);
}


size_t fread(void* restrict ptr, size_t size, size_t nmemb, FILE* restrict stream)
{
    long rv = vfs_file_read((VfsFile_t*)stream, ptr, size * nmemb);
    if (rv >= 0)
    {
        return((size_t)rv);
    }
    return(0);
}


size_t fwrite(const void* restrict ptr, size_t size, size_t nmemb, FILE* stream)
{
    long rv = vfs_file_write((VfsFile_t*)stream, ptr, size * nmemb);
    if (rv >= 0)
    {
        return((size_t)rv);
    }
    return(0);
}


char* fgets(char* str, int n, FILE* stream)
{
    vfs_gets(str, n, (VfsFile_t*)stream);
    return(str);
}


int getc(FILE* stream)
{
    return(vfs_getc((VfsFile_t*)stream));
}


int fputs(const char* str, FILE* stream)
{
    return(vfs_puts(str, (VfsFile_t*)stream));
}


int fputc(int ch, FILE* stream)
{
    return(vfs_putc(ch, (VfsFile_t*)stream));
}


/* Some additional posix file functions */

int ftruncate(int fildes, off_t length)
{
    errno = -vfs_file_truncate((VfsFile_t*)fildes, length);
    return(errno ? -1 : 0);
}


int stat(const char* restrict path, struct stat* restrict buf)
{
    VfsInfo_t info = {0};

    if (vfs_stat(path, &info) == 0)
    {
        memset(buf, 0, sizeof(struct stat));

        buf->st_ino = info.inode;
        buf->st_dev = info.device;
        buf->st_blksize = info.blocksize;
        buf->st_blocks = info.blocks;
        buf->st_size = info.size;
        buf->st_mode = info.attrib;
        buf->st_mtime = info.modified;
        buf->st_ctime = info.created;
        return(0);
    }
    return(-1);
}


int mkdir(const char* path, mode_t mode)
{
    (void)mode;
    return(-vfs_mkdir(path));
}


int remove(const char* filename)
{
    return(-vfs_remove(filename));
}


int rename(const char* oldname, const char* newname)
{
    return(-vfs_rename(oldname, newname));
}


// Wrappers for directory listings.
DIR* opendir(const char* path)
{
    VfsDir_t* dir = vfs_malloc(sizeof(VfsDir_t));
    int ret;

    if (dir != nullptr)
    {
        if (ret = vfs_dir_open(dir, path), ret < 0)
        {
            errno = ret;
            vfs_free(dir);
            dir = nullptr;
        }
    }
    return((DIR*)dir);
}


int closedir(DIR* dir)
{
    errno = -vfs_dir_close((VfsDir_t*)dir);
    return(errno);
}


struct dirent* readdir(DIR* dir)
{
    static struct dirent direntry;

    VfsInfo_t info;

    if (vfs_dir_read((VfsDir_t*)dir, &info) == 0)
    {
        direntry.d_ino |= info.inode;
        sniprintf(direntry.d_name, sizeof(direntry.d_name), "%s", info.name);
        return(&direntry);
    }
    return(nullptr);
}

#endif // VFS_POSIX


#if (VFS_CLI == 1)

/* CLI Command system */

static BaseType_t
DirCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    static VfsDir_t* dir = nullptr;

    int err = EDOOFUS;

    if (dir == nullptr)
    {
        if (dir = vfs_malloc(sizeof(VfsDir_t)), dir == nullptr)
        {
            err = ENOMEM;
        }
        else if (err = -vfs_dir_open(dir, (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, nullptr)), err != 0)
        {
            //vfs_dir_close(dir);
            vfs_free(dir);
            dir = nullptr;
        }
        else
        {
            dir->pattern = nullptr;
        }
    }
    if (dir != nullptr)
    {
        VfsInfo_t info;

        if (vfs_dir_read(dir, &info) == 0)
        {
            // TODO add more listing data
            if (info.attrib & ATR_DIR)
            {
                sniprintf(pcWriteBuffer, xWriteBufferLen, "%-.40s <DIR>\r\n", info.name);
            }
            else
            {
                sniprintf(pcWriteBuffer, xWriteBufferLen, "%-.40s %-llu\r\n", info.name, info.size);
            }
            (unsigned long)dir->pattern++;
            return(pdTRUE);
        }
        else
        {
            sniprintf(pcWriteBuffer, xWriteBufferLen, "%lu items\r\n", (unsigned long)dir->pattern);
            vfs_dir_close(dir);
            vfs_free(dir);
            dir = nullptr;
            return(pdFALSE);
        }
    }

    if (err != 0)
    {
        sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(err));
    }
    return(pdFALSE);
}


static BaseType_t
MkdirCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    char* pPath = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, nullptr);

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-vfs_mkdir(pPath)));
    return(pdFALSE);
}


static BaseType_t
DelCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    char* pPath = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, nullptr);

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-vfs_remove(pPath)));
    return(pdFALSE);
}


static BaseType_t
RenCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    BaseType_t len;
    char* pSrc = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, &len);
    char* pDst = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 2, nullptr);

    if (pDst != nullptr)	// Chop string
    {
        pSrc[len] = '\0';
    }

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-vfs_rename(pSrc, pDst)));
    return(pdFALSE);
}


static BaseType_t
CopyCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    BaseType_t len;
    char* pSrc = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, &len);
    char* pDst = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 2, nullptr);

    if (pDst != nullptr)	// Chop string
    {
        pSrc[len] = '\0';
    }

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-vfs_copy(pSrc, pDst)));
    return(pdFALSE);
}


static BaseType_t
MoveCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    BaseType_t len;
    char* pSrc = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, &len);
    char* pDst = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 2, nullptr);

    if (pDst != nullptr)	// Chop string
    {
        pSrc[len] = '\0';
    }

    int err = vfs_copy(pSrc, pDst);

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-err));
    if (err == 0)
    {
        vfs_remove(pSrc);
    }
    return(pdFALSE);
}


static BaseType_t
FormatCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    char* pPath = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, nullptr);

    sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(-vfs_format(pPath)));
    return(pdFALSE);
}


static BaseType_t
TypeCmd(const char* pcCommandString, uint8_t nParameters, char* pcWriteBuffer, size_t xWriteBufferLen)
{
    static VfsFile_t* file = nullptr;

    int err;

    if (file == nullptr)
    {
        char* pPath = (char*)FreeRTOS_CLIGetParameter(pcCommandString, 1, nullptr);

        if (pPath == nullptr)
        {
            err = EINVAL;
        }
        else
        {
            if (file = vfs_malloc(sizeof(VfsFile_t)), file == nullptr)
            {
                err = ENOMEM;
            }
            else if (err = -vfs_file_open(file, pPath, VFS_RDONLY), err != 0)
            {
                vfs_file_close(file);
                vfs_free(file);
                file = nullptr;
            }
        }
    }
    if (file != nullptr)
    {
        vfs_gets(pcWriteBuffer, xWriteBufferLen, file);
        if (vfs_file_tell(file) == vfs_file_size(file))
        {
            vfs_file_close(file);
            vfs_free(file);
            file = nullptr;
            return(pdFALSE);
        }
        return(pdTRUE);
    }

    if (err != 0)
    {
        sniprintf(pcWriteBuffer, xWriteBufferLen, "%s\r\n", strerror(err));
    }
    return(pdFALSE);
}


__attribute__ ((section(".clicommands"), used))
static const CLI_Command_Definition_t vVfsCommands[] =
{
    {&vTrue, "DIR <path>: List directory\r\n", DirCmd, 1},
    {&vTrue, "MKDIR <path>: Make directory\r\n", MkdirCmd, 1},
    {&vTrue, "DEL <path>: Delete file or directory\r\n", DelCmd, 1},
    {&vTrue, "REN <path>,<name>: Rename file or directory\r\n", RenCmd, 2},
    {&vTrue, "COPY <path>,<path>: Copy file or directory\r\n", CopyCmd, 2},
    {&vTrue, "MOVE <path>,<path>: Move file or directory\r\n", MoveCmd, 2},
    {&vTrue, "FORMAT <drive>: Format disk\r\n", FormatCmd, 1},
    {&vTrue, "TYPE <path>: Show file content\r\n", TypeCmd, 1},
};

#endif // VFS_CLI


int
vfs_init(void)
{
    int i, err;

    for (i = 0; vFileSystem[i].drive != nullptr; i++)
    {
        vFileSystem[i].namelen = strlen(vFileSystem[i].drive);

    #ifdef USE_LITTLEFS
        if ((vFileSystem[i].type & ~FS_FIXED) == FS_LITTLEFS)
        {
#include "jesfs.h"

        	sflash_spi_init();
        	sflash_info.total_flash_size = 256 * 4096;

			if (vFileSystem[i].lfs.ioctl != nullptr)
			{
				size_t val;

				if ((vFileSystem[i].lfs.ioctl)(vFileSystem[i].lfs.cfg, GET_SECTOR_SIZE, &val) == 0)
				{
					vFileSystem[i].lfs.cfg->block_count = val;
				}

				if ((vFileSystem[i].lfs.ioctl)(vFileSystem[i].lfs.cfg, GET_SECTOR_COUNT, &val) == 0)
				{
					vFileSystem[i].lfs.cfg->block_count *= val;
				}

				if ((vFileSystem[i].lfs.ioctl)(vFileSystem[i].lfs.cfg, GET_BLOCK_SIZE, &val) == 0)
				{
					vFileSystem[i].lfs.cfg->block_count /= val;
					vFileSystem[i].lfs.cfg->block_size = val;
				}
			}
        }
    #endif

        // Mount disk
        if (err = -vfs_mount(vFileSystem[i].drive, true), err == ENXIO)
        {
#if 0
            if (vFileSystem[i].type & FS_FIXED)
            {
                // Format if mount failed
                err = -vfs_format(vFileSystem[i].drive);
                syslog(nullptr, "Formatted %s result %d: %s", vFileSystem[i].drive, err, strerror(err));

            #ifdef _BOOTAPI_H
                char label[22];
                sniprintf(label, sizeof(label), "%s%s", vFileSystem[i].drive, pBootVersion->serial);
                vfs_setlabel(label);
            #endif

                err = -vfs_mount(vFileSystem[i].drive, true);
            }
#endif
        }
    #if 1
        if (err == 0)
        {
            VfsInfo_t info;

            // Get disk info
            if (err = -vfs_stat(vFileSystem[i].drive, &info), err == 0)
            {
//XXX                uint64_t sz = info.blocks * info.blocksize;
//XXX                uint32_t s1 = sz / 1024;
//XXX                uint32_t s2 = (sz - info.size) / 1024;
//XXX                syslog(nullptr, "Mounted %s %lukB, %lukB free", vFileSystem[i].drive, s1, s2);
            }
            else
            {
                //syslog(nullptr, "Mounted %s, but vfs_stat says %d: %s...!", vFileSystem[i].drive, err, strerror(err));
            }

            // TODO Check file-system
        }
        else
        {
            syslog(nullptr, "Cannot mount %s %d: %s\n", vFileSystem[i].drive, err, strerror(err));
            vfs_mount(vFileSystem[i].drive, false);
        }
    #endif
    }
#if 0
    VfsDir_t dir;
    if (vfs_dir_open(&dir, "SPI:/Script") == 0)
    {
        printf("%s is at %u\n", "SPI:/script", dir.ff.sclust);
        vfs_dir_close(&dir);
    }
    if (vfs_dir_open(&dir, "SPI:/Html") == 0)
    {
        printf("%s is at %u\n", "SPI:/Html", dir.ff.sclust);
        vfs_dir_close(&dir);
    }

    extern void testclusters(FATFS* fs);
    testclusters(vFileSystem[0].fatfs);
#endif

    struct tm s = {.tm_mday = 1};
    for (s.tm_year = 0; s.tm_year < 150; s.tm_year++)
    {
        time_t t = mktime(&s);
        struct tm* r = gmtime(&t);
        if (r->tm_year != s.tm_year)
        {
            iprintf("gmtime error at %llu in=%d, out=%d\n", t, s.tm_year, r->tm_year);
            break;
        }
    }
    return(0);
}


void
vfs_deinit(void)
{
    int i;

    for (i = 0; vFileSystem[i].drive != nullptr; i++)
    {
        vfs_mount(vFileSystem[i].drive, false);
    }
}


#ifndef CRC_FUNC
uint32_t
CRC_FUNC(uint32_t* pCrc, uint32_t* pUint32, uint32_t vLen, bool vInit)
{
	uint32_t n = 0;
	uint32_t tmp;

	if (pCrc == nullptr)
	{
		pCrc = &tmp;
	}

    static const uint32_t CrcTable[16] =
    {   // Nibble lookup table for 0x04C11DB7 polynomial
        0x00000000,0x04C11DB7,0x09823B6E,0x0D4326D9,0x130476DC,0x17C56B6B,0x1A864DB2,0x1E475005,
        0x2608EDB8,0x22C9F00F,0x2F8AD6D6,0x2B4BCB61,0x350C9B64,0x31CD86D3,0x3C8EA00A,0x384FBDBD
    };

	if (vInit)
	{
		*pCrc = 0xFFFFFFFF;
	}

	while (n < vLen)
	{
        *pCrc = *pCrc ^ pUint32[n]; // Apply all 32-bits

        // Process 32-bits, 4 at a time, or 8 rounds
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28]; // Assumes 32-bit reg, masking index to 4-bits
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28]; //  0x04C11DB7 Polynomial used in STM32
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
        *pCrc = (*pCrc << 4) ^ CrcTable[*pCrc >> 28];
		n++;
	}
	return(*pCrc);

}
#endif // CRC
