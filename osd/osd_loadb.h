/*
 *  Copyright (c) 2010  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_loadb.h 
 */

#ifndef __OSD_LOADB_H
#define __OSD_LOADB_H


skyfs_s32_t __skyfs_SS_collect_state(skyfs_state_info_t *state_info);

skyfs_s32_t __skyfs_SS_get_statusindex(skyfs_u32_t osd_id);

skyfs_u32_t __skyfs_SS_choose2read(skyfs_DL_file_t *dl_file, skyfs_dl_dest_t *des);

skyfs_s32_t __skyfs_SS_judge_status(skyfs_u32_t osd_id);
#endif
/*This is end of osd_loadb.h*/
