/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_itm.c $
 */
#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

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


#include "client_help.h"
#include "client_init.h"
#include "client_op.h"
#include "client_cache.h"
#include "client_itm.h"

//struct list_head client_bcache_list;

skyfs_s32_t __skyfs_C2M_statfs(struct statvfs *stbuf)
{
    skyfs_s32_t rc = 0;
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;
    skyfs_disk_sb_t *sb = NULL;

    skyfs_u32_t    size = 0;

    SKYFS_MSG("__skyfs_C2M_statfs:enter\n");

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_statfs:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_statfs_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_statfs:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_STATFS, 
        client_this_id, SKYFS_CLIENT,size);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, 1, 1);
       if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_statfs:send failed.rc:%d\n",rc);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    rc = msgp->error;

    if (rc >= 0) {
        sb = &(msgp->u.statfsAck.sb);
        stbuf->f_fsid= sb->fsid;
        stbuf->f_bsize = sb->blocksize;
        stbuf->f_blocks = sb->blocks;
        stbuf->f_bfree = sb->bfree;
        stbuf->f_bavail = sb->bavail;
        stbuf->f_files = sb->inodes;
        stbuf->f_ffree = sb->ifree;
        stbuf->f_favail = sb->ifavail;
        stbuf->f_namemax = SKYFS_MAX_NAME_LEN;
        SKYFS_MSG("__skyfs_C2M_statfs:fsid:%d,blocksz:%d,blocks:%llu,bfree:%llu:\n",
           sb->fsid,sb->blocksize,sb->blocks,sb->bfree);
        SKYFS_MSG("__skyfs_C2M_statfs:bavail:%llu,files:%llu,ffree:%llu\n",
           sb->bavail,sb->inodes,sb->ifree);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_statfs:exit\n");

    return rc;
}

skyfs_s32_t __skyfs_C2M_create(skyfs_ino_t pino, 
                skyfs_s8_t *name, 
                skyfs_u32_t mode,
                skyfs_ino_t *ino, 
                skyfs_u32_t *conflict_index)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;
    //skyfs_u32_t uid;
    //skyfs_u32_t gid;

    dir_id = __skyfs_C_get_dirid(pino, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_create:enter:dino:%llu,%s,mds_id:%d\n", 
        pino, name, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_create:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_create_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_create:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_CREATE, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.createReq.dir_ino = pino;
    msgp->u.createReq.mode = mode;
    msgp->u.createReq.uid = fuse_get_context()->uid;
    msgp->u.createReq.gid = fuse_get_context()->gid;
    strcpy(msgp->u.createReq.name, name);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_create: pino=%llu, name=%s.send out\n", pino, name);
       rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    SKYFS_MSG("__skyfs_C2M_create: pino=%llu, name=%s.receive back\n", pino, name);
       if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_create:send failed.rc:%d\n",rc);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    SKYFS_MSG("__skyfs_C2M_create:msgp->ver:%llu\n", msgp->ver);

    rc = msgp->error;
    if (rc >= 0) {
        *ino = msgp->u.createAck.meta.ino;
        *conflict_index = msgp->u.createAck.meta.conflict_index;
        SKYFS_ERROR("__skyfs_C2M_create:%s,ino:%llu,conflict:%u\n", name, *ino, *conflict_index);
    }else{
        SKYFS_ERROR("__skyfs_C2M_create:error:dino:%llu,%s,mds_id:%d,rc:%d\n", 
            pino, name, mds_id, rc);

    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_create:forward:name:%s,old_mds:%d,new_mds:%d\n",
            name, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_create:exit:rc:%d\n", rc);

    return rc;
}

skyfs_s32_t __skyfs_C2M_remove(skyfs_ino_t pino, 
                skyfs_s8_t *name, 
                skyfs_ino_t ino,
                skyfs_u32_t conflict_index)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;
    skyfs_u32_t nlink;

    dir_id = __skyfs_C_get_dirid(pino, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_remove:%s,dino:%llu,mds:%d,ino:%llu,%u\n", 
        name, pino, mds_id, ino, conflict_index);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_remove:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_remove_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_remove:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_REMOVE,
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.removeReq.dir_ino = pino;
    strcpy(msgp->u.removeReq.name, name);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_remove: pino=%llu, name=%s.send out\n", pino, name);
    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    SKYFS_MSG("__skyfs_C2M_remove: pino=%llu, name=%s.receive back\n", 
        pino, msgp->u.removeReq.name);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_remove:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    rc = msgp->error;
    if (rc >= 0) {
        nlink = msgp->u.removeAck.meta.nlink;
        SKYFS_MSG("__skyfs_C2M_remove: ino=%llu,nlink=%d,mode:%d\n", 
            msgp->u.removeAck.meta.ino,
            nlink,
            S_ISREG(msgp->u.removeAck.meta.mode));
        if(nlink == 0 && S_ISREG(msgp->u.removeAck.meta.mode)){
            rc = __skyfs_C_release_flength(ino);
            if(rc != 0){
                SKYFS_ERROR("__skyfs_C2M_remove:release flength,rc:%d.\n",rc);
            }
        }
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_remove:forward:name:%s,old_mds:%d,new_mds:%d\n",
            name, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_remove:exit:rc:%d\n", rc);

    return rc;
}

skyfs_s32_t __skyfs_C2M_release(skyfs_ino_t ino, skyfs_u32_t conflict_index)
{
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;
    skyfs_s32_t rc = 0;

    dir_id = __skyfs_C_get_dirid(ino, 1);
    subset_id = __sufns_C_get_subsetid(dir_id, NULL, conflict_index);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ENTER("__skyfs_C2M_release:enter:ino:%llu,mds_id:%d\n", 
        ino, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_release:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_release_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_release:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_RELEASE,
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.releaseReq.ino = ino;
    msgp->u.releaseReq.conflict_index = conflict_index;
    msgp->u.releaseReq.client_id = client_this_id;
    SKYFS_FILL_REQ(req, SKYFS_NEEDNOT_ACK, AMP_REQUEST | AMP_MSG, size);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_release:send failed.rc:%d\n",rc);
           goto err_out;
    }

err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_release:exit:rc:%d\n", rc);

    return rc;
}

skyfs_s32_t __skyfs_C2M_lock(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                int cmd,
				uint64_t lock_owner,
				struct flock * lock)
{
    skyfs_s32_t rc = 0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id = 0;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id = 0;


    SKYFS_ENTER("__skyfs_C2M_lock:ino:%llu,conflict:%u\n", ino, conflict_index);
    SKYFS_ERROR("__skyfs_C2M_lock:ino:%llu,conflict:%u, cmd %x\n", ino, conflict_index, cmd,  lock->l_type);

    if(ino != SKYFS_ROOT_INO){
		
        dir_id = __skyfs_C_get_dirid(ino, 1);
        subset_id = __sufns_C_get_subsetid(dir_id, NULL, conflict_index);
        mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);
    }else{
        mds_id = SKYFS_MASTER_MDS_ID;
    }

    SKYFS_ENTER("__skyfs_C2M_lock:enter:mds_id:%d,subset_id:%d\n", 
        mds_id, subset_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_lock:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_flock_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_lock:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_FLOCK,
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.flockReq.ino = ino;
    msgp->u.flockReq.lock_owner = lock_owner;
    msgp->u.flockReq.conflict_index = conflict_index;
    msgp->u.flockReq.clt_id = client_this_id;
    msgp->u.flockReq.pid = getpid();
    msgp->u.flockReq.op_type = cmd & 0xffff ; // F_GETLK, F_SETLK, F_SETLKW
	// l_type : low 16-bits : F_RDLCK, F_WRLCK, F_UNLCK, high 16-bits means flock_class: 1 POSIX 2 FLOCK
    msgp->u.flockReq.fl_type = lock->l_type | (cmd & 0xffff0000);   
	
    msgp->u.flockReq.start = lock->l_start;
    msgp->u.flockReq.len = lock->l_len;
    SKYFS_ERROR("__skyfs_C2M_lock: send op_type 0x%x, fl_type 0x%x \n", msgp->u.flockReq.op_type,  msgp->u.flockReq.fl_type);

	// TODO : add more args in flockReq , if l_start == l_len == 0 , should change the 2 args above 
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_lock:send failed.rc:%d\n",rc);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);

    rc = msgp->error;
    if (rc == 0 || rc == EAGAIN) {
                		
		// TODO : fill reply to  flockAck
		if((cmd & 0xffff)== F_GETLK){
			SKYFS_ERROR("getlk failed, conf_start %d, conf_len %d\n",  msgp->u.flockAck.start,  msgp->u.flockAck.len);
			lock->l_type = msgp->u.flockAck.fl_type;
			lock->l_pid = msgp->u.flockAck.pid;
			lock->l_start = msgp->u.flockAck.start;
			lock->l_len = msgp->u.flockAck.len;
		} 
		rc = -rc;
    }else{
		rc = -rc;
        SKYFS_ERROR("__skyfs_C2M_lock:get reply failed :ino:%llu,conflict:%u,mds_id:%d\n", 
            ino, conflict_index, mds_id);
    }

    if(mds_id != msgp->fromid){
        SKYFS_ERROR("__skyfs_C2M_getattr:forward:ino:%llu,old_mds:%d,new_mds:%d\n",
            ino, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_lock:exit:rc:%d\n", rc);

    return rc;
}


skyfs_s32_t __skyfs_C2M_getattr(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                struct stat *sbbuf,
		uint32_t * palgorithm)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_meta_t *meta = NULL;
    skyfs_u32_t dir_id = 0;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id = 0;
    int meta_status = 0;

	skyfs_C_meta_t      metacache;
    skyfs_timespec_t    current_time;

    SKYFS_ERROR("__skyfs_C2M_getattr:ino:%llu,conflict:%u\n", ino, conflict_index);

    if(ino != SKYFS_ROOT_INO){
		//metacache funcation
		rc = __skyfs_C_lookup_meta(ino, conflict_index, &metacache);

    		gettimeofday(&current_time, NULL);
    		//SKYFS_ERROR_1("__skyfs_C2M_getattr:ino:%llu,conflict:%u, loolup rc %d, meta_sec %lu, currsec %lu\n", 
		//		ino, conflict_index, rc, metacache.meta.atime.tv_sec, current_time.tv_sec );
		if(rc){
			meta_status = 1;
			meta = &(metacache.meta);
			SKYFS_ERROR("CACHE UP time %lu.%06lu, curtime %lu.%06lu\n", (uint64_t)metacache.update_sec, (uint64_t)metacache.update_usec,
					current_time.tv_sec, current_time.tv_usec);
			uint64_t up_time = ((uint64_t)metacache.update_sec)*1000000+ metacache.update_usec;
			if((current_time.tv_sec * 1000000 + current_time.tv_usec) < (up_time + SKYFS_REFRESH_INTERVAL*1000000)){
			//if(current_time.tv_sec - meta->atime.tv_sec < SKYFS_REFRESH_INTERVAL){
	        	sbbuf->st_ino = (ino_t)(meta->ino);
   	    		sbbuf->st_size = meta->size;
    			sbbuf->st_mode = meta->mode;
        		sbbuf->st_nlink = meta->nlink;
        		sbbuf->st_uid = meta->uid;
        		sbbuf->st_gid = meta->gid;
        		sbbuf->st_dev = meta->dev;
        		sbbuf->st_atime = (time_t)meta->atime.tv_sec;
        		sbbuf->st_mtime = (time_t)meta->mtime.tv_sec;
        		sbbuf->st_ctime = (time_t)meta->ctime.tv_sec;
   	   	  	sbbuf->st_blocks = ((skyfs_u64_t)(meta->size) >> 9);
			// added by mayl
   	   	  	sbbuf->st_blocks = ((skyfs_u64_t)(meta->space) >> 9);
	
				SKYFS_MSG("__skyfs_C2M_getattr:cached:mode:ino:%lu,mode:%u,size:%llu\n", 
					sbbuf->st_ino, meta->mode,meta->size);
				SKYFS_MSG("__skyfs_C2M_getattr:nlink:%u,uid:%u,gid:%u,dev:%u,st_blocks:%lu\n", 
					meta->nlink, meta->uid, meta->gid, meta->dev, sbbuf->st_blocks);
				SKYFS_MSG("__skyfs_C2M_getattr::atime:%lu,mtime:%lu,ctime.tv_sec:%lu\n", 
					sbbuf->st_atime, sbbuf->st_mtime, sbbuf->st_ctime);
				//meta = NULL;
				* palgorithm = meta->algorithm;
				return 0;
			}
		}

        dir_id = __skyfs_C_get_dirid(ino, 1);
        subset_id = __sufns_C_get_subsetid(dir_id, NULL, conflict_index);
        mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);
    }else{
        mds_id = SKYFS_MASTER_MDS_ID;
    }

    SKYFS_ENTER("__skyfs_C2M_getattr:enter:mds_id:%d,subset_id:%d\n", 
        mds_id, subset_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_getattr:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getmeta_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_getattr:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_READ_INODE,
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.getmetaReq.ino = ino;
    msgp->u.getmetaReq.conflict_index = conflict_index;
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_C2M_getattr:send failed.rc:%d, ino %ld, conf index %ld\n",rc, ino, conflict_index);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);

    rc = msgp->error;
    if (rc >= 0) {
        meta = &(msgp->u.getmetaAck.meta);
	sbbuf->st_ino = (ino_t)(meta->ino);
        sbbuf->st_size = meta->size;
        sbbuf->st_mode = meta->mode;
        sbbuf->st_nlink = meta->nlink;
        sbbuf->st_uid = meta->uid;
        sbbuf->st_gid = meta->gid;
        sbbuf->st_dev = meta->dev;
        sbbuf->st_atime = (time_t)meta->atime.tv_sec;
        sbbuf->st_mtime = (time_t)meta->mtime.tv_sec;
        sbbuf->st_ctime = (time_t)meta->ctime.tv_sec;

	* palgorithm = meta->algorithm;
	SKYFS_ERROR_1("get altorithm from MDS\n");
        sbbuf->st_blocks = (skyfs_u64_t)(meta->size) >> 9;
	// added by mayl
   	sbbuf->st_blocks = ((skyfs_u64_t)(meta->space) >> 9);
        /*
        if(S_ISDIR(meta->mode)){
            ///xxx error sbbuf->st_blocks = (skyfs_u32_t)(meta->size) >> 9;
        }else if(S_ISREG(meta->mode)){
        }
        */
		SKYFS_MSG("__skyfs_C2M_getattr:mode:ino:%lu,mode:%u,size:%llu,from mds.\n", 
			sbbuf->st_ino, meta->mode,meta->size);
		SKYFS_MSG("__skyfs_C2M_getattr:nlink:%u,uid:%u,gid:%u,dev:%u,st_blocks:%lu\n", 
			meta->nlink, meta->uid, meta->gid, meta->dev, sbbuf->st_blocks);
		SKYFS_MSG("__skyfs_C2M_getattr::atime:%lu,mtime:%lu,ctime.tv_sec:%lu\n", 
			sbbuf->st_atime, sbbuf->st_mtime, sbbuf->st_ctime);

	if(meta_status == 0){
		// mayl add this meta to cache
	
	}else{
		// mayl update this meta to cache
	}
        meta = NULL;
    }else{
        SKYFS_ERROR("__skyfs_C2M_getattr:enter:ino:%llu,conflict:%u,mds_id:%d\n", 
            ino, conflict_index, mds_id);
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_getattr:forward:ino:%llu,old_mds:%d,new_mds:%d\n",
            ino, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_getattr:exit:rc:%d\n", rc);

    return rc;
}


// TODO: if lookup meta failed, return 0
//       if cache timeout return 0
//       otherwise, only update the cache.size and cache.used_space, return 1
skyfs_s32_t __skyfs_C2M_setattr_cache(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                skyfs_m_setmeta_args_t *args,
		int release
		)
{
	int rc = 0;
	int got_meta = 0;
	struct timeval tv;
	skyfs_C_meta_t metacache;
    	skyfs_meta_t *meta = NULL;
	metacache.changed_space = 0;
    	rc = __skyfs_C_lookup_meta(ino, conflict_index, &metacache);
	if(rc == 0){
		//return rc;
		goto do_real_setattr;
	}

	gettimeofday(&tv, NULL);
	got_meta = 1;
	meta = &(metacache.meta);
	if((metacache.update_cnt %20 != 0 ) && (metacache.update_sec + SKYFS_REFRESH_INTERVAL > tv.tv_sec)
			&& !release){

		meta = &(metacache.meta);
		// only enlarge size if permited here
		if(meta->size < args->size)
			meta->size = args->size;
		metacache.changed_space += args->space_changed;
		rc = __skyfs_C_update_meta(ino, conflict_index,metacache.changed_space, meta);
		if(rc == 0){
			SKYFS_ERROR("set change space %lld, args space %lld  \n", metacache.changed_space, args->space_changed);
			return 1;
		}
	}

	

do_real_setattr:
	if(release && got_meta){

		if(args->size != 0){

			if(meta->size < args->size)
			  meta->size = args->size;
		}
		if (args->space_changed != 0){
		     metacache.changed_space += args->space_changed;
		}
		args->size = meta->size;
	}
	if(!got_meta && release){
		return 0;
	}

		
	args->space_changed += metacache.changed_space;
	SKYFS_ERROR("tell mds changed space %lld\n", args->space_changed);
	rc = __skyfs_C2M_setattr(ino, conflict_index, args, 1);
	//rc = __skyfs_C2M_setattr
	return rc;	

	

}


skyfs_s32_t __skyfs_C2M_setattr(skyfs_ino_t ino,
                skyfs_u32_t conflict_index,
                skyfs_m_setmeta_args_t *args,
		skyfs_u32_t do_update)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id = 0;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id = 0;
	skyfs_C_meta_t metacache;

    if(do_update){
    	SKYFS_ERROR("__skyfs_C2M_setattr:enter:ino:%llu,conflict:%u, do_update %d\n", 
        	ino, conflict_index, do_update);
    }

    if(ino != SKYFS_ROOT_INO){
        dir_id = __skyfs_C_get_dirid(ino, 1);
        subset_id = __sufns_C_get_subsetid(dir_id, NULL, conflict_index);
        mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);
    }else{
        mds_id = SKYFS_MASTER_MDS_ID;
    }

    SKYFS_MSG("__skyfs_C2M_setattr:mds_id:%d,subset_id:%d\n", 
        mds_id, subset_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_setattr:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_setmeta_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_setattr:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_WRITE_INODE,
        client_this_id, SKYFS_CLIENT, size);
    memcpy(&(msgp->u.setmetaReq), args, sizeof(skyfs_m_setmeta_args_t));;
    
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_setattr:mode:ino:%llu,mode:%u\n", ino, args->mode);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_C2M_setattr:send failed.rc:%d\n",rc);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);

    rc = msgp->error;
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_setattr::ino:%llu,conflict:%u,mds_id:%d,err:%d\n", 
            ino, conflict_index, mds_id, rc);
    }else{
		rc = 0;
    }
	
	//metacache funcation 
    rc0 = __skyfs_C_lookup_meta(ino, conflict_index, &metacache);
    if(rc0){
   		SKYFS_ERROR("__skyfs_C2M_setattr:mode:ino:%llu,mode:%u,free, valid %x\n", ino, metacache.meta.mode, args->valid);
		if(! do_update){
			rc0 = __skyfs_C_release_meta(ino, conflict_index);
		}else{
			int got_algorithm = msgp->u.setmetaAck.meta.algorithm;

			rc0 = __skyfs_C_update_meta(ino, conflict_index,0, &(msgp->u.setmetaAck.meta));
   		        SKYFS_ERROR("__skyfs_C2M_setattr:mode:ino:%llu,mode:%u,free, valid %x, algorithm %d ,update local meta , return %d\n", 
					ino, metacache.meta.mode, args->valid, got_algorithm , rc0);
		}
    }else if (do_update){
			int got_algorithm = msgp->u.setmetaAck.meta.algorithm;
			rc0 = __skyfs_C_add_meta(ino, conflict_index,&(msgp->u.setmetaAck.meta));
   		        SKYFS_ERROR("__skyfs_C2M_setattr:mode:ino:%llu,mode:%u,free, valid %x, algorithm %d add to local meta , return %d\n", 
					ino, metacache.meta.mode, args->valid, got_algorithm,rc0);
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_setattr:forward:ino:%llu,old_mds:%d,new_mds:%d\n",
            ino, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    if(do_update)
    	SKYFS_ERROR("__skyfs_C2M_setattr:exit:rc:%d\n", rc);

    return rc;
}

skyfs_s32_t __skyfs_C2M_lookup(skyfs_ino_t pino,
                skyfs_s8_t *name,
                skyfs_ino_t *ino,
                skyfs_u32_t *conflict_index)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;
    skyfs_u32_t layout_version;
	
	skyfs_C_meta_t metacache;

    dir_id = __skyfs_C_get_dirid(pino, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_lookup:%s:dino:%llu,mds_id:%u,dir_id:%u,subset_id:%u\n", 
        name, pino, mds_id, dir_id, subset_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_lookup:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_lookup_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_lookup:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_LOOKUP, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.lookupReq.dir_ino = pino;
    strcpy(msgp->u.lookupReq.name, name);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_lookup: pino=%llu, name=%s.send out\n", pino, name);
    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    SKYFS_MSG("__skyfs_C2M_lookup: pino=%llu, name=%s.receive back\n", 
	    pino, name);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_lookup:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    rc = msgp->error;
    layout_version = msgp->ver;

    __skyfs_C_judge_mds_layoutv(layout_version, mds_id);

    if (rc > 0) {
        *ino = msgp->u.lookupAck.meta.ino;
        *conflict_index = msgp->u.lookupAck.meta.conflict_index;
        SKYFS_MSG("__skyfs_C2M_lookup:ino:%llu,conflict:%u\n", *ino, *conflict_index);
		//metacache funcation 
		rc0 = __skyfs_C_lookup_meta(*ino, *conflict_index, &metacache);
		if(rc0 == 0){
			__skyfs_C_add_meta(*ino, *conflict_index, &(msgp->u.lookupAck.meta));
		}else if(metacache.meta.atime.tv_sec < msgp->u.lookupAck.meta.atime.tv_sec){
			__skyfs_C_update_meta(*ino, *conflict_index, 0, &(msgp->u.lookupAck.meta));
		}
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_lookup:forward:name:%s,old_mds:%u,new_mds:%u\n",
            name, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_lookup:exit:rc:%d\n", rc);

    return rc;
}

skyfs_C_bcache_t *
__skyfs_C_getbcache(skyfs_u32_t subset_id,
        skyfs_u32_t chunk_id,
        skyfs_ino_t ino,
        skyfs_u64_t offset,
        skyfs_u32_t client_id,
        skyfs_u32_t pid)
{
    skyfs_C_bcache_t *bcache = NULL;
    skyfs_M_bmeta_t  *bmeta = NULL;
    amp_request_t    *req = NULL;
    skyfs_msg_t      *msgp = NULL;
    amp_kiov_t       kiov;
    skyfs_s32_t      rc = 0;
    skyfs_s32_t      rc0 = 0;
    skyfs_s32_t      mds_id;
    skyfs_u32_t      size;
    skyfs_u32_t      dir_id;
    skyfs_u32_t      layout_version;

    SKYFS_ENTER("__skyfs_C_getbcache:enter,ino:%llu,offset:%llu,sid:%d,cid:%d\n", 
        ino, offset, subset_id, chunk_id);

    dir_id = __skyfs_C_get_dirid(ino, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);
    if(mds_id <= 0){
        SKYFS_ERROR("__skyfs_C_getbcache:get mds_id:%d failed.\n",mds_id);
        goto err_none;
    }

    bcache = (skyfs_C_bcache_t *)malloc(sizeof(skyfs_C_bcache_t));
    if(bcache == NULL){
        SKYFS_ERROR("__skyfs_C_getbcache:alloc bcache failed.\n");
        goto err_none;
    }

    bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_C_getbcache:alloc bmeta failed.\n");
        goto err_none;
    }

    rc = __amp_alloc_request(&req);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C_getbcache:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_readdir_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C_getbcache:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_READDIR, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.readdirReq.dino = ino;
    msgp->u.readdirReq.subset_id = subset_id;
    msgp->u.readdirReq.chunk_id = chunk_id;
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    kiov.ak_addr = bmeta;
    kiov.ak_len = sizeof(skyfs_M_bmeta_t);
    kiov.ak_offset = 0;
    kiov.ak_flag = 0;

    req->req_iov = &kiov;
    req->req_niov = 1;

    SKYFS_MSG("__skyfs_C_getbcache: ino=%llu,subset_id=%d,chunk_id=%d\n", 
        ino, subset_id, chunk_id);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C_getbcache:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    rc = msgp->error;
    layout_version = msgp->ver;

    __skyfs_C_judge_mds_layoutv(layout_version, mds_id);

    if (rc >= 0){
        memcpy(bcache->cmetap, 
            bmeta->cmetap, 
            sizeof(skyfs_M_cmeta_t) * SKYFS_MAX_META_PER_BOX);
        bcache->nfree = bmeta->nfree;
        bcache->subset_id = subset_id;
        //bcache->chunk_id = chunk_id;
	
        bcache->chunk_id = bmeta->box_id;
	if(bcache->chunk_id != chunk_id){
		SKYFS_ERROR("Wrong chunk_id %d:%d\n", bcache->chunk_id, chunk_id);
	}
	bcache->subset_id = bmeta->last_one;
        bcache->now_offset = 0;
        bcache->now_index = 0;
	// added by mayl
	bcache->start_offset = offset;
        bcache->ino = ino;
	bcache->is_back = 0;
        list_add_tail(&bcache->hash, &client_bcache_list);
    }else{
        if(bcache){
            free(bcache);
            bcache = NULL;
        } 
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C_getbcache:forward:,old_mds:%d,new_mds:%d\n",
            mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:
    free(bmeta);

    SKYFS_LEAVE("__skyfs_C_getbcache:exit,rc:%d,subid:%d,chid:%d,bcache:%p.\n",
        rc, subset_id, chunk_id, bcache);

    return bcache;
}

skyfs_C_bcache_t *
__skyfs_C_find_bcache(skyfs_ino_t ino, 
        skyfs_u64_t offset, 
        skyfs_u32_t client_id, 
        skyfs_u32_t pid)
{
    skyfs_C_bcache_t *bcache = NULL, *tmp_bcache = NULL;
    struct list_head *index = NULL, *head = NULL;

    SKYFS_ENTER("__skyfs_C_find_bcache:enter,ino:%llu,offset:%llu,client_id:%u,pid:%u\n", 
        ino, offset, client_id, pid);

    head = &client_bcache_list;
    list_for_each(index, head){
        tmp_bcache = list_entry(index, skyfs_C_bcache_t, hash);
        SKYFS_MSG("__skyfs_C_find_bcache:ino:%llu,offset:%d.\n", 
            tmp_bcache->ino, tmp_bcache->now_offset);
        if(tmp_bcache->ino == ino && tmp_bcache->now_offset == offset){
            bcache = tmp_bcache;
            bcache->is_back = 0;

		SKYFS_ERROR("find becahe for forward offset %llu, start_offset %llu, new offset %llu\n ", 
				offset, tmp_bcache->start_offset, tmp_bcache->now_offset);
	    break;
        }else if(tmp_bcache->ino == ino && tmp_bcache->now_offset >= offset && tmp_bcache->start_offset <= offset){
		bcache = tmp_bcache;
		SKYFS_ERROR("find becahe for back offset %llu, start_offset %llu, new offset %llu\n ", 
				offset, tmp_bcache->start_offset, tmp_bcache->now_offset);
		//bcache->now_index = bcache->last_index;
		bcache->is_back = 1;
	        break;	
	}

       
    }

    SKYFS_LEAVE("__skyfs_C_find_bcache:exit,bcache:%p\n", bcache);

    return bcache;
}

skyfs_s32_t
__skyfs_C_release_bcache(skyfs_ino_t ino, struct list_head * bcache_list_head)
{
    skyfs_C_bcache_t *tmp_bcache = NULL;
    struct list_head *index = NULL, *head = NULL, *tmp= NULL;
    skyfs_u32_t rc = 0;
 
    SKYFS_ENTER("__skyfs_C_release_bcache:enter,ino:%llu\n", ino);

    head = &client_bcache_list;
    list_for_each(index, head){
        tmp_bcache = list_entry(index, skyfs_C_bcache_t, hash);
        SKYFS_MSG("__skyfs_C_release_bcache:ino:%llu,offset:%d.\n", 
            tmp_bcache->ino, tmp_bcache->now_offset);
        if(tmp_bcache->ino == ino){
            tmp = index->prev;
            list_del_init(&(tmp_bcache->hash));
            free(tmp_bcache);
            index = tmp;
            rc ++;
        }
    }
    
    SKYFS_LEAVE("__skyfs_C_release_bcache:exit,rc:%d\n", rc);

    return rc;
}

skyfs_u32_t 
__skyfs_C_compose_interrupt_bufpage(skyfs_u32_t out_offset,
    skyfs_u32_t in_offset, 
        skyfs_s8_t *buf_page, 
        skyfs_C_bcache_t *bcache)
{
	skyfs_u32_t page_offset;
    	skyfs_u32_t tmp_offset = bcache->last_offset;
    	skyfs_u32_t dentry_num;
    	skyfs_u32_t bmeta_index;
    	skyfs_u32_t fill_count =0;
	
    	skyfs_dentry_t *dentry;
	int page_full = 0;
    	skyfs_s8_t *tmp_buf_page = NULL; 
	
	SKYFS_ERROR_1("__skyfs_C_compose_interrupt_bufpage:enter,start_off %lu, boff:%d, noff %d,nfree:%d,index:%d.\n", 
	tmp_offset,
        out_offset,
	bcache->now_offset,
        bcache->nfree,
        bcache->now_index);

    	page_offset = 0;
	int check_cnt = 0;
    	//bmeta_index = bcache->now_index;
    	bmeta_index = bcache->last_index;
    	dentry_num = 0;
	tmp_buf_page = buf_page;


	//skyfs_u64_t page_mark = out_offset+

	// find bmeta_index
	
	if(tmp_offset == out_offset){
		goto fill_page;
	}
	while(1){
		if( (bcache->cmetap[bmeta_index].hashkey != 0)
              		&& (bcache->cmetap[bmeta_index].type != SKYFS_RENAME)){
			if(tmp_offset == out_offset)
				break;
			tmp_offset -= sizeof(skyfs_dentry_t);
		}
		bmeta_index--;

	}
fill_page:
	fprintf(stderr, "find current start index %d, fill_count %d\n", bmeta_index, fill_count);

    	dentry = (skyfs_dentry_t *)malloc(sizeof(skyfs_dentry_t));
	// add dentry from bmeta_index to now_ofset
	while(1){
		if(bmeta_index >= SKYFS_MAX_META_PER_BOX|| page_full){
                	break;
            	}
            	bzero(dentry, sizeof(skyfs_dentry_t));

		if( (bcache->cmetap[bmeta_index].hashkey != 0)
              		&& (bcache->cmetap[bmeta_index].type != SKYFS_RENAME)){
			
			strcpy(dentry->name, bcache->cmetap[bmeta_index].name);
                	dentry->namelen = strlen(dentry->name);
                	dentry->ino = bcache->cmetap[bmeta_index].ino;
                	dentry->offset = out_offset+dentry_num*sizeof(skyfs_dentry_t);
			//
			//
			bcache->last_offset = dentry->offset;
			bcache->last_index = bmeta_index;

                	dentry->mode = bcache->cmetap[bmeta_index].mode;
			if(page_offset+2*sizeof(skyfs_dentry_t)>=SKYFS_DIR_BLK_SIZE){
                    		dentry->reclen = SKYFS_DIR_BLK_SIZE - page_offset;
                    		page_offset = SKYFS_DIR_BLK_SIZE;
				page_full = 1;
                	}else{
                    		dentry->reclen = sizeof(skyfs_dentry_t);
                    		page_offset = page_offset + sizeof(skyfs_dentry_t);
                	}
			bcache->now_offset =  dentry->reclen + dentry->offset;

                    	//dentry->reclen = sizeof(skyfs_dentry_t);
			//if(dentry_num == fill_count -1){
			//	dentry->reclen = (bcache->now_offset - dentry->offset);
			//}
			if(0)
				fprintf(stderr, "compose pageoff %lu, name %s\n, index %d, reclen %d\n",page_offset, dentry->name, bmeta_index, dentry->reclen);
                    	//page_offset += sizeof(skyfs_dentry_t);

			dentry_num++;

                	memcpy(tmp_buf_page, dentry, sizeof(skyfs_dentry_t));
                	tmp_buf_page = tmp_buf_page + sizeof(skyfs_dentry_t);
			tmp_offset += sizeof(skyfs_dentry_t);
			
		}
		bmeta_index ++;
	}
	 bcache->now_index = bmeta_index;
	if(bmeta_index >= SKYFS_MAX_META_PER_BOX)
	 	bcache->now_index = 0;

	 

	if(bmeta_index != bcache->now_index){
		SKYFS_ERROR ("compose interrupt  !!!, now_index %d , get_index %d, out_off %lu\n ", bcache->now_index, bmeta_index);


	}
	SKYFS_ERROR ("compose interrupt, now_index %d , get_index %d, out_off %lu, dentry_num %d, now_off %lu\n ", 
			bcache->now_index, bmeta_index,out_offset, dentry_num, bcache->now_offset);

	if(dentry)
		free(dentry);

	return page_offset;




}


skyfs_u32_t 
__skyfs_C_compose_bufpage(skyfs_u32_t out_offset,
    skyfs_u32_t in_offset, 
        skyfs_s8_t *buf_page, 
        skyfs_C_bcache_t *bcache)
{
    skyfs_u32_t page_offset;
    skyfs_u32_t dentry_num;
    skyfs_u32_t bmeta_index;
    skyfs_dentry_t *dentry;
    skyfs_s8_t *tmp_buf_page = NULL; 

    SKYFS_ENTER("__skyfs_C_compose_bufpage:enter,poff:%d,nfree:%d,index:%d.\n", 
        in_offset,
        bcache->nfree,
        bcache->now_index);
    page_offset = in_offset;
    bmeta_index = bcache->now_index;
    dentry_num = 0;
    tmp_buf_page = buf_page;

    if(out_offset < bcache->now_offset && bcache->is_back == 1){

	    // mayl TODO ,add  interrupt dentriesa
	 if(out_offset == 0){
		 // return to 0
		 bcache->start_offset = 0;
		 bcache->last_offset = 0;
		 bcache->last_index = 0;
		 bcache->now_index = 0;
		 bcache->now_offset = 0;
		 bcache->is_back = 0;
		 
	 }
	return __skyfs_C_compose_interrupt_bufpage( out_offset,in_offset, 
        	buf_page, bcache);
    //
	    //
    }

    dentry = (skyfs_dentry_t *)malloc(sizeof(skyfs_dentry_t));

    while(1){
    /*Get denties to fill the buf_page*/
        while(1){
        /*Get one non-empty dentry*/
            if(bmeta_index >= SKYFS_MAX_META_PER_BOX){
                break;
            }

            bzero(dentry, sizeof(skyfs_dentry_t));
            if((bcache->cmetap[bmeta_index].hashkey != 0) 
              && (bcache->cmetap[bmeta_index].type != SKYFS_RENAME)){
                SKYFS_MSG("__skyfs_C_compose_bufpage:%d\n",
                    bcache->cmetap[bmeta_index].type);
                strcpy(dentry->name, bcache->cmetap[bmeta_index].name);
                dentry->namelen = strlen(dentry->name);
                dentry->ino = bcache->cmetap[bmeta_index].ino;
                dentry->offset = out_offset+dentry_num*sizeof(skyfs_dentry_t);
                dentry->mode = bcache->cmetap[bmeta_index].mode;
		bcache->last_index = bmeta_index;
		bcache->last_offset = dentry->offset;
                SKYFS_MSG("__skyfs_C_compose_bufpage:%s,%s,page_offset:%d,%d\n",
                    dentry->name, 
                    bcache->cmetap[bmeta_index].name, 
                    page_offset,
                    bmeta_index);

                if(page_offset+2*sizeof(skyfs_dentry_t)>=SKYFS_DIR_BLK_SIZE){
                    dentry->reclen = SKYFS_DIR_BLK_SIZE - page_offset;
                    page_offset = SKYFS_DIR_BLK_SIZE;
                }else{
                    dentry->reclen = sizeof(skyfs_dentry_t);
                    page_offset = page_offset + sizeof(skyfs_dentry_t);
                }
                dentry_num ++;
                memcpy(tmp_buf_page, dentry, sizeof(skyfs_dentry_t));
                tmp_buf_page = tmp_buf_page + sizeof(skyfs_dentry_t);
                bmeta_index ++;
                break;
            }

            bmeta_index ++;
        } 

        SKYFS_MSG("__skyfs_C_compose_bufpage:index:%d\n", bmeta_index);

        if(page_offset + sizeof(skyfs_dentry_t) >= SKYFS_DIR_BLK_SIZE){
            bcache->now_offset = out_offset + SKYFS_DIR_BLK_SIZE;
            bcache->now_index = bmeta_index % SKYFS_MAX_META_PER_BOX;
            break;
        }    

        if(bmeta_index >= SKYFS_MAX_META_PER_BOX){
            bcache->now_offset = out_offset+dentry_num*sizeof(skyfs_dentry_t);
            bcache->now_index = 0;
            break;
        }
    }

    if(dentry){
        free(dentry);
    }
    SKYFS_LEAVE("__skyfs_C_compose_bufpage:exit,%d.\n", page_offset);
    SKYFS_ERROR("__skyfs_C_compose_bufpage:exit,P_off %d., dentry_size %d, out_off %d, dentry_num %d\n", 
		    page_offset, sizeof(skyfs_dentry_t), out_offset, dentry_num);
    return page_offset;
}


skyfs_s32_t
__skyfs_C2M_readdir(skyfs_ino_t ino,
        skyfs_u32_t conflict_index,
        skyfs_s8_t *buf_page,
        skyfs_u64_t offset,
        skyfs_u32_t count)
{
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t chunk_id = 0;
    skyfs_C_bcache_t *bcache = NULL;
    skyfs_u32_t pid = 0;
    skyfs_u32_t buf_off = 0;
    skyfs_s8_t *tmp_buf_page = NULL;
    skyfs_dentry_t *dentry;

    if(offset == 0)
    	SKYFS_ERROR_1("__skyfs_C2M_readdir:enter,ino:%llu,offset:%llu,count:%u\n", 
        	ino, offset, count);

    dentry = (skyfs_dentry_t *)malloc(sizeof(skyfs_dentry_t));
    if(dentry == NULL){
        goto EXIT;
    }

   subset_id = 0;
    chunk_id = 0;
    if(offset == 0){
	__skyfs_C_release_bcache(ino,NULL);
    }
    if(0){
	    /* offset maybe back to 0 !!!! */
	//__skyfs_C_release_bcache(ino);
        //bcache = __skyfs_C_getbcache(subset_id, chunk_id, 
          //          ino, offset, client_this_id, pid);
    }else{
        bcache = __skyfs_C_find_bcache(ino, offset, client_this_id, pid);
        if(bcache == NULL){
            SKYFS_ERROR("__skyfs_C2M_readdir:find bcache NULL. offset %lu\n", offset);
	    bcache = __skyfs_C_getbcache(subset_id, chunk_id, 
                    ino, offset, client_this_id, pid);
        }else if(bcache->now_index == 0 && bcache->is_back == 0){
            SKYFS_ERROR_1("__skyfs_C2M_readdir:reach end., offset %lu try next , cur_bcache %p \n", offset, bcache);
	    /*add by mayl  chunk_id > 0x10000 means get next bcache */
	    subset_id = bcache->subset_id;
	    chunk_id = bcache->chunk_id;
	    bcache = __skyfs_C_getbcache(subset_id, (chunk_id | 0x10000), 
                    ino, offset, client_this_id, pid);

	    /*add by mayl end */
	    if(bcache == NULL){
		SKYFS_ERROR_1("GET next bcache NULL \n");
            	bzero(dentry, sizeof(skyfs_dentry_t)); 
            	dentry->offset = 0;
            	memcpy(buf_page, dentry, sizeof(skyfs_dentry_t)); 
            	goto EXIT;
	    }else{
		    subset_id = bcache->subset_id;
		    chunk_id = bcache->chunk_id;
		    SKYFS_ERROR("GOT next_bcache %p , subset_id %d, chunk_id %d\n", bcache, subset_id, chunk_id);
	    }
        }
    }

	if(bcache == NULL){
        SKYFS_ERROR_1("__skyfs_C2M_readdir:bcache is NULL. offset, %lu, offset\n");
	bzero(dentry, sizeof(skyfs_dentry_t)); 
        dentry->offset = 0;
        memcpy(buf_page, dentry, sizeof(skyfs_dentry_t)); 
		goto EXIT;
	}

    
    //SKYFS_ERROR_1("start  compose bcache  \n");
    tmp_buf_page = buf_page;
    buf_off = __skyfs_C_compose_bufpage(offset, 0, tmp_buf_page, bcache);
    //SKYFS_ERROR_1("  compose bcache ret %d \n", buf_off);
    //if(buf_off < SKYFS_DIR_BLK_SIZE){
    /* changed by mayl */
    if(buf_off == 0 ){
        subset_id = bcache->subset_id;
        chunk_id = bcache->chunk_id |0x10000;
        bcache = __skyfs_C_getbcache(subset_id, chunk_id,
                    ino, offset, client_this_id, pid);

        if(bcache){
	    chunk_id = bcache->chunk_id;	
	    subset_id = bcache->subset_id;	
	    SKYFS_ERROR_1("C2M_readdir small bcache ,get next, buf_off %d, off %d,  bcache %p subset_id %d, chunk_id %d \n",
			buf_off, offset, bcache, subset_id, chunk_id);
            tmp_buf_page = buf_page + buf_off;
            buf_off=__skyfs_C_compose_bufpage(offset, buf_off, tmp_buf_page, bcache);
        }
    }

EXIT:
    SKYFS_MSG("__skyfs_C2M_readdir:before free.\n");
    if(dentry){
        free(dentry);
    }
    SKYFS_ERROR("__skyfs_C2M_readdir:exit,buf_off:%d\n", buf_off);

    return buf_off;
}



skyfs_s32_t __skyfs_C2M_symlink(skyfs_ino_t pino, 
                skyfs_s8_t *name, 
                skyfs_ino_t *ino, 
                skyfs_u32_t *conflict_index,
                const skyfs_s8_t *target)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0 = 0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;

    dir_id = __skyfs_C_get_dirid(pino, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_symlink:%s,dino:%llu,%s,mds_id:%d\n", 
        name, pino, target, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_symlink:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_symlink_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_symlink:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_SYMLINK, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.symlinkReq.dir_ino = pino;
    msgp->u.symlinkReq.mode = S_IFLNK | 0777;
    strcpy(msgp->u.symlinkReq.name, name);
    strcpy(msgp->u.symlinkReq.target, target);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_symlink: pino=%llu, name=%s.send out\n", pino, name);
    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_symlink:send failed.rc:%d\n",rc);
           goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    SKYFS_MSG("__skyfs_C2M_symlink:msgp->ver:%llu\n", msgp->ver);

    rc = msgp->error;
    if (rc >= 0) {
        *ino = msgp->u.symlinkAck.meta.ino;
        *conflict_index = msgp->u.symlinkAck.meta.conflict_index;
        SKYFS_MSG("__skyfs_C2M_symlink:%s,ino:%llu,conflict:%u\n", 
            name, *ino, *conflict_index);
    }else{
        SKYFS_ERROR("__skyfs_C2M_symlink:error:dino:%llu,%s,mds_id:%d,rc:%d\n", 
            pino, name, mds_id, rc);
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_symlink:forward:name:%s,old_mds:%d,new_mds:%d\n",
            name, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_symlink:exit:rc:%d, rc0:%d\n", rc, rc0);

    return rc;
}

skyfs_s32_t __skyfs_C2M_readlink(skyfs_ino_t pino,
                skyfs_s8_t *name,
                skyfs_s8_t *buf,
                skyfs_u32_t linksize)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t rc0 = 0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t real_size = 0;

    skyfs_u32_t dir_id = 0;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id = 0;
    skyfs_u32_t size = 0;

    dir_id = __skyfs_C_get_dirid(pino, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_readlink:%s:dino:%llu,mds_id:%d\n", 
        name, pino, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_readlink:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_readlink_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_readlink:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_READLINK, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.readlinkReq.dir_ino = pino;
    strcpy(msgp->u.readlinkReq.name, name);
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_readlink:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    SKYFS_MSG("__skyfs_C2M_readlink:msgp->ver:%llu\n", msgp->ver);

    rc = msgp->error;
    if (rc >= 0) {
        real_size = strlen(msgp->u.readlinkAck.target);
        if(real_size < linksize){
            strcpy(buf, msgp->u.readlinkAck.target);
        }else{
            strncpy(buf, msgp->u.readlinkAck.target, real_size);
        }
        SKYFS_MSG("__skyfs_C2M_readlink:symlink:%s,linksize:%d,reals:%d,rc:%d\n", 
            buf, linksize, real_size,rc);
        rc = 0;
    }else{
        SKYFS_ERROR("__skyfs_C2M_readlink:failed:dino:%llu,%s,mds_id:%d,rc:%d\n", 
            pino, name, mds_id, rc);
    }

    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_readlink:forward:name:%s,oldm:%d,newm:%d\n",
            name, mds_id, msgp->fromid);
    }

    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_readlink:exit:rc:%d, rc0:%d\n", rc, rc0);

    return rc;

}

skyfs_s32_t
__skyfs_C2M_rename(skyfs_ino_t pino_from, skyfs_ino_t pino_to, 
       skyfs_s8_t  *name_from, skyfs_s8_t *name_to,
       skyfs_ino_t ino_from, skyfs_u32_t conflict_index_from,
       skyfs_ino_t *ino, skyfs_u32_t *conflict_index)
{
    skyfs_s32_t rc = 0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;

    dir_id = __skyfs_C_get_dirid(pino_from, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name_from, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_rename:enter:from dino:%llu,%s,to dino:%llu,%s,mds_id:%d\n", 
        pino_from, name_from, pino_to, name_to, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_rename:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_rename_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_rename:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_RENAME, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.renameReq.pino_from = pino_from;
    msgp->u.renameReq.pino_to = pino_to;
    msgp->u.renameReq.ino_from = ino_from;
    msgp->u.renameReq.conflict_index_from = conflict_index_from;
    strcpy(msgp->u.renameReq.name_from, name_from);
    strcpy(msgp->u.renameReq.name_to, name_to);
    bzero(&(msgp->u.renameReq.meta), sizeof(skyfs_meta_t));
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_rename: pino=%llu, name=%s.send out\n", 
        pino_from, name_from);
    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_rename:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    SKYFS_MSG("__skyfs_C2M_rename:msgp->ver:%llu\n", msgp->ver);

    rc = msgp->error;
    if (rc >= 0) {
        *ino = msgp->u.renameAck.meta.ino;
        *conflict_index = msgp->u.renameAck.meta.conflict_index;
        SKYFS_MSG("__skyfs_C2M_rename:ino is %llu,conflict:%u\n", 
            *ino, *conflict_index);
    }else{
        SKYFS_ERROR("__rename:error:dino:%llu,%s,dino:%llu,%s,mds_id:%d,rc:%d\n", 
            pino_from, name_from, pino_to, name_to, mds_id, rc);
    }
/*
    ***Rename does'n need to update dir_depth as diff path at different MDSs***
    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_rename:forward:name:%s,old_mds:%d,new_mds:%d\n",
            name, mds_id, msgp->fromid);
    }
*/
    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_rename:exit:rc:%d, %s\n", rc, name_from);

    return rc;

}

skyfs_s32_t
__skyfs_C2M_link(skyfs_ino_t pino_from, skyfs_ino_t pino_to, 
       skyfs_s8_t  *name_from, skyfs_s8_t *name_to,
       skyfs_ino_t ino_from, skyfs_u32_t conflict_index_from,
       skyfs_ino_t *ino, skyfs_u32_t *conflict_index)
{
    skyfs_s32_t rc = 0;
    
    skyfs_msg_t *msgp = NULL;
    amp_request_t *req = NULL;

    skyfs_u32_t    size = 0;

    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t mds_id;

    dir_id = __skyfs_C_get_dirid(pino_from, 0);
    subset_id = __sufns_C_get_subsetid(dir_id, name_from, 0);
    mds_id = __skyfs_C_judge_mdsid(dir_id, subset_id);

    SKYFS_ERROR("__skyfs_C2M_link:enter:from dino:%llu,%s,to dino:%llu,%s,mds_id:%d\n", 
        pino_from, name_from, pino_to, name_to, mds_id);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_link:alloc request failed.\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_rename_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_link:alloc req_msg failed.\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_LINK, 
        client_this_id, SKYFS_CLIENT,size);
    msgp->u.linkReq.pino_from = pino_from;
    msgp->u.linkReq.pino_to = pino_to;
    msgp->u.linkReq.ino_from = ino_from;
    msgp->u.linkReq.conflict_index_from = conflict_index_from;
    strcpy(msgp->u.linkReq.name_from, name_from);
    strcpy(msgp->u.linkReq.name_to, name_to);
    bzero(&(msgp->u.linkReq.meta), sizeof(skyfs_meta_t));
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST | AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_link: pino=%llu, name=%s.send out\n", 
        pino_from, name_from);
    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2M_link:send failed.rc:%d\n",rc);
        goto err_out;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    
    SKYFS_MSG("__skyfs_C2M_link:msgp->ver:%llu\n", msgp->ver);

    rc = msgp->error;
    if (rc >= 0) {
        *ino = msgp->u.linkAck.meta.ino;
        *conflict_index = msgp->u.linkAck.meta.conflict_index;
        SKYFS_MSG("__skyfs_C2M_link:ino is %llu,conflict:%u\n", 
            *ino, *conflict_index);
    }else{
        SKYFS_ERROR("__link:error:dino:%llu,%s,dino:%llu,%s,mds_id:%d,rc:%d\n", 
            pino_from, name_from, pino_to, name_to, mds_id, rc);
    }
/*
    ***Link does'n need to update dir_depth as diff path at different MDSs***
    if(mds_id != msgp->fromid){
        rc0 = __skyfs_C_release_depth(dir_id);
        SKYFS_ERROR("__skyfs_C2M_rename:forward:name:%s,old_mds:%d,new_mds:%d\n",
            name, mds_id, msgp->fromid);
    }
*/
    free(req->req_reply);
err_out:
    free(req->req_msg);
err_req:
    __amp_free_request(req);
err_none:

    SKYFS_LEAVE("__skyfs_C2M_link:exit:rc:%d\n", rc);

    return rc;

}

skyfs_s32_t
__skyfs_C2M_get_dcache(skyfs_M_dir_cache_t *dir_cache, skyfs_u32_t dir_id)
{
    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t mds_id = 0;
    skyfs_u32_t size = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_C2M_get_dcache:enter,mds_id:%d,dir_id:%d\n", 
        mds_id, dir_id);

    mds_id = __skyfs_C_judge_dir_mdsid(dir_id, 0);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__sufns_C2M_get_dcache:alloc request failed\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getdirc_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_get_dcache:alloc req_msg failed\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_GET_DIRC, 
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.getdircReq.dir_id = dir_id;

    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_get_dcache:before send:req %p\n", req);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_get_dcache:send request failed.rc:%d\n", rc);
        goto err_msg;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    rc = msgp->error;
    if(rc >= 0){
        memcpy(dir_cache, msgp->u.mtext, sizeof(skyfs_M_dir_cache_t));
    }

    SKYFS_MSG("__skyfs_C2M_get_dcache:msgp:%p\n", msgp);

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

    SKYFS_LEAVE("__skyfs_C2M_get_dcache:exit\n");

    return rc;
}

skyfs_s32_t __skyfs_C2M_get_layout(skyfs_u32_t mds_id)
{
    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t size = 0;
    skyfs_s32_t rc = 0;

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__sufns_C2M_get_layout:alloc request failed\n");
        goto err_none;
    }

    rc = -ENOMEM;
    size = AMP_SKYFS_MSGHEAD_SIZE;
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2M_get_layout:alloc req_msg failed\n");
        goto err_req;
    }

    bzero(req->req_msg, size);

    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_M_GET_LAYOUT, 
        client_this_id, SKYFS_CLIENT, size);

    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2M_get_layout:before send:req %p\n", req);

    rc = amp_send_sync(client_comp_context, req, SKYFS_MDS, mds_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2M_get_layout:send request failed.rc:%d\n", rc);
        goto err_msg;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    rc = msgp->error;
    if(rc >= 0){
        memcpy(mds_layout, msgp->u.getlayoutAck.layout, 
            sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);
        mds_layout_version = msgp->u.getlayoutAck.layout_version;
    }

    SKYFS_MSG("__skyfs_C2M_get_layout:msgp:%p\n", msgp);

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

    SKYFS_ERROR("__skyfs_C2M_get_layout:exit,from mds:%d\n", mds_id);

    return rc;
}
/*This is end of client_itm.c*/
