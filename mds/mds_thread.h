/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_thread.h 
 */

#ifndef __MDS_THREAD_H
#define __MDS_THREAD_H

#define MDS_SRV_THREAD_NUM		16
#define MDS_SIMPLE_THREAD_NUM	8
#define MDS_FLUSH_THREAD_NUM	1
#define MDS_OSDINFO_THREAD_NUM	1


//void (* __skyfs_MS_msg_handler[SKYFS_MAX_MSG_HANDLER_NUM]) (amp_request_t *);
extern void (* __skyfs_MS_msg_handler[]) (amp_request_t *);

extern sem_t skyfs_MS_finailize_sem;

skyfs_s32_t __skyfs_MS_create_threads(void);

skyfs_s32_t __skyfs_MS_stop_threads(void);


#endif
/*This is end of file*/
