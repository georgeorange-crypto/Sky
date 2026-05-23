/*
 *  Copyright (c) 2011  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_thread.h 
 */

#ifndef __CLIENT_THREAD_H
#define __CLIENT_THREAD_H

#define CLIENT_SRV_THREAD_NUM	8

extern void (* __skyfs_C_msg_handler[]) (amp_request_t *);

extern skyfs_user_thread_t client_service_threads[]; 

skyfs_s32_t    __skyfs_C_create_threads(void);

skyfs_s32_t    __skyfs_C_stop_threads(void);

void *
__skyfs_C_service_thread(void *argv);

#endif
/*This is end of client_thread.h*/
