/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: skyfs_fs.h 
 */

#ifndef __SKYFS_FS_H
#define __SKYFS_FS_H

#include "skyfs_types.h"
#include "skyfs_const.h"


#define SKYFS_ADDR_LEN            (16)

#define SKYFS_MAX_DATA_REP_NUM     3

#define SKYFS_MAX_OSD_NIC_NUM      2 

#define SKYFS_DENTRY_HEAD_LEN  24

/*definition for attributes*/
#define SKYFS_ATTR_MODE          (1)
#define SKYFS_ATTR_UID             (2)
#define SKYFS_ATTR_GID             (4)
#define SKYFS_ATTR_SIZE           (8)
#define SKYFS_ATTR_ATIME         (16)
#define SKYFS_ATTR_MTIME         (32)
#define SKYFS_ATTR_CTIME          (64)
#define SKYFS_ATTR_ATIME_SET    (128)
#define SKYFS_ATTR_MTIME_SET    (256)
#define SKYFS_ATTR_FORCE        (512)    /* Not a change, but a change it */
#define SKYFS_ATTR_ATTR_FLAG    (1024)
#define SKYFS_ATTR_EATTR_FLAG    (4096) // set EATTR

/*add by mayl for flock*/
#define SKYFS_FL_POSIX (1)
#define SKYFS_FL_FLOCK (2)
#define ENFS_FL_DELEG        4       
#define SKYFS_FL_ACCESS       8       /* not trying to lock, just looking, getlock used */
#define SKYFS_FL_EXISTS       16      /* when unlocking, test for existence */
#define SKYFS_FL_LEASE        32      /* lease held on this file */
#define SKYFS_FL_CLOSE        64      /* unlock on close */
#define SKYFS_FL_SLEEP        128     /* A blocking lock */
#define SKYFS_FL_DOWNGRADE_PENDING    256 /* Lease is being downgraded */
#define SKYFS_FL_UNLOCK_PENDING       512 /* Lease is being broken */

/*added by mayl for compress algorithm */

#define COMPRESS_NONE_ALGORITHM 0
#define COMPRESS_ZSTD_ALGORITHM 1
#define COMPRESS_ZLIB_ALGORITHM 2
#define COMPRESS_ADM_ALGORITHM 3
#define COMPRESS_PANS_ALGORITHM 4
#define COMPRESS_MANS_ALGORITHM 7

#define COMPRESS_SZ2_ALGORITHM 8
#define COMPRESS_SZ3_ALGORITHM 9


#define COMPRESS_GZSTD_ALGORITHM 0x100

// mayl: lossy algorithms from 200 




typedef struct __skyfs_M_eattr{
	skyfs_u32_t comp_algorithm;
	skyfs_u32_t comp_paramter;
	skyfs_u32_t encry_algorithm;
	skyfs_u32_t encry_parameter;
	// ....

}skyfs_M_eattr_t;

typedef struct __skyfs_M_cmeta{
    skyfs_u32_t         pad;
    skyfs_u32_t         type;
    skyfs_s32_t         nextfree;/*Set -1 when deleting cmeta*/
    skyfs_u32_t         conflict_index;
    skyfs_u64_t         hashkey;
    skyfs_u64_t         ino;
    skyfs_u64_t         size;
    skyfs_s64_t         space;
    skyfs_u64_t         dev;
    skyfs_u16_t         mode;
    skyfs_u16_t         nlink;
    skyfs_u32_t         uid;
    skyfs_u32_t         gid;
    skyfs_u32_t         depth;
    skyfs_timespec_t    atime;
    skyfs_timespec_t    ctime;
    skyfs_timespec_t    mtime;
    skyfs_M_eattr_t     eattr;
    skyfs_s8_t          name[SKYFS_MAX_NAME_LEN];
}skyfs_M_cmeta_t;

/*
 *  The data structures defined below are relative to file system.
 */
struct __skyfs_disk_sb{
    skyfs_u32_t magic;        /*magic of skyfs*/
    skyfs_u32_t fsid;        /*id number of skyfs*/
    skyfs_u64_t size;        /*total bytes of skyfs*/
    skyfs_u32_t blocksize;        /*skyfs block size in bytes*/
    skyfs_u32_t blocksize_bits;    /*skyfs block size bits*/
    skyfs_u64_t blocks;        /*total blocks of this skyfs*/
    skyfs_u64_t bfree;        /*free blocks of this skyfs*/
    skyfs_u64_t bavail;        /*available blocks of this skyfs*/
    skyfs_u64_t frees;        /*total files of this skyfs*/
    skyfs_u64_t inodes;        /*total inodes of this skyfs*/
    skyfs_u64_t ifree;        /*free inodes of this skyfs*/
    skyfs_u64_t ifavail;        /*free inodes of this skyfs*/
    skyfs_s8_t  fs_name[8];    /*name of this skyfs*/
};
typedef struct __skyfs_disk_sb skyfs_disk_sb_t;

struct __skyfs_meta{
    skyfs_u64_t     ino;
    skyfs_u64_t     size;
    skyfs_s64_t     space;
    skyfs_u16_t     fsid;
    skyfs_u16_t     type;
    skyfs_u16_t     mode;
    skyfs_u16_t     nlink;
    skyfs_u32_t     uid;
    skyfs_u32_t     gid;
    skyfs_u32_t     dev;
    skyfs_u32_t     algorithm;
    skyfs_u32_t  conflict_index;
    skyfs_timespec_t  atime;
    skyfs_timespec_t  ctime;
    skyfs_timespec_t  mtime;
};
typedef struct __skyfs_meta skyfs_meta_t;

struct __skyfs_dentry{
    skyfs_ino_t     ino;
    skyfs_u32_t     offset;
    skyfs_u32_t     reclen;
    skyfs_u16_t     namelen;
    skyfs_u16_t     mode;
    skyfs_s8_t      name[SKYFS_MAX_NAME_LEN];
};
typedef struct __skyfs_dentry skyfs_dentry_t;





typedef struct __skyfs_L3_info{
	skyfs_u32_t L3;
	skyfs_u32_t L2_num;
	struct list_head L3_head;
	struct list_head arch_list;
}skyfs_L3_info_t;

typedef struct __skyfs_L2_info{
	skyfs_u32_t L2;
	skyfs_u32_t L3;
	skyfs_u32_t L1_num;
	skyfs_u32_t pad;
    struct list_head L2_head;
	struct list_head L3_list;
}skyfs_L2_info_t;

typedef struct __skyfs_L1_info{
	skyfs_u32_t L1;
	skyfs_u32_t L2;
	skyfs_u32_t L3;
	skyfs_u32_t ip_num;
    struct list_head L1_head;
	struct list_head L2_list;
}skyfs_L1_info_t;

typedef struct __skyfs_ip_info{
	/*Network topology*/
    skyfs_u32_t L1;
    skyfs_u32_t L2;
    skyfs_u32_t L3;

	/*Storage Architecture */
    skyfs_u32_t mds;
    skyfs_u32_t osd;
    skyfs_u32_t client;
    skyfs_u32_t lid;
    skyfs_u32_t pad;

    struct list_head L1_list;
	skyfs_s8_t       addr[SKYFS_MAX_ADDR_LEN]; 
}skyfs_ip_info_t;

typedef struct __skyfs_arch_info{
    skyfs_u32_t         ip_num;
    skyfs_u32_t         pad;
	struct list_head    topo_head;
    skyfs_ip_info_t     ip[SKYFS_MAX_IP_NUM];
}skyfs_arch_info_t;

typedef struct __skyfs_entry_info{
    skyfs_u32_t         id;
    skyfs_u32_t         ip_num;
    skyfs_ip_info_t     *ip[SKYFS_MAX_IP_PER_NODE];
}skyfs_entry_info_t;

typedef struct __skyfs_mds_info{
    skyfs_u32_t         mds_num;
    skyfs_entry_info_t  mds[SKYFS_MAX_MDS_NUM];
}skyfs_mds_info_t;

typedef struct __skyfs_osd_info{
    skyfs_u32_t         osd_num;
    skyfs_entry_info_t  osd[SKYFS_MAX_OSD_NUM];
}skyfs_osd_info_t;

typedef struct __skyfs_client_info{
    skyfs_u32_t         client_num;
    skyfs_entry_info_t  client[SKYFS_MAX_CLIENT_NUM];
}skyfs_client_info_t;

typedef struct __skyfs_dl_dest{
	skyfs_u32_t     replica_num;
	skyfs_u32_t     replica_location[8];
	skyfs_u64_t     write_version[8]; // added by mayl
	skyfs_u64_t	max_write_version; // added by mayl

}skyfs_dl_dest_t;


#endif 
