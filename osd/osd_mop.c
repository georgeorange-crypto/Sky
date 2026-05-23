/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_mop.c $
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
#include "osd_layout.h"
#include "osd_help.h"

#include "mds_fs.h"

#include "osd_ito.h"
#include "osd_dl.h"
#include "osd_loadb.h"

void __skyfs_SS_read_bmeta(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t            kiov;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_u32_t            dir_id;
    skyfs_u32_t            subset_id;
    skyfs_u32_t            bmeta_id;
    skyfs_u32_t            req_size;
    skyfs_u32_t            size;
    skyfs_u32_t            offset;
    skyfs_s32_t            fd = 0;
    skyfs_s8_t            subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t            rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.readbmetaReq.dir_id;
    subset_id = msgp->u.readbmetaReq.subset_id;
    bmeta_id = msgp->u.readbmetaReq.bmeta_id;
    size = msgp->u.readbmetaReq.size;

    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	SKYFS_ERROR("__skyfs_SS_read_bmeta:enter,did:%d,subset_id:%d,bmeta_id:%d\n",
        	dir_id, subset_id, bmeta_id);
    }
    SKYFS_MSG("__skyfs_SS_read_bmeta:size:%d,size:%ld\n", 
        size, sizeof(skyfs_s8_t) * SKYFS_BMETA_SIZE);

    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id &(SKYFS_MAX_SUBSET_PER_DIR -1));
    }
    offset = bmeta_id * size + sizeof(skyfs_M_subset_head_t);

    fd = open(subset_file_name, O_RDONLY);
    if(fd < 0){
        SKYFS_ERROR("__skyfs_SS_read_bmeta:can not open subset file:%s,errno:%d\n", 
            subset_file_name, errno);
        rc = -errno;
        goto ERR;
    }

    bmeta = malloc(sizeof(skyfs_M_bmeta_t));
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_SS_read_bmeta:alloc bmeta failed:\n");
        goto ERR;
    }

    bzero(bmeta, sizeof(skyfs_M_bmeta_t));
	bmeta->hashvalue = (skyfs_u64_t)(~0);
	bmeta->nfree = -ENOENT;
	bmeta->firstfree = -ENOENT;
    if(pread(fd, bmeta, size, offset) < 0){
        SKYFS_ERROR("__skyfs_SS_read_bmeta:read subfile %s failed,errno:%d\n",
            subset_file_name, errno);
        rc = -errno;
        goto ERR;
    }

    kiov.ak_addr = bmeta;
    kiov.ak_len = size;
    kiov.ak_offset = 0;
    kiov.ak_flag = 0;

    if(bmeta->box_id != bmeta_id){
        SKYFS_ERROR_1("__skyfs_SS_read_bmeta:error:bmeta.box_id:%d,bmeta_id:%d,nfree:%d\n",
            bmeta->box_id, bmeta_id, bmeta->nfree);    
        SKYFS_ERROR_1("__skyfs_SS_read_bmeta:cmeta0.ino:%lld,cmeta0.name:%s,fd:%d\n", 
            bmeta->cmetap[0].ino, bmeta->cmetap[0].name, fd);
		if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){ // changed by mayl
        		exit(1);
		}else if(bmeta->nfree == 0 && bmeta->firstfree == 0 
					&& bmeta->first_time.tv_sec == 0 && bmeta->last_time.tv_sec == 0){
			/* added by mayl, ENOENT in nfree and firstfree means empty hole in this bmeta */
			SKYFS_ERROR_1("SS_read_bmeta,  read a bmeta hole at subset %d, bmeta %d \n", subset_id, bmeta_id);	
			bmeta->nfree = -ENOENT;
			bmeta->firstfree = -ENOENT;
			
		}
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_DATA, 0, NULL, req_size);

    if(rc >= 0){
        SKYFS_MSG("__skyfs_SS_read_bmeta:init iov,size:%d,addr:%p.\n", size, bmeta);
        req->req_niov = 1;
        req->req_iov = &kiov;
        msgp->error = size;
    }else{
        msgp->error = rc;
        SKYFS_ERROR("__skyfs_SS_read_bmeta:read error:%d.\n", rc);
    }
    
    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
            req->req_remote_type, 
            req->req_remote_id, 
            0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_read_bmeta:send failed.rc:%d\n", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);
    
    SKYFS_ERROR_1("__skyfs_SS_read_bmeta:leav.subset:%d,bmeta:%d,cmetafirst:%s,fd:%d, nfree %d\n\n", 
        subset_id, bmeta_id, bmeta->cmetap[0].name, fd, bmeta->nfree);

    if(bmeta){
        free(bmeta);
    }
}

void __skyfs_SS_write_bmeta(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t          *kiov;
    skyfs_u32_t         dir_id;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         bmeta_id;
    skyfs_u32_t         req_size;
    skyfs_u32_t         size;
    skyfs_u32_t         offset;
    skyfs_s32_t         fd = 0;
    skyfs_s8_t          subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_s32_t         rc = 0;

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.writebmetaReq.dir_id;
    subset_id = msgp->u.writebmetaReq.subset_id;
    bmeta_id = msgp->u.writebmetaReq.bmeta_id;
    size = msgp->u.writebmetaReq.size;

    SKYFS_ERROR_1("__skyfs_SS_write_bmeta:enter.dir_id:%d,subset_id:%d,bmeta_id:%d\n",
        dir_id, subset_id, bmeta_id);


    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
	/* add by mayl for rename*/
	//memset(subset_file_name, 0, SKYFS_MAX_NAME_LEN);
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id &(SKYFS_MAX_SUBSET_PER_DIR -1));
    }
    offset = bmeta_id * size + sizeof(skyfs_M_subset_head_t);

    fd = open(subset_file_name, O_WRONLY);
    if(fd < 0){
        SKYFS_ERROR("__skyfs_SS_write_bmeta:can not open subset file:%s\n", 
            subset_file_name);
        rc = fd;
        goto ERR;
    }

    bmeta = kiov->ak_addr;
    SKYFS_MSG("__skyfs_SS_write_bmeta:hashvalue:%lld,nfree:%d,firstfree:%d\n",
        bmeta->hashvalue, bmeta->nfree, bmeta->firstfree);
    SKYFS_ERROR("__skyfs_SS_write_bmeta:cmeta.name:%s,cmeta.hash:%lld\n",
        bmeta->cmetap[0].name, bmeta->cmetap[0].hashkey);

    if(bmeta->firstfree < 0){
        SKYFS_ERROR("__skyfs_SS_write_bmeta:error:bmetafirstfree:%d\n", bmeta->firstfree);
        exit(1);
    }

    if(bmeta->box_id != bmeta_id){
        SKYFS_ERROR("__skyfs_SS_write_bmeta:error:bmeta->box_id:%d,bmeta_id:%d,fd:%d\n",
            bmeta->box_id, bmeta_id, fd);    
        exit(1);
    }

    if(pwrite(fd, kiov->ak_addr, size, offset) < 0){
        SKYFS_ERROR("__skyfs_SS_write_bmeta:write subset file failed\n");
        rc = -1;
        goto ERR;
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_SS_write_bmeta:send failed.rc:%d\n", rc);
    }

    if(kiov->ak_addr){
        free(kiov->ak_addr);
    }

    if(kiov){
        free(kiov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("__skyfs_SS_write_bmeta:leave.subset:%d,bmeta:%d,fd:%d\n\n", 
        subset_id, bmeta_id, fd);
}

void __skyfs_SS_read_subset(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    skyfs_u32_t            dir_id;
    skyfs_u32_t            subset_id;
    skyfs_u32_t            req_size;
    skyfs_s32_t            fd = 0;
    skyfs_s8_t            subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_M_subset_head_t subset_head;
    skyfs_s32_t            rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.readsubsetReq.dir_id;
    subset_id = msgp->u.readsubsetReq.subset_id;

    SKYFS_ENTER("__skyfs_SS_read_subset:enter,dir_id:%u, subset_id:%d\n",
        dir_id, subset_id);

    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id & (SKYFS_MAX_SUBSET_PER_DIR -1));

    }

    fd = open(subset_file_name, O_RDONLY);
    if(fd > 0){
        if(read(fd, &subset_head, sizeof(skyfs_M_subset_head_t)) < 0){
            SKYFS_ERROR("__skyfs_SS_read_subset:read subset error,errno:%d\n", 
				errno);
            rc = -errno;
            goto ERR;
        }
    }else{
        SKYFS_ERROR_1("__skyfs_SS_read_subset:can't open %s,errno:%d\n", 
            subset_file_name, errno);
        rc = -errno;
        goto ERR;
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readsubset_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    if(rc >= 0){
        msgp->u.readsubsetAck.split_depth = subset_head.split_depth;
        msgp->u.readsubsetAck.subset_depth = subset_head.subset_depth;
        msgp->u.readsubsetAck.nlink = subset_head.nlink;
    }

    msgp->error = rc;

    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_SS_read_subset:send failed.rc:%d\n", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);


    SKYFS_LEAVE("__skyfs_SS_read_subset:leave.sp_depth:%d,sb_depth:%d\n\n",
        subset_head.split_depth, subset_head.subset_depth);
}

void __skyfs_SS_write_subset(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    skyfs_u32_t            dir_id;
    skyfs_u32_t            subset_id;
    skyfs_u32_t            req_size;
    skyfs_s32_t            fd = 0;
    skyfs_s8_t            subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_M_subset_head_t subset_head;
    skyfs_s32_t            rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);

    subset_head.dir_id = dir_id = msgp->u.writesubsetReq.dir_id;
    subset_head.subset_id = subset_id = msgp->u.writesubsetReq.subset_id;
    subset_head.split_depth = msgp->u.writesubsetReq.split_depth;
    subset_head.subset_depth = msgp->u.writesubsetReq.subset_depth;
    subset_head.nlink = msgp->u.writesubsetReq.nlink;

    SKYFS_ERROR("__skyfs_SS_write_subset:enter,dir_id:%d, subset_id:%d\n",
        dir_id, subset_id);

    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id &(SKYFS_MAX_SUBSET_PER_DIR -1));
    }


    fd = open(subset_file_name, O_WRONLY);
    if(fd > 0){
        if(write(fd, &subset_head, sizeof(skyfs_M_subset_head_t)) < 0){
            SKYFS_ERROR("__skyfs_SS_write_subset:write subset error,errno:%d\n", errno);
            rc = -errno;
            goto ERR;
        }

        close(fd);

    }else{
        SKYFS_ERROR("__skyfs_SS_write_subset:can't open %s,errno:%d\n", 
            subset_file_name, errno);
        rc = -errno;
        goto ERR;
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_SS_write_subset:send failed.rc:%d\n", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);


    SKYFS_LEAVE("__skyfs_SS_write_subset:leave.sp_depth:%d,sb_depth:%d\n\n",
        subset_head.split_depth, subset_head.subset_depth);
}

void __skyfs_SS_enlarge_subset(amp_request_t *req)
{
    skyfs_msg_t           *msgp = NULL;
    skyfs_u32_t           dir_id;
    skyfs_u32_t           subset_id;
    skyfs_s8_t            subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t            tmp_subset_file[SKYFS_MAX_NAME_LEN];
    skyfs_M_subset_head_t subset_head;
    skyfs_s8_t            *tmp = NULL;
    skyfs_s8_t            *block = NULL;
    skyfs_u32_t           req_size;

    skyfs_M_bmeta_t     *bmeta = NULL, *bmeta_tmp1 = NULL, *bmeta_tmp2 = NULL;
    skyfs_M_cmeta_t     *cmeta = NULL;
    skyfs_u32_t         nr_in_block, nr_in_tmp1, nr_in_tmp2;
    skyfs_u32_t         block_num;
    skyfs_u64_t         judge_value;
    skyfs_u32_t         i, j;
    skyfs_s32_t         tmp_fd = 0;
    skyfs_s32_t         fd = 0;
    skyfs_s32_t         rc = 0;
    int notype = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.enlargesubsetReq.dir_id;
    subset_id = msgp->u.enlargesubsetReq.subset_id;

    SKYFS_ERROR_1("__skyfs_SS_enlarge_subset:enter.dir_id:%d,subset_id:%d\n",
        dir_id, subset_id);    

    if(subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id &(SKYFS_MAX_SUBSET_PER_DIR-1));
    }

    fd = open(subset_file_name, O_RDWR);
    if(fd <= 0){
        rc = -1;    
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:can not open %s\n", subset_file_name);
        goto ERR;
    }

    sprintf(tmp_subset_file, "%s%u-%d-tmp", SKYFS_META_PATH, dir_id, subset_id);
    tmp_fd = open(tmp_subset_file, O_RDWR|O_CREAT, 0666);
    if(tmp_fd <= 0){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:can not open %s\n", tmp_subset_file);
        goto ERR;
    }

    if(read(fd,    &subset_head, sizeof(skyfs_M_subset_head_t)) < 0){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:can't read %s head\n", subset_file_name);
        goto ERR;
    }

    block_num = (skyfs_u32_t)1 << subset_head.subset_depth;
    SKYFS_MSG("__skyfs_SS_enlarge_subset:subset_depth:%d\n", subset_head.subset_depth);

    subset_head.subset_depth ++;
    if(write(tmp_fd, &subset_head, sizeof(skyfs_M_subset_head_t)) < 0){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:write %s head failed\n", tmp_subset_file);
        goto ERR;
    }

    tmp = (skyfs_s8_t *)malloc(SKYFS_BMETA_SIZE * 2 );
    block = (skyfs_s8_t *)malloc(SKYFS_BMETA_SIZE);

    for(i = 0; i < block_num; i ++){
        bzero(tmp, SKYFS_BMETA_SIZE * 2);
        bzero(block, SKYFS_BMETA_SIZE);
        if(read(fd, block, SKYFS_BMETA_SIZE) < 0){
            rc = -1;
            SKYFS_ERROR("__skyfs_SS_enlarge_subset:read bmeta:%d failed\n", i);
            goto ERR;
        }

        bmeta = (skyfs_M_bmeta_t *)(block);
        if(bmeta->hashvalue == 0){
            SKYFS_ERROR_1("__skyfs_SS_enlarge_subset:%d empty, do nothing\n", i);
        #if 0
        /*just split it, no mater how small the bmeta is*/
        }else if(bmeta->nfree > SKYFS_MAX_META_PER_BOX / 2){
            SKYFS_MSG("__skyfs_SS_enlarge_subset:%d less than split_range\n", i);
            memcpy(tmp, block, SKYFS_BMETA_SIZE);
            bmeta_tmp1 = (skyfs_M_bmeta_t *)(tmp);
            bmeta_tmp2 = (skyfs_M_bmeta_t *)((skyfs_s8_t *)tmp + SKYFS_BMETA_SIZE);

            bmeta_tmp1->box_id = bmeta->box_id * 2;
            bmeta_tmp1->hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_tmp1->box_id);
            
            bmeta_tmp2->last_one = i * 2;
        #endif
        }else{
            judge_value = (2 * i + 1) * SKYFS_MAX_HASH_VALUE / (block_num * 2);
            SKYFS_ERROR_1("__skyfs_SS_larsub:split:bmeta:%d,judge:%lld,nfree:%d\n", 
                i, judge_value, bmeta->nfree);
            bmeta_tmp1 = (skyfs_M_bmeta_t *)(tmp);
            bmeta_tmp2 = (skyfs_M_bmeta_t *)((skyfs_s8_t *)tmp + SKYFS_BMETA_SIZE);
            nr_in_block = 0;
            nr_in_tmp1 = nr_in_tmp2 = 0;
            for(nr_in_block = 0; nr_in_block < SKYFS_MAX_META_PER_BOX; nr_in_block ++){
                cmeta = &(bmeta->cmetap[nr_in_block]);
                if(cmeta->type){
                    if((cmeta->hashkey % SKYFS_MAX_HASH_VALUE) < judge_value){
                        memcpy(&(bmeta_tmp1->cmetap[nr_in_tmp1]), 
                            cmeta, sizeof(skyfs_M_cmeta_t));
			/* add by mayl */
			bmeta_tmp1->mmetap[nr_in_tmp1].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        //nr_in_tmp1 ++;

                        nr_in_tmp1 ++;
                    }else{
                        memcpy(&(bmeta_tmp2->cmetap[nr_in_tmp2]),
                            cmeta, sizeof(skyfs_M_cmeta_t));

			/* add by mayl */
			bmeta_tmp2->mmetap[nr_in_tmp2].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        nr_in_tmp2 ++;
                    }
                }else{
			notype++;
		}
            }

            bmeta_tmp1->nfree = SKYFS_MAX_META_PER_BOX - nr_in_tmp1;
            bmeta_tmp1->firstfree = nr_in_tmp1;
            bmeta_tmp1->box_id = bmeta->box_id * 2;
            bmeta_tmp1->hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_tmp1->box_id);
            bmeta_tmp2->nfree = SKYFS_MAX_META_PER_BOX - nr_in_tmp2;
            bmeta_tmp2->firstfree = nr_in_tmp2;
            bmeta_tmp2->box_id = bmeta->box_id * 2 + 1;
            bmeta_tmp2->hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_tmp2->box_id);
        
            SKYFS_ERROR_1("__skyfs_SS_larsub::b1:%d,nfree:%d;b2:%d,nfree:%d , notype %d\n", 
                bmeta_tmp1->box_id, bmeta_tmp1->nfree, 
                bmeta_tmp2->box_id, bmeta_tmp2->nfree, notype);

            for(j = nr_in_tmp1; j < SKYFS_MAX_META_PER_BOX; j ++){
                cmeta = &(bmeta_tmp1->cmetap[j]);
                cmeta->nextfree = j + 1;
            }

            for(j = nr_in_tmp2; j < SKYFS_MAX_META_PER_BOX; j ++){
                cmeta = &(bmeta_tmp2->cmetap[j]);
                cmeta->nextfree = j + 1;
            }

        }

        if(write(tmp_fd, tmp, SKYFS_BMETA_SIZE * 2) < 0){
            rc = -1;
            SKYFS_ERROR("__skyfs_SS_enlarge_subset:write %s error\n", tmp_subset_file);
            goto ERR;
        }
    }

    if(tmp_fd){
        close(tmp_fd);
        tmp_fd = 0;
    }

    if(fd){
        close(fd);
        fd = 0;
    }

    if(unlink(subset_file_name) < 0){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:can't unlink %s\n", subset_file_name);
        goto ERR;
    }

    if(rename(tmp_subset_file, subset_file_name) < 0){
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:can not rename %s to %s.\n",
                tmp_subset_file, subset_file_name);
        goto ERR;
    }
                        
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(tmp_fd){
        close(tmp_fd);
    }

    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:send failed.rc:%d\n", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    if(block){
        free(block);
    }

    if(tmp){
        free(tmp);
    }

    SKYFS_LEAVE("__skyfs_SS_enlarge_subset:leave.subset_depth:%d\n\n", 
        subset_head.subset_depth);

    if(subset_head.subset_depth > SKYFS_MAX_SUBSET_DEPTH)   {
        SKYFS_ERROR("__skyfs_SS_enlarge_subset:subset_depth too large:%d\n",
            subset_head.subset_depth);
           exit(1);
    }
}

void __skyfs_SS_split_subset(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_request_t       **reqs = NULL;
    skyfs_u32_t         dir_id;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         ori_subset_id;
    skyfs_u32_t         split_depth;
    skyfs_u32_t         subset_depth;
    skyfs_u32_t         new_sid1;
    skyfs_u32_t         new_sid2;
    skyfs_u32_t         entry_sid;
    skyfs_u32_t         new_osdid;
    skyfs_M_subset_head_t subset_head;
    skyfs_M_subset_head_t new_shead1;
    skyfs_M_subset_head_t new_shead2;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_bmeta_t     *bmeta_tmp1 = NULL;
    skyfs_M_bmeta_t     *bmeta_tmp2 = NULL;
    skyfs_M_cmeta_t     *cmeta = NULL;
    skyfs_s8_t          subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          new_subset1[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          new_subset2[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          *block = NULL;
    skyfs_s8_t          *tmp1 = NULL, *tmp2 = NULL;
    skyfs_u32_t         req_size;
    skyfs_u32_t         block_num;
    skyfs_u32_t         i=0, j=0;
    skyfs_u32_t         nr_in_block = 0, nr_in_tmp1 = 0, nr_in_tmp2 = 0;
    skyfs_s32_t         fd = 0;
    skyfs_s32_t         new_fd1 = 0;
    skyfs_s32_t         new_fd2 = 0;
    skyfs_s32_t         offset = 0;
    skyfs_s32_t         rc = 0;

    int notype = 0;
    int hastype = 0;
    int wrong_hash = 0;
    int total_scan = 0;
    int rename_type = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.splitsubsetReq.dir_id;
    ori_subset_id = msgp->u.splitsubsetReq.subset_id;
    split_depth = msgp->u.splitsubsetReq.split_depth;
    subset_depth = msgp->u.splitsubsetReq.subset_depth;

    SKYFS_ERROR_1("__skyfs_sp_subset:enter,d_id:%u,s_id:%d,sp_depth:%d,sb_depth:%d\n", 
        dir_id, ori_subset_id, split_depth, subset_depth);
    /* add edded by mayl for rename */
    subset_id = ori_subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1);
    
    

    block = malloc(sizeof(skyfs_M_bmeta_t));
    if(block == NULL){
        SKYFS_ERROR("__skyfs_SS_split_subset:alloc blk error\n");
    }

    /*1,determine the 2rd subsetID and new osdID*/
    new_sid1 = ori_subset_id;    
    new_sid2 = ori_subset_id + (skyfs_u32_t)(1 << split_depth);

    new_osdid = __skyfs_SS_judge_osdid_rename(dir_id, new_sid2);

    SKYFS_ERROR("__skyfs_SS_split_subset:new_sid1:%d,new_sid2:%d,new_osdid:%d,id:%d\n",
        new_sid1, new_sid2, new_osdid, req->req_remote_id);

    /*2,init the two subset head*/
    if(ori_subset_id < SKYFS_MAX_SUBSET_PER_DIR){
    	sprintf(subset_file_name, "%s%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }else{
	/* add by mayl for rename */
    	sprintf(subset_file_name, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, subset_id);
    }
    fd = open(subset_file_name, O_RDONLY);
    if(fd > 0 ){
        if(read(fd, &subset_head, sizeof(skyfs_M_subset_head_t)) < 0){
            rc = -errno;
            SKYFS_ERROR("__skyfs_SS_split_subset:read %s head fail\n", subset_file_name);
            goto ERR;
        }
    }else{
        rc = -errno;    
        SKYFS_ERROR("__skyfs_SS_split_subset:can not open %s\n", subset_file_name);
        goto ERR;
    }

    split_depth ++;

    new_shead1.dir_id = dir_id;
    new_shead1.subset_id = new_sid1;
    new_shead1.split_depth = split_depth;
    new_shead1.subset_depth = subset_depth;
    new_shead1.nlink = 0;
    
    new_shead2.dir_id = dir_id;
    new_shead2.subset_id = new_sid2;
    new_shead2.split_depth = split_depth;
    new_shead2.subset_depth = subset_depth;
    new_shead2.nlink = 0;

    sprintf(new_subset1, "%s%u-%d-tmp", SKYFS_META_PATH, dir_id, new_sid1);
    new_fd1 = open(new_subset1, O_RDWR|O_CREAT, 0666);
    if(new_fd1 > 0){
        SKYFS_MSG("__skyfs_SS_split_subset:create %s\n", new_subset1);
        if(write(new_fd1, &new_shead1, sizeof(skyfs_M_subset_head_t)) < 0){
            rc = -errno;
            SKYFS_ERROR("__skyfs_SS_split_subset:w %s head fail\n", new_subset1);
            goto ERR;
        }
    }else{
        rc = -errno;    
        SKYFS_ERROR("__skyfs_SS_split_subset:can not open %s\n", new_subset1);
        goto ERR;
    }

    if(new_osdid == osd_this_id){
    	if(ori_subset_id < SKYFS_MAX_SUBSET_PER_DIR){
        	sprintf(new_subset2, "%s%u-%d", SKYFS_META_PATH, dir_id, new_sid2);
	}else{
		/* mayl add for rename */
        	sprintf(new_subset2, "%s/rn_%u-%d", SKYFS_META_PATH, dir_id, new_sid2&(SKYFS_MAX_SUBSET_PER_DIR -1));
	}
        new_fd2 = open(new_subset2, O_RDWR|O_CREAT, 0666);
        if(new_fd2 > 0){
            SKYFS_MSG("__skyfs_SS_split_subset:create %s\n", new_subset2);
            if(write(new_fd2, &new_shead2, sizeof(skyfs_M_subset_head_t)) < 0){
                rc = -errno;
                SKYFS_ERROR("__skyfs_SS_split_subset:w %s head fail\n", new_subset2);
                goto ERR;
            }
        }else{
            rc = -errno;    
            SKYFS_ERROR("__skyfs_SS_split_subset:can not open %s\n", new_subset2);
            goto ERR;
        }
    }else{
        /*create subset_file at new_osdid*/
        SKYFS_ERROR("__skyfs_SS_split_subset:the 2rd osd is %d\n", new_osdid);
        rc = __skyfs_O2O_create_subset_file(new_osdid, &new_shead2);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_SS_split_subset:error:create sub at %d failed\n",
                new_osdid);
            goto ERR;
        }
    }

    /*3,begin to split*/
    block_num = (skyfs_u32_t)1 << subset_depth;
    tmp1 = (skyfs_s8_t *)malloc(SKYFS_BMETA_SIZE);
    tmp2 = (skyfs_s8_t *)malloc(SKYFS_BMETA_SIZE);

    if(new_osdid != osd_this_id){
        reqs = malloc(sizeof(amp_request_t) * block_num);
    }

    for(i = 0; i < block_num; i ++){
        bzero(block, SKYFS_BMETA_SIZE);
        bzero(tmp1, SKYFS_BMETA_SIZE);
        bzero(tmp2, SKYFS_BMETA_SIZE);
        offset = i * SKYFS_BMETA_SIZE + sizeof(skyfs_M_subset_head_t);
        if(pread(fd, block, SKYFS_BMETA_SIZE, offset) < 0){
            rc = -errno;
            SKYFS_ERROR("__split_subset:error:read %s bmeta:%d off:%d.rc:%d,fd:%d\n", 
                subset_file_name, i, offset, rc, fd);
            exit(1);
            goto ERR;
        }
        bmeta = (skyfs_M_bmeta_t *)(block);
        bmeta_tmp1 = (skyfs_M_bmeta_t *)tmp1;
        bmeta_tmp2 = (skyfs_M_bmeta_t *)tmp2;
        nr_in_tmp1 = nr_in_tmp2 = 0;

        if(bmeta->hashvalue == 0){
            SKYFS_ERROR_1("__skyfs_SS_split_subset:bmeta:%d empty\n", i);
        }else{
            SKYFS_ERROR_1("__skyfs_SS_split_subset:split bmeta:%d,nfree:%d\n", 
                i, bmeta->nfree);
	    notype = 0;
	    hastype = 0;
	    total_scan = 0;
	    wrong_hash = 0;
            for(nr_in_block = 0; nr_in_block < SKYFS_MAX_META_PER_BOX; nr_in_block ++){
                cmeta = &(bmeta->cmetap[nr_in_block]);
		total_scan++;
		if(!(cmeta->type)){
			notype++;
		}
		//if(cmeta->type == SKYFS_RENAME){
		if(0){
			// TODO mayl will use the ccde below in  other fuction
#if 0
			hastype++;
			rename_type++;
                    	entry_sid = __skyfs_get_subset_id(cmeta->hashkey, split_depth);
		    	if(entry_sid < ((skyfs_u32_t)(1 << split_depth))){
				memcpy(&(bmeta_tmp1->cmetap[nr_in_tmp1]),
                            		cmeta, sizeof(skyfs_M_cmeta_t));
				/* add by mayl */
				bmeta_tmp1->mmetap[nr_in_tmp1].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        	nr_in_tmp1 ++;

			}else{
				memcpy(&(bmeta_tmp2->cmetap[nr_in_tmp2]),
                            		cmeta, sizeof(skyfs_M_cmeta_t));

				/* add by mayl */
				bmeta_tmp2->mmetap[nr_in_tmp2].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        		nr_in_tmp2 ++;
			
			}
#endif	

		}else if(cmeta->type){
		    hastype ++;
                    entry_sid = __skyfs_get_subset_id(cmeta->hashkey, split_depth);    
                    if(entry_sid == new_sid1 & (SKYFS_MAX_SUBSET_PER_DIR -1)){
                        memcpy(&(bmeta_tmp1->cmetap[nr_in_tmp1]),
                            cmeta, sizeof(skyfs_M_cmeta_t));
			/* add by mayl */
			bmeta_tmp1->mmetap[nr_in_tmp1].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        nr_in_tmp1 ++;
                    }else if(entry_sid == new_sid2 &(SKYFS_MAX_SUBSET_PER_DIR -1)){
                        memcpy(&(bmeta_tmp2->cmetap[nr_in_tmp2]),
                            cmeta, sizeof(skyfs_M_cmeta_t));

			/* add by mayl */
			bmeta_tmp2->mmetap[nr_in_tmp2].lock_htb_head =  bmeta->mmetap[nr_in_block].lock_htb_head;
                        nr_in_tmp2 ++;
                    }else{
			wrong_hash++;
                        SKYFS_ERROR_1("__split_sub:error,sid:%d,bid:%d,depth:%d,fd:%d,type:%u\n", 
                            entry_sid, bmeta->box_id, split_depth, fd, cmeta->type);
                        SKYFS_ERROR("__split_sub:cmeta:%s,bmeta_id:%d,nr_in_block:%d\n",
                            cmeta->name, i, nr_in_block);
                        {
                            close(fd);
                            fd = open(subset_file_name, O_RDWR);
                            if(pread(fd, block, SKYFS_BMETA_SIZE, offset) < 0){
                                SKYFS_ERROR("__split_sub:read error agin\n");
                                exit(1);
                            }
                            bmeta = (skyfs_M_bmeta_t *)(block);
                            SKYFS_ERROR("__split_sub:cmetafirst:%s,hash:%lld\n",
                                bmeta->cmetap[0].name, bmeta->cmetap[0].hashkey);
                        }
                        exit(1);
                        goto ERR;
                    }
                }
            }


            bmeta_tmp1->nfree = SKYFS_MAX_META_PER_BOX - nr_in_tmp1;
	    SKYFS_ERROR_1("bmeta1 nfree %d\n",bmeta_tmp1->nfree);
            bmeta_tmp1->firstfree = nr_in_tmp1;
            bmeta_tmp1->box_id = i;
            bmeta_tmp1->hashvalue = __skyfs_get_bmeta_hashvalue(i);
            bmeta_tmp2->nfree = SKYFS_MAX_META_PER_BOX - nr_in_tmp2;
	    SKYFS_ERROR_1("bmeta2 nfree %d\n",bmeta_tmp2->nfree);
	    //if(bmeta_tmp1->nfree  == 512 && bmeta_tmp2->nfree == 512){
	    //notype = 0;
            bmeta_tmp2->firstfree = nr_in_tmp2;
            bmeta_tmp2->box_id = i;
            bmeta_tmp2->hashvalue = __skyfs_get_bmeta_hashvalue(i);
	    
	    if(1) // added by mayl
	    {
		    SKYFS_ERROR_1(" before set firsttime, split bmeta id %d , notype %d, hastype %d , total_scan %d, wrong_hash %d, rename type %d\n", 
				    i, notype, hastype, total_scan, wrong_hash, rename_type);
	    }

	    /* added by mayl */
	    bmeta_tmp1->first_time = bmeta->first_time;
	    bmeta_tmp2->first_time = bmeta->first_time;
	    gettimeofday(&bmeta_tmp1->last_time, NULL);
	    gettimeofday(&bmeta_tmp2->last_time, NULL);
	
	    if(1) // added by mayl
	    {
		    SKYFS_ERROR_1(" after set firsttime, split bmeta id %d , n_free_1 %d, nfree_2 %d\n", 
				    i , bmeta_tmp1->nfree,bmeta_tmp2->nfree );
	    }


	    /* added by mayl */
    
            for(j = nr_in_tmp1; j < SKYFS_MAX_META_PER_BOX; j ++){
                cmeta = &(bmeta_tmp1->cmetap[j]);
                cmeta->nextfree = j + 1;
            }

            for(j = nr_in_tmp2; j < SKYFS_MAX_META_PER_BOX; j ++){
                cmeta = &(bmeta_tmp2->cmetap[j]);
                cmeta->nextfree = j + 1;
            }
        }
        
        if(write(new_fd1, tmp1, SKYFS_BMETA_SIZE) < 0){
            rc = -errno;
            SKYFS_ERROR("__skyfs_SS_split_subset:error:write %d error\n", new_fd1);
            goto ERR;
        }

        if(new_osdid == osd_this_id){
            if(write(new_fd2, tmp2, SKYFS_BMETA_SIZE) < 0){
                rc = -errno;
                SKYFS_ERROR("__skyfs_SS_split_subset:error:write %d error\n", new_fd2);
                goto ERR;
            }
        }else{
            /*send new bmeta to the osd*/
            SKYFS_ERROR("__skyfs_SS_split_subset:write bmeta:%d to osd:%d\n",
                i, new_osdid);
            reqs[i] = __skyfs_O2O_write_bmeta(new_osdid, dir_id, new_sid2, i, 
                    (skyfs_M_bmeta_t *)tmp2);
            if(reqs[i] == NULL){
                SKYFS_ERROR("__skyfs_SS_split_subset:error:write bmeta to %d failed\n", 
                    new_osdid);
            }
        }
    }

    if(new_osdid != osd_this_id){
        for(i = 0; i < block_num; i ++){
            amp_sem_down(&(reqs[i]->req_waitsem));
            free(reqs[i]->req_iov->ak_addr);
        }

        for(i = 0; i < block_num; i ++){
            if(reqs[i]->req_msg){
                    amp_free(reqs[i]->req_msg, reqs[i]->req_msglen);
            }
            if(reqs[i]->req_reply){
                amp_free(reqs[i]->req_msg, reqs[i]->req_msglen);
            }
    
            if(reqs[i]->req_iov){
                amp_free(reqs[i]->req_iov, sizeof(amp_kiov_t));
            }
            if(reqs[i]){
                   __amp_free_request(reqs[i]);
            }
        }

        if(reqs){
            free(reqs);
        }
    }

    if(fd){
        close(fd);
        fd = 0;
    }

    if(new_fd1){
        close(new_fd1);
        new_fd1 = 0;
    }

    if(new_fd2){
        close(new_fd2);
        new_fd2 = 0;
    }

    if(unlink(subset_file_name) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_SS_split_subset:error:can't unlink %s\n", subset_file_name);
        goto ERR;
    }

    if(rename(new_subset1, subset_file_name) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_SS_split_subset:error:can't rename %s to %s.\n",
            new_subset1, subset_file_name);
        goto ERR;
    }
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(new_fd1){
        close(new_fd1);
    }

    if(new_fd2){
        close(new_fd2);
    }

    if(fd){
        close(fd);
    }


    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_SS_split_subset:send failed.rc:%d\n", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    if(tmp1){
        free(tmp1);
    }

    if(tmp2){
        free(tmp2);
    }

    if(block){
        free(block);
    }

    SKYFS_ERROR("__skyfs_SS_split_subset:leave.i:%d\n\n", i);

    if(split_depth > SKYFS_MAX_DIR_DEPTH){
        SKYFS_ERROR("__skyfs_SS_split_subset:split_depth ex:%d\n", split_depth);
        exit(1);
    }
}

void __skyfs_SS_create_subset(amp_request_t *req)
{
    skyfs_msg_t           *msgp = NULL;
    skyfs_s8_t            subset_file_name[SKYFS_MAX_NAME_LEN];
    /* add rename subset file*/
    skyfs_s8_t            rn_subset_file_name[SKYFS_MAX_NAME_LEN];
    skyfs_M_subset_head_t subset_head;
    skyfs_u32_t           req_size;
    skyfs_s32_t           fd;
    //skyfs_s32_t           fd_rn;
    skyfs_s32_t           is_init_dir = 0;
    skyfs_s32_t           rc = 0;

    SKYFS_ERROR("__skyfs_SS_create_subset_file:start.rc:%d\n", rc);
    msgp = __skyfs_get_msg(req->req_msg);
    subset_head.dir_id = msgp->u.createsubsetReq.dir_id;
    subset_head.subset_id = msgp->u.createsubsetReq.subset_id;
    subset_head.split_depth = msgp->u.createsubsetReq.split_depth;
    subset_head.subset_depth = msgp->u.createsubsetReq.subset_depth;
    subset_head.nlink = msgp->u.createsubsetReq.nlink;
    
    
    SKYFS_ERROR("__skyfs_SS_create_subset:enter,dir_id:%d,subset_id:%d\n\n",
        subset_head.dir_id, subset_head.subset_id);

    sprintf(subset_file_name, "%s%u-%d", 
        SKYFS_META_PATH, 
        subset_head.dir_id, 
        subset_head.subset_id);

    if(msgp->fromType == SKYFS_MDS && subset_head.subset_id == 0 
		    && subset_head.split_depth == 0 && subset_head.subset_depth == 0 ){
	/*add ed by mayl, add the first rename subset */
	memset(rn_subset_file_name,0,SKYFS_MAX_NAME_LEN);
	sprintf(rn_subset_file_name, "%s/rn_%u-%d", 
        SKYFS_META_PATH, 
        subset_head.dir_id, 
        subset_head.subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1) );
	is_init_dir = 1;
	fd = open(rn_subset_file_name, O_RDWR|O_CREAT, 0666);
    	if(fd > 0){
        	write(fd, &subset_head, sizeof(skyfs_M_subset_head_t));
        	SKYFS_ERROR("__skyfs_SS_create_init_rename_subset:create subset file %s 0 succ\n", 
                	subset_file_name);
        	close(fd);
        	fd = 0;
    	}else{
        	rc = -1;
        	SKYFS_ERROR_1("__skyfs_SS_create_init_rename_subset:can not create %s\n", rn_subset_file_name);
        	goto ERR;
    	}


    }
    if(subset_head.subset_id >= SKYFS_MAX_SUBSET_PER_DIR){
	/* add by mayl if subset_id > max subset , means this is a rename_subset*/
	memset(subset_file_name,0,SKYFS_MAX_NAME_LEN);
	sprintf(subset_file_name, "%s/rn_%u-%d", 
        	SKYFS_META_PATH, 
        	subset_head.dir_id, 
        	subset_head.subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1) );

    }
    
    fd = open(subset_file_name, O_RDWR|O_CREAT, 0666);
    if(fd > 0){
        write(fd, &subset_head, sizeof(skyfs_M_subset_head_t));
        SKYFS_MSG("__skyfs_SS_create_subset:create subset file %s 0 succ\n", 
                subset_file_name);
        close(fd);
        fd = 0;
    }else{
        rc = -1;
        SKYFS_ERROR("__skyfs_SS_create_subset:can not create %s\n", subset_file_name);
        goto ERR;
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_createsubset_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    
    rc = amp_send_sync(osd_comp_context,
            req,
            req->req_remote_type,
            req->req_remote_id,
            0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_create_subset_file:send reply failed.rc:%d", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    } 
   
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_SS_create_subset_file:exit:\n\n");

}

/*This is end of osd_mop.c*/
