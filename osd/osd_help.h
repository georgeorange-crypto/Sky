/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_help.h 
 */

#ifndef __OSD_HELP_H
#define __OSD_HELP_H

skyfs_u32_t
__skyfs_SS_get_subset_id(skyfs_u32_t hashvalue, skyfs_u32_t depth);

skyfs_s32_t    
__skyfs_SS_init_reply(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t req_type,
                skyfs_u32_t req_niov,
                amp_kiov_t 	*req_iov,
				skyfs_u32_t size);

skyfs_u32_t __skyfs_SS_get_osdid(void);

skyfs_s32_t __skyfs_SS_get_osdpid(skyfs_u32_t osd_id);

skyfs_s32_t
__skyfs_SS_compose_chunkfile_pathname(skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_s8_t *chunkfile);
skyfs_s32_t
__skyfs_SS_create_chunkdir(skyfs_u32_t dl_subset_id, 
				skyfs_u32_t dl_chunk_id);

skyfs_s32_t
__skyfs_SS_move_obj(skyfs_DL_entry_t *dl_entry, 
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_u32_t new_subset_id, 
				skyfs_u32_t new_chunk_id,
				skyfs_u32_t osd_id);

skyfs_s32_t __skyfs_SS_forward_request(amp_request_t *req,
				skyfs_u32_t com_type,
				skyfs_u32_t id);

skyfs_s32_t
__skyfs_SS_open_subset_dir(skyfs_u32_t subset_id, 
		skyfs_u32_t chunk_id,
		skyfs_ino_t ino,
		skyfs_u64_t obj_id);

skyfs_s32_t
__skyfs_SS_compose_partname(skyfs_ino_t ino,
					skyfs_u32_t partition_id,
					skyfs_u32_t replica_id,
					skyfs_s8_t  *pname);

skyfs_s32_t
__skyfs_OSD_init_req(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t msg_type,
                skyfs_u32_t ack_flag,
                skyfs_u32_t req_type,
				skyfs_u32_t	msgsize);

skyfs_s32_t
__skyfs_SS_open_obj_file(skyfs_ino_t ino,
		skyfs_u32_t partition_id,
		skyfs_u64_t obj_id,
		skyfs_u32_t replica_id,
		skyfs_s32_t flag);

skyfs_s32_t
__skyfs_SS_prepareopen_objfile(skyfs_ino_t ino,
		skyfs_u32_t partition_id,
		skyfs_u64_t obj_id,
		skyfs_u32_t replica_id,
		skyfs_s8_t *path);

#endif
/*This is end of osd_help.h*/
