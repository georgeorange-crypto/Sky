/*
 *  Copyright (c) 2010  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_ito.h 
 */

#ifndef __CLIENT_ITO_H
#define __CLIENT_ITO_H

skyfs_s32_t
__skyfs_C2O_read(skyfs_ino_t ino,
				skyfs_s8_t *buf,
				skyfs_u64_t offset,
				skyfs_u32_t size,
				skyfs_u32_t compress_type,
				skyfs_u32_t direct_io);

skyfs_s32_t
__skyfs_C2O_write(skyfs_ino_t ino,
				const skyfs_s8_t *buf,
				skyfs_u64_t offset,
				skyfs_u32_t size,
				skyfs_u32_t base_file,
				skyfs_u32_t compress_type,
				skyfs_u32_t*  write_to_buf,
				skyfs_s64_t * changed_space);

skyfs_s32_t
__skyfs_C2O_get_dl_head(skyfs_u32_t osd_id,
				skyfs_u32_t pad_id,
				skyfs_DL_head_t *dl_head);

#endif
/*This is end of client_ito.h*/
