/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_op.h 
 */

#ifndef __OSD_OP_H
#define __OSD_OP_H

void __skyfs_SS_read(amp_request_t *req);

void __skyfs_SS_write(amp_request_t *req);

void __skyfs_SS_write_multi_objs(amp_request_t *req);

void __skyfs_SS_create_obj(amp_request_t *req);

void __skyfs_SS_remove_obj(amp_request_t *req);

void __skyfs_SS_commit(amp_request_t *req);

void __skyfs_SS_truncate(amp_request_t *req);

void __skyfs_SS_get_devinfo(amp_request_t *req);

void __skyfs_SS_enlarge_subset(amp_request_t *req);

void __skyfs_SS_split_subset(amp_request_t *req);

void __skyfs_SS_create_subset(amp_request_t *req);

void __skyfs_SS_read_bmeta(amp_request_t *req);

void __skyfs_SS_write_bmeta(amp_request_t *req);

void __skyfs_SS_read_subset(amp_request_t *req);

void __skyfs_SS_write_subset(amp_request_t *req);

void __skyfs_SS_create_dl_subset_index(amp_request_t *req);

void __skyfs_SS_get_dl_head(amp_request_t *req);

// added by mayl
void __skyfs_SS_query_replica_state(amp_request_t *req);
void __skyfs_SS_handle_replica_recover(amp_request_t *req);

void __skyfs_SS_create_dl_subset(amp_request_t *req);

void __skyfs_SS_write_dlchunk(amp_request_t *req);

void __skyfs_SS_update_head_depth(amp_request_t *req);

void __skyfs_SS_copy_obj(amp_request_t *req);

void __skyfs_SS_write_replica(amp_request_t *req);

void __skyfs_SS_get_state(amp_request_t *req);

void __skyfs_SS_update_state(amp_request_t *req);

void __skyfs_SS_init_config(amp_request_t *req);

void __skyfs_SS_remote_replica_write(amp_request_t *req);

void __skyfs_SS_recover_replica_write(amp_request_t *req);
//void __skyfs_SS_prepare_write(amp_request_t *req);

void __skyfs_SS_commit_write(amp_request_t *req);

void __skyfs_SS_do_removeobj(amp_request_t *req);
skyfs_s32_t 
skyfs_SS_mv_obj(amp_request_t *req, skyfs_u32_t osd_id);

void __skyfs_SS_listxattr(amp_request_t *req);

#endif
/*This is end of osd_op.h*/
