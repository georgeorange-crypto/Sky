/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_itm.h 
 */

#ifndef __M2M_API_H
#define __M2M_API_H

extern skyfs_s32_t
__skyfs_M2M_init_dir_cache(skyfs_u32_t mds_id, 
				skyfs_u32_t dir_id,
				skyfs_M_cmeta_t *dir_cmeta);

extern skyfs_s32_t
__skyfs_M2M_get_dir_cache(skyfs_u32_t mds_id, 
				skyfs_u32_t dir_id,
				skyfs_M_dir_cache_t *dir_cache);

extern skyfs_s32_t
__skyfs_M2M_update_dir_cache(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_s32_t update,
				skyfs_M_cmeta_t *dir_cmeta);

extern skyfs_s32_t
__skyfs_M2M_update_dir_depth(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth);

extern skyfs_s32_t 
__skyfs_M2M_create_subset_index(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t subset_depth,
				skyfs_u32_t nlink);

extern skyfs_s32_t __skyfs_M2M_trigger_balance(skyfs_state_info_t state_info);

extern skyfs_s32_t 
__skyfs_M2M_add_htbcache(skyfs_u32_t mds_id, 
				skyfs_u32_t index, 
				skyfs_u32_t last_flag);

extern skyfs_s32_t
__skyfs_M2M_collect_state();

extern skyfs_s32_t
__skyfs_M2M_start_balance(skyfs_u32_t mds_id, 
				skyfs_u32_t kind_mds_id, 
				skyfs_u32_t first_index, 
				skyfs_u32_t balance_num);

extern skyfs_s32_t 
__skyfs_M2M_get_layout(skyfs_u32_t mds_id);
#endif
/*This is end of mds_itm.h*/
