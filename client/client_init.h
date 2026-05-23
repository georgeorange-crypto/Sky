/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_init.h 
 */

#ifndef __CLIENT_INIT_H
#define __CLIENT_INIT_H

extern amp_comp_context_t  *client_comp_context;
extern int client_this_id;
extern skyfs_mds_info_t mds_info;
extern skyfs_osd_info_t osd_info;
extern skyfs_client_info_t client_info;

extern skyfs_u32_t mds_num;
extern skyfs_u32_t osd_num;
extern skyfs_u32_t client_num;

extern skyfs_u32_t is_async_write;
extern skyfs_u32_t skyfs_ib_flag;

extern struct list_head        client_request_queue;
extern pthread_mutex_t         client_request_queue_lock;
extern sem_t                   client_request_queue_sem;

extern skyfs_arch_info_t       arch_info;
extern sem_t                   client_config_sem;

int __skyfs_C_init_com(void);

skyfs_s32_t __skyfs_C_get_var_conf(void);

skyfs_s32_t __skyfs_C_alloc_pages(void *msg_in, 
				skyfs_u32_t *num, 
				amp_kiov_t **kiov);

void __skyfs_C_free_pages(skyfs_u32_t num, amp_kiov_t *kiov);

skyfs_s32_t __skyfs_C_queue_request(amp_request_t *req);

struct skyfs_pending_req {
	uint64_t req_pointer;
	uint64_t start_time;
	uint64_t op;
	struct list_head pending_entry;
};

#endif
/*This is end of client_init.h*/
