/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_profile.h 
 */

#ifndef __MDS_PROFILE_H
#define __MDS_PROFILE_H

extern void __skyfs_MS_get_state(amp_request_t *req);

extern skyfs_s32_t __skyfs_MS_collect_state(skyfs_state_info_t *state_info);

extern skyfs_u32_t __skyfs_MS_get_statev();

extern void __skyfs_MS_profile_create();

extern void __skyfs_MS_profile_split();

extern void __skyfs_MS_profile_enlarge();

#endif
/*This is end of file*/
