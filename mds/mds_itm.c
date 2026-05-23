/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_itm.c $
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
#include "mds_itm.h"
#include "mds_state.h"
#include "mds_init.h"
#include "mds_help.h"
#include "mds_layout.h"

skyfs_s32_t
__skyfs_M2M_init_dir_cache(skyfs_u32_t mds_id, 
				skyfs_u32_t dir_id, 
				skyfs_M_cmeta_t *dir_cmeta)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_init_dir_cache:enter,mds_id:%d,dir_id:%d\n", 
		mds_id, dir_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_init_dir_cache:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_initdirc_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_init_dir_cache:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_INIT_DIRC, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.initdircReq.dir_id = dir_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_init_dir_cache:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_init_dir_cache:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_M2M_init_dir_cache:msgp:%p\n", msgp);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_init_dir_cache:exit\n");

	return rc;
}

skyfs_s32_t
__skyfs_M2M_get_dir_cache(skyfs_u32_t mds_id, 
				skyfs_u32_t dir_id,
				skyfs_M_dir_cache_t *dir_cache)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_M2M_get_dir_cache:enter,mds_id:%d,dir_id:%d,dir_cache:%p\n", 
		mds_id, dir_id, dir_cache);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_get_dir_cache:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getdirc_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_get_dir_cache:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_GET_DIRC, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.getdircReq.dir_id = dir_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_get_dir_cache:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_get_dir_cache:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		memcpy(dir_cache, msgp->u.mtext, sizeof(skyfs_M_dir_cache_t));
	}

	SKYFS_MSG("__skyfs_M2M_get_dir_cache:msgp:%p\n", msgp);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_get_dir_cache:exit\n");

	return rc;
}

skyfs_s32_t
__skyfs_M2M_update_dir_cache(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_s32_t update,
				skyfs_M_cmeta_t *dir_cmeta)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_update_dir_cache:enter,mds_id:%d,dir_id:%d,subset_id:%d\n", 
		mds_id, dir_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_update_dir_cache:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_updatedirc_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_update_dir_cache:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_UPDATE_DIRC, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.updatedircReq.dir_id = dir_id;
	msgp->u.updatedircReq.subset_id = subset_id;
	msgp->u.updatedircReq.update = update;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_update_dir_cache:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_update_dir_cache:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		memcpy(dir_cmeta, msgp->u.mtext, sizeof(skyfs_M_cmeta_t));
	}

	SKYFS_MSG("__skyfs_M2M_update_dir_cache:msgp:%p\n", msgp);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_update_dir_cache:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t
__skyfs_M2M_update_dir_depth(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_update_dir_depth:enter,mds_id:%d,dir_id:%d,subset_id:%d\n", 
		mds_id, dir_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_update_dir_depth:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_updatedird_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_update_dir_depth:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_UPDATE_DIRD, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.updatedirdReq.dir_id = dir_id;
	msgp->u.updatedirdReq.subset_id = subset_id;
	msgp->u.updatedirdReq.split_depth = split_depth;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_update_dir_depth:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_update_dir_depth:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_M2M_update_dir_depth:msgp:%p\n", msgp);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_update_dir_depth:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t 
__skyfs_M2M_create_subset_index(skyfs_u32_t mds_id,
				skyfs_u32_t dir_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t subset_depth,
				skyfs_u32_t nlink)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_create_sub_index:enter,mds_id:%d,dir_id:%d,subset_id:%d\n", 
		mds_id, dir_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_create_sub_index:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_createsubi_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_create_sub_index:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_CREATE_SUBI, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.createsubiReq.dir_id = dir_id;
	msgp->u.createsubiReq.subset_id = subset_id;
	msgp->u.createsubiReq.subset_depth = subset_depth;
	msgp->u.createsubiReq.nlink = nlink;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_create_sub_index:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_create_sub_index:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_M2M_create_sub_index:msgp:%p\n", msgp);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_create_sub_index:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t __skyfs_M2M_trigger_balance(skyfs_state_info_t state_info)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t mds_id = 0;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_trigger_balance:enter\n");

	mds_id = SKYFS_MASTER_MDS_ID;
	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_trigger_balance:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_triggerbla_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_trigger_balance:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_TRIGGER_BLA, 
		mds_this_id, SKYFS_MDS,size);
	memcpy(&(msgp->u.triggerblaReq.state_info), &state_info, sizeof(skyfs_state_info_t));

	SKYFS_FILL_REQ(req, SKYFS_NEEDNOT_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_trigger_balance:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_trigger_balance:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_trigger_balance:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t 
__skyfs_M2M_add_htbcache(skyfs_u32_t mds_id, 
				skyfs_u32_t index, 
				skyfs_u32_t last_flag)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_add_htbcache:enter\n");
	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_add_htbcache:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_addhtb_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_add_htbcache:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_ADD_HTB, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.addhtbReq.index = index;
	msgp->u.addhtbReq.last_flag = last_flag;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_add_htbcache:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_add_htbcache:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	if(req->req_reply){
		free(req->req_reply);
	}

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_add_htbcache:exit.rc:%d\n", rc);
	return rc;
}

skyfs_s32_t
__skyfs_M2M_collect_state()
{
    skyfs_u32_t   req_num = mds_info.mds_num; 
    amp_request_t *req[req_num];
    skyfs_msg_t   *msgp[req_num];
    skyfs_msg_t   *msg = NULL;
    skyfs_u32_t   msgsize;

    skyfs_u32_t   i = 0;
    skyfs_u32_t   j = 0;
	skyfs_s32_t   rc = 0;

	SKYFS_MSG("__skyfs_M2M_collect_state:enter.\n");
    memset(req, 0, req_num * sizeof(amp_request_t *));
    memset(msgp, 0, req_num * sizeof(amp_message_t *));

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getstate_args_t);
    for(j = 0; j < SKYFS_MAX_MDS_NUM; j ++){
    	if(mds_info.mds[j].id > 0){
    		rc = __skyfs_MS_init_req(&req[i], 
				&msgp[i], 
            	SKYFS_MSG_M_GET_STATE, 
            	SKYFS_NEED_ACK, 
            	AMP_REQUEST|AMP_MSG,
            	msgsize);    

			msgp[i]->ver = mds_layout_version;

        	rc = amp_send_async(mds_comp_context, 
            	req[i],
            	SKYFS_MDS,
            	mds_info.mds[j].id,
            	0);
        	if(rc < 0) {
            	SKYFS_ERROR("__skyfs_M2M_collect_state:send request failed.rc:%d \n",rc);
            	goto EXIT;
        	}
        	SKYFS_ERROR("__skyfs_M2M_collect_state:send to mds %d succeed.\n", i);
			i ++;
		}
    }

    SKYFS_MSG("__skyfs_M2M_collect_state:begain to down\n");
    for(i = 0; i < req_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
    SKYFS_MSG("__skyfs_M2M_collect_state:after down\n");

    /*Step 3: alloc superblock and fill it */

    for(i = 0; i < req_num; i++){
        msg = __skyfs_get_msg(req[i]->req_reply);
		memcpy(&(mds_status[req[i]->req_remote_id].state_info), 
			&(msg->u.getstateAck.state_info), 
			sizeof(skyfs_state_info_t));
    }

EXIT:
    /*Step 4: free unused objs */
    //if(req){
        for (i = 0; i< req_num; i++) {
            if (req[i]->req_msg){
                //SKYFS_MSG("__skyfs_M2M_collect_state:before free reply,replylen:%d\n", 
                //	req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                //SKYFS_MSG("__skyfs_M2M_collect_state:before free reply,replylen:%d\n", 
                //   req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
               // SKYFS_MSG("__skyfs_M2M_collect_state:before free req\n");
                __amp_free_request(req[i]);
            }
        }
    //}

    SKYFS_LEAVE("__skyfs_M2M_collect_state:exit.\n");
 
	return rc;
}

skyfs_s32_t
__skyfs_M2M_start_balance(skyfs_u32_t mds_id, skyfs_u32_t kind_mds_id, 
				skyfs_u32_t first_index, skyfs_u32_t balance_num)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2M_start_balance:enter,mds_id:%d,kind_mdsid:%d\n",
		mds_id, kind_mds_id);
	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2M_start_balance:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_balanceload_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_start_balance:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_BALANCE_LOAD, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.balanceloadReq.kind_mds_id = kind_mds_id;
	msgp->u.balanceloadReq.balance_num = balance_num;
	msgp->u.balanceloadReq.first_index= first_index;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_start_balance:balance_num:%d, first_index:%d\n", 
		balance_num, first_index);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_start_balance:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	if(req->req_reply){
		free(req->req_reply);
	}

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_start_balance:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t __skyfs_M2M_get_layout(skyfs_u32_t mds_id)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__sufns_M2M_get_layout:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE;
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2M_get_layout:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_GET_LAYOUT, 
		mds_this_id, SKYFS_MDS,size);

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_M2M_get_layout:before send:req %p\n", req);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_MDS, mds_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2M_get_layout:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		memcpy(mds_layout, msgp->u.getlayoutAck.layout, 
			sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);
		mds_layout_version = msgp->u.getlayoutAck.layout_version;
	}

	SKYFS_ERROR("__skyfs_M2M_get_layout:mds_version:%d\n", mds_layout_version);

	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2M_get_layout:exit\n");

	return rc;
}
/*This is end of mds_itm.c*/
