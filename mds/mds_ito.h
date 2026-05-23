/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_ito.h 
 */

#ifndef __M2O_API_H
#define __M2O_API_H

extern skyfs_s32_t
__skyfs_M2O_read_bmeta(skyfs_u32_t osd_id, skyfs_meta_vector_t *vector);

extern amp_request_t *
__skyfs_M2O_write_bmeta(skyfs_u32_t osd_id, skyfs_meta_vector_t *vector);

extern skyfs_s32_t
__skyfs_M2O_read_subset(skyfs_u32_t osd_id, skyfs_M_subset_head_t *subset_head);

extern skyfs_s32_t
__skyfs_M2O_write_subset(skyfs_u32_t osd_id, skyfs_M_subset_head_t subset_head);

extern amp_request_t * 
__skyfs_M2O_split_subset_file(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth,
				skyfs_u32_t subset_depth);

extern amp_request_t * 
__skyfs_M2O_enlarge_subset_file(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id);

extern skyfs_s32_t
__skyfs_M2O_create_subset_file(skyfs_u32_t osd_id,
				skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_M2O_free_storage(skyfs_ino_t ino, skyfs_u64_t size);

#endif
/*This is end of mds_ito.h*/
