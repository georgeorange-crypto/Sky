/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_init.c $
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
#include "osd_help.h"
#include "osd_layout.h"
#include "osd_dl.h"
#include "osd_loadb.h"

#include "mds_fs.h"

#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>



extern skyfs_s32_t  
	__skyfs_O2O_recover_data_objs(int src_replica_id,int dest_replica_id,int dest_osd_id, uint64_t xid);

extern skyfs_s32_t  
	__skyfs_O2O_recover_partitions(int src_replica_id,int dest_replica_id,int dest_osd_id, uint64_t xid);

char * error_test_file_path = "/cluster/skyfs/conf/set_error";
int error_test;

int get_error_test_state()
{

	//SKYFS_ERROR_1("get test state ret %d \n", error_test);
	return error_test;
}



amp_comp_context_t      *osd_comp_context = NULL;

skyfs_htb_t skyfs_dop_htbbase[SKYFS_DOP_HASH_LEN];



skyfs_u32_t             is_async_write;
skyfs_u32_t             osd_this_id;
skyfs_u32_t             osd_this_pid;
skyfs_u32_t             pad_id = 1;
skyfs_u32_t             mds_num;
skyfs_u32_t             osd_num;
skyfs_u32_t             client_num;
skyfs_mds_info_t        mds_info;
skyfs_osd_info_t        osd_info;
skyfs_client_info_t     client_info;
skyfs_u32_t             skyfs_ib_flag;
skyfs_u32_t             skyfs_default_dlsb_bits = 0;
skyfs_u32_t             skyfs_dl_type = 0;
skyfs_u32_t             skyfs_replica = 0;
skyfs_u32_t             skyfs_lid = 0;
skyfs_u32_t             skyfs_data_stripe_cnt = 1; // added by mayl
skyfs_u32_t             skyfs_recover_data_size = 2097152; // added by mayl
skyfs_u32_t             osd_readahead_size = 0;

skyfs_u64_t             osd_nr_request;

struct list_head        osd_config_request_queue;
pthread_mutex_t         osd_config_request_queue_lock;
sem_t                   osd_config_request_queue_sem;

struct list_head        osd_stat_request_queue;
pthread_mutex_t         osd_stat_request_queue_lock;
sem_t                   osd_stat_request_queue_sem;

struct list_head        osd_serveout_queue;
pthread_mutex_t         osd_serveout_queue_lock;
sem_t                   osd_serveout_queue_sem;

struct list_head        osd_simple_queue;
pthread_mutex_t         osd_simple_queue_lock;
sem_t                   osd_simple_queue_sem;

struct list_head        osd_request_queue;
pthread_mutex_t         osd_request_queue_lock;
sem_t                   osd_request_queue_sem;

struct list_head        osd_filebuf_queue;
pthread_mutex_t         osd_filebuf_queue_lock;

pthread_mutex_t         skyfs_debug_lock;

skyfs_timespec_t    rp_start_time;
skyfs_timespec_t    rp_end_time;
skyfs_u32_t         read_nr_request = 0;
pthread_mutex_t     read_perf_lock;

skyfs_timespec_t    wp_start_time;
skyfs_timespec_t    wp_end_time;
skyfs_u32_t         write_nr_request = 0;
pthread_mutex_t     write_perf_lock;

pthread_mutex_t     osd_commit_write_lock;

skyfs_timespec_t    start_waiting_time;
skyfs_timespec_t    end_waiting_time;

skyfs_u32_t         waiting_time_flag;

skyfs_ino_t 		last_ino;
skyfs_u32_t 		last_id;

skyfs_u32_t osd_entry_creation = 0;
skyfs_u32_t old_osd_entry_creation = 0;
skyfs_timespec_t hotpot_stime;
skyfs_timespec_t hotpot_etime;

skyfs_osd_status_t osd_status[SKYFS_MAX_OSD_NUM];
skyfs_u32_t sort_osd_status[SKYFS_MAX_OSD_NUM];

pthread_mutex_t  osd_block_insert_lock;
sem_t                   osd_config_sem;
skyfs_arch_info_t       arch_info;

skyfs_s8_t skyfs_adm_addr[SKYFS_MAX_NAME_LEN];

skyfs_s8_t skyfs_disk[SKYFS_MAX_SRV_PER_NODE][SKYFS_MAX_NAME_LEN];

struct list_head osd_node_head;
pthread_mutex_t  osd_node_head_lock;

skyfs_s32_t      osd_block_insert_objbuf;

/*
 * parse parameters
 */ 
skyfs_s32_t __skyfs_SS_parse_parameter(skyfs_s32_t argc, skyfs_s8_t **argv)
{
    skyfs_u32_t daemonlize;
    skyfs_s8_t  c;
    skyfs_s32_t rc = 0;
    skyfs_u32_t i = 0;

    daemonlize = 1;
    
    SKYFS_ENTER("__skyfs_SS_parse_parameter:enter.\n");

    if(argc < 2){
__error_parameter:
        printf("Usage:\t%s [-l lid] [-b | -f]  id\n", argv[0]);
        printf("\t-l ---- active multi vm mode, diff vm use diff disk\n");
        printf("\t-b ---- running on background, it's default option\n");
        printf("\t-f ---- running on foreground\n");
        printf("\tid ---- the id of this mos\n");
        exit (1);
    }

    osd_this_id = atoi(argv[argc - 1]);
    
    while ((c = getopt(argc, argv, "bfl")) != EOF) {
		i ++;
        switch (c) {
            case '?':
                goto  __error_parameter;
                break;
            case 'b':
                daemonlize = 1;
                break;
            case 'f':
                daemonlize = 0;
                break;
			case 'l':
                //printf("parse_opt_l:%d,%s\n", i, argv[i+1]);
				skyfs_lid = atoi(argv[i+1]);
				break;

            default:
                printf("Wrong option: %c\n", c);
                exit(-1);
        }
    }

    if (daemonlize){
        __skyfs_daemonize(skyfs_lid);
    }

    SKYFS_LEAVE("__skyfs_SS_parse_parameter:osd_id.rc=%d\n",osd_this_id);
    SKYFS_LEAVE("__skyfs_SS_parse_parameter:leave.rc=%d\n",rc);
    return rc;
}

/*
 * get conf info
 */
skyfs_s32_t    __skyfs_SS_get_var_conf(void)
{
    skyfs_s32_t    rc = 0;
    
    FILE *fp;

    char str[SKYFS_MAX_NAME_LEN];
    char dir[SKYFS_MAX_NAME_LEN];
    char cmd[SKYFS_MAX_NAME_LEN];
    char type[SKYFS_MAX_NAME_LEN];
    char value[SKYFS_MAX_NAME_LEN];

	skyfs_u32_t skyfs_lid_conf;

    SKYFS_ERROR("__skyfs_SS_get_var_conf:enter\n");

    /* TODO mayl: get error test ..   */
    fp = fopen(error_test_file_path, "r");
    if(!fp){
	error_test = 0;
	SKYFS_ERROR_1("normal mode \n");
    }else{
	    error_test = 1;
	    fclose(fp);
	    SKYFS_ERROR_1("error test mode \n");
	    fp = NULL;
    }

    sprintf(dir, "%s%s", SKYFS_ARCCFG_FILE_PATH, SKYFS_VAR_CONFIG);
    fp = fopen(dir, "r");
    if(!fp){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_get_var_conf:can't open %s file\n", dir);
        return rc;
    }

    is_async_write = 0;
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        bzero(cmd, SKYFS_MAX_NAME_LEN);
        bzero(type, SKYFS_MAX_NAME_LEN);
        bzero(value, SKYFS_MAX_NAME_LEN);
        sscanf(str, "%s %s %s", cmd, type, value); 
        bzero(str, SKYFS_MAX_NAME_LEN);
            
		if(strcmp(cmd, "SKYFS_ADM_ADDR") == 0){
        	bzero(skyfs_adm_addr, SKYFS_MAX_NAME_LEN);
			strcpy(skyfs_adm_addr, type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:adm_addr:%s\n", skyfs_adm_addr);
			continue;
		}

        if(strcmp(cmd, "SKYFS_IB") == 0){
            skyfs_ib_flag = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_ib_flag:%d\n", 
				skyfs_ib_flag);
            continue;
        }

	if(strcmp(cmd, "SKYFS_ASYNC_WRITE") == 0){
            is_async_write  = atoi(type);
            SKYFS_ERROR_1("__skyfs_SS_get_var_conf:skyfs_async_write:%d\n", 
				is_async_write);
            continue;
        }


        if(strcmp(cmd, "SKYFS_DLSB_BITS") == 0){
            skyfs_default_dlsb_bits = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_default_bits:%d\n", 
                skyfs_default_dlsb_bits);
            continue;
        }      
      // added by mayl for strip data obj	
	 if(strcmp(cmd, "SKYFS_DATA_STRIPE_CNT") == 0){
            skyfs_data_stripe_cnt = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_default_data_stripe_cnt:%d\n",
                skyfs_data_stripe_cnt);
            continue;
        }

	if(strcmp(cmd, "SKYFS_RECOVER_DATA_SIZE") == 0){
            skyfs_recover_data_size = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_recover_data_size:%d\n",
                skyfs_recover_data_size);
            continue;
        }



 		if(strcmp(cmd, "SKYFS_DL_TYPE") == 0){
            skyfs_dl_type = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_dl_type:%d\n", 
                skyfs_dl_type);
            continue;
        }        

	    if(strcmp(cmd, "SKYFS_REPLICA") == 0){
            skyfs_replica = atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:skyfs_replica:%d\n", 
                skyfs_replica);
            continue;
        } 

	    if(strcmp(cmd, "OSD_DATA") == 0){
            skyfs_lid_conf = atoi(type);
			SKYFS_ERROR("__skyfs_SS_get_var_conf:lid:%d,string:%s\n", 
                skyfs_lid_conf, value);

			if(skyfs_lid_conf == skyfs_lid){
				strcpy(skyfs_disk[skyfs_lid], value);
			}
            continue;
        } 

	    if(strcmp(cmd, "OSD_READAHEAD") == 0){
            osd_readahead_size = 1<<atoi(type);
            SKYFS_ERROR("__skyfs_SS_get_var_conf:osd_readahead_size:%d\n", 
                osd_readahead_size);
            continue;
        } 

    }

    fclose(fp);

    SKYFS_ERROR("__skyfs_SS_get_var_conf:%d\n", OSD_SRV_THREAD_NUM);

    SKYFS_ERROR("__skyfs_SS_get_var_conf:exit\n");

    return rc;
}

#if 1
skyfs_s32_t __skyfs_SS_get_conf(void)
{
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_SS_get_conf:enter\n");

    
    rc = __skyfs_SS_get_var_conf();

    SKYFS_LEAVE("__skyfs_SS_get_conf:exit\n");
    return rc;
}
#endif


skyfs_s32_t __skyfs_SS_queue_request(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t msg_type = 0;
    skyfs_s32_t sem_value;
    skyfs_s32_t rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    msg_type = msgp->type & (0xff);
    SKYFS_ERROR("__skyfs_SS_queue_request:enter., type %d\n", msg_type);
    SKYFS_ERROR("__skyfs_SS_queue_request: request.msg_type:%d\n",msg_type);

    if(req->req_remote_type == SKYFS_CFG){
        SKYFS_MSG("__skyfs_SS_queue_request:it's a configuration request.\n");
        pthread_mutex_lock(&osd_config_request_queue_lock);
        list_add_tail(&req->req_list, &osd_config_request_queue);
        pthread_mutex_unlock(&osd_config_request_queue_lock);
        rc = sem_post(&osd_config_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_queue_request:config sem up error\n");
        }
    }else if(req->req_remote_type == SKYFS_ADM
			&& msg_type != SKYFS_MSG_INIT_CONFIG){
        SKYFS_MSG("__skyfs_SS_queue_request:get state request.\n");
        pthread_mutex_lock(&osd_stat_request_queue_lock);
        list_add_tail(&req->req_list, &osd_stat_request_queue);
        pthread_mutex_unlock(&osd_stat_request_queue_lock);
        rc = sem_post(&osd_stat_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_queue_request:stat sem up error\n");
        }
    }else if(msg_type == SKYFS_MSG_O_SPLIT_SUBSET){
        SKYFS_MSG("__skyfs_SS_queue_request:serveout request.\n");
        pthread_mutex_lock(&osd_serveout_queue_lock);
        //osd_nr_request ++;
        list_add_tail(&req->req_list, &osd_serveout_queue);
        pthread_mutex_unlock(&osd_serveout_queue_lock);
        rc = sem_post(&osd_serveout_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_queue_request:serverout sem up error\n");
        }
    }else if(msg_type == SKYFS_MSG_O_WRITE_OBJ 
			|| msg_type == SKYFS_MSG_O_READ_OBJ){

        SKYFS_ERROR("__skyfs_SS_queue_request:IO request.msg_type:%d\n",
            msg_type);
	/* reduce semphore wait latency by mayl*/
#if 0
	if(msg_type == SKYFS_MSG_O_READ_OBJ){
		 __skyfs_SS_read(req);
		 rc = 0;
		 goto ret_func;
	}
	if(msg_type == SKYFS_MSG_O_WRITE_OBJ){
		 __skyfs_SS_write(req);
		 rc = 0;
		 goto ret_func;
	}
#endif

	/* reduce semphore wait latency by mayl end */

        pthread_mutex_lock(&osd_request_queue_lock);
        if(osd_nr_request == 0){
            __skyfs_get_endtime(&start_waiting_time, 
                &end_waiting_time, 
                skyfs_profile_flag, 
                "waiting_time");
            waiting_time_flag = 0;
        }
        osd_nr_request ++;
        list_add_tail(&req->req_list, &osd_request_queue);
        pthread_mutex_unlock(&osd_request_queue_lock);
		sem_getvalue(&osd_request_queue_sem, &sem_value);
		SKYFS_MSG("__skyfs_SS_queue_request:before post:%d\n",
			sem_value);
		SKYFS_MSG("__skyfs_SS_queue_request:%d,%p\n",
			osd_service_threads[0].is_up, 
			&osd_request_queue_sem);
        rc = sem_post(&osd_request_queue_sem);
		sem_getvalue(&osd_request_queue_sem, &sem_value);
		SKYFS_MSG("__skyfs_SS_queue_request:after post:%d\n",
			sem_value);

        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_queue_request:common sem up error\n");
        }
    }else if(msg_type == SKYFS_MSG_INIT_CONFIG){
		__skyfs_SS_init_config(req);
	}else{//msg_type == SKYFS_MSG_STATE
		   // || msg_type == SKYFS_MSG_O_CREATE_SUBSET	
			//|| msg_type > SKYFS_MSG_O_BAK){
        SKYFS_ERROR("__skyfs_SS_queue_request:simple request.msg_type:%d\n",
			msg_type);
        pthread_mutex_lock(&osd_simple_queue_lock);
        list_add_tail(&req->req_list, &osd_simple_queue);
        pthread_mutex_unlock(&osd_simple_queue_lock);

        rc = sem_post(&osd_simple_queue_sem);

        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_queue_request:simple sem up error\n");
        }

    }

ret_func:
    SKYFS_LEAVE("__skyfs_SS_queue_request:leave.\n");
    return rc;
}

skyfs_s32_t 
__skyfs_SS_alloc_pages(void *msg_in, 
		skyfs_u32_t *num, 
		amp_kiov_t **kiov)
{
    amp_kiov_t  *iovp = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_s32_t rc = 0;
    skyfs_u32_t size = 0;
    size_t offset;
    int alignment = 0x200; // 512 bytes sector

    SKYFS_ENTER("__skyfs_SS_alloc_pages:enter\n");

    msgp = (skyfs_msg_t *)msg_in;
    

    if(msgp->type == SKYFS_MSG_O_PREPARE_WRITE){
        size = msgp->u.prepareWriteReq.vec.count;
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for prepareW,size:%d\n",size);
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
	// modified by mayl
	if(size  &(alignment-1)){
        	iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	}else{

        	void * ptr;
		 if (posix_memalign(&ptr, alignment, size) != 0){

        		iovp->ak_addr = (skyfs_s8_t *)malloc(size);
		 }else{
        		iovp->ak_addr = (skyfs_s8_t *)ptr;

		 }

	}
        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:addr:%p\n",iovp->ak_addr);
    }else if(msgp->type == SKYFS_MSG_O_WRITE_OBJ){
	int fsize = msgp->u.writeObjReq.vec.fcount;

	// mayl : check if need to do compress/de=compress
	if(fsize <= 0)
        	size = msgp->u.writeObjReq.vec.count;
	else
        	size = msgp->u.writeObjReq.vec.fcount;
	
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for Write,size:%d\n",size);
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        //iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	// modified by mayl
	if(size  &(alignment-1)){
        	iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	}else{

        	void * ptr;
		 if (posix_memalign(&ptr, alignment, size) != 0){

        		iovp->ak_addr = (skyfs_s8_t *)malloc(size);
		 }else{
        		iovp->ak_addr = (skyfs_s8_t *)ptr;

		 }

	}

        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:addr:%p\n",iovp->ak_addr);
    }else if(msgp->type == SKYFS_MSG_O_WRITE_REPLICA){
        size = msgp->u.writeRepReq.vec.count;
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for writeReplica,size:%d\n",size);
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        //iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	if(size  &(alignment-1)){
        	iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	}else{

        	void * ptr;
		 if (posix_memalign(&ptr, alignment, size) != 0){

        		iovp->ak_addr = (skyfs_s8_t *)malloc(size);
		 }else{
        		iovp->ak_addr = (skyfs_s8_t *)ptr;

		 }

	}

        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:addr:%p\n",iovp->ak_addr);
    }else if(msgp->type == SKYFS_MSG_O_WRITE_BMETA ){
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for writeBmeta.\n");    
         iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        iovp->ak_addr = (skyfs_s8_t *)malloc(sizeof(skyfs_M_bmeta_t));
        iovp->ak_len = sizeof(skyfs_M_bmeta_t);
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:len:%d.\n", iovp->ak_len);    
    }else if(msgp->type == SKYFS_MSG_O_WRITE_DLCHUNK){
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for writeDlchunk.\n");    
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        iovp->ak_addr = (skyfs_s8_t *)malloc(SKYFS_DLCHUNK_SIZE);
        iovp->ak_len = SKYFS_DLCHUNK_SIZE;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:len:%d.\n", iovp->ak_len);    
          
    }else if(msgp->type == SKYFS_MSG_O_COPY_OBJ){
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for copyobj.\n");    
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        iovp->ak_addr = (skyfs_s8_t *)malloc(SKYFS_OBJECT_SIZE);
        iovp->ak_len = SKYFS_OBJECT_SIZE;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:len:%d.\n", iovp->ak_len);    
    }else if(msgp->type == SKYFS_MSG_O_RECOVER_REPLICA){
        //SKYFS_ERROR_1("__skyfs_SS_alloc_pages:alloc for recover replia.\n");
        size = msgp->u.replicaRecoverReq.total_data_size;
	if(size == 0){
		size = 1024;
           	SKYFS_ERROR_1("__skyfs_SS_alloc_pages:alloc for recover replia., size is zero , maybe last request or error request\n");
	}	
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        iovp->ak_addr = (skyfs_s8_t *)malloc(size);
        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
	if(iovp->ak_addr == NULL){
        	SKYFS_ERROR_1("__skyfs_SS_alloc_pages:error alloc recover buffer size %ld\n", size);
		rc = -1;
	}	
        SKYFS_ERROR("__skyfs_SS_alloc_pages:len:%d.\n", iovp->ak_len); 
        	
    }else if(msgp->type == SKYFS_MSG_O_WRITE_MULTI_OBJ){ 
	    // TODO: mayl alloc pages for multiple write objs

        SKYFS_ERROR("__skyfs_SS_alloc_pages: for multi write.\n"); 

	uint32_t fsize = 0;
	for(int n = 0; n<msgp->u.Multi_writeObjReq.multi_vec.vector_cnt ; n++){
		fsize += msgp->u.Multi_writeObjReq.multi_vec.fcount[n];
	}
	size = fsize;
        SKYFS_ERROR("__skyfs_SS_alloc_pages: for multi write. size %lu\n", fsize); 
#if 0
	int fsize = msgp->u.writeObjReq.vec.fcount;

	// mayl : check if need to do compress/de=compress
	if(fsize <= 0)
        	size = msgp->u.writeObjReq.vec.count;
	else
        	size = msgp->u.writeObjReq.vec.fcount;
#endif	
        SKYFS_MSG("__skyfs_SS_alloc_pages:alloc for Write,size:%d\n",size);
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        //iovp->ak_addr = (skyfs_s8_t *)malloc(size);
#if 0
	// modified by mayl
	if(size  &(alignment-1)){
        	iovp->ak_addr = (skyfs_s8_t *)malloc(size);
	}else{

        	void * ptr;
		 if (posix_memalign(&ptr, alignment, size) != 0){

        		iovp->ak_addr = (skyfs_s8_t *)malloc(size);
		 }else{
        		iovp->ak_addr = (skyfs_s8_t *)ptr;

		 }

	}
#endif

        iovp->ak_addr = (skyfs_s8_t *)malloc(size);
        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov =iovp;
        SKYFS_MSG("__skyfs_SS_alloc_pages:addr:%p\n",iovp->ak_addr);
    }else{
        SKYFS_ERROR_1("__skyfs_SS_alloc_pages:error msg type\n");
        rc = -1;
    }

    SKYFS_LEAVE("__skyfs_SS_alloc_pages:exit\n");
    return rc;
}

void __skyfs_SS_free_pages(skyfs_u32_t num, amp_kiov_t *kiov)
{
    skyfs_u32_t i;

    SKYFS_ENTER("__skyfs_SS_free_pages:enter\n");

    for(i = 0; i < num; i ++){
        free(kiov[i].ak_addr);
    }

    SKYFS_LEAVE("__skyfs_SS_free_pages:exit\n");
    return;
}

skyfs_s32_t __skyfs_SS_init_com(void)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t serv_id = 0;
    skyfs_s32_t ctxt_num = 0;
    skyfs_u32_t i = 0;//,j = 0;
    skyfs_DL_nodeinfo_t	*node_info = NULL; 
    skyfs_u32_t osd_id;


    //skyfs_s32_t        addr;
    //struct in_addr     naddr;
    
	SKYFS_ERROR_1("__skyfs_SS_init_com: enter,osd_this_id:%d,sizeof:%d\n", 
		osd_this_id,
		sizeof(amp_comp_context_t));

	bzero(&mds_info, sizeof(skyfs_mds_info_t));
    bzero(&osd_info, sizeof(skyfs_osd_info_t));
    bzero(&client_info, sizeof(skyfs_client_info_t));

	rc = __skyfs_get_arch_info(&arch_info);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_init_com:get skyfs.conf failed\n");
        goto err_out;
    }

	rc = __skyfs_init_nodes(&arch_info, 
			&mds_info, 
			&osd_info, 
			&client_info);
	if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_init_com:init arch failed\n");
        goto err_out;

	}

	for(i = 0; i < SKYFS_MAX_IP_NUM; i++){
		if(arch_info.ip[i].osd){
			SKYFS_ERROR("skyfs_SS_init_com:id:%d,ip:%s\n", 
				arch_info.ip[i].osd,
				arch_info.ip[i].addr);
		}
	}		

	mds_num = mds_info.mds_num;
    osd_num = osd_info.osd_num;
    client_num = client_info.client_num;

	/*Init osd_node_list for partition placement*/
    INIT_LIST_HEAD(&osd_node_head);
	for(i = 1; i < SKYFS_MAX_OSD_NUM; i++){
		if(osd_info.osd[i].id){
			SKYFS_MSG("%s insert osd_id:%d\n", __FUNCTION__, osd_info.osd[i].id);
			osd_id = osd_info.osd[i].id;
			node_info = (skyfs_DL_nodeinfo_t *)malloc(sizeof(skyfs_DL_nodeinfo_t));
			if(node_info){
				SKYFS_MSG("%s insert osd_id:%p\n", __FUNCTION__, node_info);
				node_info->osd_id = osd_id;
				node_info->access_times = 100;
				node_info->active_files = 0;
    			INIT_LIST_HEAD(&node_info->file_head);
				list_add(&node_info->node_list, &osd_node_head);	
			}else{
				SKYFS_ERROR("%s alloc nodeinfo err\n", __FUNCTION__);
				goto err_out;
			}
		}
	}

    osd_nr_request = 0;
	if(osd_this_id == 0){
		osd_this_id = __skyfs_SS_get_osdid();
	}else{
		if(osd_info.osd[osd_this_id].ip[0]->lid != skyfs_lid){
        	SKYFS_ERROR_1("__skyfs_SS_init_com:osd_id or lid is error.\n");
        	rc = -1;
			goto err_out;
		}
	}

    /*1. create comp context changed by mayl */
#ifndef _AMP_RDMA
    	osd_comp_context = amp_sys_init(SKYFS_OSD, 
			osd_this_id,
			AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_CONNECT,
            __skyfs_SS_queue_request,
            __skyfs_SS_alloc_pages,
            __skyfs_SS_free_pages);
#else

    	osd_comp_context = amp_sys_init(SKYFS_OSD, 
			osd_this_id);
#endif 
    	if(osd_comp_context == 0){
        	SKYFS_ERROR_1("__skyfs_SS_init_com:com context creat error.\n");
        	rc = -1;
        	goto err_out;
    	}

	SKYFS_ERROR("__skyfs_SS_init_com:conn_type:%d,lid:%d\n", 
		osd_comp_context->conn_type, skyfs_lid);

    /*2. craete listen port chanved by mayl*/
    for (serv_id = 0 ; serv_id <AMP_LISTEN_CNT; serv_id ++){
	osd_comp_context->acc_this_listen_id = serv_id;
	SKYFS_ERROR_1("create listen conn %d \n",  osd_comp_context->acc_this_listen_id);
    	rc = amp_create_connection(osd_comp_context,
            0,
            0,
            INADDR_ANY,
            SKYFS_OSD_COM_PORT + skyfs_lid+serv_id,
            AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_LISTEN,
            __skyfs_SS_queue_request,
            __skyfs_SS_alloc_pages,
            __skyfs_SS_free_pages);
    	 if(rc < 0){
        	SKYFS_ERROR("__skyfs_SS_init_com:create listen con err,err:%d\n", rc);
        	rc = -1;
        	goto err_out;
    	}
	
	

    }
  
 // recovered by mayl
#if 1
    int j = 0;
    struct in_addr naddr;
    int addr;


    for(i = osd_this_id+1; i <= osd_num; i++){ // include myself
        for(j = 0; j< osd_info.osd[i].ip_num; j++){
            SKYFS_ERROR_1("__skyfs_SS_connect_osd:osd_num:%d,id:%d,nic_num:%d,addr:%s\n",
                osd_num,
                osd_info.osd[i].id,
                osd_info.osd[i].ip_num,
                osd_info.osd[i].ip[j]->addr);
            rc = inet_aton(osd_info.osd[i].ip[j]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR_1("__skyfs_SS_connect_osd:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);

            rc = amp_create_connection(osd_comp_context, SKYFS_OSD,
                osd_info.osd[i].id, 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[i].ip[j]->lid,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                __skyfs_SS_queue_request, 
                __skyfs_SS_alloc_pages,
				__skyfs_SS_free_pages);
            if(rc != 0) {
                SKYFS_ERROR("__skyfs_SS_connect_osd:create connection error.\n");
                goto err_out;
            }
        }
    }
#endif 
    SKYFS_MSG("__skyfs_SS_init_com:create listen success:%d\n", rc);
	SKYFS_ERROR_1("__skyfs_SS_init_com:conn_type:%d\n", osd_comp_context->conn_type);

    /*3. init global para*/
    //INIT_LIST_HEAD(&osd_request_queue);
    INIT_LIST_HEAD(&osd_serveout_queue);
    INIT_LIST_HEAD(&osd_simple_queue);
    INIT_LIST_HEAD(&osd_stat_request_queue);
    INIT_LIST_HEAD(&osd_config_request_queue);
    INIT_LIST_HEAD(&osd_filebuf_queue);
    pthread_mutex_init(&osd_request_queue_lock, NULL);
    pthread_mutex_init(&osd_serveout_queue_lock, NULL);
    pthread_mutex_init(&osd_simple_queue_lock, NULL);
    pthread_mutex_init(&osd_stat_request_queue_lock, NULL);
    pthread_mutex_init(&osd_config_request_queue_lock, NULL);
    pthread_mutex_init(&osd_filebuf_queue_lock, NULL);
    //sem_init(&osd_request_queue_sem, 0, 0);
    //sem_init(&osd_serveout_queue_sem, 0, 0);
    //sem_init(&osd_simple_queue_sem, 0, 0);
    //sem_init(&osd_stat_request_queue_sem, 0, 0);
    //sem_init(&osd_config_request_queue_sem, 0, 0);
	//sem_init(&osd_loadb_sem, 0, 0);

    pthread_mutex_init(&read_perf_lock, NULL);
    pthread_mutex_init(&write_perf_lock, NULL);

	pthread_mutex_init(&osd_commit_write_lock, NULL);
	pthread_mutex_init(&osd_node_head_lock, NULL);

	osd_block_insert_objbuf = 0;
	pthread_mutex_init(&osd_block_insert_lock, NULL);
	//Waiting for config information
    //sem_init(&osd_config_sem, 0, 0);
	//sem_wait(&osd_config_sem);

	SKYFS_ERROR("__skyfs_SS_init_com:conn_type:%d\n", osd_comp_context->conn_type);
	//strncpy(osd_comp_context->adm_addr, skyfs_adm_addr, sizeof(skyfs_adm_addr));
	//osd_comp_context->adm_addr[sizeof(skyfs_adm_addr)] = '\0';
	//rc = __skyfs_get_config(&arch_info, SKYFS_OSD, osd_this_id, osd_comp_context);


    return rc;

err_out:
    if(osd_comp_context){
        amp_sys_finalize(osd_comp_context);
    }

    SKYFS_LEAVE("__skyfs_SS_init_com: leave\n");
    return rc;
}
/*
 * Init filesystem 
 */
skyfs_s32_t    
__skyfs_SS_init_osd(void)
{
	skyfs_u32_t    i;
    skyfs_s32_t    rc = 0;
	skyfs_s32_t    sleep_time = 0;
	skyfs_s8_t     osd_store_dir[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t     cmd[SKYFS_MAX_NAME_LEN];
	struct stat buf;

	SKYFS_ERROR("__skyfs_SS_init_osd, osd_this_id:%d\n", osd_this_id);

	bzero(osd_store_dir, SKYFS_MAX_NAME_LEN * sizeof(skyfs_s8_t));
	sprintf(osd_store_dir, "%s/%d", SKYFS_OBJ_PATH, skyfs_lid);
	if((rc = stat(osd_store_dir, &buf)) == -1){
		rc = mkdir(osd_store_dir, 0666);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_SS_init_osd:create sub:%s,err:%d\n", 
        		osd_store_dir, errno);
			goto ERR;
		}
	}

	if(strlen(skyfs_disk[skyfs_lid]) != 0){
		sprintf(cmd, "mount %s %s", skyfs_disk[skyfs_lid], osd_store_dir);
		SKYFS_ERROR("__skyfs_SS_init_osd: %s\n", cmd);
		//system(cmd);
	}

    rc = __skyfs_SS_init_layout();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_init_osd, init layout error\n");
        goto ERR;
    }
        
	if(osd_this_id == 1){
		if(osd_num < 200){
			sleep_time = 5;
		}else{
			sleep_time = osd_num / 50;
		}
		sleep(sleep_time);
	}

    rc = __skyfs_SS_init_data_layout();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_init_osd, init data layout error\n");
        goto ERR;
    }
    for(i = 0; i <= osd_num; i++){
        sort_osd_status[i] = i;
    }
    /* added by mayl for pendning  io */
     for(i = 0; i < SKYFS_DOP_HASH_LEN; i++){
        INIT_LIST_HEAD(&skyfs_dop_htbbase[i].head);
        pthread_mutex_init(&skyfs_dop_htbbase[i].lock, NULL);
    }

	
ERR:

    return rc;
}

static void __skyfs_SS_sync_metadir(char * dir_path)

{
	DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir()");
        return;
    }
 
    SKYFS_ERROR_1("start sync direcory %s \n", dir_path);
    struct dirent *entry;
    struct stat statbuf; 
    while ((entry = readdir(dir)) != NULL) {
	    if(!strcmp(entry->d_name, "."))
		    continue;

	    if(!strcmp(entry->d_name, ".."))
		    continue;
	    //printf("get_file_name %s\n",  entry->d_name);

        if (entry->d_type == DT_REG) { // 只刷新普通文件
            char file_path[4096];
            snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
            int fd = open(file_path, O_RDWR);
            if (fd == -1) {
                perror("open()");
    	    SKYFS_ERROR_1("open fsync file %s FAILED\n", file_path);
                continue;
            }
	    fstat(fd, &statbuf);
    	    //printf("start fsync regular file %s , ino %lu , size %lu\n", file_path, statbuf.st_ino, statbuf.st_size);
            // 刷新文件
            fsync(fd);
            close(fd);
        }
	else{
            char subdir_path[4096];
            snprintf(subdir_path, sizeof(subdir_path), "%s/%s", dir_path, entry->d_name);
	    __skyfs_SS_sync_metadir(subdir_path);
	}
    }
 
    closedir(dir);


}

skyfs_s32_t __skyfs_SS_finalize_osd(void)
{
    skyfs_s32_t rc = 0;

	/*1, writeback dl cache*/
	rc = __skyfs_DL_writeback_cache();

	
	/*2, writeback checksum*/


    return rc;
}

/*
 * signal process
 */
void __skyfs_SS_excp_handler(skyfs_s32_t signo)
{
    SKYFS_ERROR_1("\n\n ---EXCP_HANDLER--- signal %d\n", signo);

    if(signo == SIGUSR1){
	    SKYFS_ERROR_1("Get signal %d, start test send replica objs, dest_osd_id %d, src_replica_id %d, dest_replica_id %d\n", 
			    signo, 2,1,1 );
	    //return ;


//skyfs_s32_t 
//__skyfs_O2O_recover_data_objs(int src_replica_id,int dest_replica_id,int dest_osd_id)
      int rc =  __skyfs_O2O_recover_data_objs(2,1,1, 0xffffffff);
      sleep(10);
	    return ;
    }
    if(signo == SIGUSR2){
	    SKYFS_ERROR_1("Get signal %d, start test send replica partitions, dest_osd_id %d, src_replica_id %d, dest_replica_id %d\n", 
			    signo, 2, 1,2 );
	    //return ;


//skyfs_s32_t 
//__skyfs_O2O_recover_data_objs(int src_replica_id,int dest_replica_id,int dest_osd_id)
      //int rc =  __skyfs_O2O_recover_partitions(1,2,2);
      int rc =  __skyfs_O2O_recover_partitions(2,1,1, 0xffffffff);
      sleep(10);
	    return ;
    }

     __skyfs_SS_sync_metadir(SKYFS_DL_PATH);
     __skyfs_SS_sync_metadir(SKYFS_OBJ_PATH);
     __skyfs_SS_sync_metadir(SKYFS_META_PATH);
    amp_sys_finalize(osd_comp_context);

    __skyfs_SS_finalize_osd();
    __skyfs_SS_stop_threads();

    SKYFS_ERROR_1("---END EXCP_HANDLER---\n\n");
}

skyfs_s32_t __skyfs_SS_init_signal(void)
{
    skyfs_s32_t rc = 0;

    struct sigaction act;

    SKYFS_ENTER("__skyfs_SS_init_signal:enter\n");
    act.sa_handler = __skyfs_SS_excp_handler;
    sigemptyset(&act.sa_mask);

    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);

    SKYFS_LEAVE("__skyfs_SS_init_signal:leave\n");
    return rc;
}

/*This is end of osd_init.c*/
