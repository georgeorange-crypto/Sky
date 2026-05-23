/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_fs.h 
 */

#ifndef __MDS_FS_H
#define __MDS_FS_H

extern amp_comp_context_t 	*mds_comp_context;
extern skyfs_u32_t			mds_this_id;

#define OFFSET_MAX (0x7fffffffffffffffLL)
#if 0
typedef struct __skyfs_M_cmeta{
	skyfs_u32_t			pad;
	skyfs_u32_t			type;
	skyfs_s32_t			nextfree;/*Set -1 when deleting cmeta*/
	skyfs_u32_t			conflict_index;
	skyfs_u64_t			hashkey;
	skyfs_u64_t			ino;
	skyfs_u64_t			size;
	skyfs_u64_t			dev;
	skyfs_u16_t			mode;
	skyfs_u16_t			nlink;
	skyfs_u32_t			uid;
	skyfs_u32_t			gid;
	skyfs_u32_t			depth;
	skyfs_timespec_t	atime;
	skyfs_timespec_t	ctime;
	skyfs_timespec_t	mtime;
	skyfs_s8_t			name[SKYFS_MAX_NAME_LEN];
}skyfs_M_cmeta_t;
#endif

typedef struct __skyfs_M_mmeta{
	skyfs_u32_t			id;
	skyfs_u32_t			status;
	skyfs_u64_t			offset;
	skyfs_M_cmeta_t		*cmetap;
	struct list_head *  lock_htb_head; // added by mayl , record the list head which insert to lock htb 
	struct list_head	posix_lock_head; // added by mayl for flock
	struct list_head	flock_head; // added by mayl for flock
	skyfs_u8_t		open_clt[SKYFS_NODE_BM_LEN];
	pthread_mutex_t		lock;
}skyfs_M_mmeta_t;


/* mayl add it for inode lock temp cache */
typedef struct __skyfs_M_inode_lock_t{
	skyfs_u64_t			ino;
	struct list_head	hash_tab_head; // added by mayl for locked inode cache in hash table
	struct list_head	* posix_lock_head; // added by mayl for flock
	struct list_head	* flock_head; // added by mayl for flock
}skyfs_M_inode_lock_t;


typedef struct __skyfs_M_bmeta{
	skyfs_u64_t			hashvalue;
	skyfs_u32_t			box_id;
	skyfs_s32_t			nfree;
	skyfs_s32_t			firstfree;
	skyfs_s32_t			nlink_orign;
	skyfs_s32_t			nlink_update;
	skyfs_s32_t 		last_one;
	skyfs_timespec_t	first_time;
	skyfs_timespec_t	last_time;
	pthread_mutex_t		lock;
	pthread_rwlock_t	rwlock;
	struct list_head	bmeta_hash;
	struct list_head	bmeta_list;
	skyfs_M_cmeta_t		cmetap[SKYFS_MAX_META_PER_BOX];
	skyfs_M_mmeta_t		mmetap[SKYFS_MAX_META_PER_BOX];
}skyfs_M_bmeta_t;

typedef struct __skyfs_M_subset_cache{
	skyfs_u32_t			dir_id;
	skyfs_u32_t			subset_id;
	skyfs_u32_t			split_depth;
	skyfs_u32_t			subset_depth;
	skyfs_s32_t			nlink_orign;
	skyfs_s32_t			nlink_update;
	skyfs_timespec_t	first_time;
	skyfs_timespec_t	last_time;
	pthread_mutex_t		lock;
	pthread_rwlock_t	rwlock;
	skyfs_htb_t			*bmeta_hash_base;
	struct list_head	bmeta_head;
	struct list_head	subset_hash;
}skyfs_M_subset_cache_t;

typedef struct __skyfs_M_subset_index{
	skyfs_u32_t			dir_id;
	skyfs_u32_t			subset_id;
	skyfs_u32_t			split_depth;
	skyfs_u32_t			subset_depth;
	skyfs_u32_t			nlink_orign;
	skyfs_u32_t			nlink_update;
	skyfs_timespec_t	last_time;
	pthread_mutex_t		lock;
	pthread_rwlock_t	rwlock;
	struct list_head	subset_hash;
	struct list_head	subset_list;				
}skyfs_M_subset_index_t;

typedef struct __skyfs_M_subset_head{
	skyfs_u32_t			dir_id;
	skyfs_u32_t			subset_id;
	skyfs_u32_t			split_depth;
	skyfs_u32_t			subset_depth;
	skyfs_u32_t			nlink;
	skyfs_u32_t			pad;
}skyfs_M_subset_head_t;

typedef struct __skyfs_M_dir_cache{
	skyfs_M_cmeta_t		cmeta;
	skyfs_u32_t			dir_id;
	skyfs_u32_t			depth; 
	//skyfs_u32_t			rename_depth; // add by mayh for rename 
	skyfs_u32_t			nlink_orign;
	skyfs_s32_t			nlink_update;
	skyfs_u8_t			subset_bm[SKYFS_SUBSET_BM_LEN];
	//skyfs_u8_t			rename_subset_bm[SKYFS_SUBSET_BM_LEN]; // added by mayl for rename
	pthread_mutex_t		lock;
	pthread_rwlock_t	rwlock;
	skyfs_htb_t			*subset_hash_base;
	struct list_head	subset_head;
	struct list_head	dir_hash;
	struct list_head	dir_head;
}skyfs_M_dir_cache_t;

typedef struct __skyfs_M_dir_depth{
	skyfs_u32_t			dir_id;
	skyfs_u32_t			depth;
	skyfs_u32_t         ver;
	skyfs_u8_t			subset_bm[SKYFS_SUBSET_BM_LEN];
	struct list_head	dir_depth_hash;
	pthread_mutex_t		lock;
}skyfs_M_dir_depth_t;

typedef struct __skyfs_meta_vector{
	skyfs_M_bmeta_t *bmeta;
	skyfs_u32_t		dir_id;
	skyfs_u32_t		subset_id;
	skyfs_u32_t		bmeta_id;
	skyfs_u32_t		size;
}skyfs_meta_vector_t;

typedef struct __skyfs_M_dir_id{
	skyfs_u32_t		dir_id;
	skyfs_u32_t		pad;
	struct list_head dir_head;
}skyfs_M_dir_id_t;

typedef struct __skyfs_M_wb_req{
	skyfs_s32_t 	total_bmeta_num;
	struct list_head req_list;
}skyfs_M_wb_req_t;

typedef struct __skyfs_M_wb_entry{
	skyfs_u32_t 	dir_id;
	skyfs_u32_t 	subset_id;
	struct list_head wb_list;
}skyfs_M_wb_entry_t;

typedef struct __skyfs_M_ino{
	skyfs_u32_t 	dirid_head;
	skyfs_u32_t 	conflict_ino;
}skyfs_M_ino_t;

/*add by mayl for flock*/
typedef struct __skyfs_M_flock{
	skyfs_u32_t			clt_id;
	skyfs_u32_t			l_pid;
	skyfs_u32_t			l_type; // RDLOCK, WRLOCK, UNLOCK 
	skyfs_u32_t			l_flags; // 1: SKYFS_FL_POSIX ; 2 SKYFS_FL_FLOCK
	skyfs_u64_t			l_start;
	skyfs_u64_t			l_len;   
	skyfs_u64_t			l_end; // l_end = l_start+len-1
	skyfs_u64_t			lock_owner;
	struct list_head	flock_list; // insert to mmeta
	struct list_head	posix_lock_list; // insert to mmeta
	
	
	//skyfs_u32_t			status;
	//skyfs_u64_t			offset;
	//skyfs_M_cmeta_t		*cmetap;
	//skyfs_u8_t			open_clt[SKYFS_NODE_BM_LEN];
	//pthread_mutex_t		lock;
}skyfs_M_flock_t;


#endif
/*This is end of mds_fs.h*/
