/*
 *  Copyright (c) 2004  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_types.h 
 */
#ifndef __SKYFS_TYPES_H
#define __SKYFS_TYPES_H

typedef unsigned char      skyfs_u8_t;
typedef char               skyfs_s8_t;
typedef unsigned short     skyfs_u16_t;
typedef short              skyfs_s16_t;
typedef unsigned int       skyfs_u32_t;
typedef int                skyfs_s32_t;
typedef unsigned long long skyfs_u64_t;
typedef long long          skyfs_s64_t;
typedef double			   skyfs_f64_t;

typedef skyfs_u64_t skyfs_off_t;
typedef skyfs_u64_t skyfs_loff_t;
typedef skyfs_u64_t skyfs_size_t;
typedef skyfs_u64_t skyfs_ino_t;
typedef skyfs_s32_t skyfs_pid_t;
typedef skyfs_u64_t skyfs_time_t;

typedef skyfs_u64_t skyfs_nlink_t;
typedef skyfs_u32_t skyfs_mode_t;
typedef skyfs_u32_t skyfs_uid_t;
typedef skyfs_u32_t skyfs_gid_t;

typedef struct __skyfs_object_id{
	skyfs_s8_t	obj_name[SKYFS_OBJECT_NAME_LEN];
	skyfs_u32_t grp_index;
	skyfs_u32_t pad;
}skyfs_object_id_t;

typedef struct timeval skyfs_timespec_t;

#ifdef __KERNEL__
typedef struct __skyfs_kernel_thread{
	struct task_struct *thread;
	struct semaphore startstop_sem;
	int state;
	int terminate;
	int snum;
}skyfs_kernel_thread_t;
#else
typedef struct __skyfs_user_thread{
	pthread_t thread;
	sem_t 		startsem;
	sem_t		stopsem;
	skyfs_u32_t	is_up;
	skyfs_u32_t	to_shutdown;
	skyfs_u32_t	seqno;
	// adm use it to indentify the kind of file to copy
	skyfs_u32_t	pad;
}skyfs_user_thread_t;
#endif

typedef struct __skyfs_layout_t{
	skyfs_s32_t id;
	skyfs_s32_t virtual;
	skyfs_f64_t ratio;
}skyfs_layout_t;

typedef struct __skyfs_virtal_layout_t{
	skyfs_s32_t id;
	skyfs_u32_t start_hashvalue;
	skyfs_f64_t end_hash_value;
}skyfs_virtual_layout_t;


typedef struct __skyfs_layout_L1_t{
	skyfs_s32_t id;
	skyfs_u32_t extend_flag;
	skyfs_u64_t hashvalue;
	//skyfs_u64_t hashvalue;
}skyfs_layout_L1_t;

typedef struct __skyfs_layout_L2_t{
	skyfs_s32_t id;
	skyfs_u32_t extend_flag;
	skyfs_u64_t hashvalue;
}skyfs_layout_L2_t;

typedef struct __skyfs_state_info{
	skyfs_u64_t			cpu_usage;
	skyfs_u64_t			mem_usage;
	skyfs_u64_t			disk_usage;
	skyfs_u32_t			state_version;
	skyfs_u32_t			request_num;
}skyfs_state_info_t;

typedef struct __skyfs_mds_status{
	skyfs_state_info_t state_info;
	skyfs_u32_t		   state_version;
	skyfs_u32_t		   hashnum;
	skyfs_u32_t		   seqnum;
	pthread_mutex_t	   lock;
}skyfs_mds_status_t;

typedef struct __skyfs_osd_status{
	skyfs_state_info_t state_info;
	skyfs_u32_t		   state_version;
	skyfs_u32_t		   hashnum;
	skyfs_u32_t		   seqnum;
	pthread_mutex_t	   lock;
}skyfs_osd_status_t;

typedef struct __skyfs_obj_id{
    skyfs_ino_t        ino;
	skyfs_u32_t        offset;
	skyfs_u32_t        pad;
}skyfs_obj_id_t;
#endif
/* end of skyfs_types.h*/
