/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_init.h 
 */

#ifndef __OSD_INIT_H
#define __OSD_INIT_H

#define OSD_SERVICE_COUNT (10)

/* added by mayl */
//skyfs_htb_t skyfs_dop_htbbase[SKYFS_DOP_HASH_LEN];
extern 	skyfs_u32_t  is_async_write;
extern skyfs_htb_t skyfs_dop_htbbase[];
extern amp_comp_context_t 		* osd_comp_context;
extern skyfs_u32_t				osd_this_id;
extern skyfs_u32_t				osd_this_pid;
extern skyfs_u32_t				pad_id;
extern skyfs_u32_t 			mds_num;
extern skyfs_u32_t				osd_num;
extern skyfs_u32_t				client_num;
extern skyfs_mds_info_t 		mds_info;
extern skyfs_osd_info_t		osd_info;
extern skyfs_client_info_t		client_info;
extern skyfs_u64_t				osd_nr_request;
extern skyfs_u32_t				skyfs_ib_flag;
extern skyfs_u32_t				skyfs_default_dlsb_bits;
extern skyfs_u32_t				skyfs_dl_type;
extern skyfs_u32_t				skyfs_replica;
extern skyfs_u32_t             skyfs_lid;
extern skyfs_u32_t             osd_readahead_size;
extern struct list_head		osd_config_request_queue;
extern pthread_mutex_t 		osd_config_request_queue_lock;
extern sem_t					osd_config_request_queue_sem;

extern struct list_head		osd_stat_request_queue;
extern pthread_mutex_t 		osd_stat_request_queue_lock;
extern sem_t			osd_stat_request_queue_sem;

extern struct list_head     	osd_serveout_queue;
extern pthread_mutex_t      	osd_serveout_queue_lock;
extern sem_t                	osd_serveout_queue_sem;

extern struct list_head     	osd_simple_queue;
extern pthread_mutex_t      	osd_simple_queue_lock;
extern sem_t                	osd_simple_queue_sem;

extern struct list_head		osd_request_queue;
extern pthread_mutex_t			osd_request_queue_lock;
extern sem_t			osd_request_queue_sem;

extern struct list_head        osd_filebuf_queue;
extern pthread_mutex_t         osd_filebuf_queue_lock;

extern skyfs_timespec_t    	rp_start_time;
extern skyfs_timespec_t    	rp_end_time;
extern skyfs_u32_t         	read_nr_request;
extern pthread_mutex_t     	read_perf_lock;

extern skyfs_timespec_t    	wp_start_time;
extern skyfs_timespec_t    	wp_end_time;
extern skyfs_u32_t         	write_nr_request;
extern pthread_mutex_t     	write_perf_lock;

extern pthread_mutex_t     	osd_commit_write_lock;

extern skyfs_timespec_t    	start_waiting_time;
extern skyfs_timespec_t    	end_waiting_time;

extern skyfs_u32_t         	waiting_time_flag;

extern skyfs_ino_t				last_ino;
extern skyfs_u32_t				last_id;

extern skyfs_u32_t         	osd_entry_creation ;
extern skyfs_u32_t         	old_osd_entry_creation ;
extern skyfs_timespec_t    	hotpot_stime;
extern skyfs_timespec_t    	hotpot_etime;

extern skyfs_osd_status_t 		osd_status[SKYFS_MAX_OSD_NUM];
extern skyfs_u32_t 			sort_osd_status[SKYFS_MAX_OSD_NUM];

extern sem_t                   osd_config_sem;
extern skyfs_arch_info_t       arch_info;

extern struct list_head osd_node_head;
extern pthread_mutex_t  osd_node_head_lock;

extern skyfs_s32_t      osd_block_insert_objbuf;
extern pthread_mutex_t  osd_block_insert_lock;

skyfs_s32_t __skyfs_SS_parse_parameter(skyfs_s32_t argc, skyfs_s8_t **argv);

skyfs_s32_t __skyfs_SS_get_conf(void);

skyfs_s32_t __skyfs_SS_init_com(void);

skyfs_s32_t __skyfs_SS_init_osd(void);

skyfs_s32_t __skyfs_SS_init_signal(void);

skyfs_s32_t __skyfs_SS_queue_request(amp_request_t *req);

skyfs_s32_t 
__skyfs_SS_alloc_pages(void *msg_in, skyfs_u32_t *num, amp_kiov_t **kiov);

void __skyfs_SS_free_pages(skyfs_u32_t num, amp_kiov_t *kiov);

#endif
/*This is end of osd_init.h*/
