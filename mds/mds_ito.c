/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_ito.c $
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
#include "mds_ito.h"

skyfs_s32_t
__skyfs_M2O_read_bmeta(skyfs_u32_t osd_id, skyfs_meta_vector_t *vector)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t kiov;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2O_read_bmeta:enter,size:%d\n", vector->size);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_read_bmeta:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readbmeta_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_read_bmeta:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds read bmeta, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_READ_BMETA, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.readbmetaReq.dir_id = vector->dir_id;
	msgp->u.readbmetaReq.subset_id = vector->subset_id;
	msgp->u.readbmetaReq.bmeta_id = vector->bmeta_id;
	msgp->u.readbmetaReq.size = vector->size;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	kiov.ak_addr = vector->bmeta;
	kiov.ak_len = sizeof(skyfs_M_bmeta_t);
	kiov.ak_offset = 0;
	kiov.ak_flag = 0;

	req->req_iov = &kiov;
	req->req_niov = 1;

	SKYFS_ERROR("__skyfs_M2O_read_bmeta:dir_id:%d,subset_id:%d,bmeta_id:%d,size:%d.\n",
		vector->dir_id, vector->subset_id, vector->bmeta_id, size);

	SKYFS_MSG("__skyfs_M2O_read_bmeta:before send:req %p, iov:%p\n", req, &kiov);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_read_bmeta:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	SKYFS_MSG("__skyfs_M2O_read_bmeta:receive reply:req %p\n", req);
	SKYFS_MSG("__skyfs_M2O_read_bmeta:receive reply:reply %p\n", req->req_reply);
	SKYFS_MSG("__skyfs_M2O_read_bmeta:receive reply:iov %p\n", req->req_iov);

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_M2O_read_bmeta:msgp:%p\n", msgp);

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

	SKYFS_LEAVE("__skyfs_M2O_read_bmeta:exit\n");

	return rc;
}

amp_request_t *
__skyfs_M2O_write_bmeta(skyfs_u32_t osd_id, skyfs_meta_vector_t *vector)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t *kiovp;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2O_write_bmeta:enter,osd_id:%d\n", osd_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_write_bmeta:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writebmeta_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_write_bmeta:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds write bmeta, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_BMETA, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.writebmetaReq.dir_id = vector->dir_id;
	msgp->u.writebmetaReq.subset_id = vector->subset_id;
	msgp->u.writebmetaReq.bmeta_id = vector->bmeta_id;
	msgp->u.writebmetaReq.size = vector->size;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, size);

	SKYFS_ERROR("__skyfs_M2O_write_bmeta:dir_id:%d,subset_id:%d,bmeta_id:%d.\n",
		vector->dir_id, vector->subset_id, vector->bmeta_id);
	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
	kiovp->ak_addr = vector->bmeta;
	kiovp->ak_len = sizeof(skyfs_M_bmeta_t);
	kiovp->ak_offset = 0;
	kiovp->ak_flag = 0;
	req->req_iov = kiovp;
	req->req_niov = 1;

	SKYFS_MSG("__skyfs_M2O_write_bmeta:len:%d\n", kiovp->ak_len);
	rc = amp_send_async(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_write_bmeta:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	SKYFS_LEAVE("__skyfs_M2O_write_bmeta:exit,req:%p\n",req);
	return req;

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2O_write_bmeta:exit,return NULL\n");

	return NULL;
}

skyfs_s32_t
__skyfs_M2O_read_subset(skyfs_u32_t osd_id, skyfs_M_subset_head_t *subset_head)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_M2O_read_subset:osd_id:%d,dir_id:%d,subid:%d\n",
		osd_id, subset_head->dir_id, subset_head->subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_read_subset:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readsubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_read_subset:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds read subset, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_READ_SUBSET, 
		mds_this_id, SKYFS_MDS,size);
	msgp->u.readsubsetReq.dir_id = subset_head->dir_id;
	msgp->u.readsubsetReq.subset_id = subset_head->subset_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_read_subset:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

    msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		subset_head->split_depth = msgp->u.readsubsetAck.split_depth;
		subset_head->subset_depth = msgp->u.readsubsetAck.subset_depth;
		subset_head->nlink= msgp->u.readsubsetAck.nlink;
	}else{
		SKYFS_LEAVE("__skyfs_M2O_read_subset:error:%d,dir_id:%d,subset_id:%d\n",
			rc, subset_head->dir_id, subset_head->subset_id);
	}

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

	SKYFS_LEAVE("__skyfs_M2O_read_subset:exit.sp_depth:%d,sb_depth:%d\n",
			subset_head->split_depth, subset_head->subset_depth);

	return rc;
}

skyfs_s32_t
__skyfs_M2O_write_subset(skyfs_u32_t osd_id, skyfs_M_subset_head_t subset_head)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_M2O_write_subset:osd_id:%d,dir_id:%d,subid:%d\n",
		osd_id, subset_head.dir_id, subset_head.subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_write_subset:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writesubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_write_subset:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds write subset, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_SUBSET, 
		mds_this_id, SKYFS_MDS,size);

	msgp->u.writesubsetReq.dir_id = subset_head.dir_id;
	msgp->u.writesubsetReq.subset_id = subset_head.subset_id;
	msgp->u.writesubsetReq.split_depth = subset_head.split_depth;
	msgp->u.writesubsetReq.subset_depth = subset_head.subset_depth;
	msgp->u.writesubsetReq.nlink = subset_head.nlink;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_write_subset:send request failed.rc:%d\n", rc);
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

	SKYFS_LEAVE("__skyfs_M2O_write_subset:exit.sp_depth:%d,sb_depth:%d\n",
			subset_head.split_depth, subset_head.subset_depth);

	return rc;
}
amp_request_t *
__skyfs_M2O_split_subset_file(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth,
				skyfs_u32_t subset_depth)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_M2O_split_subset_file:enter.dir_id:%u,subset_id:%u.\n",
		dir_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_split_subset_file:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_splitsubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_split_subset_file:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds split subset, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_SPLIT_SUBSET, 
					mds_this_id, SKYFS_MDS,size);
	
	msgp->u.splitsubsetReq.dir_id = dir_id;
	msgp->u.splitsubsetReq.subset_id = subset_id;
	msgp->u.splitsubsetReq.split_depth = split_depth;
	msgp->u.splitsubsetReq.subset_depth = subset_depth;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_async(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_split_subset_file:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	return req;

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2O_split_subset_file:exit\n");

	return NULL;
}

amp_request_t *
__skyfs_M2O_enlarge_subset_file(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_M2O_enlarge_subset_file:enter.dir_id:%d,subset_id:%d\n",
		dir_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_enlarge_subset_file:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_enlargesubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_enlarge_subset_file:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds enlarge subset, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_ENLARGE_SUBSET, 
					mds_this_id, SKYFS_MDS,size);
	
	msgp->u.enlargesubsetReq.dir_id = dir_id;
	msgp->u.enlargesubsetReq.subset_id = subset_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_async(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_enlarge_subset_file:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	return req;

err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_M2O_enlarge_subset_file:exit\n");

	return NULL;
}

skyfs_s32_t
__skyfs_M2O_create_subset_file(skyfs_u32_t osd_id,
				skyfs_u32_t dir_id)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_M2O_create_subset_file:enter.osd_id:%d,dir_id:%d\n",
		osd_id, dir_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_M2O_create_subset_file:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_createsubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_M2O_create_subset_file:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_ERROR("mds create subset, head size %d, total_size %d\n", AMP_SKYFS_MSGHEAD_SIZE, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_CREATE_SUBSET, 
					mds_this_id, SKYFS_MDS,size);

	SKYFS_ERROR("mds create subset, msgp %p, msg type %d, req %p \n", msgp,  msgp->type, req->req_msg);
	
	msgp->u.createsubsetReq.dir_id = dir_id;
	msgp->u.createsubsetReq.subset_id = 0;
	msgp->u.createsubsetReq.split_depth = 0;
	msgp->u.createsubsetReq.subset_depth = 0;
	msgp->u.createsubsetReq.nlink = 0;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_M2O_create_subset_file:send request failed.rc:%d\n", rc);
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

	SKYFS_LEAVE("__skyfs_M2O_create_subset_file:exit\n");
	return rc;
}

skyfs_s32_t
__skyfs_M2O_free_storage(skyfs_ino_t ino, skyfs_u64_t file_size)
{
	skyfs_u64_t obj_num;
	skyfs_u64_t obj_id;

	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;

	skyfs_s32_t rc = 0;
	skyfs_s32_t size = 0;

	obj_num = (file_size + SKYFS_OBJECT_SIZE - 1) / SKYFS_OBJECT_SIZE ;

	SKYFS_ERROR("__skyfs_M2O_free_storage:enter:remove:%llu\n", ino);

	
    for(obj_id = 0; obj_id < obj_num; obj_id ++){
		rc = -ENOMEM;
		rc = __amp_alloc_request(&req);
		if(rc != 0){
			SKYFS_ERROR("__skyfs_M2O_free_storage:alloc request failed\n");
			goto err_none;
		}

		size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_removeobj_t);
		req->req_msg = (amp_message_t *)malloc(size);
		if(!req->req_msg){
			SKYFS_ERROR("__skyfs_M2O_free_storage:alloc req_msg failed\n");
			goto err_req;
		}

		bzero(req->req_msg, size);

		SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_REMOVE_OBJ, 
					mds_this_id, SKYFS_MDS,size);

		msgp->u.removeObjReq.ino = ino;

		msgp->u.removeObjReq.obj_id = obj_id;

		msgp->u.removeObjReq.obj_size = SKYFS_OBJECT_SIZE;

	    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	    rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, 1, 1);
	    if(rc < 0){
		    SKYFS_ERROR("__skyfs_M2O_free_storage:send req err.rc:%d\n", rc);
			goto err_msg;
		}

	   	msgp = __skyfs_get_msg(req->req_reply);
	    rc = msgp->error;
		if(rc < 0){
		    SKYFS_ERROR("__skyfs_M2O_free_storage:req err.ino:%llu,rc:%d,obj_num:%llu\n", 
				ino, rc, obj_num);
		}

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

	}

err_none:

	SKYFS_LEAVE("__skyfs_M2O_free_storage:exit:%d\n", rc);

	return rc;
}

skyfs_s32_t
__skyfs_M2O_truncate_storage(skyfs_ino_t ino, skyfs_u64_t file_size, skyfs_u64_t new_file_size)
{
	skyfs_u64_t obj_num;
	skyfs_u64_t truncate_start_obj_num;
	skyfs_u64_t obj_id;

	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;

	skyfs_s32_t rc = 0;
	skyfs_s32_t size = 0;

	obj_num = (file_size + SKYFS_OBJECT_SIZE - 1) / SKYFS_OBJECT_SIZE ;
	truncate_start_obj_num = (new_file_size + SKYFS_OBJECT_SIZE - 1) / SKYFS_OBJECT_SIZE ;

	SKYFS_ERROR("__skyfs_M2O_truncate_storage:enter:remove:%llu\n", ino);
	if(new_file_size >= file_size){
	SKYFS_ERROR("__skyfs_M2O_truncate_storage:enter:extend size just return :%llu\n", ino);
		return rc ;
	}
	

	
    for(obj_id = 0; obj_id < obj_num; obj_id ++){

		rc = -ENOMEM;
		// skip first n obj_id
		if((obj_id+1) < truncate_start_obj_num &&( truncate_start_obj_num > 0))
			continue;
		rc = __amp_alloc_request(&req);
		if(rc != 0){
			SKYFS_ERROR("__skyfs_M2O_free_storage:alloc request failed\n");
			goto err_none;
		}

		size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_truncate_t);
		req->req_msg = (amp_message_t *)malloc(size);
		if(!req->req_msg){
			SKYFS_ERROR("__skyfs_M2O_free_storage:alloc req_msg failed\n");
			goto err_req;
		}

		bzero(req->req_msg, size);

		SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_TRUNCATE, 
					mds_this_id, SKYFS_MDS,size);

		msgp->u.truncateReq.ino = ino;

		msgp->u.truncateReq.obj_id = obj_id;

		if(obj_id == 0 || ((obj_id+1) ==  truncate_start_obj_num)){
			msgp->u.truncateReq.size = new_file_size % SKYFS_OBJECT_SIZE;
			if(new_file_size == (obj_id+1)* SKYFS_OBJECT_SIZE){
				msgp->u.truncateReq.size = SKYFS_OBJECT_SIZE;
			}
		}else{
			msgp->u.truncateReq.size = 0;

		}

	    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	    rc = amp_send_sync(mds_comp_context, req, SKYFS_OSD, 1, 1);
	    if(rc < 0){
		    SKYFS_ERROR("__skyfs_M2O_free_storage:send req err.rc:%d\n", rc);
			goto err_msg;
		}

	    SKYFS_ERROR("mds truncate ino %llu, obj_id %llu, new_size %lu\n", ino, obj_id,  msgp->u.truncateReq.size);

	   	msgp = __skyfs_get_msg(req->req_reply);
	    rc = msgp->error;
		if(rc < 0){
		    SKYFS_ERROR("__skyfs_M2O_free_storage:req err.ino:%llu,rc:%d,obj_num:%llu\n", 
				ino, rc, obj_num);
		}

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

	}

err_none:

	SKYFS_LEAVE("__skyfs_M2O_free_storage:exit:%d\n", rc);

	return rc;
}


/*This is end of mds_ito.c*/
