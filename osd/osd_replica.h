/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_replica.h 
 */

#ifndef __OSD_REPLICA_H
#define __OSD_REPLICA_H

skyfs_s32_t 
__skyfs_SS_choose_replica_place(skyfs_u32_t subset_id,
			skyfs_u32_t *osd_replica2,
			skyfs_u32_t *osd_replica3);

#endif
/*This is end of osd_replica.h*/
