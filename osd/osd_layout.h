/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_layout.h 
 */

#ifndef __OSD_LAYOUT_H
#define __OSD_LAYOUT_H

extern skyfs_u32_t osd_dl_version;

skyfs_s32_t __skyfs_SS_init_status_hashnum(void);

skyfs_s32_t __skyfs_SS_init_layout(void);

skyfs_u32_t
__skyfs_SS_judge_osdid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

skyfs_u32_t
__skyfs_SS_judge_osdid_rename(skyfs_u32_t dir_id, skyfs_u32_t subset_id);


skyfs_u32_t
__skyfs_SS_search_osd_dlextent(skyfs_u32_t hashvalue);

skyfs_u32_t
__skyfs_SS_search_osd_extent(skyfs_u32_t hashvalue);
#endif
/*This is end of osd_layout.h*/
