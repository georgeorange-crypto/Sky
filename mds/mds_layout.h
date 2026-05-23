/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_layout.h 
 */

#ifndef __MDS_LAYOUT_H
#define __MDS_LAYOUT_H

#define MDS_YELLOW_ALARM    96
#define MDS_RED_ALARM		10

extern skyfs_layout_t     mds_layout[SKYFS_SUBSET_HASH_LEN];
extern skyfs_u32_t        mds_layout_version;
extern pthread_mutex_t    mds_layout_lock;

extern skyfs_mds_status_t mds_status[SKYFS_MAX_MDS_NUM];
extern skyfs_u32_t	      mds_state_version;
extern pthread_mutex_t    mds_state_version_lock;

extern skyfs_u32_t __skyfs_MS_search_mds_extent(skyfs_u32_t hashvalue);

extern skyfs_u32_t __skyfs_MS_search_osd_extent(skyfs_u32_t hashvalue);

extern skyfs_s32_t __skyfs_MS_init_layout(void);

extern skyfs_s32_t __skyfs_MS_writeback_layout(void);

extern skyfs_s32_t __skyfs_MS_get_first_loadout(skyfs_u32_t mds_id, 
				skyfs_u32_t balance_num);

extern skyfs_u32_t __skyfs_MS_do_update_mdslayout(skyfs_u32_t mds_id, 
				skyfs_u32_t kind_mds_id,
				skyfs_u32_t first_index,
				skyfs_u32_t balance_num);

extern skyfs_u32_t __skyfs_MS_get_layoutv(void);

extern skyfs_s32_t 
__skyfs_MS_check_layout_version(skyfs_u32_t mds_layoutv, skyfs_u32_t mds_id);

void __skyfs_MS_init_status_hashnum();

#endif
/*This is end of file*/
