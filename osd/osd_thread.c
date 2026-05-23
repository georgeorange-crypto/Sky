/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_thread.c $
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
#include "skyfs_help.h"


#include "osd_fs.h"
#include "osd_op.h"
#include "osd_thread.h"
#include "osd_init.h"
#include "osd_thread.h"
#include "osd_profile.h"
#include "osd_loadb.h"
#include "osd_dl.h"
#include "osd_cache.h"

#include "mds_fs.h"

#include "osd_ito.h"

skyfs_user_thread_t osd_service_threads[OSD_SRV_THREAD_NUM]; 
skyfs_user_thread_t osd_serveout_threads[OSD_SRVOUT_THREAD_NUM];
skyfs_user_thread_t osd_simple_threads[OSD_SIMPLE_THREAD_NUM];
skyfs_user_thread_t osd_stat_thread;
skyfs_user_thread_t osd_moncache_thread;
skyfs_user_thread_t osd_writeback_logbuf_thread;
skyfs_user_thread_t osd_writeback_metabox_thread;
skyfs_user_thread_t osd_profile_thread;
skyfs_user_thread_t osd_loadb_thread;
skyfs_user_thread_t osd_release_filebuf_thread;

static uint64_t simple_req_cnt = 0;
static uint64_t service_req_cnt = 0;
static uint64_t service_req_time = 0;

pthread_mutex_t  separate_lock;
sem_t    osd_wb_logbuf_sem;
sem_t    osd_wb_metabox_sem;
sem_t    osd_profile_sem;
sem_t    osd_loadb_sem;
sem_t    osd_release_filebuf_sem;

skyfs_u32_t forced_to_split = 0;
skyfs_u32_t osd_threshold_nr_request = 10;

skyfs_u32_t         count_time_flag = 0;
skyfs_u32_t         time_total = 0;
skyfs_timespec_t    start_time;
skyfs_timespec_t    end_time;

void (* __skyfs_SS_msg_handler[])(amp_request_t *) = {
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
    __skyfs_SS_read,                 /*31,read*/
    __skyfs_SS_write,                /*32,write*/
    NULL,                            /*33,create obj*/
    __skyfs_SS_remove_obj,           /*34,remove obj*/
    NULL,                            /*35,commit*/
    __skyfs_SS_truncate,             /*36,truncate , mayl should call SS_truncate function here */
    __skyfs_SS_get_devinfo,          /*37,get devinfo*/
    __skyfs_SS_enlarge_subset,       /*38,enlarge subset*/
    __skyfs_SS_split_subset,         /*39,split subset*/
    __skyfs_SS_create_subset,        /*40,create subset*/
    __skyfs_SS_read_bmeta,           /*41,read bmeta*/
    __skyfs_SS_write_bmeta,          /*42,write bmeta*/
    __skyfs_SS_read_subset,          /*43,read subset*/
    __skyfs_SS_write_subset,         /*44,write subset*/
    /*control stat related*/
    NULL,                            /*45,create fs*/
    NULL,                            /*46,shutdown fs*/
    __skyfs_SS_get_state,            /*47,get state*/
    NULL,                            /*48*/
    NULL,                            /*49*/
    __skyfs_SS_do_removeobj,         /*50*/
    __skyfs_SS_create_dl_subset_index,     /*51*/
    __skyfs_SS_get_dl_head,          /*52*/
    __skyfs_SS_create_dl_subset,     /*53*/
    __skyfs_SS_write_dlchunk,        /*54*/
    __skyfs_SS_update_head_depth,    /*55*/
    __skyfs_SS_copy_obj,             /*56*/
    __skyfs_SS_update_state,         /*57*/
    NULL,                            /*58,write replica*/
    __skyfs_SS_remote_replica_write,		 /*59*/
    __skyfs_SS_commit_write,         /*60*/
    __skyfs_SS_init_config,         /*61, Init configuration*/
    NULL,					 /*62*/
    __skyfs_SS_recover_replica_write,		 /*63*/
    __skyfs_SS_query_replica_state,		 /*64*/
    __skyfs_SS_handle_replica_recover,		 /*65*/
    __skyfs_SS_listxattr,		         /*66*/
    NULL,                                        /*67*/
    NULL,                                        /*68*/
    NULL,                                        /*69*/
    NULL,                                        /*70*/
    __skyfs_SS_write_multi_objs,			 /*71*/
    NULL

};

void *
__skyfs_SS_stat_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    //amp_request_t    *req = NULL;
	skyfs_s32_t      rc = 0;
    SKYFS_ENTER("__skyfs_SS_stat_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        //SKYFS_MSG("__skyfs_SS_stat_thread:wait to work\n");
        //sem_wait(&osd_stat_request_queue_sem);
		sleep(1);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_SS_stat_thread: bye\n");
            goto EXIT;
        }

        //SKYFS_MSG("__skyfs_SS_stat_thread:before get stat process\n");
        //__skyfs_SS_get_state(req);
    	rc = __skyfs_SS_collect_state(&osd_state_info);
    }
EXIT:
    SKYFS_MSG("__skyfs_SS_stat_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_stat_thread:leave,rc:%d\n", rc);

    return NULL;
}

void *
__skyfs_SS_moncache_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
	skyfs_s32_t      rc = 0;
    SKYFS_ERROR("__skyfs_SS_moncache_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
		usleep(500000);

        if(threadp->to_shutdown){
            SKYFS_ERROR("__skyfs_SS_moncache_thread: bye\n");
            goto EXIT;
        }

    	rc = __skyfs_SS_test_freemem();
		if(rc > 0){
            		SKYFS_MSG("__skyfs_SS_moncache_thread: FREE filebuf_sem\n");
    			sem_post(&osd_release_filebuf_sem);
		}
    }
EXIT:
    SKYFS_MSG("__skyfs_SS_moncache_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_moncache_thread:leave,rc:%d\n", rc);

    return NULL;
}

void *
__skyfs_SS_release_filebuf_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    //amp_request_t       *req = NULL;
    //skyfs_msg_t         *msgp = NULL;
    //skyfs_u32_t         msg_type;
    skyfs_s32_t         rc;
    SKYFS_ERROR("__skyfs_SS_release_filebuf_thread:EnteR\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_ERROR("__skyfs_SS_release_filebuf_thread:wait to WORK1\n");
        sem_wait(&osd_release_filebuf_sem);
        SKYFS_ERROR("__skyfs_SS_release_filebuf_thread:wait to get sem WORK\n");

        if(threadp->to_shutdown){
            SKYFS_ERROR("__skyfs_SS_release_filebuf_thread: bye\n");
            goto EXIT;
        }

        SKYFS_ERROR("__skyfs_SS_release_filebuf_thread:begin to WORK\n");
		if(osd_block_insert_objbuf){
			rc = __skyfs_SS_release_filebuf();
			if(rc < 0){
           		SKYFS_ERROR("__skyfs_SS_release_filebuf_thread:release filebuf err\n");
			}
		}
    }
EXIT:
    SKYFS_MSG("__skyfs_SS_release_filebuf_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_release_filebuf_thread:leave\n");

    return NULL;
}


void *
__skyfs_SS_simple_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    amp_request_t        *req = NULL;
    skyfs_msg_t         *msgp = NULL;
    skyfs_u32_t         msg_type;
    SKYFS_ERROR("__skyfs_SS_simple_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_SS_simple_thread:wait to work\n");
        sem_wait(&osd_simple_queue_sem);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_SS_simple_thread: bye\n");
            goto EXIT;
        }

        pthread_mutex_lock(&osd_simple_queue_lock);
        if(list_empty(&osd_simple_queue)){
            pthread_mutex_unlock(&osd_simple_queue_lock);
            continue;
        }
        SKYFS_MSG("__skyfs_SS_simple_thread:get one request\n");
        req = list_entry(osd_simple_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&osd_simple_queue_lock);

        msgp = __skyfs_get_msg(req->req_msg);
        msg_type = msgp->type & (0xff);

        SKYFS_MSG("__skyfs_SS_simple_thread:before get simple process:%d\n",msg_type);
        if(!__skyfs_SS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_SS_simple_thread:no method for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
            __skyfs_SS_msg_handler[msg_type](req);
	    simple_req_cnt ++;
	    if(simple_req_cnt %1000 == 5){
		    SKYFS_ERROR_1("OSD do simple thread %lu\n", simple_req_cnt);
	    }
        }

    }
EXIT:
    SKYFS_MSG("__skyfs_SS_simple_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_simple_thread:leave\n");

    return NULL;
}


void *
__skyfs_SS_serveout_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    amp_request_t    *req = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t     msg_type;
    SKYFS_ENTER("__skyfs_SS_serveout_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        SKYFS_MSG("__skyfs_SS_serveout_thread:wait to work\n");
        sem_wait(&osd_serveout_queue_sem);

        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_SS_serveout_thread: bye\n");
            goto EXIT;
        }

        pthread_mutex_lock(&osd_serveout_queue_lock);
        if(list_empty(&osd_serveout_queue)){
            pthread_mutex_unlock(&osd_serveout_queue_lock);
            continue;
        }
        SKYFS_MSG("__skyfs_SS_serveout_thread:get one request\n");
        req = list_entry(osd_serveout_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
        pthread_mutex_unlock(&osd_serveout_queue_lock);

        msgp = __skyfs_get_msg(req->req_msg);
        msg_type = msgp->type & (0xff);

        SKYFS_MSG("__skyfs_SS_serveout_thread:before get stat process\n");
        if(!__skyfs_SS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_SS_serviceout_thread:no method for msg_type:%d\n", 
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
            __skyfs_SS_msg_handler[msg_type](req);
            //pthread_mutex_unlock(&separate_lock);
        }

    }
EXIT:
    SKYFS_MSG("__skyfs_SS_serveout_thread:service_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_serveout_thread:leave\n");

    return NULL;
}

void *
__skyfs_SS_service_thread(void *argv)
{
    skyfs_user_thread_t    *threadp = NULL;
    amp_request_t          *req = NULL;
    //amp_request_t          *tmp= NULL;
    skyfs_msg_t            *msgp = NULL;
    skyfs_u32_t            msg_type;
    //skyfs_timespec_t       mstart_time, mend_time;

	skyfs_u32_t            time_once = 0;
    

    //skyfs_s32_t     waiting_times = 1;
    SKYFS_ENTER("__skyfs_SS_service_thread:enter\n");
    threadp = (skyfs_user_thread_t *)argv;
    
    //struct list_head *index = NULL, *head = NULL;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
        /*1. get request*/
        SKYFS_ERROR("__skyfs_SS_service_thread:%lu wait to work\n", pthread_self());
        sem_wait(&osd_request_queue_sem);
        
        if(threadp->to_shutdown){
            SKYFS_MSG("__skyfs_SS_service_thread:%lu thread,bye\n", pthread_self());
            goto EXIT;
        }
        
        pthread_mutex_lock(&osd_request_queue_lock);
        if(list_empty(&osd_request_queue)){
            SKYFS_ERROR("__skyfs_SS_service_thread:%lu thread,empty\n", pthread_self());
            pthread_mutex_unlock(&osd_request_queue_lock);
            continue;
        }
	
		if(count_time_flag){		
			time_once = __skyfs_get_endtime(&start_time, &end_time, 1, "free_time");
			time_total = time_once + time_total;
        	SKYFS_ERROR("__skyfs_SS_service_thread:once:%d,total:%d\n", 
				time_once, time_total); 
			count_time_flag = 0;
		}

        SKYFS_ERROR("__skyfs_SS_service_thread:osd_nr_req %llu\n", osd_nr_request); 
		if(osd_nr_request > osd_threshold_nr_request){
    		sem_post(&osd_loadb_sem);
		}

#if 1
        req = list_entry(osd_request_queue.next, amp_request_t, req_list);
        list_del_init(&req->req_list);
#endif
#if 0
        req = NULL;

        head = &(osd_request_queue);
        list_for_each(index, head){
            tmp = list_entry(index, amp_request_t, req_list);
            msgp = __skyfs_get_msg(tmp->req_msg);
            SKYFS_MSG("__skyfs_SS_ser::ino:%llu,id:%d,lastino:%llu,lastid:%d\n",
                msgp->ino, tmp->req_remote_id, last_ino, last_id);

            if(msgp->ino== last_ino && tmp->req_remote_id == last_id){
                SKYFS_ERROR("__skyfs_SS_service:%d_match:ino:%llu,id:%d\n",
                    msgp->type,last_ino, last_id);
                req = tmp;

                if(msgp->type == 31){
                    __skyfs_get_endtime(&mstart_time, &mend_time, 1, "rmatch");
                }else if(msgp->type == 32){
                    __skyfs_get_endtime(&mstart_time, &mend_time, 1, "wmatch");
                }

                __skyfs_get_starttime(&mstart_time, 1);

                break;
            }
        }

        if(req != NULL){
            list_del_init(&req->req_list);
        }else{
            req = list_entry(osd_request_queue.next, amp_request_t, req_list);
            list_del_init(&req->req_list);
            msgp = __skyfs_get_msg(req->req_msg);
            last_ino= msgp->ino;
            last_id = req->req_remote_id;
            SKYFS_ERROR("__skyfs_SS_service:modify:ino:%llu,id:%d\n",
                last_ino, last_id);
            list_del_init(&req->req_list);
        }
#endif


        pthread_mutex_unlock(&osd_request_queue_lock);

        /*2. judge if request right*/
        msgp = __skyfs_get_msg(req->req_msg);
        if(msgp->magic != SKYFS_MSG_MAGIC){
            msgp = (skyfs_msg_t *)((skyfs_s8_t *)req->req_msg+2*AMP_MESSAGE_HEADER_LEN);
            if(msgp->magic !=SKYFS_MSG_MAGIC){
                SKYFS_ERROR("__skyfs_SS_service:[%ld]thread:wrong msg,magic:%x\n",
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
        SKYFS_MSG("__skyfs_SS_service_thread: msgp->type:%d, msg_type:%d\n",
            msgp->type, msg_type);

        if(msg_type > SKYFS_MSG_MAX){
            SKYFS_ERROR("__skyfs_SS_service_thread:[%lu]:err msgtype:%x\n",
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
        
        if(!__skyfs_SS_msg_handler[msg_type]){
            SKYFS_ERROR("__skyfs_SS_service_thread:no method for msg_type:%d\n", 
                msg_type);
            if(req->req_msg){
                amp_free(req->req_msg, req->req_msglen);
            }
            if(req->req_reply){
                amp_free(req->req_reply, req->req_replylen);
            }
            __amp_free_request(req);
        }else{
	    struct timeval tvs, tve;
	    SKYFS_MSG("__skyfs_SS_service_thread:start_to_involke %d.\n", msg_type);
	    //gettimeofday(&tvs,NULL);
            __skyfs_SS_msg_handler[msg_type](req);
	    //gettimeofday(&tve,NULL);
	    service_req_time += (tve.tv_sec *100000 + tve.tv_usec);
	    service_req_time -= (tvs.tv_sec *100000 + tvs.tv_usec);

	    service_req_cnt ++;
	    if(service_req_cnt %5000 == 5){
		    SKYFS_ERROR_1("OSD do servive cnt %lu, \n", service_req_cnt);
	    }
        }

#if 0
        pthread_mutex_lock(&osd_request_queue_lock);
		if(osd_nr_request == 0){
    		__skyfs_get_starttime(&start_time, skyfs_profile_flag);
			bzero(&end_time, sizeof(skyfs_timespec_t));
			SKYFS_ERROR("__skyfs_SS_service_thread:start_to_time.\n");
			count_time_flag = 1;
		}
        pthread_mutex_unlock(&osd_request_queue_lock);
#endif

    }

EXIT:
    SKYFS_MSG("__skyfs_SS_service_thread:[%lu]service_thread leave.\n", 
        pthread_self());
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_service_thread:leave\n");

    return NULL;
}

void *
__skyfs_SS_loadb_thread(void *argv)
{
    skyfs_user_thread_t *threadp = NULL;
    amp_request_t       *req = NULL;
	skyfs_u32_t         osd_id;
    skyfs_s32_t         rc;
    SKYFS_ENTER("__skyfs_SS_loadb_thread:enter\n");

    threadp = (skyfs_user_thread_t *)argv;

    sem_post(&threadp->startsem);
    threadp->is_up = 1;

    while(1){
retry:
    	sem_wait(&osd_loadb_sem);

        if(threadp->to_shutdown){
            SKYFS_ERROR("__skyfs_SS_loadb_thread: bye\n");
            goto EXIT;
        }
        
#if 1
    	SKYFS_ERROR("__skyfs_SS_loadb_thread:collect_state\n");
        rc = __skyfs_O2O_collect_state();
        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_loadb_thread:collect state error\n");
            goto EXIT;
        }

		if(rc > 0){
			osd_id = rc;
	        pthread_mutex_lock(&osd_request_queue_lock);
   		    if(list_empty(&osd_request_queue)){
            	SKYFS_ERROR("__skyfs_SS_loadb_thread:%ld thread,empty\n", 
					pthread_self());
            	pthread_mutex_unlock(&osd_request_queue_lock);
				goto retry;
			}
            	
		   	req = list_entry(osd_request_queue.next, amp_request_t, req_list);
        	list_del_init(&req->req_list);
			
			pthread_mutex_unlock(&osd_request_queue_lock);
			SKYFS_ERROR_1("SS loadb start mv_obj  %lu\n", osd_id);

			rc = skyfs_SS_mv_obj(req, osd_id);
		}

#endif        

    }

EXIT:
    SKYFS_MSG("__skyfs_SS_loadb_thread:loadb_thread leave.\n");
    sem_post(&threadp->stopsem);

    SKYFS_LEAVE("__skyfs_SS_loadb_thread:leave\n");

    return NULL;
}


skyfs_s32_t    
__skyfs_SS_create_threads(void)
{
    skyfs_s32_t     rc = 0;
    skyfs_u32_t     i;
    skyfs_user_thread_t    *threadp = NULL;
    
    SKYFS_ENTER("__skyfs_SS_create_threads:enter\n");

    pthread_mutex_init(&separate_lock, NULL);
	bzero(&start_time, sizeof(skyfs_timespec_t));
	bzero(&end_time, sizeof(skyfs_timespec_t));

    INIT_LIST_HEAD(&osd_request_queue);
    sem_init(&osd_request_queue_sem, 0, 0);
    sem_init(&osd_serveout_queue_sem, 0, 0);
    sem_init(&osd_simple_queue_sem, 0, 0);
    sem_init(&osd_stat_request_queue_sem, 0, 0);
    sem_init(&osd_release_filebuf_sem, 0, 0);

/*1. create service threads*/
    SKYFS_MSG("__skyfs_SS_create_threads:create service threads\n");
    for(i = 0; i < OSD_SRV_THREAD_NUM; i++){
        threadp = &osd_service_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_SS_service_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_SS_create_threads:create service thread err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
    }
/*2. create serveout threads*/
    SKYFS_MSG("__skyfs_SS_create_threads:create serveout threads\n");
    for(i = 0; i < OSD_SRVOUT_THREAD_NUM; i++){
        threadp = &osd_serveout_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_SS_serveout_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_SS_create_threads:create serveout thread err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
    }

/*3. create simple threads*/
    SKYFS_MSG("__skyfs_SS_create_threads:create simple threads\n");
    for(i = 0; i < OSD_SIMPLE_THREAD_NUM; i++){
        threadp = &osd_simple_threads[i];
        threadp->seqno = i;
        threadp->is_up = 0;
        threadp->to_shutdown = 0;
        sem_init(&threadp->startsem, 0, 0);
        sem_init(&threadp->stopsem, 0, 0);
        rc = pthread_create(&threadp->thread, 
                NULL, 
                 __skyfs_SS_simple_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_SS_create_threads:create simple thread err:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
    }

/*4. create stat metabox thread*/
    SKYFS_MSG("__skyfs_SS_create_threads:create stat request thread\n");
    threadp = &osd_stat_thread;
    threadp->seqno = 0;
    threadp->is_up = 0;
    threadp->to_shutdown = 0;
    sem_init(&threadp->startsem, 0, 0);
    sem_init(&threadp->stopsem, 0, 0);
    rc = pthread_create(&threadp->thread, 
            NULL, 
             __skyfs_SS_stat_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_SS_create_threads:create stat thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);

/*5. create monmem thread*/
    SKYFS_MSG("__skyfs_SS_create_threads:create monmem request thread\n");
    threadp = &osd_moncache_thread;
    threadp->seqno = 0;
    threadp->is_up = 0;
    threadp->to_shutdown = 0;
    sem_init(&threadp->startsem, 0, 0);
    sem_init(&threadp->stopsem, 0, 0);
    rc = pthread_create(&threadp->thread, 
            NULL, 
             __skyfs_SS_moncache_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_SS_create_threads:create moncache thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);

/*6. create release filebuf thread*/
    SKYFS_ERROR("__skyfs_SS_create_threads:create release filebuf request thread\n");
    threadp = &osd_release_filebuf_thread;
    threadp->seqno = 0;
    threadp->is_up = 0;
    threadp->to_shutdown = 0;
    sem_init(&threadp->startsem, 0, 0);
    sem_init(&threadp->stopsem, 0, 0);
    rc = pthread_create(&threadp->thread, 
            NULL, 
             __skyfs_SS_release_filebuf_thread,
            (void *)threadp);
    if(rc){
        SKYFS_ERROR("__skyfs_SS_create_threads:create release filebuf thread error:%d\n", rc);
        goto err_out;
    }
    sem_wait(&threadp->startsem);


#if 0
/*5. create balance thread*/
//    if(osd_this_id == SKYFS_MASTER_OSD_ID){
        SKYFS_MSG("__skyfs_SS_create_thread:create balance thread\n");
        threadp = &osd_loadb_thread;
		sem_init(&osd_loadb_sem, 0, 0);
        rc = pthread_create(&threadp->thread,
                NULL,
                __skyfs_SS_loadb_thread,
                (void *)threadp);
        if(rc){
            SKYFS_ERROR("__skyfs_SS_create_threads:create balance thread error:%d\n", rc);
            goto err_out;
        }
        sem_wait(&threadp->startsem);
//   }
#endif
err_out:
    SKYFS_LEAVE("__skyfs_SS_create_threads:leave\n");
    return rc;
}

skyfs_s32_t    
__skyfs_SS_stop_threads(void)
{
    skyfs_user_thread_t     *threadp = NULL;
    skyfs_s32_t 			sem_value = 0;
    skyfs_u32_t             i;
    skyfs_s32_t             rc = 0;

    SKYFS_ENTER("__skyfs_SS_stop_threads:enter\n");

    /* 1.stop service thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop service thread\n");
    for(i = 0; i < OSD_SRV_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_SS_stop_threads:stop service thread %d\n", i);
        threadp = &osd_service_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < OSD_SRV_THREAD_NUM; i++){
        sem_post(&osd_request_queue_sem);    
		sem_getvalue(&osd_request_queue_sem, &sem_value);
        SKYFS_MSG("__skyfs_SS_stop_threads:semvalue:%d\n", sem_value);
    }
    for(i = 0; i < OSD_SRV_THREAD_NUM; i++){
        threadp = &osd_service_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    /* 2.stop serveout thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop serveout thread\n");
    for(i = 0; i < OSD_SRVOUT_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_SS_stop_threads:stop serveout thread %d\n", i);
        threadp = &osd_serveout_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < OSD_SRVOUT_THREAD_NUM; i++){
        sem_post(&osd_serveout_queue_sem);    
    }
    for(i = 0; i < OSD_SRVOUT_THREAD_NUM; i++){
        threadp = &osd_serveout_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    /* 3.stop simple thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop simple thread\n");
    for(i = 0; i < OSD_SIMPLE_THREAD_NUM; i++){
        SKYFS_MSG("__skyfs_SS_stop_threads:stop simple thread %d\n", i);
        threadp = &osd_simple_threads[i];
        threadp->to_shutdown = 1;
    }
    for(i = 0; i < OSD_SIMPLE_THREAD_NUM; i++){
        sem_post(&osd_simple_queue_sem);    
    }
    for(i = 0; i < OSD_SIMPLE_THREAD_NUM; i++){
        threadp = &osd_simple_threads[i];
        if(threadp->is_up){
            sem_wait(&threadp->stopsem);
            threadp->is_up = 0;
        }
    }

    /* 4.stop stat thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop stat thread\n");
    threadp = &osd_stat_thread;
    threadp->to_shutdown = 1;
    sem_post(&osd_stat_request_queue_sem);
    if(threadp->is_up){
        sem_wait(&threadp->stopsem);
        threadp->is_up = 0;
    }

    /* 5.stop moncache thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop moncache thread\n");
    threadp = &osd_moncache_thread;
    threadp->to_shutdown = 1;
    if(threadp->is_up){
        sem_wait(&threadp->stopsem);
        threadp->is_up = 0;
    }

    /* 6.stop moncache thread*/
    SKYFS_ERROR("__skyfs_SS_stop_threads:stop filebuf thread\n");
    threadp = &osd_release_filebuf_thread;
    threadp->to_shutdown = 1;
    if(threadp->is_up){
        SKYFS_ERROR("__skyfs_SS_stop_threads:stop filebuf thread wait_stop\n");
    	sleep(1);
	// mayl post sem to trigger a empty release filebuf, thus post the stopsme
	if(osd_block_insert_objbuf == 0)
		sem_post(&osd_release_filebuf_sem);
        sem_wait(&threadp->stopsem);
        SKYFS_ERROR("__skyfs_SS_stop_threads:stop filebuf thread wait_stop end\n");
	sleep(1);
        threadp->is_up = 0;
    }

    SKYFS_ERROR("__skyfs_SS_stop_threads:stop filebuf thread END\n");
    return rc;
}
/*This is end of osd_thread.c*/
