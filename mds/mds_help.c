/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: mds_help.c $
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

pthread_mutex_t forward_request_lock;

skyfs_s32_t __skyfs_MS_set_bit(skyfs_u8_t *addr,
    skyfs_u32_t local, 
    skyfs_u32_t value)
{
    skyfs_u8_t *p;
    skyfs_u32_t off;
    skyfs_u32_t bits;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_set_bit:enter.addr:%p,local:%d,val:%d\n",addr,local,value);

    off = local / 8;
    bits = local % 8;

    p = addr + off;

    if(value == 0){
        value = 1 << bits;
        value = ~value;
        *p = *p & value;
    }
    else if(value == 1){
        value = 1 << bits;
        *p = *p | value;
    }else{
        SKYFS_ERROR("__skyfs_MS_set_bit:error value\n");
        rc = -1;
    }
    SKYFS_LEAVE("__skyfs_MS_set_bit:leave.rc:%d\n",rc);

    return rc;

}

skyfs_s32_t __skyfs_MS_is_set(skyfs_u8_t *addr, skyfs_u32_t size)
{
    skyfs_u8_t  *p = NULL;
    skyfs_u32_t i;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_is_set:addr:%p,size:%d.\n",addr,size);

    p = addr;

    for(i = 0; i < size; i++){
        if(*p != 0){
            rc = 1;
            break;
        }
        p ++;
    }

    SKYFS_LEAVE("__skyfs_MS_is_set:leave.rc=%d\n",rc);
    return rc;
}

skyfs_s32_t
__skyfs_MS_test_bit(skyfs_u8_t *addr, skyfs_u32_t local)
{
    skyfs_u8_t      *p;
    skyfs_u32_t     off;
    skyfs_u32_t     bits;
    skyfs_u8_t      value;
    skyfs_s32_t     rc = 0;

    SKYFS_ENTER("__skyfs_MS_test_bit:enter.addr:%p,local:%d\n", addr, local);

    off = local / 8;
    bits = local % 8;

    p = addr + off;
    
    value = 1 << bits;

    rc = *p & value;

    return rc;
}


skyfs_s32_t
__skyfs_MS_init_req(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t msg_type,
                skyfs_u32_t ack_flag,
                skyfs_u32_t req_type,
				skyfs_u32_t	msgsize)
{
    skyfs_s32_t rc = 0;
    amp_request_t *reqp = NULL;
	skyfs_msg_t	  *msg = NULL;

    rc = __amp_alloc_request(&reqp);

    if(rc != 0){
        SKYFS_ERROR("__skyfs_init_req:alloc request failed\n"); 
        goto err_out;
    }

    reqp->req_msg = (amp_message_t *)malloc(msgsize);
    if(!reqp->req_msg){
        SKYFS_ERROR("__skyfs_init_req:alloc req msg failed\n");
    	rc = -ENOMEM;
        goto err_out;
    }

	bzero(reqp->req_msg, msgsize);

    reqp->req_msglen = msgsize;
    reqp->req_need_ack = ack_flag;
    reqp->req_resent = 1;
    reqp->req_type = req_type;
    reqp->req_niov = 0;
    reqp->req_iov = NULL;

    msg = (skyfs_msg_t *)((skyfs_s8_t *)(reqp->req_msg) + AMP_MESSAGE_HEADER_LEN);
    msg->magic = SKYFS_MSG_MAGIC;
    msg->fs_id = 0;
    msg->type = msg_type;
    msg->error = 0;
    msg->fromid = mds_this_id;
    msg->fromType = SKYFS_MDS;
     msg->size = msgsize - AMP_SKYFS_MSGHEAD_SIZE;
	*msgp = msg;
    *req = reqp;

    return rc;

err_out:
        
    if(reqp->req_msg){
        free(reqp->req_msg);
    }

    if(reqp != NULL){
        __amp_free_request(reqp);
    }

    return rc;
}


skyfs_s32_t    
__skyfs_MS_init_reply(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t req_type,
                skyfs_u32_t req_niov,
                amp_kiov_t 	*req_iov,
				skyfs_u32_t size)
{
    amp_message_t     *replymsgp = NULL;
    skyfs_s32_t        rc = 0;

    replymsgp = (amp_message_t *)malloc(size);
    if(replymsgp == NULL){
        rc = -1;
        SKYFS_MSG("__skyfs_MS_init_reply:alloc reply failed\n");
        goto ERR;
    }

    bzero(replymsgp, size);
    memcpy(replymsgp, (*req)->req_msg, AMP_MESSAGE_HEADER_LEN);

    (*req)->req_reply = replymsgp;

    *msgp = __skyfs_get_msg((*req)->req_reply);
	(*msgp)->fromid = mds_this_id;

    (*req)->req_replylen = size;
    (*req)->req_need_ack = 0;
    (*req)->req_resent    = 1;
    (*req)->req_type = req_type;
    (*req)->req_niov = req_niov;
    (*req)->req_iov = req_iov;
     (*msgp)->size = size - AMP_SKYFS_MSGHEAD_SIZE;

ERR:
    return rc;
}

skyfs_s32_t
__skyfs_MS_forward_request(amp_request_t *req,
				skyfs_u32_t	com_type,
				skyfs_u32_t	id)
{
	skyfs_s32_t	rc = 0;
	skyfs_msg_t *msgp;


	msgp = __skyfs_get_msg(req->req_msg);
	SKYFS_ERROR("__skyfs_MS_forward_request:enter:type:%d,id:%d,msg_type:%d\n",
		com_type, id, msgp->type);

	SKYFS_MSG("__skyfs_MS_forward_request:sender_handle:%lld\n", 
		req->req_msg->amh_sender_handle);

	req->req_need_ack = SKYFS_NEEDNOT_ACK;

	rc = amp_send_sync(mds_comp_context,
			req,
			com_type,
			id,
			0);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_MS_forward_request:forward to %d %d failed\n",
			com_type, id);
	}

	SKYFS_LEAVE("__skyfs_MS_forward_request:exit:rc:%d\n", rc);

	return rc;
}

skyfs_u32_t __skyfs_MS_get_mdsid(void)
{
	skyfs_s8_t ip[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t str[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t hostname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t tmp_hostname[SKYFS_MAX_NAME_LEN];
	FILE *fp_hosts = NULL;
	skyfs_u32_t mds_id = 0;
	skyfs_u32_t i;

	SKYFS_ENTER("__skyfs_MS_get_mdsid:enter\n");

	/*1. get hostname*/
	__skyfs_get_hostname(hostname, str, skyfs_ib_flag);

	/*2. get host ip*/
	fp_hosts = fopen("/etc/hosts", "r");
	while(fgets(str, SKYFS_MAX_NAME_LEN, fp_hosts)){
		bzero(ip, SKYFS_MAX_NAME_LEN);
		bzero(tmp_hostname, SKYFS_MAX_NAME_LEN);
		sscanf(str, "%s %s", ip, tmp_hostname);
		if(strcmp(tmp_hostname, hostname) == 0){
			SKYFS_ERROR("__skyfs_MS_get_mdsid:get hostname:%s,ip:%s\n", hostname, ip);
			break;
		}
		bzero(str, SKYFS_MAX_NAME_LEN);
	}

	if(strlen(ip) == 0){
		SKYFS_ERROR("__skyfs_MS_get_mdsid:can't find %s in /etc/hosts\n", hostname);
		goto err_out;
	}

	/*3. get host id*/
	for(i = 0; i < SKYFS_MAX_MDS_NUM; i ++){
		if(mds_info.mds[i].id > 0){
			if(strncmp(mds_info.mds[i].ip[0]->addr, ip, strlen(ip)) == 0){
				mds_id = mds_info.mds[i].id;
				SKYFS_ERROR("__skyfs_MS_get_osdid:mdsid:%s,%d\n", hostname, mds_id);
				break;
			}
		}
	}

err_out:

	SKYFS_LEAVE("__skyfs_MS_get_mdsid:exit.mds_id:%d\n", mds_id);

	return mds_id;
}
/*This is end of mds_help.c*/
