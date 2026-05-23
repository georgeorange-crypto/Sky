/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_const.h 
 */
 
#ifndef __SKYFS_CONST_H
#define __SKYFS_CONST_H

/*mds related*/
#define SKYFS_MAX_NAME_LEN           256
#define SKYFS_MAX_SYMNAME_LEN        256    
#define SKYFS_OBJECT_NAME_LEN        24
#define SKYFS_NODE_BM_LEN            (SKYFS_MAX_CLIENT_NUM/8)

#define SKYFS_DIR_HASH_LEN           1024
#define SKYFS_SUBSET_HASH_LEN        10240
#define SKYFS_OSD_CONSIST_HASH_LEN   10240
/* the two const below will be changed together because the multiple of them is always 4G */
#define SKYFS_OSD_CONSIST_HASH_SEGSIZE  32768
#define SKYFS_VIRTURE_NODE_CNT 		131072

// added by mayl for flock cache
#define SKYFS_LOCK_HASH_LEN          10240
//#define SKYFS_SUBSET_HASH_LEN      10240
#define SKYFS_SUB_INDEX_HASH_LEN     10
#define SKYFS_BMETA_HASH_LEN         1024
#define SKYFS_READDIR_HASH_LEN       1024
#define SKYFS_DIR_DEPTH_HASH_LEN     1024
#define SKYFS_LONG_BITS              (sizeof(skyfs_u32_t) * 8)
#define SKYFS_TOT_HASH_BITS          30
#define SKYFS_AVA_HASH_BITS          10
#define SKYFS_SUB_HASH_BITS          (SKYFS_TOT_HASH_BITS - SKYFS_AVA_HASH_BITS)    
//#define SKYFS_MAX_HASH_VALUE        1000000
#define SKYFS_MAX_HASH_VALUE         ((skyfs_u32_t)1 << SKYFS_AVA_HASH_BITS)

#define SKYFS_MAX_DLSUBSET           ((skyfs_u32_t)1 << 20)
#define SKYFS_DLSUBSET_HASH_LEN      1024
#define SKYFS_DLSUBSET_BM_LEN        (SKYFS_MAX_DLSUBSET/8)

#define SKYFS_MAX_FLOCK_NUM          4096
#define SKYFS_MAX_MMSG_NUM           4096
#define SKYFS_MAX_DIR_PER_MDS        ((skyfs_u32_t)1 << 24)
#define SKYFS_MAX_SRV_PER_NODE       32

#define SKYFS_MAX_BMETA_NUM          20000 // changed by mayl
#define SKYFS_MAX_SUBSET_CACHE_NUM   1024
#define SKYFS_MAX_SUBSET_INDEX_NUM   1024
#define SKYFS_MAX_DIR_DEPTH_NUM      1024
#define SKYFS_MAX_DIR_CACHE_NUM      1024
#define SKYFS_MDS_L1MAPPING_LEN      10240
#define SKYFS_OSD_L1MAPPING_LEN      10240

#define SKYFS_MAX_MSG_HANDLER_NUM    100

#define SKYFS_MAX_LOG_FILE_SIZE      204800000
#define SKYFS_MAX_LOG_BUF_SIZE       10240000

#define SKYFS_MAX_DENTRY_PER_BLK    (SKYFS_DIR_BLK_SIZE/(sizeof(skyfs_dentry_t)))
#define SKYFS_DBLK_SIZE             (SKYFS_MAX_DENTRY_PER_BLK * sizeof(skyfs_dentry_t))
#define SKYFS_BMETA_SIZE             sizeof(skyfs_M_bmeta_t)
#define SKYFS_PAGE_SIZE             4096

//#define SKYFS_MAX_META_PER_BOX_BIT   12
#define SKYFS_MAX_META_PER_BOX_BIT   9
#define SKYFS_MAX_META_PER_BOX       ((skyfs_u32_t)1 << SKYFS_MAX_META_PER_BOX_BIT)
#define SKYFS_MAX_SUBSET_DEPTH       5
#define SKYFS_MAX_BMETA_PER_SUBSET  ((skyfs_u32_t)1 << SKYFS_MAX_SUBSET_DEPTH)
//#define SKYFS_FIRST_SUBSET_DEPTH    0
//#define SKYFS_FIRST_SPLIT_DEPTH    0
//#define SKYFS_MIDDLE_SPLIT_DEPTH    0
#define SKYFS_FIRST_SUBSET_DEPTH     3
#define SKYFS_FIRST_SPLIT_DEPTH      10
#define SKYFS_MIDDLE_SPLIT_DEPTH     14
#define SKYFS_MAX_DIR_DEPTH          20
#define SKYFS_MAX_SUBSET_PER_DIR    ((skyfs_u32_t)1 << SKYFS_MAX_DIR_DEPTH)
#define SKYFS_SUBSET_BM_LEN            (SKYFS_MAX_SUBSET_PER_DIR/8)
//#define SKYFS_SUBSET_BM_LEN            (1024/8)

#define SKYFS_REFRESH_INTERVAL       1	

//#define SKYFS_DL_FIRST_SPLIT_DEPTH  3 
#define SKYFS_DL_FIRST_SPLIT_DEPTH   1
//#define SKYFS_DL_FIRST_SUBSET_DEPTH  2
#define SKYFS_DL_FIRST_SUBSET_DEPTH  5

#define SKYFS_DIR_BLK_SIZE_BITS      (12)
#define SKYFS_DIR_BLK_SIZE            (1 << SKYFS_DIR_BLK_SIZE_BITS)
#define SKYFS_DATA_BLK_SIZE_BITS     (12)
#define SKYFS_DATA_BLK_SIZE           (1 << SKYFS_DATA_BLK_SIZE_BITS)
#define SKYFS_WRITEBUF_SIZE_BITS     (20)
#define SKYFS_WRITEBUF_SIZE           (1 << SKYFS_WRITEBUF_SIZE_BITS)

//#define SKYFS_DLENTRY_PER_CHUNK_BITS  3
/* tmp changed by mayl*/
#define SKYFS_DLENTRY_PER_CHUNK_BITS  6
//#define SKYFS_DLENTRY_PER_CHUNK_BITS    13
#define SKYFS_DLENTRY_PER_CHUNK       (1 << SKYFS_DLENTRY_PER_CHUNK_BITS)



#define SKYFS_DOP_HASH_LEN            10240
#define SKYFS_DL_OBJBUF_HASH_LEN      1024
#define SKYFS_DL_PAGEBUF_HASH_LEN     512
#define SKYFS_DL_SUB_INDEX_HASH_LEN   1000
#define SKYFS_DL_CHUNK_HASH_LEN       SKYFS_BMETA_HASH_LEN
#define SKYFS_DL_PARTITION_HASH_LEN   (SKYFS_BMETA_HASH_LEN *100)
#define SKYFS_DLCHUNK_SIZE            (sizeof(skyfs_DL_chunk_t))
//#define SKYFS_OBJECT_SIZE           (1 << 12) //to test 1PB
#define SKYFS_OBJECT_SIZE           (1 << 25)
#define SKYFS_OBJECT_NODE_SIZE           (1 << 17)

//#define SKYFS_OBJECT_SIZE           (1 << 25) // mayl for db test
//#define SKYFS_OBJECT_SIZE             (1 << 20)

//#define SKYFS_MAX_OBJ_PER_PART	         16
 // changed by mayl , avoid  cross-partition hole, now the max hole size grow to 16 objs, i.e. 2GB
#define SKYFS_MAX_OBJ_PER_PART	         16    
#define SKYFS_DLFILE_PER_CHUNK           128

#define SKYFS_MAX_DLSUBSET_NUM           1000
#define SKYFS_MAX_DLSUBSET_INDEX_NUM     10000
#define SKYFS_MAX_DLCHUNK_NUM            1000000

#define SKYFS_DEFAULT_REPLICA_NUM		 1
#define SKYFS_MAX_REPLICA_NUM		 8

#define SKYFS_COLLECT_INTERLEAVE    10
// by mayl : DL SUBSET file will be writeback every 128 updates  
#define SKYFS_DL_WRITEBACK_COUNT   (128)

#define SKYFS_MASTER_MDS_ID        1
#define SKYFS_MASTER_OSD_ID        1
#define SKYFS_ROOT_INO            1

#define SKYFS_MAX_FS_NUM           10

enum skyfs_meta_type{
    SKYFS_NULL_META     = 0,
    SKYFS_UNLINK        = 1,
    SKYFS_FILE          = 2,
    SKYFS_DIR           = 3,
    SKYFS_NODE          = 4,
    SKYFS_LINK          = 5,
    SKYFS_SYMLINK       = 6,
    SKYFS_NONE_INIT     = 7,
    SKYFS_DIR_INIT      = 8,
    SKYFS_RENAME        = 9,
};

/*com related */
enum skyfs_comp_type {
    SKYFS_CLIENT     = 1,   
    SKYFS_MDS        = 2,  
    SKYFS_OSD        = 3,   
    SKYFS_OSC        = 4,
    SKYFS_CFG        = 5,
    SKYFS_ADM        = 6,
    SKYFS_AMPS       = 7,
    SKYFS_AMPC       = 8,
    SKYFS_COMP_MAX,    
};

#define SKYFS_NEED_ACK             1
#define SKYFS_NEEDNOT_ACK        0

#define SKYFS_MAX_MSGBODY_SIZE     8192
// changed by mayl for RDMA
//#define SKYFS_MSG_MAGIC         (0xDEAeC)
#define SKYFS_MSG_MAGIC         (0xABCED01)
#define SKYFS_MAGIC             SKYFS_MSG_MAGIC
#define SKYFS_FSID              3

#define SKYFS_ADM_COM_PORT (22156)
#define SKYFS_MDS_COM_PORT (14345)
#define SKYFS_OSD_COM_PORT (33521)
#define SKYFS_OSC_COM_PORT (43224)
#define SKYFS_CLIENT_COM_PORT (48976)

#define SKYFS_MSG_ACK     (0x80000000)
#define SKYFS_MSG_FWD    (0x40000000)
#define SKYFS_MSG_FWD_ACK  (0x20000000)

#define SKYFS_MSG_TYPE_MASK  (0xff000000)

//#define SKYFS_MSGHEAD_SIZE ((skyfs_u32_t)(((skyfs_msg_t *)0)->u.mtext))
#define SKYFS_MSGHEAD_SIZE sizeof(skyfs_msg_head_t)
#define AMP_SKYFS_MSGHEAD_SIZE (AMP_MESSAGE_HEADER_LEN + SKYFS_MSGHEAD_SIZE)

#define SKYFS_READ 0
#define SKYFS_WRITE 1

#define SKYFS_MAX_NIC_NUM          6
#define SKYFS_MAX_ADDR_LEN        32

/*obd related*/
#define SKYFS_DEV "/dev/IP_obddev"
#define SKYFS_OSD_GROUP_NUM        10
#define SKYFS_MAX_OSD_GROUP        (SKYFS_MAX_OSD_NUM/SKYFS_OSD_GROUP_NUM + 1)

/*adm related*/
#define SKYFS_MAX_MDS_NUM      (200)
#define SKYFS_MAX_OSD_NUM      (4096)
//#define SKYFS_MAX_CLIENT_NUM   (6000)
/* mayl: limit the client cnt to 1024 */
#define SKYFS_MAX_CLIENT_NUM   (1024)
#define SKYFS_MAX_IP_NUM       (6000)
#define SKYFS_MAX_IP_PER_NODE  (8)

#define SKYFS_MAX_MODULE_NUM        2048
#define SKYFS_MAX_LINE_LEN          256

#define SKYFS_MAX_PATH_LEN             1024
#define SKYFS_EXEC_FILE_PATH         "/cluster/skyfs/bin/"
#define SKYFS_ARCCFG_FILE_PATH         "/cluster/skyfs/conf/"
#define SKYFS_META_PATH             "/cluster/skyfs/meta/"
#define SKYFS_LOCAL_META_PATH        "/cluster/skyfs/local_meta/"
#define SKYFS_DL_PATH                 "/cluster/skyfs/dl/"
#define SKYFS_OBJ_PATH                 "/cluster/skyfs/obj/"
#define SKYFS_DATA_PATH             "/mnt/test/"
#define SKYFS_GEN_CONFIG             "skyfs.conf"
#define SKYFS_VAR_CONFIG             "skyfs_var.conf"
#define SKYFS_HOSTNAME_CONFIG         "skyfs_hostname.conf"

#define SKYFS_CLIENT_STARTSCRIPT     "/start_client"
#define SKYFS_MDS_STARTSCRIPT        "/start_mds"
#define SKYFS_OSD_STARTSCRIPT          "/start_osd"

#define SKYFS_CLIENT_STOPSCRIPT     "/stop_client"
#define SKYFS_MDS_STOPSCRIPT        "/stop_mds"
#define SKYFS_OSD_STOPSCRIPT          "/stop_osd"

#endif
/*end of skyfs_const.h*/
