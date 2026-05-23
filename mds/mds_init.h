/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_init.h 
 */

#ifndef __MDS_INIT_H
#define __MDS_INIT_H

extern amp_comp_context_t 	 *mds_comp_context;
extern skyfs_u32_t			 mds_this_id;
extern skyfs_u32_t 		 mds_num;
extern skyfs_u32_t			 osd_num;
extern skyfs_u32_t			 client_num;
extern skyfs_mds_info_t 	 mds_info;
extern skyfs_osd_info_t	 osd_info;
extern skyfs_client_info_t	 client_info;
extern skyfs_u64_t			 mds_nr_request;

extern struct list_head	 mds_config_request_queue;
extern pthread_mutex_t 	 mds_config_request_queue_lock;
extern sem_t				 mds_config_request_queue_sem;

extern struct list_head     mds_wb_request_queue;
extern pthread_mutex_t      mds_wb_request_queue_lock;
extern sem_t                mds_wb_request_queue_sem;

extern struct list_head	 mds_balance_request_queue;
extern pthread_mutex_t 	 mds_balance_request_queue_lock;
extern sem_t				 mds_balance_request_queue_sem;

extern struct list_head     mds_simple_request_queue;
extern pthread_mutex_t      mds_simple_request_queue_lock;
extern sem_t                mds_simple_request_queue_sem;

extern struct list_head	 mds_request_queue;
extern pthread_mutex_t		 mds_request_queue_lock;
extern sem_t				 mds_request_queue_sem;

extern skyfs_u32_t			 mds_red_alarm;
extern skyfs_u32_t			 mds_balance_ratio;
extern skyfs_u32_t			 skyfs_ib_flag;
extern skyfs_u32_t			 skyfs_profile_flag;
extern skyfs_u32_t			 mds_profile_create;
extern skyfs_u32_t			 mds_profile_split;
extern skyfs_u32_t			 mds_profile_enlarge;

extern pthread_mutex_t 	 mds_profile_create_lock;
extern pthread_mutex_t 	 mds_profile_split_lock;
extern pthread_mutex_t 	 mds_profile_enlarge_lock;

extern sem_t                mds_config_sem;
extern skyfs_arch_info_t    arch_info;


skyfs_s32_t __skyfs_MS_parse_parameter(skyfs_s32_t argc, skyfs_s8_t **argv);

skyfs_s32_t __skyfs_MS_get_conf(void);

skyfs_s32_t __skyfs_MS_init_com(void);

skyfs_s32_t __skyfs_MS_init_fs(void);

skyfs_s32_t __skyfs_MS_init_signal(void);

skyfs_s32_t __skyfs_MS_queue_request(amp_request_t *req);


#endif
/*This is end of file*/
