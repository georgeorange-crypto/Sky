/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: mds_thread.c $
 */
#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"
#include "skyfs_fs.h"

#include "amp.h"

#include "skyfs_msg.h"
#include "skyfs_debug.h"
#include "skyfs_hash.h"

#include "mds_fs.h"
#include "mds_op.h"
#include "mds_thread.h"
#include "mds_init.h"
#include "mds_thread.h"
#include "mds_state.h"
#include "mds_cache.h"
#include "mds_itm.h"
#include "mds_loadb.h"
#include "mds_layout.h"

skyfs_user_thread_t mds_service_threads[MDS_SRV_THREAD_NUM]; 
skyfs_user_thread_t mds_simple_threads[MDS_SIMPLE_THREAD_NUM]; 
skyfs_user_thread_t mds_stat_thread;
skyfs_user_thread_t mds_writeback_thread;
skyfs_user_thread_t mds_balance_thread;

skyfs_user_thread_t mds_writeback_logbuf_thread;
skyfs_user_thread_t mds_profile_thread;

sem_t    mds_wb_logbuf_sem;
sem_t    mds_wb_metabox_sem;
sem_t    mds_profile_sem;

void (* __skyfs_MS_msg_handler[])(amp_request_t *) = {
    NULL,                            /*0, NULL operations*/
    /*meta operation related*/
    __skyfs_MS_statfs,               /*1, get fs infor*/
    __skyfs_MS_lookup,               /*2, lookup for a fs object by name*/
    __skyfs_MS_create,               /*3, create a object*/
    __skyfs_MS_remove,               /*4, remove a object*/
    __skyfs_MS_getattr,              /*5, getattr*/
    __skyfs_MS_setattr,              /*6, setattr*/
    __skyfs_MS_readdir,              /*7, read dir file content*/
    __skyfs_MS_rename,               /*8, rename fs object*/
    __skyfs_MS_link,                 /*9, link*/
    __skyfs_MS_symlink,              /*10,for follow_link op*/
    __skyfs_MS_readlink,             /*11,read symlink file*/
    __skyfs_MS_release,              /*12,release a file*/
    //NULL,                          /*13,lock op the function below added by mayl*/ 
    __skyfs_MS_flock,                 /*13,lock op*/
    __skyfs_MS_init_dcache,          /*14,init dir cache*/
    __skyfs_MS_get_dcache,           /*15,get dir cache*/
    __skyfs_MS_update_dcache,        /*16,update dir cache*/
    __skyfs_MS_update_ddepth,        /*17,update dir depth*/
    __skyfs_MS_create_subindex,      /*18,create subset index*/
    __skyfs_MS_get_state,            /*19,get mds state*/
    __skyfs_MS_add_htbcache,         /*20,add a htb cache*/
    __skyfs_MS_get_layout,           /*21,add a htb cache*/
    NULL,                            /*22,write*/
    NULL,                            /*23,write*/
    NULL,                            /*24,write*/
    NULL,                            /*25,write*/
    NULL,                            /*26,write*/
    NULL,                            /*27,write*/
    NULL,                            /*28,write*/
    NULL,                            /*29,write*/
    NULL,                            /*30,write*/
    /*data operation related*/
    NULL,                            /*31,read*/
    NULL,                            /*32,write*/
    NULL,                            /*33,create segment*/
    NULL,                            /*34,remove segment*/
    NULL,                            /*35,commit*/
    NULL,                            /*36,truncate*/
    NULL,                            /*37,get devinfo*/
    NULL,                            /*38,enlarge*/
    NULL,                            /*39,split*/
    NULL,                            /*40,split*/
    NULL,                            /*41,split*/
    NULL,                            /*42,split*/
    NULL,                            /*42,split*/
    NULL,                            /*44,split*/
    /*control stat related*/
    NULL,                            /*45,create*/
    NULL,                            /*46,shutdown fs*/
    __skyfs_MS_get_state,            /*47,get state*/
    __skyfs_MS_balance_load,         /*48,trigger balance*/
    __skyfs_MS_start_balance,        /*49,balance load*/
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
    __skyfs_MS_init_config,          /*61, Init configuration*/
    NULL,                            /*62*/
    NULL                            /*63*/
};


void *
__skyfs_MS_balance_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    amp_request_t       *req = NULL;

    SKYFS_ENTER("__skyfs_MS_balance_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_MS_balance_thread:wait to work\n");
        sem_wait(&mds_balance_request_queue_sem);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_MS_balance_thread: bye\n");
            goto EXIT;
        }

        pthread_mutex_lock(&mds_balance_request_queue_lock);
        if(list_empty(&mds_balance_request_queue)){
            SKYFS_ERROR("__skyfs_MS_balance_thread:error:no req to process\n");
            pthread_mutex_unlock(&mds_balance_request_queue_lock);
            continue;
        }

        SKYFS_MSG("__skyfs_MS_balance_thread:get one request\n");
        req = list_entry(mds_balance_request_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&mds_balance_request_queue_lock);

        __skyfs_MS_balance_load(req);
        
    }

EXIT:
    SKYFS_MSG("__skyfs_MS_balance_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_MS_balance_thread:leave\n");

    return NULL;
}

void *
__skyfs_MS_writeback_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    skyfs_M_wb_req_t    *req = NULL;
    skyfs_s32_t         rc = 0;

    SKYFS_ENTER("__skyfs_MS_writeback_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_MS_writeback_thread:wait to work\n");
        sem_wait(&mds_wb_request_queue_sem);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_MS_writeback_thread: bye\n");
            goto EXIT;
        }

        pthread_mutex_lock(&mds_wb_request_queue_lock);
        if(list_empty(&mds_wb_request_queue)){
            pthread_mutex_unlock(&mds_wb_request_queue_lock);
            continue;
        }

        SKYFS_MSG("__skyfs_MS_writeback_thread:get one request\n");
        req = list_entry(mds_wb_request_queue.next, skyfs_M_wb_req_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&mds_wb_request_queue_lock);

        pthread_mutex_lock(&total_bmeta_num_lock);
        SKYFS_ERROR("__skyfs_MS_writeback_thread:req_tot_num:%d,tot_num:%d\n",
            req->total_bmeta_num, total_bmeta_num);
        if((total_bmeta_num > SKYFS_MAX_BMETA_NUM/2)){
            pthread_mutex_unlock(&total_bmeta_num_lock);
            continue;
        }
        pthread_mutex_unlock(&total_bmeta_num_lock);
    
        SKYFS_MSG("__skyfs_MS_writeback_thread:begin writeback:%d\n", 
            req->total_bmeta_num );
        free(req);

        rc = __skyfs_MS_writeback();
    }

EXIT:
    SKYFS_MSG("__skyfs_MS_writeback_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_MS_writeback_thread:leave\n");

    return NULL;
}


void *
__skyfs_MS_stat_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    skyfs_state_info_t  state_info;
    skyfs_s32_t     rc = 0;

    SKYFS_ENTER("__skyfs_MS_stat_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_MS_stat_thread:wait to work\n");

        sleep(SKYFS_COLLECT_INTERLEAVE);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_MS_stat_thread: bye\n");
            goto EXIT;
        }

        SKYFS_MSG("__skyfs_MS_stat_thread:before get stat process\n");
        bzero(&state_info, sizeof(skyfs_state_info_t));
        rc = __skyfs_MS_collect_state(&state_info);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_stat_thread:error:read /proc/stat failed\n");
            continue;
        }
        
        if(state_info.cpu_usage < MDS_YELLOW_ALARM && mds_num > 1){
            SKYFS_ERROR("__skyfs_MS_stat_thread:need to balance:cpu_usage:%lld\n",
                state_info.cpu_usage);
            rc = __skyfs_M2M_trigger_balance(state_info);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_stat_thread:trigger balance error:%d\n", rc);
            }
        }
    }
EXIT:
    SKYFS_MSG("__skyfs_MS_stat_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_MS_stat_thread:leave\n");

    return NULL;
}

void do_mds_request(amp_request_t *req, int msg_type)
{
	if(!__skyfs_MS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_MS_simple_thread:no method exist for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
            __skyfs_MS_msg_handler[msg_type](req);
        }
}

void *
__skyfs_MS_simple_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    amp_request_t    *req = NULL;
    skyfs_msg_t     *msgp = NULL;
    skyfs_u32_t     msg_type = 0;

    SKYFS_ENTER("__skyfs_MS_simple_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_MS_simple_thread:wait to work\n");
        sem_wait(&mds_simple_request_queue_sem);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_MS_simple_thread: bye\n");
            goto EXIT;
        }

        pthread_mutex_lock(&mds_simple_request_queue_lock);
        if(list_empty(&mds_simple_request_queue)){
            pthread_mutex_unlock(&mds_simple_request_queue_lock);
            continue;
        }
        SKYFS_MSG("__skyfs_MS_simple_thread:get one request\n");
        req = list_entry(mds_simple_request_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&mds_simple_request_queue_lock);

        msgp = __skyfs_get_msg(req->req_msg);
        msg_type = msgp->type & (0xff);

        SKYFS_MSG("__skyfs_MS_simple_thread:before get process\n");
        if(!__skyfs_MS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_MS_simple_thread:no method exist for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
            __skyfs_MS_msg_handler[msg_type](req);
        }
    }
EXIT:
    SKYFS_MSG("__skyfs_MS_simple_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_MS_simple_thread:leave\n");

    return NULL;
}


void *
__skyfs_MS_service_thread(void *argv)
{
    skyfs_user_thread_t    *threadp = NULL;
    amp_request_t     *req = NULL;
    skyfs_msg_t          *msgp = NULL;
    skyfs_u32_t          msg_type;
    SKYFS_ENTER("__skyfs_MS_service_thread:enter\n");
    threadp = (skyfs_user_thread_t *)argv;
    
    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        /*1. get request*/
        SKYFS_MSG("__skyfs_MS_service_thread:%ld wait to work\n", pthread_self());
        sem_wait(&mds_request_queue_sem);
        
        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_MS_service_thread:%ld thread bye bye\n", pthread_self());
            goto EXIT;
        }
        
        pthread_mutex_lock(&mds_request_queue_lock);
        if(list_empty(&mds_request_queue)){
            pthread_mutex_unlock(&mds_request_queue_lock);
            continue;
        }

        req = list_entry(mds_request_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&mds_request_queue_lock);

        /*2. judge if request right*/
        msgp = (skyfs_msg_t *)((skyfs_s8_t *)req->req_msg + AMP_MESSAGE_HEADER_LEN);
        if(msgp->magic != SKYFS_MSG_MAGIC){
            msgp = (skyfs_msg_t *)((skyfs_s8_t *)req->req_msg + 2 * AMP_MESSAGE_HEADER_LEN);
            if(msgp->magic !=SKYFS_MSG_MAGIC){
                SKYFS_ERROR("__skyfs_MS_service_thread:[%ld]service_thread:wrong msg, magic:%x\n",
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
        SKYFS_MSG("__skyfs_MS_service_thread: msgp->type:%d, msg_type:%d\n",
            msgp->type, msg_type);

        if(msg_type > SKYFS_MSG_MAX){
            SKYFS_ERROR("__skyfs_MS_service_thread:[%ld]service_thread:too larger msg_type:%x\n",
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
        
        if(!__skyfs_MS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_MS_service_thread:no method exist for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
            __skyfs_MS_msg_handler[msg_type](req);
        }
    }

EXIT:
    SKYFS_MSG("__skyfs_MS_service_thread:[%ld]service_thread leave.\n", pthread_self());
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_MS_service_thread:leave\n");

    return NULL;
}

skyfs_s32_t    __skyfs_MS_create_threads(void)
{
    skyfs_s32_t     rc = 0;
    skyfs_u32_t     i;
    skyfs_user_thread_t    *threadp = NULL;
    
    SKYFS_ENTER("__skyfs_MS_create_threads:enter\n");
/*1. create service threads*/
    SKYFS_MSG("__skyfs_MS_create_threads:create service threads\n");
    for(i = 0; i < MDS_SRV_THREAD_NUM; i++){
        threadp = &mds_service_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_MS_service_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_MS_create_threads:create service thread err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
    }

/*2. create simple threads*/
    SKYFS_MSG("__skyfs_MS_create_threads:create simlpe threads\n");
    for(i = 0; i < MDS_SIMPLE_THREAD_NUM; i++){
        threadp = &mds_simple_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_MS_simple_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_MS_create_threads:create simple thread err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
    }

/*3. create stat thread*/
    SKYFS_MSG("__skyfs_MS_create_threads:create stat request thread\n");
    threadp = &mds_stat_thread;
    threadp->seqno = 0;
    threadp->is_up = 0;
    threadp->to_shutdown = 0;
    sem_init(&threadp->startsem, 0, 0);
    sem_init(&threadp->stopsem, 0, 0);
    rc = pthread_create(&threadp->thread, 
            NULL, 
             __skyfs_MS_stat_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_MS_create_threads:create stat thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);

/*4. create writeback thread*/
    SKYFS_MSG("__skyfs_MS_create_thread:create writeback thread\n");
    threadp = &mds_writeback_thread;
    rc = pthread_create(&threadp->thread,
            NULL,
            __skyfs_MS_writeback_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_MS_create_threads:create writeback thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);
#if 0
/*5. create balance thread*/
    SKYFS_MSG("__skyfs_MS_create_thread:create balance thread\n");
    threadp = &mds_balance_thread;
    rc = pthread_create(&threadp->thread,
            NULL,
            __skyfs_MS_balance_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_MS_create_threads:create balance thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);
#endif

err_out:
    SKYFS_LEAVE("__skyfs_MS_create_threads:leave\n");
    return rc;
}

skyfs_s32_t    __skyfs_MS_stop_threads(void)
{
    skyfs_user_thread_t    *threadp = NULL;
    skyfs_u32_t            i;
    skyfs_s32_t            rc = 0;

    SKYFS_ENTER("__skyfs_M_stop_threads:enter\n");

    /* 1.stop service thread*/
    SKYFS_MSG("__skyfs_MS_stop_threads:stop service thread\n");
    for(i = 0; i < MDS_SRV_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_MS_stop_threads:stop service thread %d\n", i);
        threadp = &mds_service_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < MDS_SRV_THREAD_NUM; i++){
        sem_post(&mds_request_queue_sem);    
    }
    for(i = 0; i < MDS_SRV_THREAD_NUM; i++){
        threadp = &mds_service_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    /* 2.stop simple thread*/
    SKYFS_MSG("__skyfs_MS_stop_threads:stop simple thread\n");
    for(i = 0; i < MDS_SIMPLE_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_MS_stop_threads:stop simple thread %d\n", i);
        threadp = &mds_simple_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < MDS_SIMPLE_THREAD_NUM; i++){
        sem_post(&mds_simple_request_queue_sem);    
    }
    for(i = 0; i < MDS_SIMPLE_THREAD_NUM; i++){
        threadp = &mds_simple_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    /* 3.stop stat thread*/
    SKYFS_MSG("__skyfs_MS_stop_threads:stop stat thread\n");
    threadp = &mds_stat_thread;
    threadp->to_shutdown = 1;
    if(threadp->is_up){
        sem_wait(&threadp->stopsem);
        threadp->is_up = 0;
    }

    /* 4.stop writeback meta thread*/
    SKYFS_MSG("__skyfs_MS_stop_threads:stop writeback thread\n");
    threadp = &mds_writeback_thread;
    threadp->to_shutdown = 1;
    sem_post(&mds_wb_request_queue_sem);
    if(threadp->is_up){
        sem_wait(&threadp->stopsem);
        threadp->is_up = 0;
    }

    /* 5.stop balance thread*/
    SKYFS_MSG("__skyfs_MS_stop_threads:stop balance thread\n");
    threadp = &mds_balance_thread;
    threadp->to_shutdown = 1;
    sem_post(&mds_balance_request_queue_sem);
    if(threadp->is_up){
        sem_wait(&threadp->stopsem);
        threadp->is_up = 0;
    }
    return rc;
}

/*This is end of mds_thread.c*/
