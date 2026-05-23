/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_ito.h 
 */

#ifndef __OSD_ITO_H
#define __OSD_ITO_H

extern uint32_t
find_replica_osd(uint32_t osd_cnt, int osd_gid, int replica_id , int replica_cnt);


extern uint32_t 
find_osdgid(uint32_t osd_cnt, skyfs_ino_t ino, size_t obj_id);

amp_request_t *
__skyfs_O2O_write_bmeta(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id, 
				skyfs_u32_t bmeta_id,
				skyfs_M_bmeta_t *tmp2);

skyfs_s32_t
__skyfs_O2O_create_subset_file(skyfs_u32_t osd_id, 
				skyfs_M_subset_head_t *subset_head);

skyfs_s32_t 
__skyfs_O2O_create_dl_subset_index(skyfs_u32_t osd_id,
				skyfs_u32_t pad_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t subset_depth,
				skyfs_u32_t nlink_origin);

skyfs_s32_t
__skyfs_O2O_get_dl_head(skyfs_u32_t osd_id,
				skyfs_u32_t pad_id,
				skyfs_DL_head_t *dl_head);

skyfs_s32_t
__skyfs_O2O_create_dl_subset(skyfs_u32_t osd_id,
				skyfs_u32_t replica_id,
				skyfs_DL_subset_head_t *dl_subset_head);

amp_request_t *
__skyfs_O2O_write_dlchunk(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_DL_chunk_t *dl_chunk);

skyfs_s32_t
__skyfs_O2O_update_head_depth(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth);

skyfs_s32_t
__skyfs_O2O_move_obj(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_s8_t  *chunkfile);

amp_request_t *
__skyfs_O2O_write_replica(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id, 
				skyfs_u32_t chunk_id, 
				skyfs_io_vector_t *vec,
				skyfs_s8_t *buf);

skyfs_s32_t __skyfs_O2O_collect_state();

skyfs_s32_t 
__skyfs_O2O_prepare_write(skyfs_DL_file_t *dl_file,
	amp_kiov_t *kiov, 
	skyfs_dl_dest_t *des,
	skyfs_io_vector_t *vec,
	skyfs_u32_t partition_id);

skyfs_s32_t 
__skyfs_O2O_commit_write(skyfs_DL_file_t *dl_file,
	skyfs_dl_dest_t *des,
	skyfs_io_vector_t *vec,
	skyfs_u32_t partition_id,
	skyfs_u32_t is_new_partition);

skyfs_s32_t 
__skyfs_O2O_remove_obj(skyfs_DL_file_t *dl_file,
	skyfs_dl_dest_t *des,
	skyfs_u64_t obj_id,
	skyfs_u32_t partition_id);

#endif
/*This is end of osd_ito.h*/
