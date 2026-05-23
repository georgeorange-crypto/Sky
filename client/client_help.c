/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_help.c $
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


#include "client_init.h"
#include "client_help.h"

skyfs_s32_t __skyfs_C_get_clientid(void)
{
	char ip[SKYFS_MAX_NAME_LEN];
	char str[SKYFS_MAX_NAME_LEN];
	char hostname[SKYFS_MAX_NAME_LEN];
	char tmp_hostname[SKYFS_MAX_NAME_LEN];
	FILE *fp_hosts = NULL;
	int  client_id = 0;
	int  i;

	SKYFS_ENTER("__skyfs_C_get_clientid:enter\n");

	/*1. get hostname*/
	__skyfs_get_hostname(hostname, str, skyfs_ib_flag);

	/*2. get host ip*/
	fp_hosts = fopen("/etc/hosts", "r");
	while(fgets(str, SKYFS_MAX_NAME_LEN, fp_hosts)){
		bzero(ip, SKYFS_MAX_NAME_LEN);
		bzero(tmp_hostname, SKYFS_MAX_NAME_LEN);
		sscanf(str, "%s %s", ip, tmp_hostname);
		if(strcmp(tmp_hostname, hostname) == 0){
			SKYFS_ERROR("__skyfs_C_get_clientid:get hostname:%s,ip:%s\n", hostname, ip);
			break;
		}
		bzero(str, SKYFS_MAX_NAME_LEN);
	}

	if(strlen(ip) == 0){
		SKYFS_ERROR("__skyfs_C_get_clientid:can't find %s in /etc/hosts\n", hostname);
		goto err_out;
	}

	/*3. get host id*/
	for(i = 0; i < SKYFS_MAX_CLIENT_NUM; i ++){
		if(client_info.client[i].id > 0){
			if(strncmp(client_info.client[i].ip[0]->addr, ip, strlen(ip)) == 0){
				client_id = client_info.client[i].id;
				SKYFS_ERROR("__skyfs_C_get_clientid:clientid:%s,%d\n", 
					hostname, client_id);
				break;
			}
		}
	}

err_out:

	SKYFS_ERROR("__skyfs_C_get_clientid:exit.client_id:%d\n", client_id);

	return client_id;
}

skyfs_s32_t
__skyfs_C_test_bit(skyfs_u8_t *addr, skyfs_u32_t local)
{
    skyfs_u8_t      *p;
    skyfs_u32_t     off;
    skyfs_u32_t     bits;
    skyfs_u8_t      value;
    skyfs_s32_t     rc = 0;

    SKYFS_ENTER("__skyfs_C_test_bit:enter.addr:%p,local:%d\n", addr, local);

    off = local / 8;
    bits = local % 8;

    p = addr + off;
    
    value = 1 << bits;

    rc = *p & value;

    return rc;
}


skyfs_s32_t    
__skyfs_C_init_reply(amp_request_t **req, 
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
        SKYFS_MSG("__skyfs_C_init_reply:alloc reply failed\n");
        goto ERR;
    }

    bzero(replymsgp, size);
    memcpy(replymsgp, (*req)->req_msg, AMP_MESSAGE_HEADER_LEN);

    (*req)->req_reply = replymsgp;

    *msgp = __skyfs_get_msg((*req)->req_reply);

    (*req)->req_replylen = size;
    (*req)->req_need_ack = 0;
    (*req)->req_resent    = 1;
    (*req)->req_type = req_type;
    (*req)->req_niov = req_niov;
    (*req)->req_iov = req_iov;
    // added by mayl for rdma
    (*msgp)->size = size - AMP_SKYFS_MSGHEAD_SIZE;

ERR:
    return rc;
}

/*This is end of clietn_help.c*/
