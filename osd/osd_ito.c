/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_ito.c $
 */

#include <dirent.h>
#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"
#include "skyfs_fs.h"

#include "amp.h"

#include "skyfs_msg.h"
#include "skyfs_debug.h"
#include "skyfs_hash.h"


#include "osd_fs.h"
#include "osd_op.h"
#include "osd_thread.h"
#include "osd_init.h"
#include "osd_thread.h"
#include "osd_profile.h"
#include "osd_layout.h"
#include "osd_help.h"
#include "osd_loadb.h"

#include "mds_fs.h"

#include "osd_ito.h"

static int recover_data_req_cnt = 0;
//extern void flush_directory(const char *dir_path, char * part_buf, char * old_buf, size_t total_buf_len, int buf_pos, size_t file_pos, int cur_fd);
extern void flush_directory(const char *dir_path, char * part_buf, size_t total_buf_len,int * buf_pos, skyfs_o_replica_recover_t * request_head, int dest_osd_id);
//extern void flush_directory(const char *dir_path);
extern skyfs_u32_t  skyfs_data_stripe_cnt;
extern skyfs_u32_t   skyfs_recover_data_size ; // added by mayl
amp_request_t *
__skyfs_O2O_write_bmeta(skyfs_u32_t osd_id, 
				skyfs_u32_t dir_id, 
				skyfs_u32_t subset_id, 
				skyfs_u32_t bmeta_id,
				skyfs_M_bmeta_t *bmeta)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t *kiovp = NULL;
	skyfs_M_bmeta_t *tmp_bmeta = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_write_bmeta:enter,osd_id:%d\n", osd_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_write_bmeta:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writebmeta_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_write_bmeta:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_BMETA, 
		osd_this_id, SKYFS_OSD,size);
	msgp->u.writebmetaReq.dir_id = dir_id;
	msgp->u.writebmetaReq.subset_id = subset_id;
	msgp->u.writebmetaReq.bmeta_id = bmeta_id;
	msgp->u.writebmetaReq.size = sizeof(skyfs_M_bmeta_t);

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, size);

	SKYFS_ERROR("__skyfs_O2O_write_bmeta:dir_id:%d,subset_id:%d,bmeta_id:%d.\n",
		dir_id, subset_id, bmeta_id);

	tmp_bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
	memcpy(tmp_bmeta, bmeta, sizeof(skyfs_M_bmeta_t));

	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
	kiovp->ak_addr = tmp_bmeta;
	kiovp->ak_len = sizeof(skyfs_M_bmeta_t);
	kiovp->ak_offset = 0;
	kiovp->ak_flag = 0;
	req->req_iov = kiovp;
	req->req_niov = 1;

	SKYFS_MSG("__skyfs_O2O_write_bmeta:len:%d\n", kiovp->ak_len);
	rc = amp_send_async(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_write_bmeta:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	SKYFS_LEAVE("__skyfs_O2O_write_bmeta:exit,req:%p\n",req);
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

	SKYFS_LEAVE("__skyfs_O2O_write_bmeta:exit,return NULL\n");

	return NULL;
}


skyfs_s32_t
__skyfs_O2O_create_subset_file(skyfs_u32_t osd_id, 
				skyfs_M_subset_head_t *subset_head)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_create_subset_file:enter.osd_id:%d,dir_id:%d,subid:%d\n",
		osd_id, subset_head->dir_id, subset_head->subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_create_subset_file:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_createsubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_create_subset_file:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_CREATE_SUBSET, 
					osd_this_id, SKYFS_OSD, size);
	msgp->u.createsubsetReq.dir_id = subset_head->dir_id;
	msgp->u.createsubsetReq.subset_id = subset_head->subset_id;
	msgp->u.createsubsetReq.split_depth = subset_head->split_depth;
	msgp->u.createsubsetReq.subset_depth = subset_head->subset_depth;;
	msgp->u.createsubsetReq.nlink = subset_head->nlink;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_create_subset_file:send request failed.rc:%d\n", rc);
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

	SKYFS_LEAVE("__skyfs_O2O_create_subset_file:exit,rc:%d\n", rc);
	return rc;

}

skyfs_s32_t 
__skyfs_O2O_create_dl_subset_index(skyfs_u32_t osd_id,
				skyfs_u32_t pad_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t subset_depth,
				skyfs_u32_t nlink)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_create_dl_subset_index:enter:osd_id:%d,subset_id:%d\n",
		osd_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_create_dl_subi:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_createdlsubi_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_create_dl_subi:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_CREATE_DL_SUBI,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.createdlsubiReq.subset_id = subset_id;
	msgp->u.createdlsubiReq.subset_depth = subset_depth;
	msgp->u.createdlsubiReq.nlink = nlink;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_create_subi:send request failed.rc:%d\n", rc);
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

	SKYFS_LEAVE("__skyfs_O2O_create_subi:exit.rc:%d\n", rc);
	return rc;
}

skyfs_s32_t
__skyfs_O2O_get_dl_head(skyfs_u32_t osd_id,
				skyfs_u32_t pad_id,
				skyfs_DL_head_t *dl_head)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_get_dl_head:enter.osd_id:%d,dl_head:%p\n",
		osd_id, dl_head);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_get_dl_head:alloc_request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;

	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_getdlhead_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_get_dir_head:alloc req_msg failed\n");
		rc = -errno;
		goto err_req;
	}
	

	bzero(req->req_msg, size);
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_GET_DLHEAD,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.getdlheadReq.pad_id = pad_id;
	
	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_O2O_get_dl_head:before send:req %p\n", req);

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_get_dl_head:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		memcpy(dl_head, msgp->u.mtext, sizeof(skyfs_DL_head_t));
	}

	SKYFS_MSG("__skyfs_O2O_get_dl_head:msgp:%p\n", msgp);

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

	SKYFS_LEAVE("__skyfs_O2O_get_dl_head:exit\n");

	return rc;
}

skyfs_s32_t
__skyfs_O2O_create_dl_subset(skyfs_u32_t osd_id,
				skyfs_u32_t replica_id,
				skyfs_DL_subset_head_t *dl_subset_head)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_create_dl_subset:enter:osd_id:%d\n", osd_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_create_dl_subset:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_createdlsubset_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_create_dl_subset:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_CREATE_DLSUBSET, 
					osd_this_id, SKYFS_OSD, size);
	
	msgp->u.createdlsubsetReq.subset_id = dl_subset_head->subset_id;
	msgp->u.createdlsubsetReq.split_depth = dl_subset_head->split_depth;
	msgp->u.createdlsubsetReq.subset_depth = dl_subset_head->subset_depth;
	msgp->u.createdlsubsetReq.nlink = dl_subset_head->nlink;
	msgp->u.createdlsubsetReq.fir_osd = dl_subset_head->fir_osd;
	msgp->u.createdlsubsetReq.sec_osd = dl_subset_head->sec_osd;
	msgp->u.createdlsubsetReq.thi_osd = dl_subset_head->thi_osd;
	msgp->u.createdlsubsetReq.replica_id = replica_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_create_dl_subset:send request failed.rc:%d\n", rc);
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

	SKYFS_LEAVE("__skyfs_O2O_create_dl_subset:exit\n");


	return rc;
}	

amp_request_t *
__skyfs_O2O_write_dlchunk(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_DL_chunk_t *dl_chunk)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t *kiovp = NULL;
	skyfs_DL_chunk_t *tmp_dlchunk= NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_write_dlchunk:enter,osd_id:%d\n", osd_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_write_dlchunk:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writedlchunk_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_write_dlchunk:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_DLCHUNK,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.writedlchunkReq.subset_id = subset_id;
	msgp->u.writedlchunkReq.chunk_id = chunk_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, size);
	SKYFS_ERROR("__skyfs_O2O_write_dlchunk:subset_id:%d,chunk_id:%d.\n",
		subset_id, chunk_id);

	tmp_dlchunk= (skyfs_DL_chunk_t *)malloc(sizeof(skyfs_DL_chunk_t));
	memcpy(tmp_dlchunk, dl_chunk, sizeof(skyfs_DL_chunk_t));

	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
	kiovp->ak_addr = tmp_dlchunk;
	kiovp->ak_len = sizeof(skyfs_DL_chunk_t);
	kiovp->ak_offset = 0;
	kiovp->ak_flag = 0;
	req->req_iov = kiovp;
	req->req_niov = 1;

	SKYFS_MSG("__skyfs_O2O_write_dlchunk:len:%d\n", kiovp->ak_len);
	rc = amp_send_async(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_write_dlchunk:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	SKYFS_LEAVE("__skyfs_O2O_write_dlchunk:exit,req:%p\n",req);
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

	SKYFS_LEAVE("__skyfs_O2O_write_dlchunk:exit,return NULL\n");


	return NULL;
}

skyfs_s32_t
__skyfs_O2O_update_head_depth(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t split_depth)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_update_head_depth:enter:osd_id:%d,subset_id:%d\n",
		osd_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_update_head_depth:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_updatehdepth_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_update_head_depth:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_UPDATE_HDEPTH,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.updatehdepthReq.subset_id = subset_id;
	msgp->u.updatehdepthReq.split_depth = split_depth;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	SKYFS_MSG("__skyfs_O2O_update_head_depth:before send:req %p\n", req);

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_update_head_depth:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_O2O_update_head_depth:msgp:%p\n", msgp);

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

	SKYFS_LEAVE("__skyfs_O2O_update_head_depth:exit.rc:%d\n", rc);


	return rc;

}

skyfs_s32_t
__skyfs_O2O_move_obj(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_s8_t  *chunkfile)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t *kiovp = NULL;
	skyfs_s8_t *buf = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;

	SKYFS_ERROR_1("__skyfs_O2O_move_obj:enter:osd_id:%d,subset_id:%d,ino:%llu\n",
		osd_id, subset_id, ino);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_move_obj:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_copyobj_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_move_obj:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_COPY_OBJ,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.copyobjReq.subset_id = subset_id;
	msgp->u.copyobjReq.chunk_id = chunk_id;
	msgp->u.copyobjReq.ino = ino;
	msgp->u.copyobjReq.obj_id = obj_id;

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, size);

	SKYFS_MSG("__skyfs_O2O_move_obj:before send:req %p\n", req);

	fd = open(chunkfile, O_RDONLY);
	if(fd < 0){
		SKYFS_ERROR("__skyfs_O2O_move_obj:cann't open %s\n", chunkfile);
		goto err_msg;	
	}

	buf = malloc(SKYFS_OBJECT_SIZE);
	if(buf == NULL){
		SKYFS_ERROR("__skyfs_O2O_move_obj:alloc buf err:%d\n", errno);
		goto err_msg;	
	}

	rc = read(fd, buf, SKYFS_OBJECT_SIZE);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_move_obj:read %s err:%d\n", chunkfile, errno);
		goto err_msg;	
	}

	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
	kiovp->ak_addr = buf;
	kiovp->ak_len = SKYFS_OBJECT_SIZE;
	kiovp->ak_offset = 0;
	kiovp->ak_flag = 0;
	req->req_iov = kiovp;
	req->req_niov = 1;

	rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_move_obj:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;

	SKYFS_MSG("__skyfs_O2O_move_obj:msgp:%p\n", msgp);

err_msg:

	if(fd){
		close(fd);
	}

	if(buf){
		free(buf);
	}

	if(kiovp){
		free(kiovp);
	}
	if(req->req_msg){
		free(req->req_msg);
	}

	if(req->req_reply){
		free(req->req_reply);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_O2O_move_obj:exit.rc:%d\n", rc);


	return rc;

}

amp_request_t *
__skyfs_O2O_write_replica(skyfs_u32_t osd_id,
				skyfs_u32_t subset_id, 
				skyfs_u32_t chunk_id, 
				skyfs_io_vector_t *vec,
				skyfs_s8_t *buf)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	amp_kiov_t *kiovp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_O2O_write_replica:enter:osd_id:%d,subset_id:%d\n",
		osd_id, subset_id);

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_O2O_write_replica:alloc request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;
	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writerep_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_O2O_write_replica:alloc req_msg failed\n");
		goto err_req;
	}

	bzero(req->req_msg, size);

	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_REPLICA,
		osd_this_id, SKYFS_OSD, size);
	msgp->u.writeRepReq.subset= subset_id;
	msgp->u.writeRepReq.chunk= chunk_id;
	memcpy(&(msgp->u.writeRepReq.vec), vec, sizeof(skyfs_io_vector_t));

	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, size);

	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
	kiovp->ak_addr = buf;
	kiovp->ak_len = vec->count;
	kiovp->ak_offset = 0;
	kiovp->ak_flag= 0;
	req->req_iov = kiovp;
	req->req_niov = 1;

	SKYFS_MSG("__skyfs_O2O_write_replica:before send:req %p\n", req);

	rc = amp_send_async(osd_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_O2O_write_replica:send request failed.rc:%d\n", rc);
		goto err_req;
	}

	SKYFS_LEAVE("__skyfs_O2O_write_replica:exit,req:%p\n",req);

err_req:

err_none:

	SKYFS_LEAVE("__skyfs_O2O_write_replica:exit,return req\n");
	return req;
}

skyfs_s32_t
__skyfs_O2O_collect_state()
{
    //skyfs_u32_t   req_num = osd_info.osd_num; 
    skyfs_u32_t   req_num = 4; 
    skyfs_u32_t   real_req_num = 0; 
    amp_request_t *req[req_num];
    skyfs_msg_t   *msgp[req_num];
    skyfs_msg_t   *msg = NULL;
    skyfs_u32_t   size;

    skyfs_u32_t   i, j;
	skyfs_s32_t   rc = 0;
	i = 0;

	SKYFS_ERROR("__skyfs_O2O_collect_state:enter.\n");
    memset(req, 0, req_num * sizeof(amp_request_t *));
    memset(msgp, 0, req_num * sizeof(amp_message_t *));

	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getstate_args_t);
    for(j = (osd_this_id + 1); j < SKYFS_MAX_OSD_NUM; (j ++) % SKYFS_MAX_OSD_NUM){
		if(osd_info.osd[j].id > 0 && j != osd_this_id){
    		rc = __amp_alloc_request(&req[i]);
			if(rc < 0){
				SKYFS_ERROR("__skyfs_O2O_collect_state:alloc request failed\n");
				goto EXIT;
			}

			rc = -ENOMEM;
			req[i]->req_msg = (amp_message_t *)malloc(size);
			if(!req[i]->req_msg){
				SKYFS_ERROR("__skyfs_O2O_collect_state:alloc req_msg failed\n");
				goto EXIT;
			}

			bzero(req[i]->req_msg, size);

			SKYFS_INIT_MSG(msgp[i], req[i], SKYFS_FSID, SKYFS_MSG_STATE,
				osd_this_id, SKYFS_OSD, size);

			SKYFS_FILL_REQ(req[i], SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

       	 	rc = amp_send_async(osd_comp_context, 
            	req[i],
            	SKYFS_OSD,
            	osd_info.osd[j].id,
            	0);
        	if(rc < 0) {
            	SKYFS_ERROR("__skyfs_O2O_collect_state:send request failed.rc:%d.\n",rc);
            	goto EXIT;
        	}
        	SKYFS_MSG("__skyfs_O2O_collect_state:send to osd %d succeed.\n", 
				osd_info.osd[j].id);
			i ++;
			real_req_num = i;
			if(i >= req_num){
				break;
			}
		}
    }

    SKYFS_MSG("__skyfs_O2O_collect_state:begain to down\n");
    for(i = 0; i < real_req_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
    SKYFS_MSG("__skyfs_O2O_collect_state:after down\n");

    /*Step 3: alloc superblock and fill it */
    for(i = 0; i < real_req_num; i++){
        msg = __skyfs_get_msg(req[i]->req_reply);
		//memcpy(&(osd_status[(req[i]->req_remote_id)].state_info), 
		//	&(msg->u.getstateAck.state_info),
		//	sizeof(skyfs_state_info_t));
		if(msg->u.getstateAck.state_info.request_num == 0) {
			rc = i;
			break;
		}
    }

EXIT:
    /*Step 4: free unused objs */
        for (i = 0; i< real_req_num; i++) {
            if (req[i]->req_msg){
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                __amp_free_request(req[i]);
            }
        }

    SKYFS_ERROR("__skyfs_O2O_collect_state:exit.rc:%d\n",rc);
 
	return rc;
}

skyfs_s32_t
__skyfs_O2O_update_state()
{
    skyfs_u32_t   req_num = osd_info.osd_num; 
    amp_request_t *req[req_num];
    skyfs_msg_t   *msgp[req_num];
    skyfs_msg_t   *msg = NULL;
    skyfs_u32_t   size;

    skyfs_u32_t   i, j;
	skyfs_s32_t   rc = 0;

	i = 0;

	SKYFS_MSG("__skyfs_O2O_update_state:enter.\n");
    memset(req, 0, req_num * sizeof(amp_request_t *));
    memset(msgp, 0, req_num * sizeof(amp_message_t *));

	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_updatestate_args_t);
    for(j = 0; j < SKYFS_MAX_OSD_NUM; j ++){
		if(osd_info.osd[j].id > 0){
    	rc = __amp_alloc_request(&req[i]);
		if(rc != 0){
			SKYFS_ERROR("__skyfs_O2O_update_state:alloc request failed\n");
			goto EXIT;
		}

		rc = -ENOMEM;
		req[i]->req_msg = (amp_message_t *)malloc(size);
		if(!req[i]->req_msg){
			SKYFS_ERROR("__skyfs_O2O_update_state:alloc req_msg failed\n");
			goto EXIT;
		}

		bzero(req[i]->req_msg, size);

		SKYFS_INIT_MSG(msgp[i], req[i], SKYFS_FSID, SKYFS_MSG_O_UPDATE_STATE,
			osd_this_id, SKYFS_OSD, size);

		memcpy(msgp[i]->u.updatestateReq.osd_status,
			osd_status,
			SKYFS_MAX_OSD_NUM * sizeof(skyfs_osd_status_t));	
	
		SKYFS_FILL_REQ(req[i], SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

        rc = amp_send_async(osd_comp_context, 
            req[i],
            SKYFS_OSD,
            osd_info.osd[j].id,
            0);
        if(rc < 0) {
            SKYFS_ERROR("__skyfs_O2O_update_state:send failed.rc:%d.\n",rc);
            goto EXIT;
        }
        SKYFS_MSG("__skyfs_O2O_update_state:send to mds %d succeed.\n", 
			osd_info.osd[j].id);
			i ++;
		}
    }

    SKYFS_MSG("__skyfs_O2O_update_state:begain to down\n");
    for(i = 0; i < req_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
    SKYFS_MSG("__skyfs_O2O_update_state:after down\n");

    /*Step 3: check msg reply*/
    for(i = 0; i < req_num; i++){
        msg = __skyfs_get_msg(req[i]->req_reply);
		if(msg->error < 0){
			goto EXIT;
		}
    }

EXIT:
    /*Step 4: free unused objs */
        for (i = 0; i< req_num; i++) {
            if (req[i]->req_msg){
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                __amp_free_request(req[i]);
            }
        }

    SKYFS_LEAVE("__skyfs_O2O_update_state:exit.\n");
 
	return rc;
}

int __skyfs_send_replica_recover_msg(skyfs_o_replica_recover_t * request_head, char * part_buf, uint64_t xid, int dest_osd_id )
{
	int rc = 0;
		
	skyfs_u32_t  msgsize;
	amp_request_t *req;
	amp_kiov_t * kiovp = NULL;
	skyfs_msg_t   *msgp = NULL;

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
	rc = __skyfs_OSD_init_req(&req, 
        &msgp, 
        SKYFS_MSG_O_RECOVER_REPLICA,
        SKYFS_NEED_ACK, 
        AMP_REQUEST|AMP_DATA,
        msgsize);
	req->req_niov = 1;

	memcpy(&msgp->u.replicaRecoverReq, request_head, sizeof(skyfs_o_replica_recover_t));
	msgp->u.replicaRecoverReq.xid = xid;
	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
        req->req_iov = kiovp;
        kiovp->ak_addr = part_buf;
        kiovp->ak_len = request_head->total_data_size;
        kiovp->ak_offset = 0;
        kiovp->ak_flag = 0;
	if(kiovp->ak_len == 0){
		kiovp->ak_len = 1024;
		SKYFS_ERROR_1("%s : send a request with zero msg, flag: %d\n", __FUNCTION__, request_head->flag);
	}
	//if(kiovp->ak_len == 416){
		SKYFS_ERROR_1("check data buf[0] : %x, ak_length %d xid %llu\n", part_buf[32], kiovp->ak_len, xid);
	//}
        rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);
		
        if(req->req_msg){
                free(req->req_msg);
        }

        if(req){
                __amp_free_request(req);
        }
	free(kiovp);
	recover_data_req_cnt ++;

	return rc;


}

#if 0
skyfs_s32_t 
__skyfs_O2O_recover_partitions(int src_replica_id, 
		int dest_replica_id,int dest_osd_id)
{
	char replica_part_dir[256];
	skyfs_s32_t rc = 0;
	amp_kiov_t * kiovp = NULL;
	skyfs_msg_t   *msgp = NULL;
	skyfs_u32_t   msgsize;
	//int has_sent = 0; 
	skyfs_u32_t   osd_id = dest_osd_id;
	struct dirent *entry;
	int remain_buf_len = 0;

	struct stat buf;
	char * part_buf = NULL;
	int buffer_fill_size = 0;
	DIR * dir = NULL;
	struct timeval tv;
	amp_request_t *req;
	skyfs_o_replica_recover_t  replica_recover_head;
	

	SKYFS_MSG("%s:partition pathname:%p\n", __FUNCTION__, pname);
	gettimeofday(&tv, NULL);
	sprintf(replica_part_dir, "%s/rep-%u-partdir",
                SKYFS_OBJ_PATH, src_replica_id);
	if((rc = stat(replica_part_dir, &buf)) == -1){
		SKYFS_ERROR_1("%s , opendir failed , err %d\n", __FUNCTION__, errno);
		// send to dest osd even if opendir failed
	}
	part_buf = (char *)malloc(skyfs_recover_data_size);
	dir = opendir(replica_part_dir);
	if(dir == NULL || rc <0){
		/* send fail reply to dest_osd return   */
	        //replica_recover_head = (skyfs_o_replica_recover_t *)part_buf;
		//replica_recover_head->flag = 2; //1: last 0: narmal 2: error
		//replica_recover_head->data_type = 1;
		//replica_recover_head->replica_obj_cnt = 0;
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		msgp->u.replicaRecoverReq.xid = tv.tv_sec*1000 + tv.tv_usec/1000; // time in ms
		msgp->u.replicaRecoverReq.flag = 3; // error
		msgp->u.replicaRecoverReq.data_type = 1; // partition
		msgp->u.replicaRecoverReq.replica_obj_cnt = 0;

		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
		req->req_iov = kiovp;
		kiovp->ak_addr = part_buf;
		kiovp->ak_len = sizeof(skyfs_DL_part_t);
	        kiovp->ak_offset = 0;
	        kiovp->ak_flag = 0;
	        rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);

		goto ERR;

	}

	//part_buf = (char *)malloc(skyfs_recover_data_size);
	memset(part_buf, 0, skyfs_recover_data_size);
	remain_buf_len = skyfs_recover_data_size;
        char * tmp_part_buf = part_buf;	
	memset(&replica_recover_head, 0, sizeof(skyfs_o_replica_recover_t));
	replica_recover_head.xid = ((uint64_t)tv.tv_sec)*1000 + tv.tv_usec/1000; // time in ms
	replica_recover_head.src_replica_id = src_replica_id;
	replica_recover_head.dest_replica_id = dest_replica_id;
	while((entry = readdir(dir)) != NULL){
	    if(!strcmp(entry->d_name, "."))
                    continue;

            if(!strcmp(entry->d_name, ".."))
                    continue;

	  if (entry->d_type == DT_REG) { // 只刷新普通文件
            char file_path[256];
            snprintf(file_path, sizeof(file_path), "%s/%s", replica_part_dir, entry->d_name);
	    int fd = open(file_path, O_RDONLY);
	    if(fd <0){
		    SKYFS_ERROR_1("open relica(or part)file %s failed\n", file_path);
		    continue;
	    }
	    if(remain_buf_len > sizeof(skyfs_DL_part_t)){
		    //if(has_sent)
		    //	has_sent = 0;
		    read(fd, tmp_part_buf, sizeof(skyfs_DL_part_t));
		    tmp_part_buf += sizeof(skyfs_DL_part_t); 
		    remain_buf_len -= sizeof(skyfs_DL_part_t);
		    replica_recover_head.replica_obj_cnt ++;
		    replica_recover_head.total_data_size +=  sizeof(skyfs_DL_part_t);
		    replica_recover_head.data_type = 1; // partition
		    replica_recover_head.flag = 2; // normal

	    }else{
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		memcpy(&msgp->u.replicaRecoverReq, &replica_recover_head, sizeof(replica_recover_head));
		msgp->u.replicaRecoverReq.xid = tv.tv_sec*1000 + tv.tv_usec/1000; // time in ms
		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
                req->req_iov = kiovp;
                kiovp->ak_addr = part_buf;
                kiovp->ak_len = replica_recover_head.total_data_size;
                kiovp->ak_offset = 0;
                kiovp->ak_flag = 0;
                rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);
		usleep(1000);
		
        	if(req->req_msg){
                	free(req->req_msg);
        	}

        	if(req){
                	__amp_free_request(req);
        	}

		//has_sent = 1;
		tmp_part_buf = part_buf;
		remain_buf_len = skyfs_recover_data_size;
		replica_recover_head.replica_obj_cnt = 0;
                replica_recover_head.total_data_size = 0;
                replica_recover_head.data_type = 1; // partition
                replica_recover_head.flag = 1; // last send


	
	    }

	  } // end DT_REG



	}// end_while
send_last:{
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		memcpy(&msgp->u.replicaRecoverReq, &replica_recover_head, sizeof(replica_recover_head));
		msgp->u.replicaRecoverReq.xid = tv.tv_sec*1000 + tv.tv_usec/1000; // time in ms
		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
                req->req_iov = kiovp;
                kiovp->ak_addr = part_buf;
                kiovp->ak_len = replica_recover_head.total_data_size;
                kiovp->ak_offset = 0;
                kiovp->ak_flag = 0;
		if(kiovp->ak_len == 0){
			kiovp->ak_len = 4*1024; // keep a small paylaod
			msgp->u.replicaRecoverReq.data_type = 1; // last_data
		}

                rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);

	  }


	// check if there is last 

ERR:
	if(part_buf != NULL)
		free(part_buf);
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	return rc;

}
#endif

skyfs_s32_t 
__skyfs_O2O_recover_stripe_data_objs(int src_replica_id, int stripe_id,
		int dest_replica_id,int dest_osd_id, uint64_t xid,int type)
{
	char replica_data_dir[256];
	char replica_data_root_dir[256];
	skyfs_s32_t rc = 0;
	amp_kiov_t * kiovp = NULL;
	skyfs_msg_t   *msgp = NULL;
	skyfs_u32_t   msgsize;
	int fd = 0;
	int buf_pos = 0;
	//int has_sent = 0; 
	skyfs_u32_t   osd_id = dest_osd_id;
	struct dirent *entry, *sub_entry;
	int remain_buf_len = 0;
	int cur_replica_obj_cnt = 0;
	int total_replica_obj_cnt = 0;

	struct stat buf;
	char * part_buf = NULL;
	int buffer_fill_size = 0;
	DIR *dir = NULL, *subdir = NULL;
	struct timeval tv;
	amp_request_t *req;
	skyfs_o_replica_recover_t  replica_recover_head;
	size_t read_pos = 0;
	size_t data_offset = 0;
	skyfs_ino_t tmp_ino = 0;
	skyfs_u32_t tmp_obj_num = 0;
	skyfs_replica_recover_head_t * cur_replica_head = NULL;
	int new_file = 0;
	char file_path[256];
	int read_cnt = 0;
	char * tmp_buf = NULL;	

	memset(replica_data_root_dir, 0, 256);
	part_buf = (char *)malloc(skyfs_recover_data_size);
	if(type == 2){
		sprintf(replica_data_root_dir, "%s/%u-%u/%u",
                	SKYFS_OBJ_PATH, skyfs_lid,stripe_id, src_replica_id);
	}
	if(type == 1){
		sprintf(replica_data_root_dir, "%s/rep-%d-partdir",
                	SKYFS_OBJ_PATH, src_replica_id);
	}

#if 0
	gettimeofday(&tv, NULL);
	xid = tv.tv_sec;
	xid *=1000;
	xid += tv.tv_usec/1000;
#endif
	replica_recover_head.xid = xid;
	replica_recover_head.data_type = type; // replica data
	replica_recover_head.flag = 2; // normal data
	replica_recover_head.data_stripe_id = stripe_id;
	replica_recover_head.replica_obj_cnt = 0;
	replica_recover_head.total_data_size = 0;
	replica_recover_head.src_replica_id = src_replica_id;
	replica_recover_head.dest_replica_id = dest_replica_id;
	SKYFS_ERROR_1(" %s: replica_root dir %s\n", __FUNCTION__, replica_data_root_dir);

	
	if((rc = stat(replica_data_root_dir, &buf)) == -1){
		SKYFS_ERROR_1("%s , open root replcia dir %s failed , err %d\n", __FUNCTION__, replica_data_root_dir,errno);
		replica_recover_head.flag = 3; // error;
		goto send_last_msg;

		
		// send to dest osd even if opendir failed
	}


	
	SKYFS_ERROR_1("try to sync and copy replica data dir %s, part_buf %p \n", replica_data_root_dir, part_buf);
	flush_directory(replica_data_root_dir, part_buf, skyfs_recover_data_size, &buf_pos, &replica_recover_head, dest_osd_id);
	SKYFS_ERROR_1("\n %s sync dir ok , remain msg size %lu\n", __FUNCTION__, buf_pos);
	replica_recover_head.flag = 1;

	if(buf_pos == 0 ){
		replica_recover_head.replica_obj_cnt = 0;
        	replica_recover_head.total_data_size = 0;
	}
send_last_msg:
	rc =  __skyfs_send_replica_recover_msg(&replica_recover_head, part_buf, xid, dest_osd_id);
	SKYFS_ERROR_1("\n %s sync dir last message, return %d, recover_data_req_cnt %d\n",__FUNCTION__,rc, recover_data_req_cnt );
	if(part_buf)
		free(part_buf);
	
	return rc;

		
	
	// TODO : send the remain msg 
	
	dir = opendir(replica_data_root_dir);
	if(dir == NULL || rc <0){
		/* send fail reply to dest_osd return   */
	        //replica_recover_head = (skyfs_o_replica_recover_t *)part_buf;
		//replica_recover_head->flag = 2; //1: last 0: narmal 2: error
		//replica_recover_head->data_type = 1;
		//replica_recover_head->replica_obj_cnt = 0;
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		msgp->u.replicaRecoverReq.xid = xid; // time in ms
		msgp->u.replicaRecoverReq.flag = 3; // error
		msgp->u.replicaRecoverReq.data_type = 2; // data obj
		msgp->u.replicaRecoverReq.replica_obj_cnt = 0;

		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
		req->req_iov = kiovp;
		kiovp->ak_addr = part_buf;
		kiovp->ak_len = sizeof(skyfs_DL_part_t);
	        kiovp->ak_offset = 0;
	        kiovp->ak_flag = 0;
	        rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);

		goto ERR;

	}

	//part_buf = (char *)malloc(skyfs_recover_data_size);
	memset(part_buf, 0, skyfs_recover_data_size);
	tmp_buf = part_buf;
	remain_buf_len = skyfs_recover_data_size;
        char * tmp_part_buf = part_buf;	
	memset(&replica_recover_head, 0, sizeof(skyfs_o_replica_recover_t));
	replica_recover_head.xid = xid; // time in ms
	while((entry = readdir(dir)) != NULL){
	   // new dir 
	    if(!strcmp(entry->d_name, "."))
                    continue;

            if(!strcmp(entry->d_name, ".."))
                    continue;

	  if (entry->d_type != DT_DIR) { // 检测进入 subdir
		  continue;
	  }

	   memset(replica_data_dir, 0, 256);
	   sprintf(replica_data_dir, "%s/%s",
                replica_data_root_dir, entry->d_name);
	   subdir = opendir(replica_data_dir);
	   if(subdir == NULL){

		//skyfs_DL_part_t * tmp_part = (skyfs_DL_part_t *)part_buf;
		// TODO : should send the current good part buf at first !! 
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		msgp->u.replicaRecoverReq.xid = xid; // time in ms
		msgp->u.replicaRecoverReq.flag = 3; // error
		msgp->u.replicaRecoverReq.data_type = 2; // data obj
		msgp->u.replicaRecoverReq.replica_obj_cnt = 0;

		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
		req->req_iov = kiovp;
		kiovp->ak_addr = part_buf;
		kiovp->ak_len = sizeof(skyfs_DL_part_t);
	        kiovp->ak_offset = 0;
	        kiovp->ak_flag = 0;

		// record invalid sub-data-dir
		memset(part_buf, 0, 256);
		memcpy(part_buf,replica_data_dir,strlen(replica_data_dir));
	        rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);
		if(req->req_msg){
			free(req->req_msg);
		}
		if(req){
			__amp_free_request(req);
		}

		SKYFS_ERROR_1("can not open replica_data dir %s\n", replica_data_dir);

		continue;

	   }else{ 
		   

			   // read and send all data now
		   while(sub_entry = readdir(subdir) != NULL){
			
			if(sub_entry->d_type != DT_REG)
				continue;
read_next_phase:
			if(fd <= 0){
				new_file = 1;
            			snprintf(file_path, sizeof(file_path), "%s/%s", replica_data_dir, sub_entry->d_name);
	    			fd = open(file_path, O_RDONLY);
				SKYFS_ERROR_1("%s-%d, open obj file %s, fd %d\n",__FUNCTION__, __LINE__, file_path);
				
			}
	    		if(fd<= 0){
		    		SKYFS_ERROR_1("open relica(or part)file %s failed\n", file_path);
		    		continue;
	    		}
			if(new_file){
				cur_replica_obj_cnt++;
				sscanf(entry->d_name,"%llu-%d", &tmp_ino, &tmp_obj_num);
				read_pos = 0;
				new_file = 0;
			}
read_new_buf:
			// now parse filename , get ino,obj_id 
			if(data_offset < skyfs_recover_data_size - sizeof(skyfs_replica_recover_head_t)){
				//if(new_file){
				cur_replica_head = (skyfs_replica_recover_head_t*)tmp_buf;
				cur_replica_head->ino = tmp_ino;
				cur_replica_head->obj_id = tmp_obj_num;
				cur_replica_head->start_offset = read_pos;
				//tmp_buf = part_buf;
				tmp_buf += sizeof(skyfs_replica_recover_head_t);
				data_offset += sizeof(skyfs_replica_recover_head_t);
				read_cnt = pread(fd,tmp_buf, skyfs_recover_data_size - data_offset,read_pos);
				if(read_cnt >0 && read_cnt < (skyfs_recover_data_size-data_offset)){
						  // read TO END of file, buf not full
					new_file = 1;
					data_offset+= read_cnt;
					cur_replica_head->size = read_cnt;
					tmp_buf += read_cnt;
					read_pos = 0;
					read_cnt = 0;
					replica_recover_head.total_data_size += (read_cnt+ sizeof(skyfs_replica_recover_head_t));
					close(fd);
					fd = 0;
					SKYFS_ERROR_1("%s-%d: file end after read , data_offset, tmp_ino %llu, obj_id %llu, offset %llu, size %llu \n",
							__FUNCTION__, __LINE__ ,  tmp_ino, tmp_obj_num, cur_replica_head->start_offset, cur_replica_head->size);

					continue;
				}else if(read_cnt == (skyfs_recover_data_size - data_offset)){
						  // part_buf full , file not END,
					
					cur_replica_head->size = read_cnt;
					replica_recover_head.total_data_size += (read_cnt+ sizeof(skyfs_replica_recover_head_t));
					msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
					rc = __skyfs_OSD_init_req(&req, 
                			&msgp, 
                			SKYFS_MSG_O_RECOVER_REPLICA,
                			SKYFS_NEED_ACK, 
                			AMP_REQUEST|AMP_DATA,
                			msgsize);
					req->req_niov = 1;
					SKYFS_ERROR_1("%s-%d: buff full after read , send the part_buf NOW： data_offset, tmp_ino %llu, obj_id %llu, offset %llu, size %llu \n",
							__FUNCTION__, __LINE__ ,  tmp_ino, tmp_obj_num, cur_replica_head->start_offset, cur_replica_head->size);

					memcpy(&msgp->u.replicaRecoverReq, &replica_recover_head, sizeof(replica_recover_head));
					msgp->u.replicaRecoverReq.xid = xid; // time in ms
					msgp->u.replicaRecoverReq.flag = 2; // normal
					msgp->u.replicaRecoverReq.data_type = 2; // data obj
					msgp->u.replicaRecoverReq.replica_obj_cnt = cur_replica_obj_cnt;

					kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
					req->req_iov = kiovp;
					kiovp->ak_addr = part_buf;
					kiovp->ak_len =  replica_recover_head.total_data_size;
	       				kiovp->ak_offset = 0;
	        			kiovp->ak_flag = 0;
					read_cnt = 0;
					replica_recover_head.total_data_size = 0;

					SKYFS_ERROR_1("%s-%d: buff full after read , send request data_size %llu, part_buf %p--%p  \n",
							__FUNCTION__, __LINE__ ,   kiovp->ak_len, kiovp->ak_addr, part_buf);
		// record invalid sub-data-dir
	        			rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);

					SKYFS_ERROR_1("%s-%d:  , send ret %d \n",
						__FUNCTION__, __LINE__ , rc);
					if(req->req_msg){
						free(req->req_msg);
					}
					if(req){
					__amp_free_request(req);
					}

					cur_replica_obj_cnt = 0;

					new_file = 0;
					data_offset = 0;
					tmp_buf = part_buf;
					read_pos += read_cnt;

					
					//data_offset += sizeof(skyfs_replica_recover_head_t);
					//TODO :send part buf and goto read_continue

					goto read_next_phase;

				}else if(read_cnt <= 0){		  
					// read TO END of file or error, buf not full
					new_file = 1;
					read_cnt = 0;
					//data_offset+= read_cnt;
					cur_replica_head->size = 0;
					replica_recover_head.total_data_size += (read_cnt+ sizeof(skyfs_replica_recover_head_t));
					data_offset += read_cnt;
					tmp_buf += read_cnt;
					close(fd);
					fd = 0;
					read_pos = 0;

					SKYFS_ERROR_1("%s-%d: file end or  read error , data_offset, tmp_ino %llu, obj_id %llu, offset %llu, size %llu \n",
							__FUNCTION__, __LINE__ ,  tmp_ino, tmp_obj_num, cur_replica_head->start_offset, cur_replica_head->size);
					continue;

				}
					  

			}else{
				// TODO: send part buf

				// clear buf
					
					
					//cur_replica_head = (skyfs_replica_recover_head_t*)tmp_buf;
					SKYFS_ERROR_1("%s-%d: buff full , send the part_buf NOW： data_offset, tmp_ino %llu, obj_id %llu, offset %llu, size %llu \n",
							__FUNCTION__, __LINE__ ,  tmp_ino, tmp_obj_num, cur_replica_head->start_offset, cur_replica_head->size);
					msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
					rc = __skyfs_OSD_init_req(&req, 
                			&msgp, 
                			SKYFS_MSG_O_RECOVER_REPLICA,
                			SKYFS_NEED_ACK, 
                			AMP_REQUEST|AMP_DATA,
                			msgsize);
					req->req_niov = 1;

					memcpy(&msgp->u.replicaRecoverReq, &replica_recover_head, sizeof(replica_recover_head));
					msgp->u.replicaRecoverReq.xid = xid; // time in ms
					msgp->u.replicaRecoverReq.flag = 2; // normal
					msgp->u.replicaRecoverReq.data_type = 2; // data obj
					msgp->u.replicaRecoverReq.replica_obj_cnt = cur_replica_obj_cnt;

					kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
					req->req_iov = kiovp;
					kiovp->ak_addr = part_buf;
					kiovp->ak_len =  replica_recover_head.total_data_size;
	       				kiovp->ak_offset = 0;
	        			kiovp->ak_flag = 0;
					SKYFS_ERROR_1("%s-%d: buff full , send request data_size %llu, part_buf %p--%p  \n",
							__FUNCTION__, __LINE__ ,   kiovp->ak_len, kiovp->ak_addr, part_buf);

		// record invalid sub-data-dir
	        			rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);
					if(req->req_msg){
						free(req->req_msg);
					}
					if(req){
					__amp_free_request(req);
					}

				SKYFS_ERROR_1("%s-%d:  , send ret %d \n",
				__FUNCTION__, __LINE__ , rc);
				data_offset = 0;
				tmp_buf = part_buf;
				new_file = 0;
				replica_recover_head.total_data_size = 0;
				goto read_new_buf;
			
			}



		   } // end while


	   } // end else , loop  this file data dir 


	  


	}// end_while loop this replica data dir 
send_last:
	   {
		msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_recover_t);
		rc = __skyfs_OSD_init_req(&req, 
                &msgp, 
                SKYFS_MSG_O_RECOVER_REPLICA,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
		req->req_niov = 1;

		SKYFS_ERROR_1("%s-%d: send last , send the part_buf NOW： data_offset, tmp_ino %llu, obj_id %llu, offset %llu, size %llu \n",
					__FUNCTION__, __LINE__ ,  tmp_ino, tmp_obj_num, cur_replica_head->start_offset, cur_replica_head->size);
		memcpy(&msgp->u.replicaRecoverReq, &replica_recover_head, sizeof(replica_recover_head));
		//msgp->u.replicaRecoverReq.xid = xid; // time in ms
		msgp->u.replicaRecoverReq.flag = 1; // send last
		msgp->u.replicaRecoverReq.data_type = 2; // data obj
		msgp->u.replicaRecoverReq.replica_obj_cnt = cur_replica_obj_cnt;

		kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
                req->req_iov = kiovp;
                kiovp->ak_addr = part_buf;
                kiovp->ak_len =  replica_recover_head.total_data_size;
                kiovp->ak_offset = 0;
                kiovp->ak_flag = 0;
		if(kiovp->ak_len == 0){
			kiovp->ak_len = 4*1024; // keep a small paylaod
			msgp->u.replicaRecoverReq.data_type = 1; // last_data
		}

		SKYFS_ERROR_1("%s-%d: send last , send request data_size %llu, part_buf %p--%p  \n",
				__FUNCTION__, __LINE__ ,   kiovp->ak_len, kiovp->ak_addr, part_buf);

                rc = amp_send_sync(osd_comp_context, req, SKYFS_OSD, dest_osd_id, 1);

		SKYFS_ERROR_1("%s-%d:  , send ret %d \n",
				__FUNCTION__, __LINE__ , rc);

	  }


	// check if there is last 

ERR:
	if(part_buf != NULL)
		free(part_buf);
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:
	
	SKYFS_ERROR_1("%s-%d:  , ret %d \n",
				__FUNCTION__, __LINE__ , rc);

	return rc;





}

skyfs_s32_t 
__skyfs_O2O_recover_data_objs(int src_replica_id,int dest_replica_id,int dest_osd_id, uint64_t xid, int data_stripe_id)

{
	//int i = 0;
	int rc = 0;
	int stripe_num = 0;
	//uint64_t xid;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int start_stripe_id,end_stripe_id;
	if(data_stripe_id <0){
		SKYFS_ERROR_1("O2O_recover_data obj , stripe id %d, recover all\n", data_stripe_id);
		start_stripe_id = 0 ;
		end_stripe_id = skyfs_data_stripe_cnt;
		
	}else{
		SKYFS_ERROR_1("O2O_recover_data obj , stripe id %d, recover this only\n", data_stripe_id);
                start_stripe_id = data_stripe_id ;
                end_stripe_id = data_stripe_id+1;

	}
	//xid = ((uint64_t)tv.tv_sec)*1000 + (tv.tv_usec/1000); // time in ms
	for (stripe_num = 0; stripe_num<skyfs_data_stripe_cnt; stripe_num++){
		rc = __skyfs_O2O_recover_stripe_data_objs(src_replica_id, stripe_num, dest_replica_id, dest_osd_id, xid, 2);
		if(rc<0){
			SKYFS_ERROR_1("%s  failed ret %d , %d:%d:%d\n ", __FUNCTION__, rc,src_replica_id, stripe_num, dest_replica_id);

		}
	}
	return rc;


}

skyfs_s32_t 
__skyfs_O2O_recover_partitions(int src_replica_id,int dest_replica_id,int dest_osd_id, uint64_t xid)

{
	//int i = 0;
	int rc = 0;
	int stripe_num = 0;
	//uint64_t xid;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	//xid = ((uint64_t)tv.tv_sec)*1000 + (tv.tv_usec/1000); // time in ms
	for (stripe_num = 0; stripe_num < 1; stripe_num++){
		rc = __skyfs_O2O_recover_stripe_data_objs(src_replica_id, stripe_num, dest_replica_id, dest_osd_id, xid, 1);
		if(rc<0){
			SKYFS_ERROR_1("%s  failed ret %d , %d:%d:%d\n ", __FUNCTION__,rc,src_replica_id, stripe_num, dest_replica_id);

		}
	}
	return rc;


}



skyfs_s32_t 
__skyfs_O2O_prepare_write(skyfs_DL_file_t *dl_file,
	amp_kiov_t *kiov, 
	skyfs_dl_dest_t *des,
	skyfs_io_vector_t *vec,
	skyfs_u32_t partition_id)
{
	skyfs_u32_t replica_num = des->replica_num;
	amp_request_t *req[replica_num + 1];
	skyfs_msg_t   *msgp[replica_num + 1];
	skyfs_msg_t   *msg = NULL;
	skyfs_u32_t   msgsize;
	skyfs_u32_t   osd_id;
	skyfs_u32_t   i;
	skyfs_s32_t   rc = 0;

	SKYFS_MSG("%s:enter. replica_num:%u, ino:%llu\n", __FUNCTION__, replica_num, dl_file->ino);	

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_preparewrite_t);

	for(i = 1; i <= replica_num; i ++){
		osd_id = des->replica_location[i];
		if(osd_id <= 0)
			continue;
		SKYFS_MSG("%s:before des:osd_id:%u\n", __FUNCTION__,osd_id);
        if(osd_id > 0){
			SKYFS_MSG("%s:before init:\n", __FUNCTION__);
            rc = __skyfs_OSD_init_req(&req[i], 
                &msgp[i], 
                SKYFS_MSG_O_PREPARE_WRITE,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_DATA,
                msgsize);
			SKYFS_MSG("%s:before set\n", __FUNCTION__);
			msgp[i]->u.prepareWriteReq.ino = dl_file->ino;
			msgp[i]->u.prepareWriteReq.partition_id= partition_id;
			msgp[i]->u.prepareWriteReq.client_id = osd_this_id;
			memcpy(&msgp[i]->u.prepareWriteReq.vec, vec, sizeof(skyfs_io_vector_t));
			memcpy(&msgp[i]->u.prepareWriteReq.des, des, sizeof(skyfs_dl_dest_t));
			msgp[i]->u.prepareWriteReq.vec.replica_id = i;
			
		    req[i]->req_niov = 1;
			req[i]->req_iov = kiov;

            SKYFS_MSG("%s:send to osd_id:%d,ino:%llu\n",
                __FUNCTION__, osd_id, dl_file->ino);

            rc = amp_send_async(osd_comp_context, 
                req[i],
                SKYFS_OSD,
                osd_id, 
                0);
            if(rc < 0) {
                SKYFS_ERROR("%s:send request failed.rc:%d\n", __FUNCTION__, rc);
                goto EXIT;
            }
            SKYFS_MSG("%s:send to osd %d succeed.\n", __FUNCTION__, i);
        }
    }

	for(i = 1; i <= replica_num; i++){
		osd_id = des->replica_location[i];
		if(osd_id <= 0)
			continue;
        amp_sem_down(&(req[i]->req_waitsem));
        SKYFS_MSG("%s:replica %d return.\n", __FUNCTION__, i);
    }
	
	for(i = 1; i <=replica_num; i++){
		osd_id = des->replica_location[i];
		if(osd_id <= 0)
			continue;
    	msg = __skyfs_get_msg(req[i]->req_reply);
		rc = msg->error;
		if(rc < 0){
			SKYFS_ERROR("%s:send data to %d err, errno:%d\n", 
				__FUNCTION__, msg->fromid, rc);
			goto EXIT;
		}
	}

EXIT:
        for (i = 1; i<= replica_num; i++) {

		osd_id = des->replica_location[i];
		if(osd_id <= 0)
			continue;
            if (req[i]->req_msg){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                SKYFS_MSG("%s:before free req\n", __FUNCTION__);
                __amp_free_request(req[i]);
            }
        }

	SKYFS_MSG("%s:eixt. rc:%d\n", __FUNCTION__, rc);	

	return rc;

}

skyfs_s32_t 
__skyfs_O2O_commit_write(skyfs_DL_file_t *dl_file,
	skyfs_dl_dest_t *des,
	skyfs_io_vector_t *vec,
	skyfs_u32_t partition_id,
	skyfs_u32_t is_new_partition)
{

	skyfs_u32_t replica_num = des->replica_num;
	amp_request_t *req[replica_num+1];
	skyfs_msg_t   *msgp[replica_num+1];
	skyfs_msg_t   *msg = NULL;
	skyfs_u32_t   msgsize;
	skyfs_u32_t   i;
	skyfs_u32_t   osd_id;
	skyfs_s32_t   rc = 0;
	skyfs_s32_t   write_size = 0;

	SKYFS_MSG("%s:enter. replica_num:%d rc:%d\n", __FUNCTION__, replica_num, rc);	

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_commitwrite_t);

	for(i = 1; i <= replica_num; i ++){
		osd_id = des->replica_location[i];
        if(osd_id > 0){
            rc = __skyfs_OSD_init_req(&req[i], 
                &msgp[i], 
                SKYFS_MSG_O_COMMIT_WRITE,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_MSG,
                msgsize);
			msgp[i]->u.commitWriteReq.ino = dl_file->ino;
			msgp[i]->u.commitWriteReq.partition_id= partition_id;
			msgp[i]->u.commitWriteReq.client_id = osd_this_id;
			//msgp[i]->u.commitWriteReq.replica_id = i | (is_new_partition << 31);
			msgp[i]->u.commitWriteReq.replica_id = i;
			memcpy(&msgp[i]->u.commitWriteReq.vec, vec, sizeof(skyfs_io_vector_t));
			memcpy(&msgp[i]->u.commitWriteReq.des, des, sizeof(skyfs_dl_dest_t));

	        SKYFS_MSG("%s:send to osd_id:%d\n",
                __FUNCTION__, osd_id);
            rc = amp_send_async(osd_comp_context, 
                req[i],
                SKYFS_OSD,
                osd_id, 
                0);
            if(rc < 0) {
                SKYFS_ERROR("%s:send request failed.rc:%d\n", __FUNCTION__, rc);
                goto EXIT;
            }
            SKYFS_MSG("%s:send to osd %d succeed.\n", __FUNCTION__, i);
        }
    }


	for(i = 1; i <= replica_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
	
	for(i = 1; i <=replica_num; i++){
    	msg = __skyfs_get_msg(req[i]->req_reply);
		rc = msg->error;
		if(rc > 0){
			if(i == 1){
				write_size = rc;
				// mayl , only add write_version[] if commiting correctly
				des->write_version[i]++;	
			}else if(write_size != rc){
				SKYFS_ERROR("%s:write data size not equal,replica %d,before:%d, now:%d\n", 
					__FUNCTION__, i, write_size, rc);
				goto EXIT;
			}
		}else{
			SKYFS_ERROR("%s:commit data to %d err, errno:%d\n", 
				__FUNCTION__, msg->fromid, rc);
			goto EXIT;
		}
	}

EXIT:
        for (i = 1; i <= replica_num; i++) {
            if (req[i]->req_msg){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                SKYFS_MSG("%s:before free req\n", __FUNCTION__);
                __amp_free_request(req[i]);
            }
        }

	SKYFS_MSG("%s:eixt. rc:%d\n", __FUNCTION__, rc);	
	return rc;
}

skyfs_s32_t 
__skyfs_O2O_remove_obj(skyfs_DL_file_t *dl_file,
	skyfs_dl_dest_t *des,
	skyfs_u64_t obj_id,
	skyfs_u32_t partition_id)
{
	skyfs_u32_t replica_num = des->replica_num;
	amp_request_t *req[replica_num+1];
	skyfs_msg_t   *msgp[replica_num+1];
	skyfs_msg_t   *msg = NULL;
	skyfs_u32_t   msgsize;
	skyfs_u32_t   i;
	skyfs_u32_t   osd_id;
	skyfs_s32_t   rc = 0;
	uint32_t      osd_gid;
	//skyfs_s32_t   write_size = 0;

	SKYFS_ERROR("%s:enter. replica_num:%d rc:%d\n", __FUNCTION__, replica_num, rc);	

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_doremoveobj_t);

	for(i = 1; i <= replica_num; i ++){
		// changed by mayl fro replica in 
		osd_gid = find_osdgid(osd_num, dl_file->ino, obj_id);
		osd_id = find_replica_osd(osd_num, osd_gid, i , replica_num);
		//osd_id = des->replica_location[i];

        if(osd_id > 0){
            rc = __skyfs_OSD_init_req(&req[i], 
                &msgp[i], 
                SKYFS_MSG_O_DO_REMOVEOBJ,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_MSG,
                msgsize);
			msgp[i]->u.doRemoveObjReq.ino = dl_file->ino;
			msgp[i]->u.doRemoveObjReq.obj_id = obj_id;
			msgp[i]->u.doRemoveObjReq.partition_id = partition_id;
			msgp[i]->u.doRemoveObjReq.reserve_size = -1; // remove it.
			msgp[i]->u.doRemoveObjReq.replica_id = i;

	        SKYFS_MSG("%s:send to osd_id:%d\n",
                __FUNCTION__, osd_id);
            rc = amp_send_async(osd_comp_context, 
                req[i],
                SKYFS_OSD,
                osd_id, 
                0);
            if(rc < 0) {
                SKYFS_ERROR_1("%s:send request failed.rc:%d\n", __FUNCTION__, rc);
                goto EXIT;
            }
            SKYFS_ERROR("%s:send to osd %d succeed.\n", __FUNCTION__, i);
        }
    }


	for(i = 1; i <= replica_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
	
	for(i = 1; i <=replica_num; i++){
    	msg = __skyfs_get_msg(req[i]->req_reply);
		rc = msg->error;
		if(rc < 0){
			SKYFS_ERROR("%s:remove obj in %u err, errno:%d\n", 
				__FUNCTION__, msg->fromid, rc);
		}
	}

EXIT:
        for (i = 1; i <= replica_num; i++) {
            if (req[i]->req_msg){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                SKYFS_MSG("%s:before free req\n", __FUNCTION__);
                __amp_free_request(req[i]);
            }
        }

	SKYFS_ERROR_1("%s:eixt. rc:%d\n", __FUNCTION__, rc);	
	return rc;
}


skyfs_s32_t 
__skyfs_O2O_truncate_obj(skyfs_DL_file_t *dl_file,
	skyfs_dl_dest_t *des,
	skyfs_u64_t obj_id,
	skyfs_u32_t partition_id,
	skyfs_s64_t reserve_size)
{
	skyfs_u32_t replica_num = des->replica_num;
	amp_request_t *req[replica_num+1];
	skyfs_msg_t   *msgp[replica_num+1];
	skyfs_msg_t   *msg = NULL;
	skyfs_u32_t   msgsize;
	skyfs_u32_t   i;
	skyfs_u32_t   osd_id;
	skyfs_s32_t   rc = 0;
	uint32_t      osd_gid;
	//skyfs_s32_t   write_size = 0;

	SKYFS_ERROR_1("%s:enter. replica_num:%d obj_id:%d, reserve size %lld \n", __FUNCTION__, replica_num, obj_id, reserve_size);	

	msgsize = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_doremoveobj_t);

	for(i = 1; i <= replica_num; i ++){
		// changed by mayl fro replica in 
		osd_gid = find_osdgid(osd_num, dl_file->ino, obj_id);
		osd_id = find_replica_osd(osd_num, osd_gid, i , replica_num);
		//osd_id = des->replica_location[i];

        if(osd_id > 0){
            rc = __skyfs_OSD_init_req(&req[i], 
                &msgp[i], 
                SKYFS_MSG_O_DO_REMOVEOBJ,
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_MSG,
                msgsize);
			msgp[i]->u.doRemoveObjReq.ino = dl_file->ino;
			msgp[i]->u.doRemoveObjReq.obj_id = obj_id;
			msgp[i]->u.doRemoveObjReq.partition_id= partition_id;
			msgp[i]->u.doRemoveObjReq.reserve_size = reserve_size;
			msgp[i]->u.doRemoveObjReq.replica_id = i;

	        SKYFS_MSG("%s:send to osd_id:%d\n",
                __FUNCTION__, osd_id);
            rc = amp_send_async(osd_comp_context, 
                req[i],
                SKYFS_OSD,
                osd_id, 
                0);
            if(rc < 0) {
                SKYFS_ERROR_1("%s:send request failed.rc:%d\n", __FUNCTION__, rc);
                goto EXIT;
            }
            SKYFS_ERROR("%s:send to osd %d succeed.\n", __FUNCTION__, i);
        }
    }


	for(i = 1; i <= replica_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
	
	for(i = 1; i <=replica_num; i++){
    	msg = __skyfs_get_msg(req[i]->req_reply);
		rc = msg->error;
		if(rc < 0){
			SKYFS_ERROR("%s:remove obj in %u err, errno:%d\n", 
				__FUNCTION__, msg->fromid, rc);
		}
	}

EXIT:
        for (i = 1; i <= replica_num; i++) {
            if (req[i]->req_msg){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                SKYFS_MSG("%s:before free reply,replylen:%d\n", 
                    __FUNCTION__, req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                SKYFS_MSG("%s:before free req\n", __FUNCTION__);
                __amp_free_request(req[i]);
            }
        }

	SKYFS_ERROR_1("%s:eixt. rc:%d\n", __FUNCTION__, rc);	
	return rc;
}


/*This is end of osd_op.c*/
