/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_cache.h 
 */

#ifndef __OSD_CACHE_H
#define __OSD_CACHE_H

skyfs_s32_t 
__skyfs_SS_locate_objbuf(skyfs_ino_t ino, 
		skyfs_u64_t obj_id, 
		skyfs_u64_t offset, 
		skyfs_u32_t count, 
		skyfs_O_objbuf_t **objbuf);

skyfs_O_filebuf_t * 
__skyfs_SS_find_filebuf(skyfs_ino_t ino);

skyfs_s32_t 
__skyfs_SS_fill_buffer(skyfs_s8_t *buf, 
		skyfs_O_objbuf_t *objbuf,
		skyfs_u64_t offset,
		skyfs_u32_t count);

skyfs_s32_t
__skyfs_SS_insert_objbuf(skyfs_ino_t ino, 
		skyfs_u64_t obj_id, 
		skyfs_u64_t offset, 
		skyfs_u32_t count, 
		skyfs_s8_t  *buf);

skyfs_s32_t 
__skyfs_SS_release_datac(skyfs_ino_t ino, 
		skyfs_u32_t partition_id, 
		skyfs_u64_t obj_id, 
		skyfs_u64_t offset, 
		skyfs_u32_t count);

skyfs_s32_t __skyfs_SS_test_freemem(void);

skyfs_s32_t __skyfs_SS_release_filebuf(void);

skyfs_s32_t 
__skyfs_SS_read_objbuf(skyfs_ino_t ino, 
		skyfs_u64_t obj_id, 
		skyfs_u64_t offset, 
		skyfs_u32_t count, 
		skyfs_O_objbuf_t **objbuf,
		skyfs_s8_t  **buf);


long long str_to_val(const char * str);
long long get_proc_used_mem();
long long get_sys_free_mem();
#endif
/*This is end of osd_cache.h*/
