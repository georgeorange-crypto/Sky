/*
 *  Copyright (c) 2013 by XING JING
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_fs.h 
 */

#ifndef __OSD_FS_H
#define __OSD_FS_H

/*skyfs_DL_file_t holds data location of file, file content can be located through ino + partition_id + replica_id the content belogs to.*/
/*If obj_size < 1MB, all the obj will be allocated in local*/
typedef struct __skyfs_DL_part{
	skyfs_ino_t     	ino; //8
	skyfs_u32_t     	partition_id; //12
	skyfs_u32_t     	replica_id;  //16
	skyfs_u32_t    		free;//20
	skyfs_u32_t    		state; // 1: normal 2 syncing  // 24
	skyfs_u32_t     	obj_location[SKYFS_MAX_OBJ_PER_PART]; //88
	char * 			interval_tree_handles[SKYFS_MAX_OBJ_PER_PART];
	skyfs_s64_t		replica_write_version; // added by mayl //96
	skyfs_s64_t		max_write_version; // added by mayl , if max_write_version equal  replica_write_version of this osd, then choose it to READ //104	
	struct list_head	part_hash;
	pthread_rwlock_t	part_lock;
}skyfs_DL_part_t;

typedef struct __skyfs_replica_recover_head{
	skyfs_ino_t ino;
	skyfs_u64_t obj_id; // 1 partition_id for partition recovery
	skyfs_u64_t start_offset;
	skyfs_u64_t size;
}skyfs_replica_recover_head_t;

typedef struct __skyfs_replica_osd{
	skyfs_u64_t ino;
	skyfs_u64_t part_id;
	skyfs_u64_t osd_gid;
        skyfs_u32_t replica_id;
	skyfs_u32_t osd_id;
	struct list_head replica_hash;
}skyfs_replica_osd_t;

typedef struct __skyfs_DL_file{
	skyfs_ino_t        	ino;
	skyfs_u64_t		end_pos;
	skyfs_u32_t     	partition_num;
	skyfs_u32_t     	replica_num;
	//skyfs_u64_t     	write_version;
	skyfs_u32_t     	obj_size;
	skyfs_u32_t     	real_location;
	skyfs_u32_t     	nextfree;
	skyfs_u32_t     	hashkey;
	struct list_head	pending_io_list; // TODO mayl added, pending io list in runtime
	struct list_head *	pending_io_pointer; // TODO mayl added , pending io list in storage; for enlarge and split 
	skyfs_htb_t         *objbuf_hash_base;
    //pthread_mutex_t     lock;
}skyfs_DL_file_t;

typedef struct __skyfs_IO_file{

	skyfs_ino_t        	ino;
	struct list_head	pending_io_list; // TODO mayl added, pending io list in runtime
	struct list_head	htb_entry; // TODO mayl added, pending io list in htb
	pthread_spinlock_t 	lock;
}skyfs_io_file_t;

typedef struct __skyfs_pending_IO{
	skyfs_u64_t ino;
	skyfs_u64_t offset;
	skyfs_u64_t length;
	skyfs_u32_t type;
	struct list_head	pending_io_entry;
}skyfs_pending_io_t;

typedef struct __skyfs_DL_entry{
    skyfs_u32_t         id;
    skyfs_u32_t         hashkey;
    skyfs_ino_t         ino;
    skyfs_u64_t         obj_id;
    skyfs_u32_t         update;
    skyfs_u32_t         transfer;
    skyfs_u32_t         nextfree;
    skyfs_s32_t         fd;
    skyfs_u32_t         latest_osd;
    skyfs_u32_t         fir_osd;
    skyfs_u32_t         sec_osd;
    skyfs_u32_t         thi_osd;
    skyfs_u32_t         client_id;
    skyfs_u32_t         access_time;
    pthread_mutex_t     lock;
}skyfs_DL_entry_t;

typedef struct __skyfs_DL_subset_index{
	skyfs_u32_t			subset_id;
	skyfs_u32_t			split_depth;
	skyfs_u32_t			subset_depth;
	skyfs_timespec_t	last_time;
	pthread_mutex_t		lock;
	pthread_rwlock_t	rwlock;
	struct list_head	subset_hash;
	struct list_head	subset_list;
	//Need to delete below later
    skyfs_u32_t         nlink_origin;
	skyfs_u32_t         nlink_update;
}skyfs_DL_subset_index_t;

typedef struct __skyfs_DL_chunk{
	skyfs_u64_t         hashvalue;
	skyfs_u32_t         chunk_id;
	skyfs_s32_t         nfree;
	skyfs_u32_t         firstfree;
	pthread_mutex_t     lock;
	pthread_rwlock_t    rwlock;
	struct list_head    chunk_hash;
	struct list_head    chunk_list;
	skyfs_DL_file_t	    dlfile[SKYFS_DLFILE_PER_CHUNK];
    //skyfs_DL_entry_t    dlentry[SKYFS_DLENTRY_PER_CHUNK];
	//Need to delete below later
    skyfs_u32_t         nlink_origin;
	skyfs_u32_t         nlink_update;
}skyfs_DL_chunk_t;

typedef struct __skyfs_DL_subset_head{
	skyfs_u32_t         subset_id;
	skyfs_u32_t         split_depth;
	skyfs_u32_t         subset_depth;
	skyfs_u32_t         nlink;
	//Need to delete below later
    skyfs_u32_t         fir_osd;
	skyfs_u32_t         sec_osd;
	skyfs_u32_t         thi_osd;
}skyfs_DL_subset_head_t;

typedef struct __skyfs_DL_subset{
	skyfs_u32_t         subset_id;
	skyfs_u32_t         split_depth;
	skyfs_u32_t         subset_depth;
	pthread_mutex_t     lock;
	pthread_rwlock_t    rwlock;
	skyfs_htb_t         *chunk_hash_base;
	struct list_head    chunk_head;
	struct list_head    subset_hash;
	//Need to delete below later
    skyfs_u32_t         fir_osd;
    skyfs_u32_t         sec_osd;
    skyfs_u32_t         thi_osd;
    skyfs_u32_t         nlink_origin;
	skyfs_u32_t         nlink_update;
}skyfs_DL_subset_t;

typedef struct __skyfs_DL_head{
	skyfs_u32_t         depth;
	skyfs_u32_t         ver;
	skyfs_u8_t          subset_bm[SKYFS_DLSUBSET_BM_LEN];
	pthread_mutex_t     lock;
	skyfs_htb_t         *subset_hash_base;	
	skyfs_htb_t         *partition_hash_base;	
}skyfs_DL_head_t;

typedef struct __skyfs_DL_depth{
	skyfs_u32_t         depth;
	skyfs_u32_t         ver;
	skyfs_u8_t          subset_bm[SKYFS_DLSUBSET_BM_LEN];
	struct list_head    depth_list;
	pthread_mutex_t     lock;
}skyfs_DL_depth_t;

typedef struct __skyfs_DL_nodeinfo{
	skyfs_u32_t         osd_id;
	skyfs_u32_t			access_times;
	skyfs_u32_t			active_files;
	skyfs_timespec_t    last_time;
	struct list_head    node_list;
	struct list_head    file_head;
}skyfs_DL_nodeinfo_t;

typedef struct __skyfs_DL_fileinfo{
	skyfs_ino_t         ino;
	skyfs_u32_t         access_times;
	skyfs_timespec_t    last_time;
	struct list_head    file_list;
}skyfs_DL_fileinfo_t;

typedef struct __skyfs_O_databuf{
	skyfs_ino_t         ino;
	skyfs_u64_t         offset;
	skyfs_u32_t			client_id;
	skyfs_u32_t         count;
	skyfs_u32_t         partition_id;
	skyfs_s32_t         ref;
	struct list_head    databuf_hash;
	skyfs_s8_t          *buf;
}skyfs_O_databuf_t;

typedef struct __skyfs_O_filebuf{
	skyfs_ino_t         ino;
	skyfs_u64_t         timelen;
	struct list_head    file_list;
	struct list_head    obj_head;
	skyfs_htb_t         *objbuf_hash_base;
	pthread_rwlock_t    rwlock;
}skyfs_O_filebuf_t;

typedef struct __skyfs_O_objbuf{
	skyfs_u64_t         ino;
	skyfs_u64_t         obj_id;
	skyfs_u64_t         offset;
	skyfs_u32_t         obj_size;
	struct list_head    obj_hash;
	struct list_head    obj_list;
	struct list_head    page_head;
	skyfs_htb_t         *pagebuf_hash_base;	
}skyfs_O_objbuf_t;

typedef struct __skyfs_O_pagebuf{
	skyfs_u64_t         page_id;
	struct list_head    page_hash;
	struct list_head    page_list;
	skyfs_s8_t          page[SKYFS_PAGE_SIZE];
}skyfs_O_pagebuf_t;
	
#endif
/*This is end of osd_fs.h*/
