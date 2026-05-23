/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_init.c $
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


#include "client_help.h"
#include "client_init.h"


struct list_head client_pending_list;


extern skyfs_htb_t         *compbuf_hash_base;
int osd_connect_state[SKYFS_MAX_OSD_NUM];
int mds_connect_state[SKYFS_MAX_MDS_NUM];

amp_comp_context_t      *client_comp_context = NULL;
int                     client_this_id;

skyfs_mds_info_t        mds_info;
skyfs_osd_info_t        osd_info;
skyfs_client_info_t     client_info;

skyfs_u32_t             is_async_write;

skyfs_u32_t             mds_num;
skyfs_u32_t             osd_num;
skyfs_u32_t             client_num;

skyfs_u32_t             skyfs_ib_flag = 0;
skyfs_u32_t             skyfs_replica = 0;

struct list_head        client_request_queue;
pthread_mutex_t         client_request_queue_lock;
pthread_mutex_t         client_pending_lock; // added by mayl
sem_t                   client_request_queue_sem;

sem_t                   client_config_sem;
skyfs_arch_info_t       arch_info;

skyfs_s8_t skyfs_adm_addr[SKYFS_MAX_NAME_LEN];

int get_replica()
{
	return skyfs_replica;
}

int __skyfs_C_init_com(void)
{
    struct in_addr naddr;
    int addr;
    int rc = 0;
    int i,j, listen_num;

    SKYFS_ENTER("__skyfs_C_init_com:enter\n");
    
    /*0.get skyfs_var.conf*/
    rc = __skyfs_C_get_var_conf();
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C_init_com:get skyfs_var.conf failed\n");
        goto err_out;
    }

    /*1.get mds_info & osd_info*/
    bzero(&mds_info, sizeof(skyfs_mds_info_t));
    bzero(&osd_info, sizeof(skyfs_osd_info_t));
    bzero(&client_info, sizeof(skyfs_client_info_t));

	rc = __skyfs_get_arch_info(&arch_info);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C_get_conf:get skyfs.conf failed\n");
        goto err_out;
    }

	rc = __skyfs_init_nodes(&arch_info, 
			&mds_info, 
			&osd_info, 
			&client_info);
	if(rc < 0){
        SKYFS_ERROR("__skyfs_C_get_conf:init arch failed\n");
        goto err_out;
	}

	mds_num = mds_info.mds_num;
    osd_num = osd_info.osd_num;
    client_num = client_info.client_num;

	if(client_this_id == 0){
		client_this_id = __skyfs_C_get_clientid();
	}

	/*3.init client amp comp*/
    SKYFS_MSG("__skyfs_C_init_com:create client_comp_context\n");
#ifdef _AMP_RDMA
    client_comp_context = amp_sys_init(SKYFS_CLIENT, 
			client_this_id);
#else
    client_comp_context = amp_sys_init(SKYFS_CLIENT, 
			client_this_id,
			AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_CONNECT,
            __skyfs_C_queue_request,
            __skyfs_C_alloc_pages,
            __skyfs_C_free_pages);
#endif
    if(client_comp_context == NULL){
        SKYFS_ERROR("__skyfs_C_init_com:create client_comp_context failed\n");
        goto err_out;
    }

    SKYFS_MSG("__skyfs_C_init_com:create listen connection\n");
#ifndef _AMP_RDMA
	rc = amp_create_connection(client_comp_context,
            0,
            0,
            INADDR_ANY,
            SKYFS_CLIENT_COM_PORT+client_this_id,
            AMP_CONN_TYPE_TCP,
            AMP_CONN_DIRECTION_LISTEN,
            __skyfs_C_queue_request,
            __skyfs_C_alloc_pages,
            __skyfs_C_free_pages);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C_init_com:create listen con err,err:%d\n", rc);
        rc = -1;
        goto err_out;
    }
#endif
	/*4.create connection with mds*/
	for(i = 0; i < SKYFS_MAX_MDS_NUM; i++){
		for(j = 0; j < mds_info.mds[i].ip_num; j++){
			SKYFS_ERROR("__skyfs_C_init_com:mds_num:%d,id:%d,nic_num:%d,addr:%s\n",
				mds_num,
				mds_info.mds[i].id,
				mds_info.mds[i].ip_num,
				mds_info.mds[i].ip[j]->addr);
			rc = inet_aton(mds_info.mds[i].ip[j]->addr, &naddr);
			if(rc == 0){
				SKYFS_ERROR("__skyfs_C_init_com:wrong ip address\n");
				exit(1);
			}
			addr = htonl(naddr.s_addr);
			for(listen_num = 0; listen_num < AMP_LISTEN_CNT; listen_num++){
			rc = amp_create_connection(client_comp_context, SKYFS_MDS,
				mds_info.mds[i].id,
				addr,
				SKYFS_MDS_COM_PORT + listen_num,
				AMP_CONN_TYPE_TCP,
				AMP_CONN_DIRECTION_CONNECT,
				NULL,
				NULL,
				NULL);
			if(rc != 0){
				SKYFS_ERROR("__skyfs_C_init_com:create connection error.\n");
				goto err_out;
			}
			}
		}
	}
	
	/*5.connet osd 1*/
	if(osd_info.osd[1].id != 0){
            rc = inet_aton(osd_info.osd[1].ip[0]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR("__skyfs_C_init_com:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);
	    // changed by mayl
	    for(listen_num = 0; listen_num < AMP_LISTEN_CNT; listen_num++){
            rc = amp_create_connection(client_comp_context, SKYFS_OSD,
                osd_info.osd[1].id , 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[1].ip[0]->lid + listen_num,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                NULL, 
				__skyfs_C_alloc_pages,
                NULL);
            if(rc != 0) {
                SKYFS_ERROR_1("__skyfs_C_init_com:create osd_1 connection error.\n");
                goto err_out;
             }else {
		    SKYFS_ERROR_1("__skyfs_C_init_com:create connection SUCCESS , osd1 , port %d\n",  SKYFS_OSD_COM_PORT+listen_num);
	    }

	    }
		}

#if 0

	// TODO : mayl need to support more than 2 osd node
	/*6.connet osd 2*/
	if(osd_info.osd[2].id != 0){
            rc = inet_aton(osd_info.osd[2].ip[0]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR_1("__skyfs_C_init_com:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);
	    // changed by mayl
	    for(listen_num = 0; listen_num < AMP_LISTEN_CNT; listen_num++){
            rc = amp_create_connection(client_comp_context, SKYFS_OSD,
                osd_info.osd[2].id , 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[2].ip[0]->lid + listen_num,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                NULL, 
				__skyfs_C_alloc_pages,
                NULL);
            if(rc != 0) {
                SKYFS_ERROR_1("__skyfs_C_init_com:create osd_2 connection error.\n");
                goto err_out;
             }else {
		    SKYFS_ERROR_1("__skyfs_C_init_com:create connection SUCCESS , osd2, port %d\n",  SKYFS_OSD_COM_PORT+listen_num);
	    }

	    }
		}


#endif
for(int osd_idx =2; osd_idx< SKYFS_MAX_OSD_NUM; osd_idx++){

	  osd_connect_state[osd_idx] = 0;
	  if(osd_info.osd[osd_idx].id != 0){
            rc = inet_aton(osd_info.osd[osd_idx].ip[0]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR_1("__skyfs_C_init_com:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);
	    // changed by mayl
	    for(listen_num = 0; listen_num < AMP_LISTEN_CNT; listen_num++){
            rc = amp_create_connection(client_comp_context, SKYFS_OSD,
                osd_info.osd[osd_idx].id , 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[osd_idx].ip[0]->lid + listen_num,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                NULL, 
				__skyfs_C_alloc_pages,
                NULL);
            if(rc != 0) {
                SKYFS_ERROR_1("__skyfs_C_init_com:create osd_2 connection error.\n");
		
	  	//osd_connect_state[osd_idx] = 0;
                goto err_out;
             }else {
		    SKYFS_ERROR_1("__skyfs_C_init_com:create connection SUCCESS , osd2, port %d\n",  SKYFS_OSD_COM_PORT+listen_num);
	    }

	    }
	    if(rc == 0){
	    	fprintf(stderr, "adm connect osd %d success \n",osd_idx);

	  	osd_connect_state[osd_idx] = 1;
	    }
	    else{
	    	fprintf(stderr, "adm connect osd %d failed \n",osd_idx);

	  	osd_connect_state[osd_idx] = 0;
	    }

	  }

		
	}



#if 0
	/*6.connet its local osd*/
		if(client_this_id != 1 && osd_info.osd[client_this_id].id != 0){
		    rc = inet_aton(osd_info.osd[client_this_id].ip[0]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR("__skyfs_C_init_com:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);
	    // changed by mayl
	    for(listen_num = 0; listen_num < AMP_LISTEN_CNT; listen_num++){
            rc = amp_create_connection(client_comp_context, SKYFS_OSD,
                osd_info.osd[client_this_id].id, 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[client_this_id].ip[0]->lid + listen_num,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                NULL, 
				__skyfs_C_alloc_pages,
                NULL);
            if(rc != 0) {
                SKYFS_ERROR("__skyfs_C_init_com:create connection error.\n");
                goto err_out;
            }
	    }
		}
#endif 
 
#if 0
    for(i=0; i < SKYFS_MAX_OSD_NUM; i++){
        for(j = 0; j< osd_info.osd[i].ip_num; j++){
            SKYFS_ERROR("__skyfs_C_init_com:osd_num:%d,id:%d,nic_num:%d,addr:%s\n",
                osd_num,
                osd_info.osd[i].id,
                osd_info.osd[i].ip_num,
                osd_info.osd[i].ip[j]->addr);
            rc = inet_aton(osd_info.osd[i].ip[j]->addr, &naddr);
            if(rc == 0) {
                SKYFS_ERROR("__skyfs_C_init_com:wrong ip address\n");
                exit(1);
            }
            addr = htonl(naddr.s_addr);
            rc = amp_create_connection(client_comp_context, SKYFS_OSD,
                osd_info.osd[i].id, 
                addr,
                SKYFS_OSD_COM_PORT + osd_info.osd[i].ip[j]->lid,
                AMP_CONN_TYPE_TCP,
                AMP_CONN_DIRECTION_CONNECT,
                NULL, 
				__skyfs_C_alloc_pages,
                NULL);
            if(rc != 0) {
                SKYFS_ERROR("__skyfs_C_init_com:create connection error.\n");
                goto err_out;
            }
        }
    }
#endif
    INIT_LIST_HEAD(&client_request_queue);
    INIT_LIST_HEAD(&client_pending_list);
    pthread_mutex_init(&client_request_queue_lock, NULL);
    pthread_mutex_init(&client_pending_lock, NULL);
    sem_init(&client_request_queue_sem, 0, 0);

err_out:

    SKYFS_LEAVE("__skyfs_C_init_com:exit\n");

    return rc;
}

skyfs_s32_t __skyfs_C_alloc_pages(void *msg_in, 
                skyfs_u32_t *num, 
                amp_kiov_t **kiov)
{
    amp_kiov_t  *iovp = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t size = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_C_alloc_pages:enter\n");
    msgp = (skyfs_msg_t *)msg_in;

    if(msgp->type == SKYFS_MSG_O_READ_OBJ 
       || msgp->type == SKYFS_MSG_O_LISTXATTR){
        iovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        bzero(iovp, sizeof(amp_kiov_t));
        if(msgp->error > 0){
            size = msgp->error;
	    if(msgp->type == SKYFS_MSG_O_READ_OBJ){
		    // TODO : mayl  set foffset and flen 
		    iovp->ak_foffset =  msgp->u.readObjAck.foffset;
		    iovp->ak_flen =  msgp->u.readObjAck.flen;
	    }
        }else{
		if(msgp->type == SKYFS_MSG_O_LISTXATTR){
			size = 16;
		}
	}

        iovp->ak_addr = (skyfs_s8_t *)malloc(size);
        iovp->ak_len = size;
        iovp->ak_offset = 0;
        iovp->ak_flag = 0;
        *num = 1;
        *kiov = iovp;
    }

    SKYFS_LEAVE("__skyfs_C_alloc_pages:exit.type:%d, size:%d\n", 
        msgp->type, size);

    return rc;
}

void __skyfs_C_free_pages(skyfs_u32_t num, amp_kiov_t *kiov)
{
    skyfs_u32_t i;

    SKYFS_ENTER("__skyfs_C_free_pages:enter\n");

    for(i = 0; i < num; i ++){
        free(kiov[i].ak_addr);
    }

    SKYFS_LEAVE("__skyfs_C_free_pages:exit\n");
    return;
}

skyfs_s32_t __skyfs_C_get_var_conf(void)
{
    skyfs_s32_t rc = 0;
    
    FILE *fp;

    char str[SKYFS_MAX_NAME_LEN]; 
    char dir[SKYFS_MAX_NAME_LEN];
    char cmd[SKYFS_MAX_NAME_LEN];
    char type[SKYFS_MAX_NAME_LEN];

    SKYFS_ENTER("__skyfs_C_get_var_conf:enter\n");

    sprintf(dir, "%s%s", SKYFS_ARCCFG_FILE_PATH, SKYFS_VAR_CONFIG);
    fp = fopen(dir, "r");
    if(!fp){
        rc = -1;
        SKYFS_ERROR("__skyfs_C_get_var_conf:can't open %s file\n", dir);
        return rc;
    }

    is_async_write = 0;
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        bzero(cmd, SKYFS_MAX_NAME_LEN);
        bzero(type, SKYFS_MAX_NAME_LEN);
        sscanf(str, "%s %s", cmd, type); 
        bzero(str, SKYFS_MAX_NAME_LEN);
        if(strcmp(cmd, "SKYFS_IB") == 0){
            skyfs_ib_flag = atoi(type);
            SKYFS_ERROR("__skyfs_C_get_var_conf:skyfs_ib_flag:%d\n", skyfs_ib_flag);
            continue;
        }

	if(strcmp(cmd, "SKYFS_REPLICA") == 0){
            skyfs_replica = atoi(type);
	    continue;
	}

	// mayl async write only for test performance
	if(strcmp(cmd, "SKYFS_ASYNC_WRITE") == 0){
            is_async_write = atoi(type);
	    SKYFS_ERROR_1("skyfs client found is_async_write %d\n", is_async_write);
	    continue;
	}


		if(strcmp(cmd, "SKYFS_ADM_ADDR") == 0){
        	bzero(skyfs_adm_addr, SKYFS_MAX_NAME_LEN);
			strcpy(skyfs_adm_addr, type);
            SKYFS_ERROR("__skyfs_C_get_var_conf:adm_addr:%s\n", skyfs_adm_addr);
			continue;
		}

    }

    fclose(fp);

    SKYFS_LEAVE("__skyfs_C_get_var_conf:exit\n");
    return rc;
}

skyfs_s32_t __skyfs_C_queue_request(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t msg_type = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_C_queue_request:enter.\n");
    msgp = __skyfs_get_msg(req->req_msg);
    msg_type = msgp->type & (0xff);

    if(msg_type == SKYFS_MSG_INIT_CONFIG){
    	__skyfs_C_init_config(req);
#if 0
        SKYFS_MSG("__skyfs_C_queue_request:it's a configuration request.\n");
        pthread_mutex_lock(&client_request_queue_lock);
        list_add_tail(&req->req_list, &client_request_queue);
        pthread_mutex_unlock(&client_request_queue_lock);
        rc = amp_sem_up(&client_request_queue_sem);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_C_queue_request:config sem up error\n");
        }
#endif
    }else{
        SKYFS_ERROR("__skyfs_C_queue_request:request\n");
	}
    SKYFS_LEAVE("__skyfs_C_queue_request:leave.\n");
    return rc;
}
/*This is end of client_init.c*/
