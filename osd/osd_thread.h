/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: osd_thread.h 
 */

#ifndef __OSD_THREAD_H
#define __OSD_THREAD_H

#define OSD_SRV_THREAD_NUM	    50
#define OSD_SRVOUT_THREAD_NUM	6
#define OSD_SIMPLE_THREAD_NUM   2
#define OSD_FLUSH_THREAD_NUM	1
#define OSD_OSDINFO_THREAD_NUM	1

//void (* __skyfs_SS_msg_handler[]) (amp_request_t *);

extern skyfs_user_thread_t osd_service_threads[]; 
extern sem_t skyfs_SS_finailize_sem;
extern sem_t osd_loadb_sem;

extern skyfs_u32_t forced_to_split;

skyfs_s32_t __skyfs_SS_create_threads(void);

skyfs_s32_t __skyfs_SS_stop_threads(void);



#endif
/*This is end of osd_thread.h*/
