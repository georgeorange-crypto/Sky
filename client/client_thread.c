/* 
 *  Copyright (c) 2011  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_thread.c $
 */

#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"
#include "skyfs_fs.h"

#include "amp.h"

#include "skyfs_msg.h"
#include "skyfs_debug.h"
#include "skyfs_hash.h"
#include "skyfs_help.h"

#include "mds_fs.h"

#include "osd_fs.h"


#include "client_help.h"
#include "client_init.h"
#include "client_op.h"
#include "client_cache.h"
#include "client_itm.h"
#include "client_ito.h"
#include "client_thread.h"


void (* __skyfs_C_msg_handler[])(amp_request_t *) = {
    NULL,                            /*0, NULL operations*/
    /*meta operation related*/
    NULL,                            /*1, get fs infor*/
    NULL,                            /*2, lookup for a fs object by name*/
    NULL,                            /*3, create a object*/
    NULL,                            /*4, remove a object*/
    NULL,                            /*5, getattr*/
    NULL,                            /*6, setattr*/
    NULL,                            /*7, read dir file content*/
    NULL,                            /*8, rename fs object*/
    NULL,                            /*9, link*/
    NULL,                            /*10,for follow_link op*/
    NULL,                            /*11,read symlink file*/
    NULL,                            /*12,release a file*/
    NULL,                            /*13,lock op*/
    NULL,                            /*14,lock op*/
    NULL,                            /*15,lock op*/
    NULL,                            /*16,lock op*/
    NULL,                            /*17,lock op*/
    NULL,                            /*18,lock op*/
    NULL,                            /*19,lock op*/
    NULL,                            /*20,lock op*/
    NULL,                            /*21,backup*/
    NULL,                            /*22,release a file*/
    NULL,                            /*23,lock op*/
    NULL,                            /*24,lock op*/
    NULL,                            /*25,lock op*/
    NULL,                            /*26,lock op*/
    NULL,                            /*27,lock op*/
    NULL,                            /*28,lock op*/
    NULL,                            /*29,lock op*/
    NULL,                            /*30,lock op*/

    /*data operation related*/
    NULL,                            /*31,read*/
    NULL,                            /*32,write*/
    NULL,                            /*33,create obj*/
    NULL,                            /*34,remove obj*/
    NULL,                            /*35,commit*/
    NULL,                            /*36,truncate*/
    NULL,                            /*37,get devinfo*/
    NULL,                            /*38,enlarge subset*/
    NULL,                            /*39,split subset*/
    NULL,                            /*40,create subset*/
    NULL,                            /*41,read bmeta*/
    NULL,                            /*42,write bmeta*/
    NULL,                            /*43,read subset*/
    NULL,                            /*44,write subset*/
    /*control stat related*/
    NULL,                            /*45,create fs*/
    NULL,                            /*46,shutdown fs*/
    NULL,                            /*47,get state*/
    NULL,                            /*48*/
    NULL,                            /*49*/
    NULL,                            /*50*/
    NULL,                            /*51*/
    NULL,                            /*52*/
    NULL,                            /*53*/
    NULL,                            /*54*/
    NULL,                            /*55*/
    NULL,                            /*56*/
    NULL,                            /*57*/
    NULL,                            /*58*/
    NULL,                            /*59*/
    NULL,                            /*60*/
    __skyfs_C_init_config,           /*61, Init configuration*/
    NULL,                            /*62*/
    NULL                            /*63*/
};


skyfs_user_thread_t client_service_threads[CLIENT_SRV_THREAD_NUM]; 

void *
__skyfs_C_service_thread(void *argv)
{
    skyfs_user_thread_t    *threadp = NULL;
    amp_request_t          *req = NULL;
    //amp_request_t          *tmp= NULL;
    skyfs_msg_t            *msgp = NULL;
    skyfs_u32_t            msg_type;
    //skyfs_timespec_t       mstart_time, mend_time;
    //skyfs_s32_t     waiting_times = 1;
	//
    SKYFS_ENTER("__skyfs_C_service_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;
    
    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        /*1. get request*/
        SKYFS_MSG("__skyfs_C_service_thread:%ld wait to work\n", pthread_self());
        sem_wait(&client_request_queue_sem);
        
        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_C_service_thread:%ld thread,8\n", pthread_self());
            goto EXIT;
        }
        
        pthread_mutex_lock(&client_request_queue_lock);
        if(list_empty(&client_request_queue)){
            pthread_mutex_unlock(&client_request_queue_lock);
            continue;
        }
        req = list_entry(client_request_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);

        pthread_mutex_unlock(&client_request_queue_lock);

        /*2. judge if request right*/
        msgp = __skyfs_get_msg(req->req_msg);
        if(msgp->magic != SKYFS_MSG_MAGIC){
            msgp = (skyfs_msg_t *)((skyfs_s8_t *)req->req_msg+2*AMP_MESSAGE_HEADER_LEN);
            if(msgp->magic !=SKYFS_MSG_MAGIC){
                SKYFS_ERROR("__skyfs_C_service:[%ld]thread:wrong msg,magic:%x\n",
                    pthread_self(), msgp->magic);
                if(req->req_msg){
                    amp_free(req->req_msg, req->req_msglen);
                }
                if(req->req_reply){
                    amp_free(req->req_reply, req->req_replylen);
                }

                __amp_free_request(req);
                continue;
            }
        }
    
        /*3. get the msg type and call the related method*/
        msg_type = msgp->type & (0xff);
        SKYFS_MSG("__skyfs_C_service_thread: msgp->type:%d, msg_type:%d\n",
            msgp->type, msg_type);

        if(msg_type > SKYFS_MSG_MAX){
            SKYFS_ERROR("__skyfs_C_service_thread:[%ld]service_thread:err msg_type:%x\n",
                pthread_self(), msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }

            __amp_free_request(req);
            continue;
        }
        
        if(!__skyfs_C_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_C_service_thread:no method exist for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
            //pthread_mutex_lock(&separate_lock);
            __skyfs_C_msg_handler[msg_type](req);
            //pthread_mutex_unlock(&separate_lock);
        }
    }

EXIT:
    SKYFS_MSG("__skyfs_C_service_thread:[%ld]service_thread leave.\n", 
        pthread_self());
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_C_service_thread:leave\n");

    return NULL;
}


skyfs_s32_t __skyfs_C_create_threads(void)
{
    skyfs_s32_t     rc = 0;
    skyfs_u32_t     i;
    skyfs_user_thread_t    *threadp = NULL;
    
    SKYFS_ENTER("__skyfs_C_create_threads:enter\n");

	/*1. create service threads*/
    SKYFS_MSG("__skyfs_C_create_threads:create service threads\n");
    for(i = 0; i < CLIENT_SRV_THREAD_NUM; i++){
        threadp = &client_service_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_C_service_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_C_create_threads:crt srv thread,err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
	}
err_out:
	return rc;
}

skyfs_s32_t    
__skyfs_C_stop_threads(void)
{
    skyfs_user_thread_t    *threadp = NULL;
    skyfs_u32_t            i;
    skyfs_s32_t            rc = 0;

    SKYFS_ENTER("__skyfs_C_stop_threads:enter\n");

    /* 1.stop service thread*/
    SKYFS_MSG("__skyfs_C_stop_threads:stop service thread\n");
    for(i = 0; i < CLIENT_SRV_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_C_stop_threads:stop service thread %d\n", i);
        threadp = &client_service_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < CLIENT_SRV_THREAD_NUM; i++){
        sem_post(&client_request_queue_sem);    
    }
    for(i = 0; i < CLIENT_SRV_THREAD_NUM; i++){
        threadp = &client_service_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    return rc;
}
