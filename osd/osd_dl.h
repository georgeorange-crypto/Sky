/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_dl.h 
 */

#ifndef __OSD_DL_H
#define __OSD_DL_H

extern skyfs_u32_t skyfs_profile_flag;
extern skyfs_state_info_t osd_state_info;

extern skyfs_htb_t skyfs_dlsubset_cache_htbbase[SKYFS_DLSUBSET_HASH_LEN];
extern skyfs_htb_t skyfs_writecache_htbbase[SKYFS_DLSUBSET_HASH_LEN];

skyfs_s32_t
__skyfs_SS_init_data_layout(void);

skyfs_DL_subset_t * __skyfs_DL_alloc_subset();

skyfs_s32_t 
__skyfs_DL_read_subset(skyfs_DL_subset_t *dl_subset, 
				skyfs_u32_t pad_id, 
				skyfs_u32_t subset_id);

skyfs_s32_t 
__skyfs_DL_release_subset(skyfs_DL_subset_t *dl_subset);

skyfs_u32_t 
__skyfs_DL_judge_osdid(skyfs_u32_t subset_id);


extern skyfs_s32_t
__skyfs_DL_get_head(skyfs_DL_head_t *dl_head, skyfs_u32_t pad_id);

extern skyfs_u32_t
__skyfs_SS_check_dl_htbcache(skyfs_htb_t *htbp);

skyfs_s32_t
__skyfs_DL_do_create_subset(skyfs_DL_subset_head_t *dl_subset_head);

skyfs_s32_t
__skyfs_DL_do_create_subset_index(skyfs_u32_t subset_id, 
				skyfs_u32_t subset_depth, 
				skyfs_u32_t nlink);
skyfs_s32_t
__skyfs_DL_do_update_hdepth(skyfs_u32_t subset_id, 
				skyfs_u32_t split_depth);

extern skyfs_htb_t *
__skyfs_SS_locate_dl_subset(skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *osd_id);

extern skyfs_DL_subset_t *
__skyfs_SS_get_dl_subset(skyfs_htb_t *htbp,
				skyfs_u32_t subset_id);

extern skyfs_DL_chunk_t *
__skyfs_SS_locate_dl_chunk(skyfs_DL_subset_t *dl_subset,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *chunk_id);

extern skyfs_DL_chunk_t *
__skyfs_SS_get_dl_chunk(skyfs_DL_subset_t *dl_subset,
				skyfs_u32_t chunk_id);

extern skyfs_DL_entry_t *
__skyfs_SS_locate_dl_entry(skyfs_DL_chunk_t *dl_chunk,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id);

extern skyfs_DL_entry_t *
__skyfs_SS_lookup_dl_entry(skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *chunk_id);

skyfs_s32_t 
__skyfs_SS_alloc_dl_entry(skyfs_DL_chunk_t *dl_chunk,
				skyfs_DL_entry_t **dl_entry);

skyfs_s32_t
__skyfs_SS_init_dl_entry(skyfs_DL_entry_t *dl_entry,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t client_id);

skyfs_u32_t
__skyfs_SS_check_osd_id(skyfs_DL_file_t *dl_file);

skyfs_s32_t
__skyfs_SS_enlarge_dlsubset(skyfs_DL_subset_t *dl_subset);

skyfs_s32_t
__skyfs_SS_split_dlsubset(skyfs_DL_subset_t *dl_subset);

skyfs_s32_t __skyfs_SS_choose2splitdl(void);

skyfs_s32_t
__skyfs_DL_clear_htbcache(skyfs_u32_t hashindex);

skyfs_s32_t
__skyfs_DL_create_local_subset(skyfs_DL_subset_head_t *dl_subset_head);


/* added by mayl for dl HA */
skyfs_s32_t
__skyfs_DL_writeback_subset_without_release(skyfs_DL_subset_t *dl_subset);

skyfs_s32_t __skyfs_DL_writeback_cache(void);



skyfs_DL_part_t  *  __skyfs_SS_check_alloc_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, 
		 skyfs_u32_t replica_id, skyfs_u32_t location, skyfs_DL_part_t * got_part);
skyfs_u64_t __skyfs_SS_get_objid(skyfs_DL_file_t *dl_file, skyfs_u64_t offset);

skyfs_s32_t __skyfs_SS_locate_obj(skyfs_DL_file_t *dl_file, skyfs_u64_t obj_id);

skyfs_DL_part_t *
__skyfs_DL_lookup_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id);

skyfs_DL_part_t *
__skyfs_DL_get_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id);

skyfs_s32_t __skyfs_SS_fill_des(skyfs_DL_file_t *dl_file, 
			skyfs_u64_t obj_id, 
			skyfs_u32_t partition_id,
			skyfs_dl_dest_t *des);

skyfs_s32_t 
__skyfs_SS_alloc_partition(skyfs_DL_file_t *dl_file, 
			skyfs_u64_t obj_id, 
			skyfs_s32_t *partition_id);

skyfs_s32_t 
__skyfs_SS_alloc_obj(skyfs_DL_file_t *dl_file, 
				skyfs_u64_t obj_id, 
				skyfs_u32_t partition_id);

skyfs_s32_t __skyfs_SS_place_partition(skyfs_DL_file_t *dl_file, 
				skyfs_u32_t partition_id,
				skyfs_dl_dest_t *dest);

skyfs_DL_file_t *
__skyfs_SS_locate_dl_file(skyfs_DL_chunk_t *dl_chunk, skyfs_ino_t ino);

skyfs_s32_t
__skyfs_SS_alloc_dlfile(skyfs_DL_chunk_t *dl_chunk,
			skyfs_DL_file_t **dl_file);

skyfs_s32_t
__skyfs_SS_init_dlfile(skyfs_DL_file_t *dl_file, 
		skyfs_ino_t ino, 
		skyfs_u32_t partition_num,
		skyfs_u32_t replica_num,
		skyfs_u32_t obj_size,
		skyfs_u32_t real_location,
		skyfs_u32_t hashkey);

skyfs_s32_t __skyfs_SS_alloc_objs(skyfs_DL_file_t *dl_file, skyfs_u32_t partition_id);
#endif
/*This is end of osd_dl.h*/
