/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_meta.h 
 */

#ifndef __MDS_META_H
#define __MDS_META_H

extern skyfs_u32_t skyfs_free_dir_id_head;
extern skyfs_htb_t skyfs_free_dir_id;

extern skyfs_u32_t skyfs_conflict_ino_head;
extern skyfs_htb_t skyfs_conflict_ino;

skyfs_ino_t
__skyfs_MS_alloc_ino(skyfs_ino_t dir_ino,
                skyfs_s8_t *name,
                skyfs_u32_t mode);
skyfs_ino_t
__skyfs_MS_alloc_conflict_ino(skyfs_ino_t dir_ino, skyfs_u32_t mode);

void __skyfs_MS_copy_meta(skyfs_meta_t *meta, skyfs_M_cmeta_t *cmeta);

skyfs_s32_t
__skyfs_MS_set_cmeta(skyfs_m_setmeta_args_t *args,
                skyfs_M_cmeta_t *cmeta);
skyfs_s32_t
__skyfs_MS_release_meta(skyfs_M_bmeta_t *bmeta, skyfs_M_mmeta_t *mmeta);

skyfs_s32_t
__skyfs_MS_free_meta(skyfs_M_bmeta_t *bmeta, skyfs_M_mmeta_t *mmeta);

skyfs_s32_t
__skyfs_MS_alloc_meta(skyfs_M_bmeta_t *bmeta,
                skyfs_M_cmeta_t **cmeta,
                skyfs_M_mmeta_t **mmeta);

skyfs_s32_t
__skyfs_MS_unlink_meta(skyfs_M_mmeta_t *mmeta);

skyfs_s32_t
__skyfs_MS_enlarge_subset(skyfs_M_subset_cache_t *subset_cache);

skyfs_s32_t
__skyfs_MS_split_subset(skyfs_M_subset_cache_t *subset_cache);

skyfs_s32_t
__skyfs_MS_split_dir(skyfs_ino_t dir_ino);

skyfs_s32_t
__skyfs_MS_init_file(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args);

skyfs_s32_t
__skyfs_MS_init_dir(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args);

skyfs_s32_t
__skyfs_MS_init_node(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args);

skyfs_s32_t
__skyfs_MS_init_cmeta(skyfs_meta_t *meta,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_ino_t ino,
                skyfs_s8_t *name);

skyfs_s32_t
__skyfs_MS_init_symlink(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_symlink_args_t *args);

skyfs_s32_t
__skyfs_MS_get_symlink(skyfs_M_cmeta_t *cmeta,
        skyfs_s8_t *buf);

skyfs_s32_t __skyfs_MS_delete_obd_file(skyfs_M_cmeta_t *cmeta);

skyfs_s32_t __skyfs_MS_writeback_metaino();

skyfs_s32_t __skyfs_MS_get_metaino();
#endif
/*This is end of file*/
