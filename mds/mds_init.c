/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_init.c $
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


#include "mds_fs.h"
#include "mds_op.h"
#include "mds_thread.h"
#include "mds_init.h"
#include "mds_help.h"
#include "mds_cache.h"
#include "mds_meta.h"
#include "mds_layout.h"

#include "mds_ito.h"

//extern skyfs_M_cmeta_t root_cmeta;

extern skyfs_M_cmeta_t root_cmeta;
extern void do_mds_request(amp_request_t *req, int msg_type);
extern int alloc_meta_cnt;
extern int free_meta_cnt;

amp_comp_context_t      *mds_comp_context;
skyfs_u32_t             mds_this_id;
skyfs_u32_t             mds_num;
skyfs_u32_t             osd_num;
skyfs_u32_t             client_num;
skyfs_mds_info_t        mds_info;
skyfs_osd_info_t        osd_info;
skyfs_client_info_t     client_info;
skyfs_arch_info_t       arch_info;

skyfs_u64_t             mds_nr_request;

struct list_head        mds_config_request_queue;
pthread_mutex_t         mds_config_request_queue_lock;
sem_t                   mds_config_request_queue_sem;

struct list_head        mds_wb_request_queue;
pthread_mutex_t         mds_wb_request_queue_lock;
sem_t                   mds_wb_request_queue_sem;

struct list_head        mds_balance_request_queue;
pthread_mutex_t         mds_balance_request_queue_lock;
sem_t                   mds_balance_request_queue_sem;

struct list_head        mds_simple_request_queue;
pthread_mutex_t         mds_simple_request_queue_lock;
sem_t                   mds_simple_request_queue_sem;

struct list_head        mds_request_queue;
pthread_mutex_t         mds_request_queue_lock;
sem_t                   mds_request_queue_sem;

skyfs_u32_t             mds_red_alarm = 0;
skyfs_u32_t             mds_balance_ratio = 0;
skyfs_u32_t             skyfs_ib_flag = 0;
skyfs_u32_t             skyfs_profile_flag = 0;
skyfs_u32_t             mds_profile_create = 0;
skyfs_u32_t             mds_profile_split = 0;
skyfs_u32_t             mds_profile_enlarge = 0;

pthread_mutex_t         mds_profile_create_lock;
pthread_mutex_t         mds_profile_split_lock;
pthread_mutex_t         mds_profile_enlarge_lock;

pthread_mutex_t         skyfs_debug_lock;

sem_t                   mds_config_sem;
skyfs_arch_info_t       arch_info;

skyfs_s8_t              skyfs_adm_addr[SKYFS_MAX_NAME_LEN];
/*
 * parse parameters
 */ 
skyfs_s32_t __skyfs_MS_parse_parameter(skyfs_s32_t argc, skyfs_s8_t **argv)
{
    skyfs_u32_t daemonlize;
    skyfs_s8_t    c;
    skyfs_s32_t rc = 0;

    daemonlize = 1;
    
    SKYFS_ENTER("__skyfs_MS_parse_parameter:enter.\n");

    if(argc < 2){
__error_parameter:
        printf("Usage:\t%s [-b | -f]  id\n", argv[0]);
        printf("\t-b ---- running on background, it's default option\n");
        printf("\t-f ---- running on foreground\n");
        printf("\tid ---- the id of this mos\n");
        exit (1);
    }

    mds_this_id = atoi(argv[argc - 1]);
    
    while ((c = getopt(argc, argv, "bf")) != EOF) {
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
            default:
                printf("Wrong option: %c\n", c);
                exit(-1);
        }
    }

    if (daemonlize){
        __skyfs_daemonize(0);
    }

    SKYFS_LEAVE("__skyfs_MS_parse_parameter:leave.rc=%d\n",rc);
    return rc;
}

/*
 * get conf info
 */
skyfs_s32_t __skyfs_M_get_var_conf(void)
{
    skyfs_s32_t rc = 0;
    
    FILE *fp;

    char str[SKYFS_MAX_NAME_LEN], dir[SKYFS_MAX_NAME_LEN], cmd[SKYFS_MAX_NAME_LEN], type[SKYFS_MAX_NAME_LEN];

    SKYFS_ENTER("__skyfs_M_get_var_conf:enter\n");

    sprintf(dir, "%s%s", SKYFS_ARCCFG_FILE_PATH, SKYFS_VAR_CONFIG);
    fp = fopen(dir, "r");
    if(!fp){
        rc = -1;
        SKYFS_ERROR("__skyfs_M_get_var_conf:can't open %s file\n", dir);
        return rc;
    }

    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        bzero(cmd, SKYFS_MAX_NAME_LEN);
        bzero(type, SKYFS_MAX_NAME_LEN);
        sscanf(str, "%s %s", cmd, type); 
        bzero(str, SKYFS_MAX_NAME_LEN);
		if(strcmp(cmd, "SKYFS_ADM_ADDR") == 0){
        	bzero(skyfs_adm_addr, SKYFS_MAX_NAME_LEN);
			strcpy(skyfs_adm_addr, type);
            SKYFS_ERROR("__skyfs_M_get_var_conf:adm_addr:%s\n", skyfs_adm_addr);
			continue;
		}

        if(strcmp(cmd, "MDS_RED_ALARM") == 0){
            mds_red_alarm = atoi(type);
            SKYFS_ERROR("__skyfs_M_get_var_conf:red_alarm:%d\n", mds_red_alarm);
            continue;
        }
        if(strcmp(cmd, "MDS_BALANCE_RATIO") == 0){
            mds_balance_ratio = atoi(type);
            SKYFS_ERROR("__skyfs_M_get_var_conf:balance_ratio:%d\n", 
				mds_balance_ratio);
            continue;
        }    
        if(strcmp(cmd, "SKYFS_IB") == 0){
            skyfs_ib_flag = atoi(type);
            SKYFS_ERROR("__skyfs_M_get_var_conf:skyfs_ib_flag:%d\n", 
				skyfs_ib_flag);
            continue;
        }
        if(strcmp(cmd, "SKYFS_PROFILE") == 0){
            skyfs_profile_flag = atoi(type);
            SKYFS_ERROR("__skyfs_M_get_var_conf:skyfs_profile_flag:%d\n", 
                skyfs_profile_flag);
            continue;
        }
    }

    fclose(fp);

    if(mds_red_alarm == 0){
        mds_red_alarm = MDS_RED_ALARM;
    }

    if(mds_balance_ratio == 0){
        mds_balance_ratio = 10;
    }

    SKYFS_LEAVE("__skyfs_M_get_var_conf:exit\n");
    return rc;
}

skyfs_s32_t __skyfs_MS_get_conf(void)
{
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_get_conf:enter\n");

    
#if 0
    rc = __skyfs_get_arch_info(&arch_info);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_get_conf:get dcfs3.conf failed\n");
        goto err_out;
    }

    mds_num = mds_info.mds_num;
    osd_num = osd_info.osd_num;
    client_num = client_info.client_num;
#endif

    rc = __skyfs_M_get_var_conf();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_get_conf:get var conf failed\n");
        goto err_out;
    }

    
    
err_out:
    SKYFS_LEAVE("__skyfs_MS_get_conf:exit\n");
    return rc;
}

/*
 * ini com
 */

skyfs_s32_t __skyfs_MS_queue_request(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t msg_type = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_queue_request:enter.\n");

    msgp = __skyfs_get_msg(req->req_msg);
    msg_type = msgp->type;

    if((msg_type >= SKYFS_MSG_M_STATFS && msg_type <= SKYFS_MSG_M_FLOCK)
            || msg_type == SKYFS_MSG_M_BALANCE_LOAD){
	
	 /*
	 if((msg_type >= SKYFS_MSG_M_STATFS && msg_type <= SKYFS_MSG_M_FLOCK))
	 {
		 // optimazed by mayl , skip queuing the request, do request here directly
		 do_mds_request(req, (msg_type & 0xff));
		 return rc;
	 }*/

        SKYFS_MSG("__skyfs_MS_queue_request:common request.\n");
        pthread_mutex_lock(&mds_request_queue_lock);
        mds_nr_request ++;
        list_add_tail(&req->req_list, &mds_request_queue);
        pthread_mutex_unlock(&mds_request_queue_lock);
        rc = amp_sem_up(&mds_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_queue_request:common sem up error\n");
        }
    }else if((msg_type > SKYFS_MSG_M_FLOCK && msg_type < SKYFS_MSG_O_READ_OBJ)
            || msg_type == SKYFS_MSG_STATE){
        SKYFS_MSG("__skyfs_MS_queue_request:simple request.\n");
        pthread_mutex_lock(&mds_simple_request_queue_lock);
        mds_nr_request ++;
        list_add_tail(&req->req_list, &mds_simple_request_queue);
        pthread_mutex_unlock(&mds_simple_request_queue_lock);
        rc = amp_sem_up(&mds_simple_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_queue_request:simple sem up error\n");
        }
    }else if(msg_type == SKYFS_MSG_INIT_CONFIG) {
		__skyfs_MS_init_config(req);	
	}else if(msg_type == SKYFS_MSG_M_TRIGGER_BLA){
        SKYFS_MSG("__skyfs_MS_queue_request:get balance request.\n");
        pthread_mutex_lock(&mds_balance_request_queue_lock);
        list_add_tail(&req->req_list, &mds_balance_request_queue);
        pthread_mutex_unlock(&mds_balance_request_queue_lock);
        rc = amp_sem_up(&mds_balance_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_queue_request:stat sem up error\n");
        }
    }else if(req->req_remote_type == SKYFS_CFG){
        SKYFS_MSG("__skyfs_MS_queue_request:it's a configuration request.\n");
        pthread_mutex_lock(&mds_config_request_queue_lock);
        list_add_tail(&req->req_list, &mds_config_request_queue);
        pthread_mutex_unlock(&mds_config_request_queue_lock);
        rc = amp_sem_up(&mds_config_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_queue_request:config sem up error\n");
        }
    }else{
    	SKYFS_ERROR("__skyfs_MS_queue_request:err type:%d.\n", msg_type);
    }

    SKYFS_LEAVE("__skyfs_MS_queue_request:leave.\n");
    return rc;
}

skyfs_s32_t __skyfs_MS_alloc_pages(void *msg, skyfs_u32_t *num, amp_kiov_t **kiov)
{
    amp_kiov_t *iovp = NULL;

    SKYFS_MSG("__skyfs_MS_alloc_pages:enter\n");

    iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
    bzero(iovp, sizeof(amp_kiov_t));
    iovp->ak_addr = (skyfs_s8_t *)malloc(SKYFS_DIR_BLK_SIZE);
    iovp->ak_len = SKYFS_DIR_BLK_SIZE;
    iovp->ak_offset = 0;
    iovp->ak_flag = 0;
    *num = 1;
    *kiov =iovp;

    SKYFS_MSG("__skyfs_MS_alloc_pages:exit\n");
    return 0;
}

void __skyfs_MS_free_pages(skyfs_u32_t num, amp_kiov_t *kiov)
{
    skyfs_u32_t i;

    for(i = 0; i < num; i ++){
        free(kiov[i].ak_addr);
    }

    return;
}

skyfs_s32_t __skyfs_MS_init_com(void)
{
	struct in_addr naddr;
    int addr;
    int rc = 0;
    int i,j;

    SKYFS_ENTER("__skyfs_MS_init_com: enter\n");
    SKYFS_ERROR_1("__skyfs_MS_init_com: cmeta size:%lu\n", 
		sizeof(skyfs_M_cmeta_t));

    mds_nr_request = 0;

	bzero(&mds_info, sizeof(skyfs_mds_info_t));
    bzero(&osd_info, sizeof(skyfs_osd_info_t));
    bzero(&client_info, sizeof(skyfs_client_info_t));

	rc = __skyfs_get_arch_info(&arch_info);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MDS_init_com:get skyfs.conf failed\n");
        goto err_out;
    }

	rc = __skyfs_init_nodes(&arch_info, 
			&mds_info, 
			&osd_info, 
			&client_info);
	if(rc < 0){
        SKYFS_ERROR("__skyfs_MDS_init_com:init arch failed\n");
        goto err_out;

	}

	mds_num = mds_info.mds_num;
    osd_num = osd_info.osd_num;
    client_num = client_info.client_num;

	if(mds_this_id == 0){
		mds_this_id = __skyfs_MS_get_mdsid();
	}

    /*1. create comp context*/
#ifndef _AMP_RDMA
    mds_comp_context = amp_sys_init(SKYFS_MDS, 
			mds_this_id,
			AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_CONNECT,
            __skyfs_MS_queue_request,
            __skyfs_MS_alloc_pages,
            __skyfs_MS_free_pages);
#else
    mds_comp_context = amp_sys_init(SKYFS_MDS, 
			mds_this_id);

#endif
    if(mds_comp_context == 0){
        SKYFS_ERROR_1("__skyfs_MS_init_com:com context creat error.\n");
        rc = -1;
        goto err_out;
    }

    /*2. craete listen ports changed  by mayl*/
    for(i=0; i<AMP_LISTEN_CNT; i++ ){
    	mds_comp_context->acc_this_listen_id = i;
   	 rc = amp_create_connection(mds_comp_context,
            0,
            0,
            INADDR_ANY,
            SKYFS_MDS_COM_PORT+i,
            AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_LISTEN,
            __skyfs_MS_queue_request,
            __skyfs_MS_alloc_pages,
            __skyfs_MS_free_pages);
    	if(rc < 0){
        	SKYFS_ERROR("__skyfs_MS_init_com:cre l_connection err:%d\n", rc);
        	goto err_out;
    	}
    }

    for(i=0; i < SKYFS_MAX_OSD_NUM; i++){
		if(osd_info.osd[i].id){
        	for(j = 0; j< osd_info.osd[i].ip_num; j++){
           		SKYFS_ERROR_1("__skyfs_MS_init_com:osd_num:%d,id:%d,nic_num:%d,addr:%s\n",
                	osd_num,
                	osd_info.osd[i].id,
                	osd_info.osd[i].ip_num,
                	osd_info.osd[i].ip[j]->addr);
				SKYFS_ERROR("__skyfs_MS_init_com:i:%d,j:%d,lid:%d\n",
					i, j, osd_info.osd[i].ip[j]->lid);

            	rc = inet_aton(osd_info.osd[i].ip[j]->addr, &naddr);
            	if(rc == 0) {
                	SKYFS_ERROR_1("__skyfs_MS_init_com:wrong ip address\n");
                	exit(1);
            	}
            	addr = htonl(naddr.s_addr);
            	rc = amp_create_connection(mds_comp_context, SKYFS_OSD,
                	osd_info.osd[i].id, 
                	addr,
                	SKYFS_OSD_COM_PORT + osd_info.osd[i].ip[j]->lid,
                	AMP_CONN_TYPE_TCP,
                	AMP_CONN_DIRECTION_CONNECT,
                	__skyfs_MS_queue_request, 
					__skyfs_MS_alloc_pages,
                	NULL);
            	if(rc != 0) {
                	SKYFS_ERROR_1("__skyfs_MS_init_com:create conn err.i:%d,j:%d,add:%s\n",
						i,j,osd_info.osd[i].ip[j]->addr);
                	goto err_out;
            	}
        	}
		}
    }

    /*3. init global para*/
    INIT_LIST_HEAD(&mds_request_queue);
    INIT_LIST_HEAD(&mds_simple_request_queue);
    INIT_LIST_HEAD(&mds_balance_request_queue);
    INIT_LIST_HEAD(&mds_config_request_queue);
    INIT_LIST_HEAD(&mds_wb_request_queue);
    pthread_mutex_init(&mds_request_queue_lock, NULL);
    pthread_mutex_init(&mds_simple_request_queue_lock, NULL);
    pthread_mutex_init(&mds_balance_request_queue_lock, NULL);
    pthread_mutex_init(&mds_config_request_queue_lock, NULL);
    pthread_mutex_init(&mds_wb_request_queue_lock, NULL);
    sem_init(&mds_request_queue_sem, 0, 0);
    sem_init(&mds_simple_request_queue_sem, 0, 0);
    sem_init(&mds_balance_request_queue_sem, 0, 0);
    sem_init(&mds_config_request_queue_sem, 0, 0);
    sem_init(&mds_wb_request_queue_sem, 0, 0);

	//Waiting for config information
    //sem_init(&mds_config_sem, 0, 0);
	//sem_wait(&mds_config_sem);

	//rc = __skyfs_get_config(&arch_info, SKYFS_MDS, mds_this_id, mds_comp_context);
	//rc = __skyfs_init_nodes(&arch_info, &mds_info, &osd_info, &client_info);

	
	SKYFS_ERROR_1("__skyfs_MS_init_com: exit with success:%d\n", rc); 

    return rc;

err_out:
    if(mds_comp_context){
        amp_sys_finalize(mds_comp_context);
    }

    SKYFS_LEAVE("__skyfs_MS_init_com: leave\n");
    return rc;
}

skyfs_s32_t __skyfs_MS_judge_fs_exist(void)
{
    skyfs_s32_t rc = 0;
    struct stat buf;
    skyfs_s8_t root_name[SKYFS_MAX_NAME_LEN];
    
    SKYFS_ENTER("__skyfs_MS_judge_fs_exist: enter\n");

    sprintf(root_name, "%s%s", SKYFS_LOCAL_META_PATH, "root");
    
    if(stat(root_name, &buf) == -1){
        SKYFS_MSG("__skyfs_MS_judge_fs_exist:root not exist\n");
        rc = -1;
    }
    
    SKYFS_ENTER("__skyfs_MS_judge_fs_exist: exit.rc:%d\n", rc);
    return rc;
}

skyfs_s32_t __skyfs_MS_create_fs(void)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t fd;
    skyfs_u32_t dir_id, osd_id;
    skyfs_M_cmeta_t cmeta;
    skyfs_s8_t pathname[SKYFS_MAX_NAME_LEN];

    SKYFS_ERROR_1("__skyfs_MS_create_fs:enter\n");
    alloc_meta_cnt =0;
    free_meta_cnt = 0;

    /*1. create root cmeta*/
    bzero(&cmeta, sizeof(skyfs_M_cmeta_t));
    cmeta.type = SKYFS_DIR;
    memcpy(cmeta.name, "/", strlen("/"));
    cmeta.name[strlen("/")] = '\0';
    cmeta.size = SKYFS_DIR_BLK_SIZE;
    gettimeofday(&cmeta.atime, NULL);
    cmeta.ctime = cmeta.mtime =cmeta.atime;
    cmeta.mode = S_IFDIR|0755;
    cmeta.nlink = 2;
    cmeta.ino = SKYFS_ROOT_INO;
    cmeta.type = SKYFS_DIR;

    sprintf(pathname, "%s%s", SKYFS_LOCAL_META_PATH, "root");
    fd = open(pathname, O_RDWR|O_CREAT, 0666);
    if(fd > 0){
        write(fd, &cmeta, sizeof(skyfs_M_cmeta_t));
        close(fd);
    }else{
        rc = -1;
        SKYFS_ERROR_1("__skyfs_MS_create_fs:create %s failed\n", pathname);
        goto err_out;
    }

    /*2. create root dir file in osd*/
    dir_id = __skyfs_MS_get_dir_id(SKYFS_ROOT_INO, 0);
    osd_id = __skyfs_MS_judge_osdid(dir_id, 0);
    rc = __skyfs_M2O_create_subset_file(osd_id, dir_id);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_create_fs:create root subset failed\n");
        goto err_out;
    }
    SKYFS_ERROR_1("__skyfs_MS_create_fs:create root subset ret %d\n", rc);

    /*3. mayl: create root dir, copy root_cmeta for updating depth*/
    rc = __skyfs_MS_init_dir_cache(&cmeta);
    memcpy(&root_cmeta, &cmeta, sizeof (skyfs_M_cmeta_t));
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_create_fs:init root dir cache failed\n");
        goto err_out;
    }else{
        SKYFS_ERROR_1("__skyfs_MS_create_fs:init root dir cache , ino %lu , depth %lu\n", root_cmeta.ino, root_cmeta.depth);
    }

err_out:

    return rc;
}

skyfs_s32_t __skyfs_MS_init_super(void)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t fd;
    //skyfs_u32_t dir_id, osd_id;
    skyfs_M_cmeta_t cmeta;
    skyfs_s8_t pathname[SKYFS_MAX_NAME_LEN];

    SKYFS_ENTER("__skyfs_MS_init_super:enter\n");

	/*1. Read dir*/
	sprintf(pathname, "%s%s", SKYFS_LOCAL_META_PATH, "root");
    fd = open(pathname, O_RDWR|O_CREAT, 0666);
    if(fd > 0){
        read(fd, &cmeta, sizeof(skyfs_M_cmeta_t));
        close(fd);
    }else{
        rc = -1;
        SKYFS_ERROR("__skyfs_MS_init_super:read %s failed,errno:%d\n", 
			pathname, errno);
        goto err_out;
    }

	/*2. Init root dir*/
    rc = __skyfs_MS_init_dir_cache(&cmeta);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_create_fs:init root dcache failed,rc:%d\n",
			rc);
        goto err_out;
    }

	if(__skyfs_MS_get_metaino() <  0){
		skyfs_free_dir_id_head = 1;
        skyfs_conflict_ino_head = 1;
	}

err_out:

    SKYFS_LEAVE("__skyfs_MS_init_super:exit\n");

    return rc;
}

/*
 * Init filesystem 
 */
skyfs_s32_t    __skyfs_MS_init_fs(void)
{
    skyfs_s32_t    rc = 0;

    SKYFS_ERROR_1("__skyfs_MS_init_fs:enter\n");

    /*1. init dir_cache, subset_cache, dir_depth*/
    rc = __skyfs_MS_init_cache();
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_init_fs:init cache failed\n");
        goto err_out;
    }

    /*2. init layout*/
    rc = __skyfs_MS_init_layout();
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_init_fs:init layout failed\n");    
        goto err_out;
    }

    /*3. create new fs if it's master and init root inode*/
    SKYFS_MSG("__skyfs_MS_init_fs:mds_this_id:%d\n", mds_this_id);
    if(mds_this_id == SKYFS_MASTER_MDS_ID){
        rc = __skyfs_MS_judge_fs_exist();
	SKYFS_ERROR("Fs exist :%d\n", rc);
        if(rc < 0){
            rc = __skyfs_MS_create_fs();
            if(rc < 0){
                SKYFS_ERROR_1("__skyfs_MS_init_fs:create fs failed\n");
                goto err_out;
            }
        }else{
            rc = __skyfs_MS_init_super();
            if(rc < 0){
                SKYFS_ERROR_1("__skyfs_MS_init_fs:init super failed\n");
                goto err_out;
            }
        }
    }

    /*4. init shared parameter*/
    gettimeofday(&last_stat_time, NULL);

    pthread_mutex_init(&skyfs_statfs_lock, NULL);
    pthread_mutex_init(&forward_request_lock, NULL);

    pthread_mutex_init(&(skyfs_free_dir_id.lock), NULL);
    INIT_LIST_HEAD(&(skyfs_free_dir_id.head));

    pthread_mutex_init(&(skyfs_conflict_ino.lock), NULL);
    INIT_LIST_HEAD(&(skyfs_conflict_ino.head));

    pthread_mutex_init(&(mds_profile_create_lock), NULL);
    pthread_mutex_init(&(mds_profile_split_lock), NULL);
    pthread_mutex_init(&(mds_profile_enlarge_lock), NULL);
    pthread_mutex_init(&(skyfs_debug_lock), NULL);

    SKYFS_ERROR("__skyfs_MS:dcache:%lu,ddepth:%lu,subset:%lu,bmeta:%lu\n",
        sizeof(skyfs_M_dir_cache_t), 
        sizeof(skyfs_M_dir_depth_t), 
        sizeof(skyfs_M_subset_cache_t),
        sizeof(skyfs_M_bmeta_t));

err_out:
    
    SKYFS_ERROR_1("__skyfs_MS_init_fs:leave\n");
    return rc;
}

skyfs_s32_t    __skyfs_MS_finalize_fs(void)
{
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_finalize_fs:enter\n");

    /*1. write back root inode and cache*/
    if(mds_this_id == SKYFS_MASTER_MDS_ID){
        rc = __skyfs_MS_writeback_root_meta();
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_finalize_fs:writeback root meta failed\n");
            goto err_out;
        }
    }

    rc = __skyfs_MS_writeback_cache();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_finalize_fs:writeback cache faield\n");
        goto err_out;
    }

    /*2, free dir_cache, subset_cache and depth*/
    rc = __skyfs_MS_finalize_cache();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_finalize_fs:release cache failed\n");
        goto err_out;
    }

    /*3, write back layout info*/
    rc = __skyfs_MS_writeback_layout();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_finalize_fs:writeback layout failed\n");
        goto err_out;
    }

    /*4, write back ino info*/
	rc = __skyfs_MS_writeback_metaino();
	if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_finalize_fs:writeback metaino failed\n");
        goto err_out;

	}

err_out:

    SKYFS_LEAVE("__skyfs_MS_finalize_fs:exit\n");

    return rc;
}

/*
 * signal process
 */
void __skyfs_MS_excp_handler(skyfs_s32_t signo)
{
    SKYFS_ERROR("\n\n ---EXCP_HANDLER---\n");

    /*show profiling result*/
    SKYFS_ERROR("total_access_bmeta_num:%d\n", total_access_bmeta_num);
    SKYFS_ERROR("total_read_bmeta_num:%d\n", total_read_bmeta_num);
    SKYFS_ERROR("total_bmeta_num:%d\n", total_bmeta_num);

    __skyfs_MS_finalize_fs();
    __skyfs_MS_stop_threads();

    SKYFS_ERROR("---END EXCP_HANDLER---\n\n");
}

skyfs_s32_t __skyfs_MS_init_signal(void)
{
    skyfs_s32_t rc = 0;

    struct sigaction act;

    SKYFS_ENTER("__skyfs_MS_init_signal:enter\n");
    act.sa_handler = __skyfs_MS_excp_handler;
    sigemptyset(&act.sa_mask);

    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);

    SKYFS_LEAVE("__skyfs_MS_init_signal:leave\n");
    return rc;
}
/*This is end of mds_init.c*/
