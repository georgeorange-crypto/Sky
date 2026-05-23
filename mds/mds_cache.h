/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_cache.h 
 */

#ifndef __MDS_CACHE_H
#define __MDS_CACHE_H

//skyfs_htb_t skyfs_dir_cache_htbbase[SKYFS_DIR_HASH_LEN];
//skyfs_htb_t skyfs_subset_cache_htbbase[SKYFS_SUBSET_HASH_LEN];
extern skyfs_htb_t skyfs_dir_cache_htbbase[];
extern skyfs_htb_t skyfs_subset_cache_htbbase[];
/* added by mayl for flock temp cache */
//skyfs_htb_t skyfs_flock_cache_htbbase[SKYFS_LOCK_HASH_LEN];
extern skyfs_htb_t skyfs_flock_cache_htbbase[];

extern skyfs_s32_t total_bmeta_num;
extern pthread_mutex_t total_bmeta_num_lock;

extern skyfs_u32_t total_access_bmeta_num;
extern skyfs_u32_t total_read_bmeta_num;

skyfs_u32_t __skyfs_MS_get_dir_id(skyfs_ino_t ino, skyfs_u32_t flag);

skyfs_u32_t __skyfs_MS_get_subsetid_by_name(skyfs_M_dir_depth_t *dir_depth, 
				skyfs_s8_t *name);

skyfs_u32_t	__skyfs_MS_get_subsetid_by_ino(skyfs_M_dir_depth_t *dir_depth, 
				skyfs_ino_t ino, 
				skyfs_u32_t conflict_index);

skyfs_u32_t	__skyfs_MS_get_bmetaid(skyfs_u32_t subset_depth, skyfs_u32_t hashvalue);

skyfs_u32_t __skyfs_MS_judge_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);
skyfs_u32_t __skyfs_MS_judge_dir_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_u32_t __skyfs_MS_judge_osdid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_u32_t __skyfs_MS_get_subset_hashvalue(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_u32_t __skyfs_MS_get_bmeta_hashvalue(skyfs_u32_t bmeta_id);

/* added by mayl for flock and posix lock */
skyfs_s32_t
__skyfs_add_inode_lock_cache(skyfs_u64_t ino, struct list_head * flock_head,struct list_head * posix_lock_head, skyfs_M_mmeta_t * mmeta);


/* added by mayl for flock and posix lock */ 
skyfs_s32_t
__skyfs_remove_inode_lock_cache(skyfs_u64_t ino, struct list_head * node_lock_head, skyfs_M_mmeta_t * mmeta);

skyfs_htb_t *
__skyfs_MS_locate_subset_by_name(skyfs_ino_t dir_ino, 
				skyfs_s8_t *name,
				skyfs_u32_t	*mds_id,
				skyfs_u32_t	*dir_id,
				skyfs_u32_t	*subset_id);

skyfs_htb_t *
__skyfs_MS_locate_subset_by_ino(skyfs_ino_t ino, 
				skyfs_u32_t conflict_index,
				skyfs_u32_t	*mds_id,
				skyfs_u32_t	*dir_id,
				skyfs_u32_t	*subset_id);

skyfs_htb_t *
__skyfs_MS_locate_rename_subset_by_ino(skyfs_ino_t ino, 
				skyfs_u32_t conflict_index,
				skyfs_u32_t	*mds_id,
				skyfs_u32_t	*dir_id,
				skyfs_u32_t	*subset_id);


skyfs_M_subset_cache_t *
__skyfs_MS_find_subset(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id);

skyfs_M_bmeta_t *
__skyfs_MS_locate_bmeta_by_name(skyfs_M_subset_cache_t *subset_cache,
				skyfs_s8_t *name,
				skyfs_u32_t *bmeta_id);

skyfs_M_bmeta_t *
__skyfs_MS_locate_bmeta_by_ino(skyfs_M_subset_cache_t *subset_cache,
				skyfs_ino_t ino,
				skyfs_u32_t	conflict_index,
				skyfs_u32_t *bmeta_id);

skyfs_M_bmeta_t *
__skyfs_MS_locate_rename_bmeta_by_ino(skyfs_M_subset_cache_t *subset_cache,
				skyfs_ino_t ino,
				skyfs_u32_t	conflict_index,
				skyfs_u32_t *bmeta_id);


skyfs_M_mmeta_t *
__skyfs_MS_locate_mmeta_by_name(skyfs_M_bmeta_t *bmeta,
				skyfs_s8_t	*name);

skyfs_M_mmeta_t *
__skyfs_MS_locate_mmeta_by_ino(skyfs_M_bmeta_t *bmeta,
				skyfs_ino_t ino,
				skyfs_u32_t	conflict_index);


skyfs_M_mmeta_t *
__skyfs_MS_locate_rename_mmeta_by_ino(skyfs_M_bmeta_t *bmeta,
                skyfs_ino_t ino,
                skyfs_u32_t    conflict_index);


skyfs_s32_t
__skyfs_MS_check_mmeta_exist(skyfs_M_bmeta_t *bmeta,
				skyfs_s8_t *name,
				skyfs_ino_t ino);

skyfs_M_bmeta_t * __skyfs_MS_alloc_bmeta(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_s32_t __skyfs_MS_release_bmeta(skyfs_M_bmeta_t *bmeta);

skyfs_s32_t __skyfs_MS_free_bmeta(skyfs_M_bmeta_t *bmeta);

skyfs_M_bmeta_t *
__skyfs_MS_find_bmeta(skyfs_htb_t *htbp,
				skyfs_u32_t	bmeta_id);

skyfs_s32_t
__skyfs_MS_init_bmeta(skyfs_M_bmeta_t *bmeta, skyfs_u32_t bmeta_id);

skyfs_M_bmeta_t *
__skyfs_MS_get_bmeta(skyfs_M_subset_cache_t *subset_cache,
				skyfs_u32_t bmeta_id);
skyfs_s32_t
__skyfs_MS_read_bmeta(skyfs_M_bmeta_t *bmeta, 
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id, 
				skyfs_u32_t bmeta_id);

skyfs_M_subset_cache_t * __skyfs_MS_alloc_subset(void);

skyfs_s32_t
__skyfs_MS_release_subset(skyfs_M_subset_cache_t *subset_cache);

skyfs_M_subset_cache_t *
__skyfs_MS_find_subset(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id, 
				skyfs_u32_t	subset_id);

skyfs_s32_t 
__skyfs_MS_do_create_subindex(skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t subset_depth,
				skyfs_u32_t nlink);

skyfs_M_subset_cache_t *
__skyfs_MS_get_subset(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id);

skyfs_s32_t
__skyfs_MS_read_subset(skyfs_M_subset_cache_t *subset_cache,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id);

skyfs_s32_t
__skyfs_MS_write_subset(skyfs_M_subset_cache_t *subset_cache);

skyfs_M_subset_index_t * __skyfs_MS_alloc_subset_index(void);

skyfs_s32_t 
__skyfs_MS_release_subset_index(skyfs_M_subset_index_t *subset_index);

skyfs_M_subset_index_t *
__skyfs_MS_find_subset_index(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id, 
				skyfs_u32_t	subset_id);

skyfs_M_dir_depth_t *
__skyfs_MS_alloc_dir_depth(void);

skyfs_s32_t
__skyfs_MS_release_dir_depth(skyfs_M_dir_depth_t *dir_depth);

skyfs_M_dir_depth_t*
__skyfs_MS_find_dir_depth(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id);

skyfs_M_dir_depth_t * __skyfs_MS_get_dir_depth(skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_MS_do_update_ddepth(skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth);

skyfs_s32_t
__skyfs_MS_update_dir_depth(skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id, 
				skyfs_u32_t split_depth);


skyfs_M_dir_cache_t *
__skyfs_MS_alloc_dir_cache(skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_MS_release_dir_cache(skyfs_M_dir_cache_t *dir_cache);

skyfs_M_dir_cache_t *
__skyfs_MS_find_dir_cache(skyfs_htb_t *htbp,
				skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_MS_do_init_dcache(skyfs_u32_t dir_id, skyfs_M_cmeta_t *cmeta, skyfs_u32_t needlock);

skyfs_s32_t
__skyfs_MS_init_dir_cache(skyfs_M_cmeta_t *cmeta);

skyfs_s32_t
__skyfs_MS_do_get_dcache(skyfs_u32_t dir_id, skyfs_M_dir_cache_t *dir_cache);

skyfs_s32_t
__skyfs_MS_get_dir_cache(skyfs_M_dir_cache_t *dir_cache, skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_MS_do_update_dcache(skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_s32_t update,
				skyfs_M_cmeta_t *dir_cmeta);

skyfs_s32_t
__skyfs_MS_update_dir_cache(skyfs_M_subset_cache_t *subset_cache,
				skyfs_M_cmeta_t *dir_cmeta,
				skyfs_u32_t flag);

skyfs_s32_t
__skyfs_MS_writeback_subset(skyfs_M_subset_cache_t *subset_cache);

skyfs_s32_t
__skyfs_MS_do_clear_dir_cache(skyfs_M_dir_cache_t *dir_cache);

skyfs_s32_t __skyfs_MS_init_cache(void);

skyfs_s32_t __skyfs_MS_finalize_cache(void);

skyfs_s32_t __skyfs_MS_writeback_root_meta(void);

skyfs_s32_t __skyfs_MS_writeback_cache(void);

skyfs_s32_t __skyfs_MS_writeback(void);

skyfs_s32_t __skyfs_MS_clear_htbcache(skyfs_u32_t index, skyfs_u32_t kind_mds_id);

skyfs_s32_t __skyfs_MS_check_htbcache(skyfs_htb_t *htbp);

skyfs_s32_t __skyfs_MS_move_wb_entry(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_s32_t __skyfs_MS_clear_dcache(skyfs_u32_t hashindex);

skyfs_s32_t
__skyfs_MS_do_write_dcache(skyfs_M_dir_cache_t *dcache, 
		skyfs_M_cmeta_t *cmeta);

#endif
/*This is end of mds_cache.h*/
