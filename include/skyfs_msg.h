/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_msg.h 399 2008-07-28 08:05:47Z xingjing$
 */
 
#ifndef __SKYFS_MSG_H
#define __SKYFS_MSG_H

#define SKYFS_NEED_ACK		1
#define SKYFS_NEEDNOT_ACK	0

#define OSD_RW_HEAD_LEN ((unsigned int)(((skyfs_rw_t *)0) -> vec))

// changed by mayl for RDMA 
#define SKYFS_INIT_MSG(__msgp, __reqp, __fsid, __type, __fromid, __fromtype, __req_size)	\
do{ \
	(__msgp) = (skyfs_msg_t *)((char *)((__reqp)->req_msg) \
		 + AMP_MESSAGE_HEADER_LEN); \
	(__msgp)->magic = SKYFS_MAGIC; \
	(__msgp)->fs_id = (__fsid); \
	(__msgp)->type  = (__type); \
	(__msgp)->size  = (__req_size) - AMP_SKYFS_MSGHEAD_SIZE; \
	(__msgp)->error = 0; \
	(__msgp)->fromid = (__fromid); \
	(__msgp)->fromType = (__fromtype); \
}while(0);

#define SKYFS_FILL_REQ(__reqp, __need_ack, __req_type, __req_size) \
do{ \
    (__reqp)->req_msglen = (__req_size); \
    (__reqp)->req_need_ack = (__need_ack); \
    (__reqp)->req_resent = 1; \
    (__reqp)->req_type = (__req_type); \
    (__reqp)->req_niov = 0; \
    (__reqp)->req_iov = NULL; \
}while(0);

#define MAX_VECTOR_CNT (32)

/*
 *  Message types in SKYFS.
 */
enum skyfs_msg_types{
/* MDS related.*/
	SKYFS_MSG_M_STATFS = 1,			/*1*/
	SKYFS_MSG_M_LOOKUP,             /*2*/
	SKYFS_MSG_M_CREATE,             /*3*/
	SKYFS_MSG_M_REMOVE,             /*4*/
	SKYFS_MSG_M_READ_INODE,         /*5*/
	SKYFS_MSG_M_WRITE_INODE,        /*6*/
	SKYFS_MSG_M_READDIR,            /*7*/
	SKYFS_MSG_M_RENAME,             /*8*/
	SKYFS_MSG_M_LINK,               /*9*/
	SKYFS_MSG_M_SYMLINK,            /*10*/
	SKYFS_MSG_M_READLINK,           /*11*/
	SKYFS_MSG_M_RELEASE,            /*12*/ 
	SKYFS_MSG_M_FLOCK,              /*13*/
	SKYFS_MSG_M_INIT_DIRC, 			/*14*/
	SKYFS_MSG_M_GET_DIRC, 			/*15*/
	SKYFS_MSG_M_UPDATE_DIRC,		/*16*/
	SKYFS_MSG_M_UPDATE_DIRD,		/*17*/
	SKYFS_MSG_M_CREATE_SUBI,		/*18*/
	SKYFS_MSG_M_GET_STATE,			/*19*/
	SKYFS_MSG_M_ADD_HTB,			/*20*/
	SKYFS_MSG_M_GET_LAYOUT,			/*21*/
	SKYFS_MSG_M_2	,				/*22*/
	SKYFS_MSG_M_3	,				/*23*/
	SKYFS_MSG_M_4	,				/*24*/
	SKYFS_MSG_M_5	,				/*25*/
	SKYFS_MSG_M_6	,				/*26*/
	SKYFS_MSG_M_7	,				/*27*/
	SKYFS_MSG_M_8	,				/*28*/
	SKYFS_MSG_M_9	,				/*29*/
	SKYFS_MSG_M_10	,				/*30*/
	/* OSD related.*/
	SKYFS_MSG_O_READ_OBJ,           /*31*/
	SKYFS_MSG_O_WRITE_OBJ,          /*32*/
	SKYFS_MSG_O_CREATE_OBJ,		    /*33*/
	SKYFS_MSG_O_REMOVE_OBJ,  		/*34*/
	SKYFS_MSG_O_COMMIT,             /*35*/
	SKYFS_MSG_O_TRUNCATE,           /*36*/
	SKYFS_MSG_O_GET_DEVINFO,        /*37*/
	SKYFS_MSG_O_ENLARGE_SUBSET,		/*38*/
	SKYFS_MSG_O_SPLIT_SUBSET,		/*39*/	
	SKYFS_MSG_O_CREATE_SUBSET,		/*40*/	
	SKYFS_MSG_O_READ_BMETA,			/*41*/	
	SKYFS_MSG_O_WRITE_BMETA,		/*42*/	
	SKYFS_MSG_O_READ_SUBSET,		/*43*/	
	SKYFS_MSG_O_WRITE_SUBSET,		/*44*/	

/* Configuration related msg */ 
	SKYFS_MSG_CREATEFS,             /*45*/
	SKYFS_MSG_SHUTDOWN,             /*46*/
	SKYFS_MSG_STATE, 	            /*47*/
	SKYFS_MSG_M_TRIGGER_BLA,		/*48*/
	SKYFS_MSG_M_BALANCE_LOAD,		/*49*/
	SKYFS_MSG_O_DO_REMOVEOBJ,	    /*50*/
	SKYFS_MSG_O_CREATE_DL_SUBI,     /*51*/
	SKYFS_MSG_O_GET_DLHEAD,         /*52*/
	SKYFS_MSG_O_CREATE_DLSUBSET,    /*53*/
	SKYFS_MSG_O_WRITE_DLCHUNK,      /*54*/
	SKYFS_MSG_O_UPDATE_HDEPTH,      /*55*/
	SKYFS_MSG_O_COPY_OBJ,           /*56*/
	SKYFS_MSG_O_UPDATE_STATE,       /*57*/

/* HA related*/ 
	SKYFS_MSG_O_WRITE_REPLICA,      /*58*/
	SKYFS_MSG_O_PREPARE_WRITE,      /*59*/
	SKYFS_MSG_O_COMMIT_WRITE,       /*60*/
	SKYFS_MSG_INIT_CONFIG,          /*61*/
	SKYFS_MSG_GET_CONFIG,           /*62*/
	SKYFS_MSG_O_RECOVER_REPLICA,      /*63*/
	SKYFS_MSG_O_QUERY_REPLICA,       /*64*/
	SKYFS_MSG_O_ASK_RECOVER_REPLICA,       /*65*/

 	SKYFS_MSG_O_LISTXATTR,                 /*66*/
 	SKYFS_MSG_O_GETXATTR,                  /*67*/  
 	SKYFS_MSG_O_SETXATTR,                  /*68*/
 	SKYFS_MSG_O_REMOVEXATTR,               /*69*/
 	SKYFS_MSG_O_READ_MULTI_OBJ,               /*70*/
 	SKYFS_MSG_O_WRITE_MULTI_OBJ,               /*71*/
	SKYFS_MSG_MAX                   /*max msg id*/ 
};

/*The overall request wrapper*/	
typedef struct __skyfs_m_statfs_args{
	skyfs_s32_t		flags;
	skyfs_s32_t		pad;
}skyfs_m_statfs_args_t;

typedef struct __skyfs_m_statfs_ack{
	skyfs_disk_sb_t sb;
	skyfs_meta_t	root_inode;
}skyfs_m_statfs_ack_t;

typedef struct __skyfs_m_lookup_args{
	skyfs_ino_t dir_ino;
	skyfs_u32_t subdir_seqno;
	skyfs_u32_t flag;
	char	     name[SKYFS_MAX_NAME_LEN+1];
}skyfs_m_lookup_args_t;

typedef struct __skyfs_m_lookup_ack{
	skyfs_meta_t parent_meta;
	skyfs_meta_t meta;
	skyfs_u32_t  mps_id;
	skyfs_u32_t  pad;
}skyfs_m_lookup_ack_t;

typedef struct __skyfs_m_create_args{
	skyfs_ino_t  dir_ino;
	skyfs_u32_t  flag;
	skyfs_u32_t  subdir_seqno;
	skyfs_u32_t  mode;
	skyfs_u32_t  dev;
	skyfs_u32_t  uid;
	skyfs_u32_t  gid;
	char          name[SKYFS_MAX_NAME_LEN + 1];
}skyfs_m_create_args_t;

typedef struct __skyfs_m_create_ack{
	skyfs_meta_t meta;
	skyfs_meta_t dir_meta;
}skyfs_m_create_ack_t;





typedef struct __skyfs_m_remove_args{
	skyfs_ino_t dir_ino;
	skyfs_ino_t ino;
	skyfs_u32_t conflict_index;
	skyfs_u32_t pad;
	char 	     name[SKYFS_MAX_NAME_LEN + 1];
}skyfs_m_remove_args_t;

typedef struct __skyfs_m_remove_ack{
	skyfs_meta_t meta;
	skyfs_u32_t  conflict_index;/*temp used, can be deleted*/
}skyfs_m_remove_ack_t;

typedef struct __skyfs_m_getmeta_args{
	skyfs_ino_t ino;
	skyfs_u32_t conflict_index;
	skyfs_u32_t client_id;
}skyfs_m_getmeta_args_t;

typedef struct __skyfs_m_getmeta_ack{
	skyfs_meta_t meta;
}skyfs_m_getmeta_ack_t;

typedef struct __skyfs_m_setmeta_args{
	skyfs_ino_t  ino;
	skyfs_size_t size;
	skyfs_u32_t  valid;
	skyfs_u32_t  mode;
	skyfs_u32_t  uid;
	skyfs_u32_t  gid;
	skyfs_u32_t  truncate_flag;
	skyfs_u32_t  conflict_index;
	skyfs_u32_t  algorithm; // high 16-bit define which algorithm(comp, encry , or merge), low 16-bit define detailed algorithm 	 
	skyfs_s64_t  space_changed;	 
	skyfs_timespec_t atime;
	skyfs_timespec_t mtime;
	skyfs_timespec_t ctime;
}skyfs_m_setmeta_args_t;

typedef struct __skyfs_m_setmeta_ack{
	skyfs_meta_t meta;
}skyfs_m_setmeta_ack_t;

typedef struct __skyfs_m_readdir_args{
	skyfs_ino_t     dino;
	skyfs_u32_t     offset;
	skyfs_u32_t     count;
	skyfs_u32_t	client_id;
	skyfs_u32_t	pid;
        skyfs_u32_t     subset_id;
        skyfs_u32_t     chunk_id;
}skyfs_m_readdir_args_t;

typedef struct __skyfs_m_rename_args{
	skyfs_ino_t pino_from;
	skyfs_ino_t pino_to;
	skyfs_ino_t ino_from;
	skyfs_u64_t conflict_index_from;
	char 	   name_from[SKYFS_MAX_NAME_LEN + 1];
	char 	   name_to[SKYFS_MAX_NAME_LEN + 1];
	skyfs_meta_t meta;
}skyfs_m_rename_args_t;

typedef struct __skyfs_m_rename_ack{
	skyfs_meta_t meta;
}skyfs_m_rename_ack_t;

typedef struct __skyfs_m_link_args{
	skyfs_ino_t pino_from;
	skyfs_ino_t pino_to;
	skyfs_ino_t ino_from;
	skyfs_u64_t conflict_index_from;
	char 	   name_from[SKYFS_MAX_NAME_LEN + 1];
	char 	   name_to[SKYFS_MAX_NAME_LEN + 1];
	skyfs_meta_t meta;
}skyfs_m_link_args_t;

typedef struct __skyfs_m_link_ack{
	skyfs_meta_t meta;
	skyfs_meta_t dir_meta;
}skyfs_m_link_ack_t;

typedef struct __skyfs_m_symlink_args{
	skyfs_ino_t  dir_ino;
	skyfs_u16_t  uid;
	skyfs_u16_t  gid;
	skyfs_u16_t  mode;
	skyfs_u16_t  pad;
	char 	     name[SKYFS_MAX_NAME_LEN + 1];
	char 	     target[SKYFS_MAX_NAME_LEN + 1];
}skyfs_m_symlink_args_t;

typedef struct __skyfs_m_symlink_ack{
	skyfs_meta_t meta;
}skyfs_m_symlink_ack_t;

typedef struct __skyfs_m_readlink_args{
	skyfs_ino_t dir_ino;
	skyfs_s8_t  name[SKYFS_MAX_NAME_LEN + 1];
}skyfs_m_readlink_args_t;

typedef struct __skyfs_m_readlink_ack{
	skyfs_s8_t   target[SKYFS_MAX_PATH_LEN];
}skyfs_m_readlink_ack_t;

typedef struct __skyfs_m_release_args{
	skyfs_ino_t ino;
	skyfs_u32_t	conflict_index;
	skyfs_u32_t	client_id;
}skyfs_m_release_args_t;

typedef struct __skyfs_m_flock_args{
	skyfs_u32_t clt_id;
	skyfs_pid_t pid;
	skyfs_ino_t ino;
	skyfs_u64_t lock_owner; // add for destributed lock
	skyfs_u32_t conflict_index; // added by mayl
	skyfs_u32_t op_type; //cmd
	skyfs_u32_t fl_type; //l_type
	skyfs_u32_t mp_id;
	skyfs_u32_t pad;
	skyfs_loff_t start;
	skyfs_loff_t len;   // changed by mayl for POSIX standard
}skyfs_m_flock_args_t;

/*
  struct flock {
               ...
               short l_type;    // Type of lock: F_RDLCK, 1
                                //   F_WRLCK2 , F_UNLCK 3 
               short l_whence;  // How to interpret l_start:
                                  // SEEK_SET, SEEK_CUR, SEEK_END 
               off_t l_start;   // Starting offset for lock 
               off_t l_len;     // Number of bytes to lock 
               pid_t l_pid;     // PID of process blocking our lock
                                  // (set by F_GETLK and F_OFD_GETLK) 
               ...
           };

    // flle lock cmds
	#define F_GETLK		14
	#define F_SETLK		6
	#define F_SETLKW	7

	// flock.l_type

	#define F_RDLCK		1
	#define F_WRLCK		2
	#define F_UNLCK	    3


		

 * */

typedef struct __skyfs_m_flock_ack{
	skyfs_u32_t clt_id;
	skyfs_pid_t pid;
	skyfs_ino_t ino;
	skyfs_s32_t op_type;
	skyfs_u32_t fl_type;
	skyfs_u32_t mp_id;
	skyfs_u32_t pad;
	skyfs_loff_t start;
	skyfs_loff_t len;
}skyfs_m_flock_ack_t;

typedef struct __skyfs_m_initdirc_args{
	skyfs_u32_t 	dir_id;
	skyfs_u32_t 	pad;
	skyfs_M_cmeta_t dir_cmeta;
}skyfs_m_initdirc_args_t;

typedef struct __skyfs_m_initdirc_ack{

}skyfs_m_initdirc_ack_t;

typedef struct __skyfs_m_getdirc_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t pad;
}skyfs_m_getdirc_args_t;

typedef struct __skyfs_m_getdirc_ack{
	//skyfs_M_dir_cache_t dir_cache;
	//use msg->mtest to hold
}skyfs_m_getdirc_ack_t;

typedef struct __skyfs_m_updatedirc_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_s32_t update;
	skyfs_u32_t pad;
}skyfs_m_updatedirc_args_t;

typedef struct __skyfs_m_updatedirc_ack{
	//skyfs_M_cmeta_t dir_cmeta;
	//use msg->mtest to hold
}skyfs_m_updatedirc_ack_t;

typedef struct __skyfs_m_updatedird_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
	skyfs_u32_t pad;
}skyfs_m_updatedird_args_t;

typedef struct __skyfs_m_updatedird_ack{

}skyfs_m_updatedird_ack_t;

typedef struct __skyfs_m_createsubi_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
}skyfs_m_createsubi_args_t;

typedef struct __skyfs_m_createsubi_ack{

}skyfs_m_createsubi_ack_t;

typedef struct __skyfs_m_triggerbla_args{
	skyfs_u32_t        state_version;
	skyfs_u32_t        pad;
	skyfs_state_info_t state_info;
}skyfs_m_triggerbla_args_t;

typedef struct __skyfs_m_balanceload_args{
	skyfs_u32_t  kind_mds_id;
	skyfs_u32_t  pad;
	skyfs_u32_t  balance_num;
	skyfs_u32_t  first_index;
}skyfs_m_balanceload_args_t;

typedef struct __sufns_m_getstate_args{
	skyfs_u32_t  pad;
	skyfs_u32_t  pad2;
}skyfs_m_getstate_args_t;

typedef struct __skyfs_m_getstate_ack{
	skyfs_u32_t        state_version;
	skyfs_u32_t        pad;
	skyfs_state_info_t state_info;
}skyfs_m_getstate_ack_t;

typedef struct __skyfs_m_addhtb_args{
	skyfs_u32_t  index;
	skyfs_u32_t  last_flag;
}skyfs_m_addhtb_args_t;

typedef struct __skyfs_m_getlayout_ack{
	skyfs_layout_t layout[SKYFS_SUBSET_HASH_LEN];
	skyfs_u32_t    layout_version;
}skyfs_m_getlayout_ack_t;
/* OSD msg structure define related*/
typedef struct __skyfs_io_vector{
	skyfs_u64_t page_idx; /* index in obj file*/
	skyfs_ino_t ino;
	skyfs_u64_t obj_id;
	skyfs_u32_t offset;	  
	skyfs_u32_t count;	  
	skyfs_u64_t foffset;	  
	skyfs_u32_t fcount;	  
	skyfs_u32_t algorithm; // 0 : 保留原始数据  1， 2， ... 数据处理算法	  
	skyfs_u32_t replica_num;
	skyfs_u32_t replica_id;
	skyfs_u32_t partition_id; 
	skyfs_u32_t obj_size;
	skyfs_u32_t direct_op;
	skyfs_u32_t forward_count; // changed by mayl if this value more tan replica_num, reply error.
}skyfs_io_vector_t;

// added by mayl for clustered IO request


typedef struct __skyfs_multi_io_vector{
	skyfs_u64_t page_idx; /* index in obj file*/
	skyfs_ino_t ino;
	skyfs_u64_t obj_id;
	skyfs_u32_t vector_cnt; // 一次传输的向量数，原始数据和压缩数据总长都不超过2MB, 且向量数不超过32
				//
	skyfs_u32_t offset[MAX_VECTOR_CNT];	  
	skyfs_u32_t count[MAX_VECTOR_CNT];	  
	skyfs_u64_t foffset[MAX_VECTOR_CNT];	  
	skyfs_u32_t fcount[MAX_VECTOR_CNT];

	skyfs_u32_t algorithm; // 0 : 保留原始数据  1， 2， ... 数据处理算法	  
	skyfs_u32_t replica_num;
	skyfs_u32_t replica_id;
	skyfs_u32_t partition_id; 
	skyfs_u32_t obj_size;
	skyfs_u32_t direct_op;
	skyfs_u32_t forward_count; // changed by mayl if this value more tan replica_num, reply error.
}skyfs_multi_io_vector_t;


// added by mayl for xattr

typedef struct __skyfs_o_listxattr_args{
        skyfs_ino_t  ino;
	skyfs_io_vector_t vec;
}skyfs_o_listxattr_args_t;

typedef struct __skyfs_o_listxattr_ack{
        skyfs_ino_t real_ino;
        skyfs_u32_t xattr_cnt;
	// with xattr names and values vector here
}skyfs_o_listxattr_ack_t;

typedef struct __skyfs_o_getxattr_args{
        skyfs_ino_t  ino;
	char xattrname[SKYFS_MAX_NAME_LEN]; 
	skyfs_io_vector_t vec;
}skyfs_o_getxattr_args_t;

typedef struct __skyfs_o_getxattr_ack{
        skyfs_ino_t real_ino;
        skyfs_u32_t xattr_value_len;
	// with xattr names and values vector here
}skyfs_o_getxattr_ack_t;

typedef struct __skyfs_o_setxattr_args{
        skyfs_ino_t  ino;
	char xattrname[SKYFS_MAX_NAME_LEN]; 
        skyfs_u32_t xattr_value_len;
	// with xattr names and values vector here
}skyfs_o_setxattr_args_t;

typedef struct __skyfs_o_setxattr_ack{
        skyfs_ino_t real_ino;
        skyfs_u32_t setxattr_value_len;
}skyfs_o_setxattr_ack_t;



typedef struct __skyfs_o_readobj{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_u32_t       pad;
	skyfs_io_vector_t vec;
}skyfs_o_readobj_t;

typedef struct __skyfs_o_readobj_ack{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_u64_t 	  check_data1; // added by mayl
	skyfs_u64_t 	  check_data2; // added by mayl
        skyfs_u64_t       foffset;
        skyfs_u32_t       flen;
        skyfs_u32_t       algorithm;
	skyfs_u32_t       pad;
}skyfs_o_readobj_ack_t;

typedef struct __skyfs_o_writeobj{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_u64_t 	  check_data1; // added by mayl
	skyfs_u64_t 	  check_data2; // added by mayl
	skyfs_u32_t       pad;
	skyfs_io_vector_t vec;
}skyfs_o_writeobj_t;

typedef struct __skyfs_o_writeobj_ack{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_s64_t	  space_changed; // dense size , not including 
	//uint64_t 	  exe_time;
	skyfs_u32_t       pad; // use pad to record network time
}skyfs_o_writeobj_ack_t;

typedef struct __skyfs_o_multi_writeobj{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_u64_t 	  check_data1; // added by mayl
	skyfs_u64_t 	  check_data2; // added by mayl
	skyfs_u32_t       pad;
	skyfs_multi_io_vector_t multi_vec;
}skyfs_o_multi_writeobj_t;

typedef struct __skyfs_o_multi_writeobj_ack{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_s32_t       ret_vals[MAX_VECTOR_CNT];
	skyfs_s64_t	  space_changed; // dense size , not including 
	skyfs_u32_t       pad;
}skyfs_o_multi_writeobj_ack_t;


typedef struct __skyfs_o_writerep{
	skyfs_u32_t       dest;
	skyfs_u32_t       subset;
	skyfs_u32_t       chunk;
	skyfs_u32_t       pad;
	skyfs_io_vector_t vec;
}skyfs_o_writerep_t;


typedef struct __skyfs_o_createobj{
	skyfs_object_id_t obj_id;
}skyfs_o_createobj_t;

typedef struct __skyfs_o_removeobj{
	skyfs_ino_t ino;
	skyfs_u64_t obj_id;
	skyfs_u32_t obj_size;
	skyfs_u32_t pad;
}skyfs_o_removeobj_t;

typedef struct __skyfs_o_doremoveobj{
	skyfs_ino_t ino;
	skyfs_u64_t obj_id;
	// added by mayl, for truncate. indicate the object size after truncate, 0 means truncate to 0 , -1 means delete
	skyfs_s64_t reserve_size; 
	skyfs_u32_t partition_id;
    	skyfs_u32_t replica_id;
}skyfs_o_doremoveobj_t;

typedef struct __skyfs_o_preparewrite{
	skyfs_ino_t ino;	
	skyfs_u32_t partition_id;	
	skyfs_u32_t client_id;
	skyfs_io_vector_t vec;
	skyfs_dl_dest_t des;
}skyfs_o_preparewrite_t;

typedef struct __skyfs_o_preparewrite_ack{
}skyfs_o_preparewrite_ack_t;

// added by mayl
typedef struct __skyfs_o_replica_recover{
	skyfs_u64_t xid;
        skyfs_u32_t src_replica_id; 
        skyfs_u32_t dest_replica_id;
	skyfs_u32_t flag ; // 1 last send 2 normal,3 error
        skyfs_u32_t data_type; //  1: replica_ parttion_file 2: obj_file 
        skyfs_u32_t data_stripe_id;
        skyfs_u32_t replica_obj_cnt;
        skyfs_u32_t total_data_size;
        //skyfs_io_vector_t vec;
        //skyfs_dl_dest_t des;
}skyfs_o_replica_recover_t;

typedef struct __skyfs_o_replica_recover_ack{
}skyfs_o_replica_recover_ack_t;


typedef struct __skyfs_o_ask_replica_recover{
	skyfs_u64_t xid;
	skyfs_u32_t dest_osd_id;
        skyfs_u32_t src_replica_id; 
        skyfs_u32_t dest_replica_id;
	skyfs_u32_t flag ; // 1 recover all obj in this replica 2 recover replica in kiov buf below
        skyfs_u32_t data_type; //  1: replica_ parttion_file 2: obj_file 
        skyfs_s32_t data_stripe_id;
        skyfs_u32_t replica_obj_cnt;
        //skyfs_u32_t total_data_size;
        //skyfs_io_vector_t vec;
        //skyfs_dl_dest_t des;
}skyfs_o_ask_replica_recover_t;

typedef struct __skyfs_o_ask_replica_recover_ack{
	uint64_t task_xid;
}skyfs_o_ask_replica_recover_ack_t;


typedef struct __skyfs_o_replica_query_req{
  uint32_t client_id;
}skyfs_o_replica_query_req_t;

typedef struct __skyfs_o_replica_query_ack{
        skyfs_u32_t replica_cnt; 
        skyfs_u32_t used_partition_cnt[8];
        skyfs_u32_t fault_partition_cnt[8];
	skyfs_u64_t running_recover_xid[8] ; // 1 last send 2 normal,3 error 
        //skyfs_io_vector_t vec;
        //skyfs_dl_dest_t des;
}skyfs_o_replica_query_ack_t;




typedef struct __skyfs_o_commitwrite{
	skyfs_ino_t ino;	
	skyfs_u32_t partition_id;
	skyfs_u32_t client_id;
	skyfs_u32_t replica_id;
	skyfs_u32_t replica_cnt; // added by mayl for replica 
	skyfs_io_vector_t vec;
	skyfs_dl_dest_t des;
}skyfs_o_commitwrite_t;

typedef struct __skyfs_o_commitwrite_ack{
}skyfs_o_commitwrite_ack_t;

typedef struct __skyfs_o_truncate{
        // added by mayl
	skyfs_ino_t ino;	
	skyfs_u64_t obj_id;
	skyfs_u32_t size;
}skyfs_o_truncate_t;

typedef struct __skyfs_o_capacity {
	skyfs_u64_t   bsize;  
	skyfs_u64_t   blocks; 
	skyfs_u64_t   bfree;  
	skyfs_u64_t   bavail; 
	skyfs_u64_t   files;  
	skyfs_u64_t   ffree;  
	skyfs_u64_t   favail;  
}skyfs_o_capacity_t;

typedef struct __skyfs_o_getdevinfo_args{

}skyfs_o_getdevinfo_args_t;

typedef struct __skyfs_o_getdevinfo_ack{
	skyfs_o_capacity_t	cap;
}skyfs_o_getdevinfo_ack_t;

typedef struct __skyfs_o_readbmeta_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t bmeta_id;
	skyfs_u32_t size;
}skyfs_o_readbmeta_args_t;

typedef struct __skyfs_o_readbmeta_ack{

}skyfs_o_readbmeta_ack_t;

typedef struct __skyfs_o_writebmeta_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t bmeta_id;
	skyfs_u32_t size;
}skyfs_o_writebmeta_args_t;

typedef struct __skyfs_o_writebmeta_ack{

}skyfs_o_writebmeta_ack_t;

typedef struct __skyfs_o_splitsubset_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
	skyfs_u32_t subset_depth;
}skyfs_o_splitsubset_args_t;

typedef struct __skyfs_o_splitsubset_ack{

}skyfs_o_splitsubset_ack_t;

typedef struct __skyfs_o_enlargesubset_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
}skyfs_o_enlargesubset_args_t;

typedef struct __skyfs_o_enlargesubset_ack{

}skyfs_o_enlargesubset_ack_t;

typedef struct __skyfs_o_createsubset_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
	skyfs_u32_t pad;
}skyfs_o_createsubset_args_t;

typedef struct __skyfs_o_createsubset_ack{

}skyfs_o_createsubset_ack_t;

typedef struct __skyfs_o_readsubset_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
}skyfs_o_readsubset_args_t;

typedef struct __skyfs_o_readsubset_ack{
	skyfs_u32_t split_depth;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
	skyfs_u32_t pad;
}skyfs_o_readsubset_ack_t;

typedef struct __skyfs_o_writesubset_args{
	skyfs_u32_t dir_id;
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
	skyfs_u32_t pad;
}skyfs_o_writesubset_args_t;

typedef struct __skyfs_o_writesubset_ack{

}skyfs_o_writesubset_ack_t;

/*Data layout related*/
typedef struct __skyfs_o_createdlsubi_args{
	skyfs_u32_t subset_id;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
}skyfs_o_createdlsubi_args_t;

typedef struct __skyfs_o_getdlhead_args{
	skyfs_u32_t pad_id;
	skyfs_u32_t pad_id2;
}skyfs_o_getdlhead_args_t;

typedef struct __skyfs_o_createdlsubset_args{
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
	skyfs_u32_t subset_depth;
	skyfs_u32_t nlink;
	skyfs_u32_t replica_id;
	skyfs_u32_t fir_osd;
	skyfs_u32_t sec_osd;
	skyfs_u32_t thi_osd;
}skyfs_o_createdlsubset_args_t;

typedef struct __skyfs_o_writedlchunk_args{
	skyfs_u32_t subset_id;
	skyfs_u32_t chunk_id;
}skyfs_o_writedlchunk_args_t;

typedef struct __skyfs_o_updatehdepth_args{
	skyfs_u32_t subset_id;
	skyfs_u32_t split_depth;
}skyfs_o_updatehdepth_args_t;

typedef struct __skyfs_o_copyobj_args{
	skyfs_u32_t subset_id;
	skyfs_u32_t chunk_id;
	skyfs_ino_t ino;
	skyfs_u64_t obj_id;
}skyfs_o_copyobj_args_t;

typedef struct __skyfs_o_updatestate_args{
	skyfs_osd_status_t osd_status[SKYFS_MAX_OSD_NUM];
}skyfs_o_updatestate_args_t;
/* adm tool related*/
struct __skyfs_module_state
{
    skyfs_u32_t state;
};
typedef struct __skyfs_module_state skyfs_module_state_t;

struct __skyfs_client_state_info
{
    skyfs_u32_t  client_id;
    skyfs_u32_t  mount_state;
};
typedef struct __skyfs_client_state_info skyfs_client_state_info_t;

struct __skyfs_adm_unit{
	skyfs_u32_t	  base[6];
};
typedef struct __skyfs_adm_unit skyfs_adm_unit_t;

struct __skyfs_usage_info
{
    skyfs_u32_t 	cpu_usage;
    skyfs_u32_t 	memory_usage;
    skyfs_u32_t 	disk_usage;
    skyfs_u32_t 	pad;
    skyfs_u64_t		disk_total;
    skyfs_u32_t 	read_speed;
    skyfs_u32_t 	write_speed;
    skyfs_u32_t 	nr_handled_request_per_second;
    skyfs_u32_t 	length_request_queue;
	skyfs_u64_t		nr_read;
	skyfs_u64_t 	nr_write;
	skyfs_u64_t 	nr_request;

	skyfs_u32_t 	nr_read_client;
	skyfs_u32_t 	nr_write_client;
};
typedef struct __skyfs_usage_info skyfs_usage_info_t;

struct __skyfs_module_state_info
{
    skyfs_u32_t         module_id;
    skyfs_u32_t         module_state;

    skyfs_usage_info_t   module_usage;
};
typedef struct __skyfs_module_state_info skyfs_module_state_info_t;    


struct __skyfs_initconfig_args{
    skyfs_arch_info_t    arch_info;
    skyfs_u32_t          ip_num;
    skyfs_u32_t          pad;
};
typedef struct __skyfs_initconfig_args skyfs_initconfig_args_t;

struct __skyfs_initconfig_ack{


};
typedef struct __skyfs_initconfig_ack skyfs_initconfig_ack_t;

struct __skyfs_getconfig_args{
	skyfs_u32_t          type;
	skyfs_u32_t          id;
};
typedef struct __skyfs_getconfig_args skyfs_getconfig_args_t;

struct __skyfs_getconfig_ack{
    skyfs_arch_info_t    arch_info;
    skyfs_u32_t          ip_num;
    skyfs_u32_t          pad;
};
typedef struct __skyfs_getconfig_ack skyfs_getconfig_ack_t;


typedef struct __skyfs_msg_head {
	skyfs_u32_t  magic; 	   		  /* 0xDCFeC */
	skyfs_u32_t  size;	     	      /* the size of the msg */
	skyfs_u32_t  fs_id;				  /* file system id */
	skyfs_u32_t  seqno;			      /* index of wait request queue */
	skyfs_u32_t  type;			      /* msg type */
	skyfs_s32_t  error;	
	skyfs_u16_t  fromid;      	  	  /* source node of the msg */
	skyfs_u16_t  fromType;  	      /* type of source daemon */
	skyfs_u32_t  opType;
	skyfs_u64_t  ver;   
	skyfs_u64_t  ino;   
}skyfs_msg_head_t;

/*Message struct define*/
typedef struct __skyfs_msg {
	skyfs_u32_t  magic; 	   		  /* 0xDCFeC */
	skyfs_u32_t  size;	     	      /* the size of the msg */
	skyfs_u32_t  fs_id;				  /* file system id */
	skyfs_u32_t  seqno;			      /* index of wait request queue */
	skyfs_u32_t  type;			      /* msg type */
	skyfs_s32_t  error;	
	skyfs_u16_t  fromid;      	  	  /* source node of the msg */
	skyfs_u16_t  fromType;  	      /* type of source daemon */
	skyfs_u32_t  opType ;
	skyfs_u64_t  ver;   
	skyfs_u64_t  ino;   
	union {
		/*MDS related*/
		skyfs_m_statfs_args_t  	statfsReq;
		skyfs_m_statfs_ack_t	statfsAck;

		skyfs_m_lookup_args_t	lookupReq;
		skyfs_m_lookup_ack_t	lookupAck;

		skyfs_m_create_args_t	createReq;
		skyfs_m_create_ack_t	createAck;

		skyfs_m_remove_args_t	removeReq;
		skyfs_m_remove_ack_t	removeAck;

		skyfs_m_getmeta_args_t	getmetaReq;
		skyfs_m_getmeta_ack_t	getmetaAck;

		skyfs_m_setmeta_args_t	setmetaReq;
		skyfs_m_setmeta_ack_t	setmetaAck;

		skyfs_m_readdir_args_t	readdirReq;

		skyfs_m_rename_args_t	renameReq;
		skyfs_m_rename_ack_t	renameAck;

		skyfs_m_link_args_t     linkReq;
		skyfs_m_link_ack_t	    linkAck;
		
		skyfs_m_symlink_args_t	symlinkReq;
		skyfs_m_symlink_ack_t	symlinkAck;

		skyfs_m_readlink_args_t	readlinkReq;
		skyfs_m_readlink_ack_t	readlinkAck;

		skyfs_m_release_args_t	releaseReq;

		skyfs_m_flock_args_t	flockReq;
		skyfs_m_flock_ack_t	    flockAck;

		skyfs_m_initdirc_args_t initdircReq;
		skyfs_m_initdirc_ack_t  initdircAck;

		skyfs_m_getdirc_args_t  getdircReq;
		skyfs_m_getdirc_ack_t   getdircAck;

		skyfs_m_updatedirc_args_t  updatedircReq;
		skyfs_m_updatedirc_ack_t   updatedircAck;

		skyfs_m_updatedird_args_t  updatedirdReq;
		skyfs_m_updatedird_ack_t   updatedirdAck;

		skyfs_m_createsubi_args_t  createsubiReq;
		skyfs_m_createsubi_ack_t   createsubiAck;

		skyfs_m_triggerbla_args_t  triggerblaReq;

		skyfs_m_balanceload_args_t balanceloadReq;

		skyfs_m_getstate_args_t    getstateReq;
		skyfs_m_getstate_ack_t     getstateAck;

		skyfs_m_addhtb_args_t      addhtbReq;

		skyfs_m_getlayout_ack_t    getlayoutAck;
	
		/*OSD related*/
		skyfs_o_readobj_t		        readObjReq;
		skyfs_o_readobj_ack_t		    readObjAck;

		skyfs_o_writeobj_t		        writeObjReq;
		skyfs_o_writeobj_ack_t		    	writeObjAck;

		skyfs_o_multi_writeobj_t		 Multi_writeObjReq;
		skyfs_o_multi_writeobj_ack_t		 Multi_writeObjAck;

		skyfs_o_listxattr_args_t		listxattrReq;
		skyfs_o_listxattr_ack_t		    	listxattrAck;

		skyfs_o_getxattr_args_t		        getxattrReq;
		skyfs_o_getxattr_ack_t		    	getxattrAck;


		skyfs_o_setxattr_args_t		        setxattrReq;
		skyfs_o_setxattr_ack_t		    	setxattrAck;

		skyfs_o_writerep_t		        writeRepReq;

		skyfs_o_createobj_t		        createObjReq;
		skyfs_o_removeobj_t		        removeObjReq;
		skyfs_o_doremoveobj_t		    doRemoveObjReq;

		skyfs_o_preparewrite_t		    prepareWriteReq;
		skyfs_o_preparewrite_ack_t		prepareWriteAck;
		// added by mayl
		skyfs_o_replica_recover_t		replicaRecoverReq;
		skyfs_o_replica_recover_ack_t		replicaRecoverAck;
		
		skyfs_o_replica_query_req_t		replicaQueryReq;
		skyfs_o_replica_query_ack_t		replicaQueryAck;

		skyfs_o_ask_replica_recover_t		replicaAskReq;
		skyfs_o_ask_replica_recover_ack_t	replicaAskAck;


		skyfs_o_commitwrite_t		    commitWriteReq;
		skyfs_o_commitwrite_ack_t		commitWriteAck;

		skyfs_o_truncate_t		        truncateReq;

		skyfs_o_getdevinfo_args_t       getDevinfoReq;
		skyfs_o_getdevinfo_ack_t        getDevinfoAck;

		skyfs_o_readbmeta_args_t        readbmetaReq;
		skyfs_o_readbmeta_ack_t	        readbmetaAck; 

		skyfs_o_writebmeta_args_t       writebmetaReq;
		skyfs_o_writebmeta_ack_t        writebmetaAck;

		skyfs_o_splitsubset_args_t      splitsubsetReq;
		skyfs_o_splitsubset_ack_t       splitsubsetAck;

		skyfs_o_enlargesubset_args_t	enlargesubsetReq;
		skyfs_o_enlargesubset_ack_t		enlargesubsetAck;

		skyfs_o_createsubset_args_t		createsubsetReq;
		skyfs_o_createsubset_ack_t		createsubsetAck;

		skyfs_o_readsubset_args_t		readsubsetReq;
		skyfs_o_readsubset_ack_t		readsubsetAck;

		skyfs_o_writesubset_args_t		writesubsetReq;
		skyfs_o_writesubset_ack_t		writesubsetAck;

		skyfs_o_createdlsubi_args_t     createdlsubiReq;
		skyfs_o_getdlhead_args_t        getdlheadReq;
		skyfs_o_createdlsubset_args_t   createdlsubsetReq;
		skyfs_o_writedlchunk_args_t     writedlchunkReq;
		skyfs_o_updatehdepth_args_t     updatehdepthReq;
		skyfs_o_copyobj_args_t          copyobjReq;
		skyfs_o_updatestate_args_t      updatestateReq;

		/*modle information*/
        skyfs_module_state_t 	     	module_state;
        skyfs_client_state_info_t	    client_state;
        skyfs_module_state_info_t 	    module_state_info;
		skyfs_initconfig_args_t         initconfigReq;
		skyfs_initconfig_ack_t          initconfigAck;
		skyfs_getconfig_args_t          getconfigReq;
		skyfs_getconfig_ack_t           getconfigAck;

		skyfs_u8_t mtext[SKYFS_MAX_MSGBODY_SIZE];/* generic msg content */
	}u;
}skyfs_msg_t;

static inline skyfs_msg_t * __skyfs_get_msg(amp_message_t *reqmsg)
{
	skyfs_msg_t *msg = NULL;
	
    msg = (skyfs_msg_t *)((skyfs_s8_t *)reqmsg + AMP_MESSAGE_HEADER_LEN);
	
	return msg;
}
#endif
/* end of skyfs_msg.h*/
