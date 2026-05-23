/*
 *  Copyright (c) 2007  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_op.h 
 */

#ifndef __MDS_OP_H
#define __MDS_OP_H

extern skyfs_timespec_t       last_stat_time; 

extern pthread_mutex_t skyfs_statfs_lock;

void __skyfs_MS_statfs(amp_request_t *req);

void __skyfs_MS_lookup(amp_request_t *req);

void __skyfs_MS_getattr(amp_request_t *req);

void __skyfs_MS_setattr(amp_request_t *req);

void __skyfs_MS_flock(amp_request_t *req);

void __skyfs_MS_release(amp_request_t *req);

void __skyfs_MS_create(amp_request_t *req);

void __skyfs_MS_remove(amp_request_t *req);

void __skyfs_MS_link(amp_request_t *req);

void __skyfs_MS_symlink(amp_request_t *req);

void __skyfs_MS_readlink(amp_request_t *req);

void __skyfs_MS_rename(amp_request_t *req);

void __skyfs_MS_readdir(amp_request_t *req);

void __skyfs_MS_init_dcache(amp_request_t *req);

void __skyfs_MS_get_dcache(amp_request_t *req);

void __skyfs_MS_update_dcache(amp_request_t *req);

void __skyfs_MS_update_ddepth(amp_request_t *req);

void __skyfs_MS_create_subindex(amp_request_t *req);

void __skyfs_MS_get_state(amp_request_t *req);

void __skyfs_MS_add_htbcache(amp_request_t *req);

void __skyfs_MS_get_layout(amp_request_t *req);

void __skyfs_MS_init_config(amp_request_t *req);
#endif
/*This is end of file*/
