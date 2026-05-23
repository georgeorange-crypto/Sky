/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_itm.h 
 */

#ifndef __CLIENT_ITM_H
#define __CLIENT_ITM_H


skyfs_s32_t __skyfs_C2M_lookup_stat(skyfs_ino_t pino,
                skyfs_s8_t *name,
                skyfs_ino_t *ino,
                skyfs_u32_t *conflict_index,
                struct stat * sbbuf);


skyfs_s32_t __skyfs_C2M_statfs(struct statvfs *stbuf);

skyfs_s32_t __skyfs_C2M_create(skyfs_ino_t pino, 
                skyfs_s8_t *lastname, 
                skyfs_u32_t mode,
                skyfs_ino_t *ino, 
                skyfs_u32_t *conflict_index);

skyfs_s32_t __skyfs_C2M_remove(skyfs_ino_t pino, 
                skyfs_s8_t *lastname, 
                skyfs_ino_t ino, 
                skyfs_u32_t conflict_index);

skyfs_s32_t __skyfs_C2M_release(skyfs_ino_t ino, skyfs_u32_t conflict_index);

skyfs_s32_t __skyfs_C2M_lock(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                int cmd,
				uint64_t lock_owner,
				struct flock * lock);


skyfs_s32_t __skyfs_C2M_getattr(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                struct stat *sbbuf,
		uint32_t * palgorithm);

skyfs_s32_t __skyfs_C2M_setattr(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                skyfs_m_setmeta_args_t *args,
		skyfs_u32_t do_update);

skyfs_s32_t __skyfs_C2M_lookup(skyfs_ino_t pino,
                skyfs_s8_t *name,
                skyfs_ino_t *ino,
skyfs_u32_t *conflict_index);

skyfs_s32_t __skyfs_C2M_get_dcache(skyfs_M_dir_cache_t *dir_cache, 
                skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_C2M_readdir(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                skyfs_s8_t *buf_page,
                skyfs_u64_t offset,
                skyfs_u32_t count);

skyfs_s32_t 
__skyfs_C2M_symlink(skyfs_ino_t pino, 
                skyfs_s8_t *name, 
                skyfs_ino_t *ino, 
                skyfs_u32_t *conflict_index,
                const skyfs_s8_t *target);

skyfs_s32_t 
__skyfs_C2M_readlink(skyfs_ino_t pino,
                skyfs_s8_t *name,
                skyfs_s8_t *buf,
                skyfs_u32_t linksize);
skyfs_s32_t
__skyfs_C2M_rename(skyfs_ino_t pino_from, skyfs_ino_t pino_to, 
                skyfs_s8_t  *name_from, skyfs_s8_t *name_to,
                skyfs_ino_t ino_from, skyfs_u32_t conflict_index_from,
                skyfs_ino_t *ino, skyfs_u32_t *conflict_index);

skyfs_s32_t
__skyfs_C2M_link(skyfs_ino_t pino_from, skyfs_ino_t pino_to, 
                skyfs_s8_t  *name_from, skyfs_s8_t *name_to,
                skyfs_ino_t ino_from, skyfs_u32_t conflict_index_from,
                skyfs_ino_t *ino, skyfs_u32_t *conflict_index);

skyfs_s32_t __skyfs_C2M_get_layout(skyfs_u32_t mds_id);

skyfs_C_bcache_t *
__skyfs_C_getbcache(skyfs_u32_t subset_id,
        skyfs_u32_t chunk_id,
        skyfs_ino_t ino,
        skyfs_u64_t offset,
        skyfs_u32_t client_id,
        skyfs_u32_t pid);
	/*struct list_head *  bcache_list_head);*/

skyfs_C_bcache_t *
__skyfs_C_find_bcache(skyfs_ino_t ino, 
        skyfs_u64_t offset, 
        skyfs_u32_t client_id, 
        skyfs_u32_t pid);
	 //struct list_head *  bcache_list_head);

skyfs_s32_t
__skyfs_C_release_bcache(skyfs_ino_t ino,  struct list_head *  bcache_list_head);

skyfs_u32_t 
__skyfs_C_compose_bufpage(skyfs_u32_t out_offset,
    skyfs_u32_t in_offset, 
    skyfs_s8_t *buf_page, 
    skyfs_C_bcache_t *bcache);


#endif
/*This is end of client_itm.h*/
