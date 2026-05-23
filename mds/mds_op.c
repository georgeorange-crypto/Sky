/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_op.c $
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
#include "mds_op.h"
#include "mds_help.h"
#include "mds_cache.h"
#include "mds_meta.h"
#include "mds_flock.h"
#include "mds_state.h"
#include "mds_layout.h"

skyfs_disk_sb_t        tmp_sb;
skyfs_timespec_t       last_stat_time; 
pthread_mutex_t        skyfs_statfs_lock;
static int create_cnt = 0;
extern int alloc_meta_cnt ;
extern int free_meta_cnt ;

//extern void convert_flock_arg_to_fl(skyfs_m_flock_args_t * req_arg, skyfs_M_flock_t * fl);

skyfs_s32_t __skyfs_MS_do_statfs(skyfs_disk_sb_t *sb)
{
    skyfs_u32_t   req_num = osd_info.osd_num; 
    amp_request_t *req[req_num];
    skyfs_msg_t   *msgp[req_num];
    skyfs_msg_t   *msg = NULL;
    skyfs_u32_t   msgsize;

    skyfs_u32_t i,j;
    skyfs_s32_t rc = 0;

	i = 0;
   
    SKYFS_MSG("__skyfs_MS_do_statfs:enter.\n");
    memset(req, 0, req_num * sizeof(amp_request_t *));
    memset(msgp, 0, req_num * sizeof(amp_message_t *));

    gettimeofday(&last_stat_time, NULL);
    msgsize = AMP_SKYFS_MSGHEAD_SIZE;
    for(j = 0; j < SKYFS_MAX_OSD_NUM; j ++){
        if(osd_info.osd[j].id > 0){
            rc = __skyfs_MS_init_req(&req[i], 
                &msgp[i], 
                SKYFS_MSG_O_GET_DEVINFO, 
                SKYFS_NEED_ACK, 
                AMP_REQUEST|AMP_MSG,
                msgsize);    
            SKYFS_MSG("__skyfs_MS_do_statfs:send to osd_id:%d\n",
                osd_info.osd[j].id);
            rc = amp_send_async(mds_comp_context, 
                req[i],
                SKYFS_OSD,
                osd_info.osd[j].id, 
                0);
            if(rc < 0) {
                SKYFS_ERROR("__skyfs_MS_do_statfs:send request failed.rc:%d\n",rc);
                goto EXIT;
            }
            SKYFS_MSG("__skyfs_MS_do_statfs:send to osd %d succeed.\n", j);
			i ++;
        }
    }

    SKYFS_ERROR_1("__skyfs_MS_do_statfs:begain to down:%d\n",req_num);
    for(i = 0; i < req_num; i++){
        amp_sem_down(&(req[i]->req_waitsem));
    }
    SKYFS_ERROR_1("__skyfs_MS_do_statfs:after down\n");

    /*Step 3: alloc superblock and fill it */
    memset(sb, 0, sizeof(skyfs_disk_sb_t));
    sb->magic = SKYFS_MSG_MAGIC;
    sb->fsid =  SKYFS_FSID;

    for(i = 0; i < req_num; i++){
        msg = __skyfs_get_msg(req[i]->req_reply);
		if(msg && msg->error >= 0){
        	sb->blocksize  = msg->u.getDevinfoAck.cap.bsize;
        	sb->blocks = sb->blocks + msg->u.getDevinfoAck.cap.blocks;
        	sb->bfree  = sb->bfree + msg->u.getDevinfoAck.cap.bfree;
        	sb->bavail = sb->bavail + msg->u.getDevinfoAck.cap.bavail;
        	sb->inodes = sb->inodes + msg->u.getDevinfoAck.cap.files;
        	sb->ifree  = sb->ifree + msg->u.getDevinfoAck.cap.ffree;
        	sb->ifavail= sb->ifavail + msg->u.getDevinfoAck.cap.favail;
		}
    }

    sb->size = sb->blocksize * sb->blocks;
    sb->blocksize_bits = SKYFS_DATA_BLK_SIZE_BITS;
    memcpy(&tmp_sb, sb, sizeof(skyfs_disk_sb_t));

EXIT:
    /*Step 4: free unused objs */
    //if(req){
        for (i = 0; i< req_num; i++) {
            if (req[i]->req_msg){
                SKYFS_MSG("__skyfs_MS_do_statfs:before free reply,replylen:%d\n", 
                        req[i]->req_msglen);
                amp_free(req[i]->req_msg, req[i]->req_msglen);
            }
            if (req[i]->req_reply){
                SKYFS_MSG("__skyfs_MS_do_statfs:before free reply,replylen:%d\n", 
                        req[i]->req_replylen);
                amp_free(req[i]->req_reply, req[i]->req_replylen);
            }
            if (req[i]){
                SKYFS_MSG("__skyfs_MS_do_statfs:before free req\n");
                __amp_free_request(req[i]);
            }
        }
    //}

    SKYFS_MSG("__skyfs_MS_do_statfs:exit.\n");
 
    return rc;
}


skyfs_s32_t __skyfs_MS_get_root(skyfs_meta_t *root_meta)
{
    skyfs_M_dir_cache_t    dir_cache;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        rc = 0;

    SKYFS_MSG("__skyfs_MS_get_root:enter.\n");
    
    dir_id = __skyfs_MS_get_dir_id(SKYFS_ROOT_INO, 0);

    rc = __skyfs_MS_get_dir_cache(&dir_cache, dir_id);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_get_root:get root error\n");
        goto ERR;
    }
    
    __skyfs_MS_copy_meta(root_meta, &(dir_cache.cmeta));

#if 0
    dir_htbp = &skyfs_dir_cache_htbbase[0];
    pthread_mutex_lock(&(dir_htbp->lock));

    head = &(dir_htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_get_root:dir hash entry empty.\n");
        pthread_mutex_unlock(&(dir_htbp->lock));
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_dir_cache_t, dir_hash);
        if(tmp->cmeta.ino == SKYFS_ROOT_INO){
            root_dir = tmp;
        }
    }

    if(root_dir == NULL){
        pthread_mutex_unlock(&(dir_htbp->lock));
        goto ERR;
    }

    pthread_mutex_lock(&(root_dir->lock));
    pthread_mutex_unlock(&(dir_htbp->lock));

    __skyfs_MS_copy_meta(root_meta, &(root_dir->cmeta));    
    pthread_mutex_unlock(&(root_dir->lock));
    
#endif
ERR:

    SKYFS_MSG("__skyfs_MS_get_root:exit.\n");
    return rc;
}

void __skyfs_MS_statfs(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;

    skyfs_disk_sb_t     sb;
    skyfs_meta_t        root_meta;
    skyfs_timespec_t    current_time;

    skyfs_s32_t         rc = 0;
    skyfs_s32_t         rp = 0;

    skyfs_s32_t         size = 0;

    SKYFS_ENTER("__skyfs_MS_statfs:enter\n");

    pthread_mutex_lock(&skyfs_statfs_lock);
    gettimeofday(&current_time, NULL);
    if((current_time.tv_sec - last_stat_time.tv_sec) < 20){
        memcpy(&sb, &tmp_sb, sizeof(skyfs_disk_sb_t));
    }else{
        rc = __skyfs_MS_do_statfs(&sb);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_statfs:get OSD info failed.\n");
            goto ERR;
        }
    }

    rc = __skyfs_MS_get_root(&root_meta);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_statfs:get root inode faile.\n");
        goto ERR;
    }


ERR:

    pthread_mutex_unlock(&skyfs_statfs_lock);

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_statfs_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rp < 0){
        SKYFS_ERROR("__skyfs_MS_statfs:init reply failed.\n");
        goto ERR;
    }


    if(rp >= 0){
        memcpy(&(msgp->u.statfsAck.sb), &sb, sizeof(skyfs_disk_sb_t));
        memcpy(&(msgp->u.statfsAck.root_inode), &root_meta, sizeof(skyfs_meta_t));
    }
    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
                    req, 
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_statfs:send reply failed.\n");
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    rc = __amp_free_request(req);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_statfs:free request failed.\n");
    }
    SKYFS_LEAVE("__skyfs_MS_statfs:exit.\n");

}

void __skyfs_MS_lookup(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    skyfs_u32_t         req_type;
    skyfs_u32_t         req_id;
   
    skyfs_ino_t         dir_ino;
    skyfs_s8_t          name[SKYFS_MAX_NAME_LEN + 1];
    skyfs_htb_t         *htbp = NULL;
    skyfs_u32_t         mds_id = 0;
    skyfs_u32_t         dir_id;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         bmeta_id;
    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_mmeta_t     *mmeta = NULL;
    skyfs_M_cmeta_t     *cmeta = NULL;

    skyfs_u32_t         tmp_subset_id;
    skyfs_u32_t         hashkey;
    skyfs_s32_t         find = -1;
    skyfs_s32_t         size;
    skyfs_s32_t         rc = 2;
    skyfs_u32_t 	display_cnt = 0;
    
    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;
    
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    msgp = __skyfs_get_msg(req->req_msg);
    dir_ino = msgp->u.lookupReq.dir_ino;
    memcpy(name, msgp->u.lookupReq.name, strlen(msgp->u.lookupReq.name));
    name[strlen(msgp->u.lookupReq.name)] = '\0';
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR("__skyfs_MS_lookup:enter:%llu, %s\n", dir_ino, name);
    hashkey = __skyfs_name2hashvalue(name);

relocate_subset:
    htbp = __skyfs_MS_locate_subset_by_name(dir_ino, name, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        if(mds_id != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_lookup:error:can not get subset %d in OSD\n",
			subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

     tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
     
    if(tmp_subset_id != subset_id){
	if(display_cnt<3){
        	SKYFS_ERROR("__skyfs_MS_lookup:subset splited while waiting:old:%d,new:%d, dir %d, sp_depth %d \n",
            		subset_id, tmp_subset_id, dir_id, subset_cache->split_depth);
		display_cnt++;
	}
	else{
		exit(1);
	}
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_subset;
    }

    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_lookup: get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_lookup:error:can not read bmeta in OSD\n");
            find = 0;
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_name(bmeta, name);    
    if(mmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_lookup:can not find:%s in subset:%d,bmeta:%d\n",
            name, subset_id, bmeta_id);
        SKYFS_MSG("__skyfs_MS_lookup:sp_depth:%d,sb_depth:%d,nfree:%d,firstfree:%d\n",
            subset_cache->split_depth, subset_cache->subset_depth,
            bmeta->nfree, bmeta->firstfree);
        find = 0;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_mutex_lock(&(mmeta->lock));
    cmeta = mmeta->cmetap;

    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_lookup:set bit error.\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));

        goto ERR;
    }

    SKYFS_MSG("__skyfs_MS_lookup:%s, mode:%d, type:%d, status:%d\n", 
		cmeta->name, cmeta->mode, cmeta->type, mmeta->status);
    if(S_ISDIR(cmeta->mode) && mmeta->status != 1){
	 /*TODO : mayl remove the one code line below */
        rc = __skyfs_MS_init_dir_cache(cmeta);
       	if(rc < 0){
        	SKYFS_ERROR("__skyfs_MS_lookup:init dir cache error");
            pthread_mutex_unlock(&(mmeta->lock));
            pthread_rwlock_unlock(&(bmeta->rwlock));
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
        mmeta->status = 1;
	}

    find = 2;
ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_lookup_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(find < 0){
        rc = 1;
    }else if(find == 0){
        rc = 0;
    }else if(find == 2){
        if(rc >= 0){
            __skyfs_MS_copy_meta(&msgp->u.lookupAck.meta, cmeta);
            pthread_mutex_unlock(&(mmeta->lock));
            pthread_rwlock_unlock(&(bmeta->rwlock));
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            rc = 2;
        }else{
            rc = 1;
        }
    }

    msgp->error = rc; 
    msgp->ver = __skyfs_MS_get_layoutv();

    SKYFS_MSG("__skyfs_MS_lookup:type:%d,id:%d,type:%d,id:%d.\n", 
        req->req_remote_type, req->req_remote_id, req_type, req_id);

    SKYFS_MSG("__skyfs_MS_lookup:sender_handle:%llu\n", 
        req->req_msg->amh_sender_handle);

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "lookup");
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req_type, 
             req_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_lookup:send reply failed.rc:%d\n", rc); 
    } 

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "lookup-net");


ERR_NONE:
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_lookup:exit\n\n");
}

/* helper function for searching renamed meta by ino . All related object will be locked when return -- added by mayl */
static skyfs_s32_t  __skyfs_MS_get_rename_meta_locked(skyfs_u32_t dir_id,  
	skyfs_M_subset_cache_t ** p_subset, skyfs_M_bmeta_t **p_bmeta, skyfs_M_mmeta_t **p_mmeta, 
	skyfs_htb_t** p_htbp, skyfs_ino_t ino, skyfs_s32_t iswr)
{

    skyfs_htb_t        *htbp = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_s32_t        err_num = 0;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_cmeta_t 	*cmeta_to = NULL;
    skyfs_M_mmeta_t    *mmeta_to = NULL;
    skyfs_u64_t 		mask = ((skyfs_u64_t)3) << 30;

    skyfs_s32_t 	rc = 0;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
	if(ino & mask == 0){
		rc = -ENOENT;
		//goto ERR;
		return rc;
	}
	/* step 1: get subset cache */
relocate_subset:
    /*Normal process*/
    subset_id = SKYFS_MAX_SUBSET_PER_DIR;
    htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, 0, 
              &mds_id, &dir_id, &subset_id);
    /* subset id only set const value SKYFS_MAX_SUBSET_PER_DIR */
    if(htbp == NULL){
        rc = -ENOENT;
        //TODO : only support one mds NOW -- mayl __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
	SKYFS_ERROR_1("get_renamed_meta_locked ,err_num %d\n", err_num);
		err_num = -1;
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        //__skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
		err_num = -2;
	SKYFS_ERROR_1("get_renamed_meta_locked ,err_num %d\n", err_num);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
		err_num = -3;
        pthread_rwlock_unlock(&(htbp->rwlock));
	SKYFS_ERROR_1("get_renamed_meta_locked ,err_num %d\n", err_num);
        goto ERR;
    }
	if(iswr){
    	pthread_rwlock_wrlock(&(subset_cache->rwlock));
	}else{
		pthread_rwlock_rdlock(&(subset_cache->rwlock));
	}

relocate_bmeta:
    /* TODO INPLEMENT the fuction below*/
    bmeta = __skyfs_MS_locate_rename_bmeta_by_ino(subset_cache, ino, 0, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_ERROR_1("__skyfs_MS_rename: get bmeta:%d\n", bmeta_id);
        bmeta  = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR_1("__skyfs_MS_rename:error:can not read bmeta in %d OSD \n", bmeta_id);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    //pthread_rwlock_wrlock(&(bmeta->rwlock));
    if(iswr){
    	pthread_rwlock_wrlock(&(bmeta->rwlock));
    }else{
	pthread_rwlock_rdlock(&(bmeta->rwlock));
    }
    pthread_mutex_unlock(&(bmeta->lock));
    
    mmeta_to = __skyfs_MS_locate_rename_mmeta_by_ino(bmeta,ino,0);
    SKYFS_ERROR("get renamed mmeta %p :", mmeta_to);
    if(mmeta_to == NULL)
	    rc = -ENOENT;
    else
	    rc = 0;
	
       

ERR:
    if(rc >= 0){
		pthread_mutex_lock(&(mmeta_to->lock));
		*p_bmeta = bmeta;
		*p_htbp = htbp;
		*p_subset = subset_cache;
		*p_mmeta = mmeta_to;
        //__skyfs_MS_copy_meta(&msgp->u.renameAck.meta, cmeta_to);
       // pthread_mutex_unlock(&(mmeta_to->lock));
        //pthread_rwlock_unlock(&(bmeta->rwlock));
        //pthread_rwlock_unlock(&(subset_cache->rwlock));
        //pthread_rwlock_unlock(&(htbp->rwlock));
        
        }else{
        	pthread_rwlock_unlock(&(bmeta->rwlock));
        	pthread_rwlock_unlock(&(subset_cache->rwlock));
        	pthread_rwlock_unlock(&(htbp->rwlock));
        }
		
ERR_NONE:
    if(rc <0)
    	SKYFS_ERROR_1("find renamed file meta in  rename_subset %d, bmeta_id %d , ino %llx, return %d\n ", subset_id, bmeta_id, ino,rc);
		return rc;

}


void __skyfs_MS_getattr(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;  
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;
   
    skyfs_ino_t        ino;
    skyfs_u32_t        conflict_index;
    skyfs_htb_t        *htbp = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        tmp_subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_u32_t        hashkey;
    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;
    int err_num = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    ino = msgp->u.getmetaReq.ino;
    conflict_index = msgp->u.getmetaReq.conflict_index;
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR("__skyfs_MS_getattr:enter:ino:%llu, conflict_index:%d\n",
        ino, conflict_index);
    
    /*Special case to read root meta*/
    if(ino == SKYFS_ROOT_INO){
        size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getmeta_ack_t);
        __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
        rc = __skyfs_MS_get_root(&msgp->u.getmetaAck.meta);
        msgp->error = rc;
        rc = amp_send_sync(mds_comp_context,
                   req,
                req_type,
                req_id,
                0);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_getattr:send reply failed.rc:%d\n", rc);
        }
        goto ERR_NONE;
    }

relocate_subset:
    /*Normal process*/
    htbp = __skyfs_MS_locate_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
	if(htbp == NULL){
		subset_id = SKYFS_MAX_SUBSET_PER_DIR;
		htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
		if(htbp != 	NULL){
			/* added by mayl , goto ERR, try to get the mmeta directly in rename subset */
			rc = -ENOENT;
			goto ERR;
		}
	}
    if(htbp == NULL){
        rc = -ENOENT;
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
		err_num = -1;
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
	err_num = -2;
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
	err_num = -3;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));
   
    hashkey = __skyfs_ino2hashvalue(ino, conflict_index);
    tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("__skyfs_MS_getattr:subset splited while waiting:old:%d,new:%d\n",
            subset_id, tmp_subset_id);
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_subset;
    }

    bmeta = __skyfs_MS_locate_bmeta_by_ino(subset_cache, ino, conflict_index, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_getattr:need get bmeta %d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id); 
        if(bmeta == NULL){
            rc = -ENOENT;
	    err_num = -4;
            SKYFS_ERROR("__skyfs_MS_getattr:error can't read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_ino(bmeta, ino, conflict_index);
    if(mmeta == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_MS_getattr:can not find mmeta in bmeta %d\n",
		    bmeta_id);
        SKYFS_ERROR("__skyfs_MS_getattr:error nfree:%d,firstfree:%d\n",
            bmeta->nfree, bmeta->firstfree);
	err_num = -5;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta->lock));
	SKYFS_ERROR("__skyfs_MS_getattr:mode:%s,%u,ino:%llu\n", 
		mmeta->cmetap->name, mmeta->cmetap->mode, mmeta->cmetap->ino);

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getmeta_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.getmetaAck.meta, mmeta->cmetap);
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }
	if(rc == -ENOENT){
		/*added by mayl */
		// : find the mmeta in rename_subset
		
		rc = __skyfs_MS_get_rename_meta_locked(dir_id,&subset_cache,
			&bmeta,&mmeta,&htbp,ino,0);
		if(rc >= 0){
			__skyfs_MS_copy_meta(&msgp->u.getmetaAck.meta, mmeta->cmetap);
        	pthread_mutex_unlock(&(mmeta->lock));
        	pthread_rwlock_unlock(&(bmeta->rwlock));
        	pthread_rwlock_unlock(&(subset_cache->rwlock));
        	pthread_rwlock_unlock(&(htbp->rwlock));
		}
		
	}
   if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_getattr:Failed.ino %llx, conflict_index %x ,rc:%d\n", 
			ino, conflict_index,rc);
	if(err_num == -5){
		SKYFS_ERROR_1("__skyfs_MS_getattr: locatee mmeta Failed.ino %llx, conflict_index %x , sid %d, bmeta_id %d\n", 
			ino, conflict_index,bmeta->box_id, subset_cache->subset_id);
	}
    }
    msgp->error = rc;
    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_getattr:send reply failed.ino %llx, conflict_index %x ,rc:%d\n", 
			ino, conflict_index,rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_getattr:exit.\n");
}

void __skyfs_MS_setattr(amp_request_t *req)
{
    skyfs_msg_t            *msgp = NULL;    
    skyfs_m_setmeta_args_t *args = NULL;
    skyfs_u32_t            req_type;
    skyfs_u32_t            req_id;
   
    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_ino_t        ino;
    skyfs_u32_t        conflict_index;
    skyfs_htb_t        *htbp = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    args = &(msgp->u.setmetaReq);
    ino = args->ino;
    conflict_index = args->conflict_index;
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR("__skyfs_MS_setattr:enter:ino:%llu,conflict_index:%u\n",
        ino, conflict_index);

    htbp = __skyfs_MS_locate_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);

	if(htbp == NULL){
		subset_id = SKYFS_MAX_SUBSET_PER_DIR;
		htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
		if(htbp != 	NULL){
			/* added by mayl , goto ERR, try to get the mmeta directly in rename subset */
			rc = -ENOENT;
			goto ERR;
		}
	}
	
    if(htbp == NULL){
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    bmeta = __skyfs_MS_locate_bmeta_by_ino(subset_cache, ino, conflict_index, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_setattr:get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_setattr:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_ino(bmeta, ino, conflict_index);
    if(mmeta == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_MS_setattr:can not find mmeta in bmeta\n");
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta->lock));
    SKYFS_ERROR("__skyfs_MS_setattr:mode:%s,%u,ino:%llu\n", 
		mmeta->cmetap->name, mmeta->cmetap->mode, mmeta->cmetap->ino);
    rc = __skyfs_MS_set_cmeta(args, mmeta->cmetap);
    if(rc < 0){
        rc = -EPERM;
        SKYFS_ERROR_1("__skyfs_MS_setattr:set cmeta failed\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_setmeta_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.setmetaAck.meta, mmeta->cmetap);
	if(args->valid & SKYFS_ATTR_EATTR_FLAG){
		SKYFS_ERROR_1("setxattr algorithm return %d, piggyback algorithm %d\n", rc, msgp->u.setmetaAck.meta.algorithm);
	}
	
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }
	if(rc == -ENOENT){
		/*added by mayl */
		// : find the mmeta in rename_subset
		
		rc = __skyfs_MS_get_rename_meta_locked(dir_id,&subset_cache,
			&bmeta,&mmeta,&htbp,ino,0);
		if(rc >= 0){
			//__skyfs_MS_copy_meta(&msgp->u.getmetaAck.meta, mmeta->cmetap);
			rc = __skyfs_MS_set_cmeta(args, mmeta->cmetap);
			if(rc < 0)
				SKYFS_ERROR_1("skyfs_MS_setattr  return %d when setting cmeta\n", rc);
			else
				__skyfs_MS_copy_meta(&msgp->u.setmetaAck.meta, mmeta->cmetap);
        	pthread_mutex_unlock(&(mmeta->lock));
        	pthread_rwlock_unlock(&(bmeta->rwlock));
        	pthread_rwlock_unlock(&(subset_cache->rwlock));
        	pthread_rwlock_unlock(&(htbp->rwlock));
		}
		
	}

    msgp->error = rc;
    SKYFS_MSG("__skyfs_MS_setattr:err:rc:%d\n", rc);
    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_setattr:send reply failed.rc:%d\n", rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_setattr:exit:\n\n");

}


//added by mayl , for lock/unlock posix_lock/flock
void __skyfs_MS_flock(amp_request_t *req)
{
    skyfs_msg_t            *msgp = NULL;    
    skyfs_m_flock_args_t *args = NULL;
    skyfs_u32_t            req_type;
    skyfs_u32_t            req_id;
   
    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_ino_t        ino;
    skyfs_u32_t        conflict_index;
    skyfs_htb_t        *htbp = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_flock_t    fl ;
    skyfs_M_flock_t    conflict_fl ;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;
	skyfs_s32_t  ori_lock_empty = 0;
	skyfs_s32_t  new_lock_empty = 0; 

    msgp = __skyfs_get_msg(req->req_msg);
    args = &(msgp->u.flockReq);
    ino = args->ino;
    conflict_index = args->conflict_index;
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR_1("__skyfs_MS_flock:enter:ino:%llu,conflict_index:%u\n",
        ino, conflict_index);

    htbp = __skyfs_MS_locate_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
	if(htbp == NULL){
		subset_id = SKYFS_MAX_SUBSET_PER_DIR;
		htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
		if(htbp != 	NULL){
			/* added by mayl , goto ERR, try to get the mmeta directly in rename subset */
			rc = -ENOENT;
			goto ERR;
		}
	}
    if(htbp == NULL){
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    bmeta = __skyfs_MS_locate_bmeta_by_ino(subset_cache, ino, conflict_index, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_flcok:get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_flock:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_ino(bmeta, ino, conflict_index);
    if(mmeta == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_MS_flock:can not find mmeta in bmeta\n");
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta->lock));
	if(list_empty(&mmeta->flock_head) && list_empty(&mmeta->posix_lock_head)){
		ori_lock_empty = 1;
	}
    SKYFS_ERROR_1("__skyfs_MS_flock:mode:%s,%u,ino:%llu\n", 
		mmeta->cmetap->name, mmeta->cmetap->mode, mmeta->cmetap->ino);
    //rc = __skyfs_MS_set_cmeta(args, mmeta->cmetap);
    // TODO: mayl need add function __skyfs_MS_lock_file and related helper.
    convert_flock_arg_to_fl(args, &fl);
    rc = __skyfs_MS_lock_file(mmeta, &fl, &conflict_fl);
    if(rc < 0){
        rc = -EPERM;
        SKYFS_ERROR("__skyfs_MS_flock:set cmeta failed\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }else{
			if(list_empty(&mmeta->flock_head) && list_empty(&mmeta->posix_lock_head)){
				new_lock_empty = 1;
			}
			if(ori_lock_empty && !new_lock_empty){
				/* add a new lock node head to lock htb */
				__skyfs_add_inode_lock_cache(ino, &mmeta->flock_head, &mmeta->posix_lock_head, mmeta);
			}
			if(!ori_lock_empty && new_lock_empty){
				/* remove lock node head from lock htb */
				if(mmeta->lock_htb_head != NULL)
					__skyfs_remove_inode_lock_cache(ino, mmeta->lock_htb_head, mmeta);
			}
		
	}

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_flock_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
		if(rc == EAGAIN && ((args->op_type & 0xffff) == F_GETLK)){
			 msgp->u.flockAck.fl_type = conflict_fl.l_type;
			 msgp->u.flockAck.pid = conflict_fl.l_pid;
			 msgp->u.flockAck.start = conflict_fl.l_start;
			 msgp->u.flockAck.len = conflict_fl.l_len;
			 msgp->u.flockAck.clt_id = conflict_fl.clt_id;
			 SKYFS_ERROR("GET LK conflict , l_start %d, clt_id %d",  conflict_fl.l_start, conflict_fl.clt_id);

		}
        //__skyfs_MS_copy_meta(&msgp->u.setmetaAck.meta, mmeta->cmetap);
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }
	if(rc == -ENOENT){
		/*added by mayl */
		// : find the mmeta in rename_subset
		
		rc = __skyfs_MS_get_rename_meta_locked(dir_id,&subset_cache,
			&bmeta,&mmeta,&htbp,ino,0);
		if(rc >= 0){
			//__skyfs_MS_copy_meta(&msgp->u.getmetaAck.meta, mmeta->cmetap);
			//rc = __skyfs_MS_set_cmeta(args, mmeta->cmetap);
			
			convert_flock_arg_to_fl(args, &fl);
			rc = __skyfs_MS_lock_file(mmeta, &fl, &conflict_fl);
			if(rc < 0){
				rc = -EPERM;
				SKYFS_ERROR_1("__skyfs_MS_flock:lock cmeta failed\n");
				
					//goto ERR;
				}else{
						if(list_empty(&mmeta->flock_head) && list_empty(&mmeta->posix_lock_head)){
							new_lock_empty = 1;
						}
						if(ori_lock_empty && !new_lock_empty){
							/* add a new lock node head to lock htb */
							__skyfs_add_inode_lock_cache(ino, &mmeta->flock_head, &mmeta->posix_lock_head, mmeta);
						}
						if(!ori_lock_empty && new_lock_empty){
							/* remove lock node head from lock htb */
							if(mmeta->lock_htb_head != NULL)
								__skyfs_remove_inode_lock_cache(ino, mmeta->lock_htb_head, mmeta);
						}
					
				}
			
        	pthread_mutex_unlock(&(mmeta->lock));
        	pthread_rwlock_unlock(&(bmeta->rwlock));
        	pthread_rwlock_unlock(&(subset_cache->rwlock));
        	pthread_rwlock_unlock(&(htbp->rwlock));
		}
		
	}

    msgp->error = rc;
    SKYFS_ERROR_1("__skyfs_MS_flock:err:rc:%d\n", rc);
    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_flock:send reply failed.rc:%d\n", rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_flock:exit:\n\n");

}

void __skyfs_MS_release(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;    
   
    skyfs_ino_t        ino;
    skyfs_u64_t        size;
    skyfs_u32_t        conflict_index;
    skyfs_u32_t        client_id;
    skyfs_htb_t        *htbp = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        tmp_subset_id;
    skyfs_u32_t        bmeta_id = 0;
    skyfs_u32_t        delete = 0;
    skyfs_u32_t        hashkey = 0;
    skyfs_s32_t        relocate_times = 10;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_subset_cache_t    *subset_cache = NULL;

    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    ino = msgp->u.releaseReq.ino;
    conflict_index = msgp->u.releaseReq.conflict_index;
    client_id = msgp->u.releaseReq.client_id;

    SKYFS_MSG("__skyfs_MS_release:enter:client:%d,ino:%llu,conflict_index:%u\n",
        client_id, ino, conflict_index);

relocate_subset:
    htbp = __skyfs_MS_locate_subset_by_ino(ino, conflict_index, 
                    &mds_id, &dir_id, &subset_id);
	if(htbp == NULL){
		subset_id = SKYFS_MAX_SUBSET_PER_DIR;
		htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, conflict_index, 
              &mds_id, &dir_id, &subset_id);
		if(htbp != 	NULL){
			/* added by mayl , goto ERR, try to get the mmeta directly in rename subset */
			rc = -ENOENT;
			goto ERR;
		}
	}
    if(htbp == NULL){
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        rc = -EEXIST;
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    hashkey = __skyfs_ino2hashvalue(ino, conflict_index);
    tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("__skyfs_MS_release:splited while waiting:old:%u,new:%u,spdepth:%d\n",
            subset_id, tmp_subset_id, subset_cache->split_depth);
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        relocate_times --;
        if(relocate_times){
            goto relocate_subset;
        }else{
            rc = -EINVAL;
            goto ERR;
        }
    }

    bmeta = __skyfs_MS_locate_bmeta_by_ino(subset_cache, ino, conflict_index, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_release:can't find %d bmeta in cache\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_release:can't read %d bmeta in OSD\n", bmeta_id);
            rc = -EEXIST;
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_ino(bmeta, ino, conflict_index);
    if(mmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_release:can not find mmeta in bmeta\n");
        rc = -EEXIST;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta->lock));

	size = mmeta->cmetap->size;
    SKYFS_ERROR("__skyfs_MS_release:ino:%llu,mmeta->id:%d,%p\n", ino, mmeta->id, mmeta);
    if(!__skyfs_MS_test_bit(mmeta->open_clt, client_id)){
        SKYFS_ERROR("__skyfs_MS_release:client %d is not set\n", client_id);
    }
    
    rc = __skyfs_MS_set_bit(mmeta->open_clt, client_id, 0);

    /*return 1 to implite has to delete meta and data*/
    rc = __skyfs_MS_release_meta(bmeta, mmeta);
    if(rc == 1){
        SKYFS_ERROR("__skyfs_MS_release:delete %s ino:%llu\n", 
            mmeta->cmetap->name, ino);
        delete = 1;    
    }

    pthread_mutex_unlock(&(mmeta->lock));

    if(delete){
        rc = __skyfs_MS_free_meta(bmeta, mmeta);
    }

ERR:
ERR_NONE:
    if(rc >= 0){
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }

	if(rc < 0 ){
		/*added by mayl */
		// : find the mmeta in rename_subset
		
		rc = __skyfs_MS_get_rename_meta_locked(dir_id,&subset_cache,
			&bmeta,&mmeta,&htbp,ino,1);
		if(rc >= 0){
			


			size = mmeta->cmetap->size;
    		SKYFS_ERROR("__skyfs_MS_release:ino:%llu,mmeta->id:%d,%p\n", ino, mmeta->id, mmeta);
    		if(!__skyfs_MS_test_bit(mmeta->open_clt, client_id)){
        		SKYFS_ERROR("__skyfs_MS_release:client %d is not set\n", client_id);
    		}
    
    		rc = __skyfs_MS_set_bit(mmeta->open_clt, client_id, 0);

    		/*return 1 to implite has to delete meta and data*/
    		rc = __skyfs_MS_release_meta(bmeta, mmeta);
    		if(rc == 1){
        		SKYFS_ERROR("__skyfs_MS_release:delete %s ino:%llu\n", 
            	mmeta->cmetap->name, ino);
        	delete = 1;    
    		}
    		pthread_mutex_unlock(&(mmeta->lock));

    		if(delete){
        		rc = __skyfs_MS_free_meta(bmeta, mmeta);
    		}
        	//pthread_mutex_unlock(&(mmeta->lock));
        	pthread_rwlock_unlock(&(bmeta->rwlock));
        	pthread_rwlock_unlock(&(subset_cache->rwlock));
        	pthread_rwlock_unlock(&(htbp->rwlock));
		}
		
	}

    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_release:exit:\n\n");

}

/*
 * When a dir was created, the related structure will not 
 * created until it was read.
 */

static void print_bmeta_info( skyfs_M_bmeta_t * bmeta , int subset_id, int sp_depth)
{
	SKYFS_ERROR_1(" list the spliting subset %d, sp_depth %d ,  bemta %d  cmetas info :\n ", subset_id, sp_depth, bmeta->box_id );
	for (int i = 0 ; i<SKYFS_MAX_META_PER_BOX; i++){

		skyfs_M_cmeta_t *cmeta = (skyfs_M_cmeta_t *)(&(bmeta->cmetap[i]));
		SKYFS_ERROR_1("cmeta[%d]: type %d, hashkey %x, name %s\n",
				i, cmeta->type, cmeta->hashkey, cmeta->name);
	}
}
void __skyfs_MS_create(amp_request_t *req)
{
    skyfs_msg_t           *msgp = NULL;    
    skyfs_m_create_args_t *args = NULL;
   
    skyfs_ino_t        ino;
    skyfs_ino_t        dir_ino;
    skyfs_s8_t         name[SKYFS_MAX_NAME_LEN + 1];
    skyfs_u64_t        hashkey;
    skyfs_u32_t        tmp_subset_id;
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;
    skyfs_u32_t        type;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_htb_t        *htbp = NULL;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_M_cmeta_t    dir_cmeta;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;
    
    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;

    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    msgp = __skyfs_get_msg(req->req_msg);
    args = &(msgp->u.createReq);
    type = args->flag;
    dir_ino = args->dir_ino;
    memcpy(name, args->name, strlen(args->name));
    name[strlen(args->name)] = '\0';
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR("__skyfs_MS_create:%s,dino:%llu\n", name, dir_ino);

    hashkey = __skyfs_name2hashvalue(name);

relocate_subset:
    SKYFS_MSG("__skyfs_MS_create:locate sunbset :%llu, %s\n", dir_ino, name);
    htbp = __skyfs_MS_locate_subset_by_name(dir_ino, name, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        rc = -ENOENT;
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_MSG("__skyfs_MS_create:get subset:%d,%d failed\n", dir_id, subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(subset_cache->rwlock));

    tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("__skyfs_MS_create:subset splited while waiting:old:%d,new:%d\n",
            subset_id, tmp_subset_id);
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_subset;
    }

    SKYFS_MSG("__skyfs_MS_create:get subset:%ld,%d,%d,%s\n", 
        pthread_self(), dir_id, subset_id, name);

relocate_bmeta:
    SKYFS_MSG("__skyfs_MS_create:locate bmeta:%llu, %s\n", dir_ino, name);
    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_create:can't find bmeta:%d in cache\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_create:can't read bmeta:%d in OSD\n", bmeta_id);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    ino = __skyfs_MS_alloc_ino(dir_ino, name, args->mode);
    rc = __skyfs_MS_check_mmeta_exist(bmeta, name, ino);
    if(rc == -EEXIST){
        SKYFS_ERROR("__skyfs_MS_create:mmeta %s already exist\n", name);
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }else if(rc == -EAGAIN){
        SKYFS_ERROR("__skyfs_MS_create:ino %llu already exist\n", ino);
        ino = __skyfs_MS_alloc_conflict_ino(dir_ino, args->mode);
    }

    if(bmeta->nfree > 0){
        rc = __skyfs_MS_alloc_meta(bmeta, &cmeta, &mmeta);
        /*We may free the subset lock here*/
    }else{
        SKYFS_ERROR_1("__skyfs_MS_create:subset_id:%d,subdepth:%d,bmeta_id:%d,nfree:%d, al_meta %d, free_meta %d\n", 
            subset_cache->subset_id, subset_cache->subset_depth, 
            bmeta->box_id, bmeta->nfree, alloc_meta_cnt, free_meta_cnt);
        pthread_rwlock_unlock(&(bmeta->rwlock));
        if(subset_cache->split_depth < SKYFS_FIRST_SPLIT_DEPTH){
    	    SKYFS_ERROR_1("__skyfs_MS_create sp1  :mode:%d, DIR %u, dir_ino %u , subset %d, cnt %d, \n",args->mode,dir_id, dir_ino, subset_id, create_cnt);
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }else if(subset_cache->subset_depth < SKYFS_FIRST_SUBSET_DEPTH){
    	    SKYFS_ERROR_1("__skyfs_MS_create enlarge1  :mode:%d, DIR %u, dir_ino %u , subset %d, cnt %d\n",args->mode,dir_id, dir_ino, subset_id, create_cnt);
            rc = __skyfs_MS_enlarge_subset(subset_cache);
            goto relocate_bmeta;
        }else if(subset_cache->split_depth < SKYFS_MIDDLE_SPLIT_DEPTH){
            /*Just split the subset we needed*/
    	    SKYFS_ERROR_1("__skyfs_MS_create  sp2 :mode:%d, DIR %u, dir_ino %u , subset %d, cnt %d hashkey %lx, name %s\n",
			    args->mode,dir_id, dir_ino, subset_id, create_cnt, hashkey, name);
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }else if(subset_cache->subset_depth < SKYFS_MAX_SUBSET_DEPTH){
    	    SKYFS_ERROR_1("__skyfs_MS_create enlarge2  :mode:%d, DIR %u, dir_ino %u , subset %d, cnt %d\n",args->mode,dir_id, dir_ino, subset_id, create_cnt);
            rc = __skyfs_MS_enlarge_subset(subset_cache);
            goto relocate_bmeta;
        }else{
            /*Just split the subset we needed*/
    	    SKYFS_ERROR_1("__skyfs_MS_create sp3  :mode:%d, DIR %u, dir_ino %u , subset %d, cnt %d, hashkey %lx, name %s\n",
			    args->mode,dir_id, dir_ino, subset_id, create_cnt, hashkey , name);
	    print_bmeta_info(bmeta,  subset_id, subset_cache->split_depth);
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }
    }
   
    cmeta->hashkey = __skyfs_name2hashvalue(name);
    cmeta->conflict_index = cmeta->hashkey;
    SKYFS_MSG("__skyfs_MS_create:%s hash:%llu\n", name, cmeta->hashkey);

    __skyfs_MS_profile_create();

    create_cnt++;
    if(S_ISREG(args->mode)){
        SKYFS_MSG("__skyfs_MS_create: init file\n");
        __skyfs_MS_init_file(ino, mmeta, cmeta, args);
    }else if(S_ISDIR(args->mode)) {
	
    	SKYFS_ERROR_1("__skyfs_MS_create dir :mode:%d, DIR %u, dir_ino %u , subset %d\n",args->mode,dir_id, dir_ino, subset_id);
        SKYFS_MSG("__skyfs_MS_create: init dir \n");
        __skyfs_MS_init_dir(ino, mmeta, cmeta, args);
    }else{
        SKYFS_MSG("__skyfs_MS_create: init nod\n");
        __skyfs_MS_init_node(ino, mmeta, cmeta, args);
    }

    __skyfs_MS_update_dir_cache(subset_cache, &dir_cmeta, 1);
    //subset_cache->nlink_update ++;

    SKYFS_MSG("__skyfs_MS_create:mmeta->id:%d,%p\n", mmeta->id, mmeta);
    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_create_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.createAck.meta, cmeta);
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }

    msgp->error = rc;

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "create");
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_create:send reply failed.rc:%d\n", rc);
    }

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "create-net");

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_MS_create:exit:\n\n");

}

void __skyfs_MS_remove(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;    
   
    skyfs_ino_t        dir_ino;
    skyfs_s8_t         name[SKYFS_MAX_NAME_LEN + 1];

    skyfs_ino_t        ino;
    skyfs_u32_t        conflict_index;

    skyfs_u64_t        hashkey;
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;

	skyfs_u32_t        from_second = 0;
	skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id = 0;
    skyfs_u32_t        subset_id = 0;
    skyfs_u32_t        tmp_subset_id = 0;
    skyfs_u32_t        bmeta_id;
    skyfs_htb_t        *htbp = NULL;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    dir_cmeta;

    skyfs_u32_t        mds_id_to = 0;
    skyfs_u32_t        dir_id_to = 0;
    skyfs_u32_t        subset_id_to = 0;
    skyfs_u32_t        tmp_subset_id_to = 0;
    skyfs_u32_t        bmeta_id_to;
    skyfs_htb_t        *htbp_to = NULL;

    skyfs_M_subset_cache_t    *subset_cache_to = NULL;
    skyfs_M_bmeta_t    *bmeta_to = NULL;
    skyfs_M_mmeta_t    *mmeta_to = NULL;

    skyfs_meta_t       meta;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_ino = msgp->u.removeReq.dir_ino;
    memcpy(name, msgp->u.removeReq.name, strlen(msgp->u.removeReq.name));
    name[strlen(msgp->u.removeReq.name)] = '\0';
    ino = msgp->u.removeReq.ino;
    conflict_index = msgp->u.removeReq.conflict_index;
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR_1("__skyfs_MS_remove:enter:dir_ino:%llu, name:%s\n",
        dir_ino, name);
    if(ino){
        SKYFS_MSG("__skyfs_MS_remove:forward 2 half:dir_ino:%llu, name:%s\n",
            dir_ino, name);
        goto second_half;
    }
    /*1. first half, unlink the dentry*/
    hashkey = __skyfs_name2hashvalue(name);

    htbp = __skyfs_MS_locate_subset_by_name(dir_ino, name, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        rc = -ENOENT;
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("__skyfs_MS_remove:subset splited while waiting:old:%d,new:%d\n",
            subset_id, tmp_subset_id);
    }

    SKYFS_MSG("__skyfs_MS_remove:name:%s, dir_id:%d, subset_id:%d\n",
        name, dir_id, subset_id);
    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_remove:can't find bmeta in cache\n");
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_remove:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_name(bmeta, name);    
    if(mmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_remove:can not find mmeta in bmeta\n");
        rc = -EPERM;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta->lock));
    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);
    rc = __skyfs_MS_unlink_meta(mmeta);
    bzero(mmeta->cmetap->name, SKYFS_MAX_NAME_LEN);
    mmeta->cmetap->hashkey = 0;
    mmeta->cmetap->type = SKYFS_NULL_META;
    free_meta_cnt++;
    pthread_mutex_unlock(&(mmeta->lock));

    __skyfs_MS_update_dir_cache(subset_cache, &dir_cmeta, 0);

    ino = msgp->u.removeReq.ino = mmeta->cmetap->ino;
    conflict_index = msgp->u.removeReq.conflict_index = mmeta->cmetap->conflict_index;
    __skyfs_MS_copy_meta(&meta, mmeta->cmetap); 
    pthread_mutex_unlock(&(mmeta->lock));
    pthread_rwlock_unlock(&(bmeta->rwlock));
    pthread_rwlock_unlock(&(subset_cache->rwlock));
    pthread_rwlock_unlock(&(htbp->rwlock));

 	SKYFS_ERROR("__skyfs_MS_remove:dir_ino:%llu,ino:%llu,name:%s\n",
        dir_ino, ino, name);

    if(rc != SKYFS_LINK){
        rc = 0;
        goto normal_exit;
    } 

    /*2. second half, unlink the cmeta*/
second_half:
relocate_subset:
    rc = 0;
	from_second = 1;
    SKYFS_ERROR_1("__skyfs_MS_remove:2 half, unlink:ino: %llu,%u\n", 
        ino, conflict_index);
    htbp_to = __skyfs_MS_locate_subset_by_ino(ino, conflict_index, 
              &mds_id_to, &dir_id_to, &subset_id_to);
	if(htbp == NULL){
		subset_id_to= SKYFS_MAX_SUBSET_PER_DIR;
		htbp = __skyfs_MS_locate_rename_subset_by_ino(ino, conflict_index, 
              &mds_id_to, &dir_id_to, &subset_id_to);
		if(htbp != 	NULL){
			/* added by mayl , goto ERR, try to get the mmeta directly in rename subset */
			rc = -ENOENT;
			goto ERR_NONE;
		}
	}
    if(htbp_to == NULL){
        rc = -ENOENT;
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp_to->rwlock));

    mds_id_to = __skyfs_MS_check_htbcache(htbp_to);
    if(mds_id_to != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
        goto ERR_NONE;
    }

    subset_cache_to = __skyfs_MS_get_subset(htbp_to, dir_id_to, subset_id_to);
    if(subset_cache_to == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache_to->rwlock));
    
    tmp_subset_id_to=__skyfs_get_subset_id(conflict_index,subset_cache_to->split_depth);
    if(tmp_subset_id_to != subset_id_to){
        SKYFS_ERROR("__skyfs_MS_remove:subset splited while waiting:old:%d,new:%d\n",
            subset_id_to, tmp_subset_id_to);
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto relocate_subset;
    }

    bmeta_to = __skyfs_MS_locate_bmeta_by_ino(subset_cache_to, ino, conflict_index, &bmeta_id_to);
    if(bmeta_to == NULL){
        SKYFS_ERROR("__skyfs_MS_remove:need get bmeta %d\n", bmeta_id_to);
        bmeta_to = __skyfs_MS_get_bmeta(subset_cache_to, bmeta_id_to); 
        if(bmeta_to == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_remove:can't read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta_to->rwlock));
    pthread_mutex_unlock(&(bmeta_to->lock));

    mmeta_to = __skyfs_MS_locate_mmeta_by_ino(bmeta_to, ino, conflict_index);
    if(mmeta_to == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_MS_remove:can not find mmeta in bmeta %d\n",bmeta_id_to);
        SKYFS_ERROR("__skyfs_MS_remove:nfree:%d,firstfree:%d\n",
            bmeta_to->nfree, bmeta_to->firstfree);
        pthread_rwlock_unlock(&(bmeta_to->rwlock));
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto ERR;
    }

    pthread_mutex_lock(&(mmeta_to->lock));
    rc = __skyfs_MS_set_bit(mmeta_to->open_clt, req_id, 1);
    //rc = __skyfs_MS_unlink_meta(mmeta_to);
    if(mmeta_to->cmetap->nlink > 0){
        mmeta_to->cmetap->nlink --;
    }
    __skyfs_MS_copy_meta(&meta, mmeta_to->cmetap);
    SKYFS_ERROR("__skyfs_MS_remove:%s,%llu, cmeta_type:%d\n",name,ino,meta.type);

    pthread_mutex_unlock(&(mmeta_to->lock));
    pthread_rwlock_unlock(&(bmeta_to->rwlock));
    pthread_rwlock_unlock(&(subset_cache_to->rwlock));
    pthread_rwlock_unlock(&(htbp_to->rwlock));

ERR_SECOND:

normal_exit:
ERR:
    SKYFS_ERROR("__skyfs_MS_remove:%s,%llu, cmeta_type:%d\n",name,ino,meta.type);
	if(from_second && rc == -ENOENT){
		// TODO:
		/*added by mayl */
		// : find the mmeta in rename_subset
		
		rc = __skyfs_MS_get_rename_meta_locked(dir_id,&subset_cache_to,
			&bmeta_to,&mmeta_to,&htbp_to,ino,0);
		if(rc >= 0){
			
			rc = __skyfs_MS_set_bit(mmeta_to->open_clt, req_id, 1);
    		//rc = __skyfs_MS_unlink_meta(mmeta_to);
    		if(mmeta_to->cmetap->nlink > 0){
        		mmeta_to->cmetap->nlink --;
    		}
    		__skyfs_MS_copy_meta(&meta, mmeta_to->cmetap);
    		SKYFS_ERROR("__skyfs_MS_remove:%s,%llu, cmeta_type:%d\n",name,ino,meta.type);
        	pthread_mutex_unlock(&(mmeta_to->lock));
        	pthread_rwlock_unlock(&(bmeta_to->rwlock));
        	pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        	pthread_rwlock_unlock(&(htbp_to->rwlock));
		}
		
	}
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_remove_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    msgp->error = rc;
    if(rc >= 0){
        memcpy(&(msgp->u.removeAck.meta), &meta, sizeof(skyfs_meta_t));
    } 

    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_remove:send reply failed.rc:%d\n", rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_MS_remove:exit:\n");
}

void __skyfs_MS_link(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;    
   
    skyfs_ino_t        pino_from;
    skyfs_s8_t         name_from[SKYFS_MAX_NAME_LEN + 1];
    skyfs_ino_t        pino_to;
    skyfs_s8_t         name_to[SKYFS_MAX_NAME_LEN + 1];

    //skyfs_u64_t        hashkey;
    skyfs_u64_t        hashkey_to;
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;

    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id = 0;
    skyfs_u32_t        subset_id = 0;
    //skyfs_u32_t        tmp_subset_id = 0;
    skyfs_u32_t        bmeta_id;

    skyfs_u32_t        mds_id_to = 0;
    skyfs_u32_t        dir_id_to = 0;
    skyfs_u32_t        subset_id_to = 0;
    skyfs_u32_t        tmp_subset_id_to = 0;
    skyfs_u32_t        bmeta_id_to;

    skyfs_htb_t        *htbp = NULL;
    skyfs_htb_t        *htbp_to = NULL;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta;

    skyfs_M_subset_cache_t    *subset_cache_to = NULL;
    skyfs_M_bmeta_t    *bmeta_to = NULL;
    skyfs_M_mmeta_t    *mmeta_to = NULL;
    skyfs_M_cmeta_t    *cmeta_to;
    skyfs_M_cmeta_t    dir_cmeta_to;
    skyfs_ino_t        ino_to;

    skyfs_M_cmeta_t    tran_cmeta;
    skyfs_meta_t       *meta = NULL;

    skyfs_s32_t        rc = 0;
    skyfs_u32_t        size;
    skyfs_m_link_args_t *args = NULL;

    msgp = __skyfs_get_msg(req->req_msg);
    args = &(msgp->u.linkReq);
    pino_from = args->pino_from;
    pino_to = args->pino_to;
    memcpy(name_from, args->name_from, strlen(args->name_from));
    name_from[strlen(args->name_from)] = '\0';
    memcpy(name_to, args->name_to, strlen(args->name_to));
    name_to[strlen(args->name_to)] = '\0';
    meta = &(args->meta);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    hashkey_to = __skyfs_name2hashvalue(name_to);
    SKYFS_ERROR_1("__skyfs_MS_link:enter:from %llx, %s, to %llx, %s.\n", 
        pino_from, name_from, pino_to, name_to);
  
    if(meta->ino){
        /*first half process of rename has been excuted.*/
        goto second_half;
    }

    /*1. Begin to process the first_half rename, set non-valid*/ 
    htbp = __skyfs_MS_locate_subset_by_name(pino_from, name_from, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        if(mds_id != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_link:error:can not get subset %d in OSD\n", subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name_from, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_link: get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_link:error:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_name(bmeta, name_from);    
    if(mmeta == NULL){
        SKYFS_MSG("__skyfs_MS_link:can not find:%s in subset:%d,bmeta:%d\n",
            name_from, subset_id, bmeta_id);
        SKYFS_MSG("__skyfs_MS_link:sp_depth:%d,sb_depth:%d,nfree:%d,firstfree:%d\n",
            subset_cache->split_depth, subset_cache->subset_depth,
            bmeta->nfree, bmeta->firstfree);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_mutex_lock(&(mmeta->lock));
    cmeta = mmeta->cmetap;

    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_link:set bit error.\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    cmeta->nlink ++;
    SKYFS_ERROR_1("__skyfs_MS_link:%s,ino:%llx,nlink:%d,type:%d.\n", 
        name_from,cmeta->ino,cmeta->nlink,cmeta->type);
    __skyfs_MS_copy_meta(meta, cmeta);

    pthread_mutex_unlock(&(mmeta->lock));
    pthread_rwlock_unlock(&(bmeta->rwlock));
    pthread_rwlock_unlock(&(subset_cache->rwlock));
    pthread_rwlock_unlock(&(htbp->rwlock));

    /*2. Begin to process the second_half rename, create cmeta*/ 
second_half:
relocate_subset:
    SKYFS_MSG("__skyfs_MS_link:second half,%u\n", meta->nlink);

     htbp_to = __skyfs_MS_locate_subset_by_name(pino_to, name_to, &mds_id_to, &dir_id_to, &subset_id_to);
    if(htbp_to == NULL){
        if(mds_id_to != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&(htbp_to->rwlock));

    mds_id_to = __skyfs_MS_check_htbcache(htbp_to);
    if(mds_id_to != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        __skyfs_MS_copy_meta(meta, &tran_cmeta);
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
        goto ERR_NONE;
    }

    subset_cache_to = __skyfs_MS_get_subset(htbp_to, dir_id_to, subset_id_to);
    if(subset_cache_to == NULL){
        SKYFS_ERROR("__skyfs_MS_link:error:can not get subset %d in OSD\n", 
            subset_id_to);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(subset_cache_to->rwlock));

    tmp_subset_id_to = __skyfs_get_subset_id(hashkey_to, subset_cache_to->split_depth);
    if(tmp_subset_id_to != subset_id_to){
        SKYFS_ERROR("__skyfs_MS_link:subset splited while waiting:old:%d,new:%d\n",
            subset_id_to, tmp_subset_id_to);
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto relocate_subset;
    }

relocate_bmeta:
    bmeta_to = __skyfs_MS_locate_bmeta_by_name(subset_cache_to, name_to, &bmeta_id_to);
    if(bmeta_to == NULL){
        SKYFS_MSG("__skyfs_MS_link: get bmeta:%d\n", bmeta_id_to);
        bmeta_to = __skyfs_MS_get_bmeta(subset_cache_to, bmeta_id_to);
        if(bmeta_to == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_link:error:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta_to->rwlock));
    pthread_mutex_unlock(&(bmeta_to->lock));

    ino_to = meta->ino;
   
    if(bmeta_to->nfree > 0){
        rc = __skyfs_MS_alloc_meta(bmeta_to, &cmeta_to, &mmeta_to);
        /*We may free the subset lock here*/
    }else{
        SKYFS_MSG("__skyfs_MS_link:subset_id:%d,subdepth:%d,bmeta_id:%d,nfree:%d\n", 
            subset_cache_to->subset_id, 
            subset_cache_to->subset_depth, 
            bmeta_to->box_id, bmeta_to->nfree);
        pthread_rwlock_unlock(&(bmeta_to->rwlock));
        if(subset_cache_to->split_depth < SKYFS_FIRST_SPLIT_DEPTH){
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }else if(subset_cache_to->subset_depth < SKYFS_FIRST_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache_to);
            goto relocate_bmeta;
        }else if(subset_cache_to->split_depth < SKYFS_MIDDLE_SPLIT_DEPTH){
            /*Just split the subset we needed*/
	    SKYFS_ERROR_1("skyfs_ms_link sp 2 subset_id %d, hashkey %lx, name %s ", subset_cache_to->subset_id, hashkey_to, name_to );
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }else if(subset_cache_to->subset_depth < SKYFS_MAX_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache_to);
            goto relocate_bmeta;
        }else{
            /*Just split the subset we needed*/
	    SKYFS_ERROR_1("skyfs_ms_link sp 3 subset_id %d, hashkey %lx, name %s ", subset_cache_to->subset_id, hashkey_to, name_to );
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }
    }
   
    cmeta_to->hashkey = hashkey_to;
    cmeta_to->conflict_index = meta->conflict_index;
    //cmeta_to->conflict_index = 0;
    SKYFS_MSG("__skyfs_MS_link:%s,%llu, hash:%llu\n", 
        name_to, ino_to, cmeta_to->hashkey);

    /*copy meta to cmeta*/
    __skyfs_MS_init_cmeta(meta, mmeta_to, cmeta_to, ino_to, name_to);
    cmeta_to->type = SKYFS_LINK;
    cmeta_to->nlink = 0;

      // added by mayl
    if(S_ISDIR(cmeta_to->mode)){
        mmeta_to->status = 1;
        }


    __skyfs_MS_update_dir_cache(subset_cache_to, &dir_cmeta_to, 1);
    SKYFS_MSG("__skyfs_MS_link:mmeta->id:%d,%p\n", mmeta_to->id, mmeta_to);
    rc = __skyfs_MS_set_bit(mmeta_to->open_clt, req_id, 1);

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_link_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.linkAck.meta, cmeta_to);
        pthread_mutex_unlock(&(mmeta_to->lock));
        pthread_rwlock_unlock(&(bmeta_to->rwlock));
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
    }

    msgp->error = rc;

    SKYFS_MSG("__skyfs_MS_link:type:%d,id:%d,type:%d,id:%d.\n", 
        req->req_remote_type, req->req_remote_id, req_type, req_id);

    SKYFS_MSG("__skyfs_MS_link:sender_handle:%llu\n", 
        req->req_msg->amh_sender_handle);

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req_type, 
             req_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_link:send reply failed.rc:%d\n", rc); 
    } 


ERR_NONE:
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_link:exit\n\n");

}

void __skyfs_MS_symlink(amp_request_t *req)
{
    skyfs_msg_t           *msgp = NULL;    
    skyfs_m_symlink_args_t *args = NULL;
   
    skyfs_ino_t        ino;
    skyfs_ino_t        dir_ino;
    skyfs_s8_t         name[SKYFS_MAX_NAME_LEN + 1];
    skyfs_u64_t        hashkey;
    skyfs_u32_t        tmp_subset_id;
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;
    //skyfs_u32_t        type;
    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_htb_t        *htbp = NULL;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_M_cmeta_t    dir_cmeta;

    skyfs_s32_t        size;
    skyfs_s32_t        rc = 0;
    
    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;

    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    msgp = __skyfs_get_msg(req->req_msg);
    args = &(msgp->u.symlinkReq);
    //type = args->flag;
    dir_ino = args->dir_ino;
    strncpy(name, args->name, strlen(args->name));
    name[strlen(args->name)] = '\0';
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ERROR("__skyfs_MS_symlink:enter: dino:%llu,name:%s,target:%s\n", 
		dir_ino, name, args->target);

    hashkey = __skyfs_name2hashvalue(name);

relocate_subset:
    SKYFS_MSG("__skyfs_MS_symlink:locate sunbset :%llu, %s\n", dir_ino, name);
    htbp = __skyfs_MS_locate_subset_by_name(dir_ino, name, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        rc = -ENOENT;
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_MSG("__skyfs_MS_symlink:get subset:%d,%d failed\n", 
			dir_id, subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(subset_cache->rwlock));

    tmp_subset_id = __skyfs_get_subset_id(hashkey, subset_cache->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("__skyfs_MS_symlink:splited while waiting:old:%d,new:%d\n",
            subset_id, tmp_subset_id);
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_subset;
    }

    SKYFS_MSG("__skyfs_MS_symlink:get subset:%ld,%d,%d,%s\n", 
        pthread_self(), dir_id, subset_id, name);

relocate_bmeta:
    SKYFS_MSG("__skyfs_MS_symlink:locate bmeta:%llu, %s\n", dir_ino, name);
    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_symlink:can't find bmeta:%d in cache\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_symlink:can't read bmeta:%d in OSD\n", 
				bmeta_id);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    ino = __skyfs_MS_alloc_ino(dir_ino, name, args->mode);
    rc = __skyfs_MS_check_mmeta_exist(bmeta, name, ino);
    if(rc == -EEXIST){
        SKYFS_ERROR("__skyfs_MS_symlink:mmeta %s already exist\n", name);
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }else if(rc == -EAGAIN){
        SKYFS_ERROR("__skyfs_MS_symlink:ino %llu already exist\n", ino);
        ino = __skyfs_MS_alloc_conflict_ino(dir_ino, args->mode);
    }

    if(bmeta->nfree > 0){
        rc = __skyfs_MS_alloc_meta(bmeta, &cmeta, &mmeta);
        /*We may free the subset lock here*/
    }else{
        SKYFS_MSG("__skyfs_MS_symlink:subset_id:%d,subdepth:%d,bmeta_id:%d,nfree:%d\n", 
            subset_cache->subset_id, subset_cache->subset_depth, 
            bmeta->box_id, bmeta->nfree);
        pthread_rwlock_unlock(&(bmeta->rwlock));
        if(subset_cache->split_depth < SKYFS_FIRST_SPLIT_DEPTH){
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }else if(subset_cache->subset_depth < SKYFS_FIRST_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache);
            goto relocate_bmeta;
        }else if(subset_cache->split_depth < SKYFS_MIDDLE_SPLIT_DEPTH){
            /*Just split the subset we needed*/
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }else if(subset_cache->subset_depth < SKYFS_MAX_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache);
            goto relocate_bmeta;
        }else{
            /*Just split the subset we needed*/
            rc = __skyfs_MS_split_subset(subset_cache);
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto relocate_subset;
        }
    }
   
    cmeta->hashkey = __skyfs_name2hashvalue(name);
    cmeta->conflict_index = cmeta->hashkey;
    SKYFS_MSG("__skyfs_MS_symlink:%s hash:%llu\n", name, cmeta->hashkey);

    __skyfs_MS_profile_create();

    SKYFS_MSG("__skyfs_MS_symlink:mode:%d\n",args->mode);
    rc = __skyfs_MS_init_symlink(ino, mmeta, cmeta, args);
    if(rc < 0){
        goto ERR;
    }

    __skyfs_MS_update_dir_cache(subset_cache, &dir_cmeta, 1);
    //subset_cache->nlink_update ++;

    SKYFS_MSG("__skyfs_MS_symlink:mmeta->id:%d,%p\n", mmeta->id, mmeta);
    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_symlink_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.symlinkAck.meta, cmeta);
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }

    msgp->error = rc;

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "symlink");
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    rc = amp_send_sync(mds_comp_context,
            req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_symlink:send reply failed.rc:%d\n", rc);
    }

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "create-net");

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_MS_symlink:exit:\n\n");
}

void __skyfs_MS_readlink(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    skyfs_u32_t         req_type;
    skyfs_u32_t         req_id;
   
    skyfs_ino_t         dir_ino;
    skyfs_s8_t          name[SKYFS_MAX_NAME_LEN + 1];
    skyfs_htb_t         *htbp = NULL;
    skyfs_u32_t         mds_id = 0;
    skyfs_u32_t         dir_id;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         bmeta_id;
    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_mmeta_t     *mmeta = NULL;
    skyfs_M_cmeta_t     *cmeta = NULL;

    skyfs_s32_t         find = -1;
    skyfs_s32_t         size;
    skyfs_s32_t         rc = 2;
    
    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;
    
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    msgp = __skyfs_get_msg(req->req_msg);
    dir_ino = msgp->u.readlinkReq.dir_ino;
    memcpy(name, msgp->u.readlinkReq.name, strlen(msgp->u.readlinkReq.name));
    name[strlen(msgp->u.readlinkReq.name)] = '\0';
    req_type = msgp->fromType;
    req_id = msgp->fromid;

    SKYFS_ENTER("__skyfs_MS_readlink:enter:%llu, %s\n", dir_ino, name);
    htbp = __skyfs_MS_locate_subset_by_name(dir_ino, name, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        if(mds_id != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_readlink:error:cann't get subset %d in OSD\n", subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_readlink: get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_readlink:error:can not read bmeta in OSD\n");
            find = 0;
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    mmeta = __skyfs_MS_locate_mmeta_by_name(bmeta, name);    
    if(mmeta == NULL){
        SKYFS_MSG("__skyfs_MS_readlink:can not find:%s in subset:%d,bmeta:%d\n",
            name, subset_id, bmeta_id);
        SKYFS_MSG("__skyfs_MS_readlink:sp_depth:%d,sb_depth:%d,nfree:%d,firstfree:%d\n",
            subset_cache->split_depth, subset_cache->subset_depth,
            bmeta->nfree, bmeta->firstfree);
        find = 0;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_mutex_lock(&(mmeta->lock));
    cmeta = mmeta->cmetap;

    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_readlink:set bit error.\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));

        goto ERR;
    }

    find = 2;
ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_lookup_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(find < 0){
        rc = 1;
    }else if(find == 0){
        rc = 0;
    }else if(find == 2){
        if(rc >= 0){
            rc = __skyfs_MS_get_symlink(cmeta, msgp->u.readlinkAck.target);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_readlink:get symlink err,rc:%d\n", rc);
            }else{
                rc = 2;
            }
            pthread_mutex_unlock(&(mmeta->lock));
            pthread_rwlock_unlock(&(bmeta->rwlock));
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
        }else{
            rc = 1;
        }
    }

    msgp->error = rc; 
    msgp->ver = __skyfs_MS_get_layoutv();

    SKYFS_MSG("__skyfs_MS_readlink:type:%d,id:%d,type:%d,id:%d.\n", 
        req->req_remote_type, req->req_remote_id, req_type, req_id);

    SKYFS_MSG("__skyfs_MS_readlink:symlink:%s\n", 
        msgp->u.readlinkAck.target);

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "readlink");
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req_type, 
             req_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_readlink:send reply failed.rc:%d\n", rc); 
    } 

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "readlink-net");


ERR_NONE:
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_readlink:exit\n\n");

}
static void  __skyfs_MS_insert_rename_meta(skyfs_u32_t dir_id,  skyfs_M_cmeta_t *cmeta, skyfs_M_mmeta_t *mmeta)
{

    skyfs_htb_t        *htbp = NULL;
    skyfs_htb_t        *htbp_lock = NULL;
    skyfs_M_inode_lock_t * node = NULL;
    skyfs_u32_t        mds_id = 0;
    skyfs_s32_t        err_num = 0;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        bmeta_id;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_cmeta_t 	*cmeta_to = NULL;
    skyfs_M_mmeta_t    *mmeta_to = NULL;

    skyfs_s32_t 	rc = 0;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
	/* step 1: get subset cache */
relocate_subset:
    /*Normal process*/
    subset_id = SKYFS_MAX_SUBSET_PER_DIR;
    htbp = __skyfs_MS_locate_rename_subset_by_ino(cmeta->ino, cmeta->conflict_index, 
              &mds_id, &dir_id, &subset_id);
    /* subset id only set const value SKYFS_MAX_SUBSET_PER_DIR */
    if(htbp == NULL){
        rc = -ENOENT;
        //TODO : only support one mds NOW -- mayl __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
	err_num = -1;
        goto ERR_NONE;
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        //__skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
	err_num = -2;
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        rc = -ENOENT;
	err_num = -3;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(subset_cache->rwlock));

relocate_bmeta:
    /* TODO INPLEMENT the fuction below*/
    bmeta = __skyfs_MS_locate_rename_bmeta_by_ino(subset_cache, cmeta->ino, cmeta->conflict_index, &bmeta_id);
    if(bmeta == NULL){
        //SKYFS_ERROR_1("__skyfs_MS_rename_MOVE: get bmeta:%d failed\n", bmeta_id);
	err_num = -6;
        bmeta  = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            rc = -ENOENT;
	    err_num = -7;
            SKYFS_ERROR_1("__skyfs_MS_rename_MOVE:error:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));
    
    
    rc = __skyfs_MS_alloc_rename_meta(bmeta, &cmeta_to, &mmeta_to, cmeta->ino);

        /*We may free the subset lock here*/
    

    /*  step 2  copy the cmeta & mmeta */

    cmeta_to->hashkey = cmeta->ino;
    cmeta_to->conflict_index = cmeta->conflict_index;
    //cmeta_to->conflict_index = 0;
    //SKYFS_MSG("__skyfs_MS_rename:%s,%llu, hash:%llu\n", 
      //  name_to, ino_to, cmeta_to->hashkey);

    /* TODO inplement the function below copy meta to cmeta*/
    SKYFS_ERROR_1("try to init renamed meta stab, ino %lx, cmeta %p , mmeta %p \n ", cmeta->ino, cmeta_to ,mmeta_to);
    __skyfs_MS_init_rename_cmeta(cmeta, mmeta_to, cmeta_to, cmeta->ino, NULL);
    bzero(cmeta_to->name, SKYFS_MAX_NAME_LEN);
    cmeta_to->type = SKYFS_RENAME;
    memcpy(mmeta_to->open_clt, mmeta->open_clt, SKYFS_NODE_BM_LEN);
	// copy mmeta flock
     //TODO need modify  the 3 line code below carefully !! 
     
     htbp_lock = &skyfs_flock_cache_htbbase[(cmeta->ino) % SKYFS_LOCK_HASH_LEN]; 
     pthread_mutex_lock(&(htbp_lock->lock));
     mmeta_to->lock_htb_head = mmeta->lock_htb_head;
     
     if(!list_empty(&mmeta->posix_lock_head)){
	     memcpy(&mmeta_to->posix_lock_head, &mmeta->posix_lock_head, sizeof(struct list_head));
	     SKYFS_ERROR("try to Adjust posix_lock_head %p %p \n ", mmeta, &mmeta->posix_lock_head);
	 	/* reset next->prev and prev->next*/
	     struct list_head * posix_next = ((struct list_head *)(&mmeta_to->posix_lock_head))->next;
	     struct list_head * posix_prev = ((struct list_head *)(&mmeta_to->posix_lock_head))->prev;
	     SKYFS_ERROR(" posix_lock_head prev %p next %p \n ", posix_prev, posix_next);
	     if(posix_next == NULL){
		     SKYFS_ERROR_1("rename posix_next IS NULL\n");
	     }else{ 
		     posix_next->prev = &mmeta_to->posix_lock_head;
	     }
	     if(posix_prev == NULL){
		     SKYFS_ERROR_1("rename posix_prev IS NULL\n");
	     }else{ 
		     posix_prev->next = &mmeta_to->posix_lock_head;
	     }
	     SKYFS_ERROR("adjust posix lock  next prev success\n ");
	}
     // TODO: MAYL

     if(!list_empty(&mmeta->flock_head)){
	     memcpy(&mmeta_to->flock_head, &mmeta->flock_head, sizeof(struct list_head));
	     SKYFS_ERROR("try to Adjust flock_head %p %p \n ", mmeta, &mmeta->flock_head);
	 	/* reset next->prev and prev->next*/
	     struct list_head * flock_next = ((struct list_head *)(&mmeta_to->flock_head))->next;
	     struct list_head * flock_prev = ((struct list_head *)(&mmeta_to->flock_head))->prev;
	     SKYFS_ERROR("  flock_head prev %p next %p \n ", flock_prev, flock_next);
	     if(flock_next != NULL){
	     	flock_next->prev = &mmeta_to->flock_head;
	     }else{
		     SKYFS_ERROR_1("rename flock next is NULL \n");
	     }
	     if(flock_prev != NULL){
	     	flock_prev->next = &mmeta_to->flock_head;
	     }else{

		     SKYFS_ERROR_1("rename flock prev is NULL \n");
	     }
	     SKYFS_ERROR("adjust flock lock  next prev success\n ");
	}

     if(mmeta_to->lock_htb_head != NULL){
	      node = list_entry(mmeta_to->lock_htb_head, skyfs_M_inode_lock_t, hash_tab_head);
	      if(node != NULL){
		      if(node->ino != cmeta_to->ino){
			      SKYFS_ERROR_1("insert rename meta , ino wrong!\n");
			      exit(-1);

		      }
		      node->posix_lock_head = &mmeta_to->posix_lock_head;
		      node->flock_head = &mmeta_to->flock_head;

	      }
     }

     pthread_mutex_unlock(&(htbp_lock->lock));
    //cmeta_to->nlink = 0;

    //__skyfs_MS_update_dir_cache(subset_cache, &dir_cmeta_to, 1);

ERR:
    if(rc >= 0){
        //__skyfs_MS_copy_meta(&msgp->u.renameAck.meta, cmeta_to);
       // pthread_mutex_unlock(&(mmeta_to->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }
ERR_NONE:
    SKYFS_ERROR_1("move renamed file  cmeta to rename_subset %d, bmeta_id %d , return %d, err %d, bmeta %p \n ", subset_id, bmeta_id, rc, err_num, bmeta);


}

void __skyfs_MS_rename(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;    
   
    skyfs_ino_t        pino_from;
    skyfs_s8_t         name_from[SKYFS_MAX_NAME_LEN + 1];
    skyfs_ino_t        pino_to;
    skyfs_s8_t         name_to[SKYFS_MAX_NAME_LEN + 1];

    //skyfs_u64_t        hashkey;
    skyfs_u64_t        hashkey_to;
    skyfs_u32_t        req_type;
    skyfs_u32_t        req_id;

    skyfs_u32_t        mds_id = 0;
    skyfs_u32_t        dir_id = 0;
    skyfs_u32_t        subset_id = 0;
    //skyfs_u32_t        tmp_subset_id = 0;
    skyfs_u32_t        bmeta_id;

    skyfs_u32_t        mds_id_to = 0;
    skyfs_u32_t        dir_id_to = 0;
    skyfs_u32_t        subset_id_to = 0;
    skyfs_u32_t        tmp_subset_id_to = 0;
    skyfs_u32_t        bmeta_id_to;

    skyfs_htb_t        *htbp = NULL;
    skyfs_htb_t        *htbp_to = NULL;

    skyfs_M_subset_cache_t    *subset_cache = NULL;
    skyfs_M_bmeta_t    *bmeta = NULL;
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta;
    skyfs_M_cmeta_t    dir_cmeta;
    skyfs_ino_t        ino;

    skyfs_M_subset_cache_t    *subset_cache_to = NULL;
    skyfs_M_bmeta_t    *bmeta_to = NULL;
    skyfs_M_mmeta_t    *mmeta_to = NULL;
    skyfs_M_cmeta_t    *cmeta_to;
    skyfs_M_cmeta_t    dir_cmeta_to;
    skyfs_ino_t        ino_to;

    skyfs_M_cmeta_t    tran_cmeta;
    skyfs_meta_t       *meta = NULL;

    skyfs_s32_t        rc = 0;
    skyfs_u32_t        size;

    msgp = __skyfs_get_msg(req->req_msg);
    pino_from = msgp->u.renameReq.pino_from;
    pino_to = msgp->u.renameReq.pino_to;
    memcpy(name_from, msgp->u.renameReq.name_from, strlen(msgp->u.renameReq.name_from));
    name_from[strlen(msgp->u.renameReq.name_from)] = '\0';
    memcpy(name_to, msgp->u.renameReq.name_to, strlen(msgp->u.renameReq.name_to));
    name_to[strlen(msgp->u.renameReq.name_to)] = '\0';
    ino = msgp->u.renameReq.ino_from;
    meta = &(msgp->u.renameReq.meta);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    hashkey_to = __skyfs_name2hashvalue(name_to);
    SKYFS_ERROR_1("__skyfs_MS_rename:enter:from %llx, %s, to %llx, %s.\n", 
        pino_from, name_from, pino_to, name_to);
  
    if(meta->ino){
        /*first half process of rename has been excuted.*/
        goto second_half;
    }

    /*1. Begin to process the first_half rename, set non-valid*/ 
    htbp = __skyfs_MS_locate_subset_by_name(pino_from, name_from, &mds_id, &dir_id, &subset_id);
    if(htbp == NULL){
        if(mds_id != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    SKYFS_ERROR("rename_1\n");
    pthread_rwlock_rdlock(&(htbp->rwlock));

    mds_id = __skyfs_MS_check_htbcache(htbp);
    if(mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id);
        goto ERR_NONE;
    }

    SKYFS_ERROR("rename_2\n");
    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_rename:error:can not get subset %d in OSD\n", subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));

    SKYFS_ERROR("rename_3\n");
    bmeta = __skyfs_MS_locate_bmeta_by_name(subset_cache, name_from, &bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_rename: get bmeta:%d\n", bmeta_id);
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_rename:error:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_rdlock(&(bmeta->rwlock));
    pthread_mutex_unlock(&(bmeta->lock));

    SKYFS_ERROR("rename_4\n");
    mmeta = __skyfs_MS_locate_mmeta_by_name(bmeta, name_from);    
    if(mmeta == NULL){
        SKYFS_MSG("__skyfs_MS_rename:can not find:%s in subset:%d,bmeta:%d\n",
            name_from, subset_id, bmeta_id);
        SKYFS_MSG("__skyfs_MS_rename:sp_depth:%d,sb_depth:%d,nfree:%d,firstfree:%d\n",
            subset_cache->split_depth, subset_cache->subset_depth,
            bmeta->nfree, bmeta->firstfree);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_mutex_lock(&(mmeta->lock));
    cmeta = mmeta->cmetap;

    SKYFS_ERROR("rename_5\n");
    rc = __skyfs_MS_set_bit(mmeta->open_clt, req_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_rename:set bit error.\n");
        pthread_mutex_unlock(&(mmeta->lock));
        pthread_rwlock_unlock(&(bmeta->rwlock));
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    __skyfs_MS_update_dir_cache(subset_cache, &dir_cmeta, 0);

    SKYFS_ERROR_1("__skyfs_MS_rename:nlink:%d,type:%d,ino:%llx, dir %x, sid %d ,bmeta %d, hashkey %x, name %s, name_to %s, dir_to %x\n",
		cmeta->nlink, cmeta->type, cmeta->ino, pino_from, subset_id, bmeta->box_id, cmeta->hashkey, cmeta->name, name_to, pino_to);
    __skyfs_MS_copy_meta(meta, cmeta);

	if(cmeta->type != SKYFS_LINK){
    	cmeta->type = SKYFS_RENAME;
    	
	// added by mayl
	//cmeta->hashkey = (uint32_t)hashkey_to;
	skyfs_u64_t mask = ((skyfs_u64_t)(3<<30)) & 0x0ffffffff;
	if(cmeta->ino & mask){
		// conflict ino
		SKYFS_ERROR_1("ino %lx, mask %lx, mask_ino %lx\n", cmeta->ino, mask, (cmeta->ino & mask));
		__skyfs_MS_insert_rename_meta(dir_id,cmeta,mmeta);
		rc = __skyfs_MS_free_meta(bmeta, mmeta);
	}else{
		// normal ino
		bzero(cmeta->name, SKYFS_MAX_NAME_LEN);
	}
	// TODO: mayl need to add flock list to new mmeta
	
    	pthread_mutex_unlock(&(mmeta->lock));
	}else{
    		pthread_mutex_unlock(&(mmeta->lock));
		rc = __skyfs_MS_free_meta(bmeta, mmeta);		
	}

    pthread_rwlock_unlock(&(bmeta->rwlock));
    pthread_rwlock_unlock(&(subset_cache->rwlock));
    pthread_rwlock_unlock(&(htbp->rwlock));

    /*2. Begin to process the second_half rename, create cmeta*/ 
second_half:
relocate_subset:
    SKYFS_MSG("__skyfs_MS_rename:second half,%u\n", meta->nlink);

     htbp_to = __skyfs_MS_locate_subset_by_name(pino_to, name_to, &mds_id_to, &dir_id_to, &subset_id_to);
    if(htbp_to == NULL){
        if(mds_id_to != 0){
            rc = -ENOENT;
            __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
            goto ERR_NONE;
        }else{
            rc = -EEXIST;
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&(htbp_to->rwlock));

    mds_id_to = __skyfs_MS_check_htbcache(htbp_to);
    if(mds_id_to != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        __skyfs_MS_copy_meta(meta, &tran_cmeta);
        __skyfs_MS_forward_request(req, SKYFS_MDS, mds_id_to);
        goto ERR_NONE;
    }

    subset_cache_to = __skyfs_MS_get_subset(htbp_to, dir_id_to, subset_id_to);
    if(subset_cache_to == NULL){
        SKYFS_ERROR("__skyfs_MS_rename:error:can not get subset %d in OSD\n", 
            subset_id_to);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(subset_cache_to->rwlock));

    tmp_subset_id_to = __skyfs_get_subset_id(hashkey_to, subset_cache_to->split_depth);
    if(tmp_subset_id_to != subset_id_to){
        SKYFS_ERROR("__skyfs_MS_create:subset splited while waiting:old:%d,new:%d\n",
            subset_id_to, tmp_subset_id_to);
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
        goto relocate_subset;
    }

relocate_bmeta:
    bmeta_to = __skyfs_MS_locate_bmeta_by_name(subset_cache_to, name_to, &bmeta_id_to);
    if(bmeta_to == NULL){
        SKYFS_MSG("__skyfs_MS_rename: get bmeta:%d\n", bmeta_id_to);
        bmeta_to = __skyfs_MS_get_bmeta(subset_cache_to, bmeta_id_to);
        if(bmeta_to == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("__skyfs_MS_rename:error:can not read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto ERR;
        }
    }

    pthread_rwlock_wrlock(&(bmeta_to->rwlock));
    pthread_mutex_unlock(&(bmeta_to->lock));

    ino_to = meta->ino;
    if(bmeta_to->nfree > 0){
        rc = __skyfs_MS_alloc_meta(bmeta_to, &cmeta_to, &mmeta_to);
        /*We may free the subset lock here*/
    }else{
        SKYFS_MSG("__skyfs_MS_rename:subset_id:%d,subdepth:%d,bmeta_id:%d,nfree:%d\n", 
            subset_cache_to->subset_id, 
            subset_cache_to->subset_depth, 
            bmeta_to->box_id, bmeta_to->nfree);
        pthread_rwlock_unlock(&(bmeta_to->rwlock));
        if(subset_cache_to->split_depth < SKYFS_FIRST_SPLIT_DEPTH){
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }else if(subset_cache_to->subset_depth < SKYFS_FIRST_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache_to);
            goto relocate_bmeta;
        }else if(subset_cache_to->split_depth < SKYFS_MIDDLE_SPLIT_DEPTH){
            /*Just split the subset we needed*/
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }else if(subset_cache_to->subset_depth < SKYFS_MAX_SUBSET_DEPTH){
            rc = __skyfs_MS_enlarge_subset(subset_cache_to);
            goto relocate_bmeta;
        }else{
            /*Just split the subset we needed*/
            rc = __skyfs_MS_split_subset(subset_cache_to);
            pthread_rwlock_unlock(&(subset_cache_to->rwlock));
            pthread_rwlock_unlock(&(htbp_to->rwlock));
            goto relocate_subset;
        }
    }
   
    cmeta_to->hashkey = hashkey_to;
    cmeta_to->conflict_index = meta->conflict_index;
    //cmeta_to->conflict_index = 0;
    SKYFS_ERROR_1("__skyfs_MS_rename:%s,%llu, hash:%llx\n", 
        name_to, ino_to, cmeta_to->hashkey);

    /*copy meta to cmeta*/
    __skyfs_MS_init_cmeta(meta, mmeta_to, cmeta_to, ino_to, name_to);
    cmeta_to->type = SKYFS_LINK;
    cmeta_to->nlink = 0;

     // added by mayl for reanme dir
    if(S_ISDIR(cmeta_to->mode)){
        mmeta_to->status = 1;
        }


    __skyfs_MS_update_dir_cache(subset_cache_to, &dir_cmeta_to, 1);
    SKYFS_MSG("__skyfs_MS_rename:mmeta->id:%d,%p\n", mmeta_to->id, mmeta_to);
    rc = __skyfs_MS_set_bit(mmeta_to->open_clt, req_id, 1);

ERR:
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_rename_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        __skyfs_MS_copy_meta(&msgp->u.renameAck.meta, cmeta_to);
        pthread_mutex_unlock(&(mmeta_to->lock));
        pthread_rwlock_unlock(&(bmeta_to->rwlock));
        pthread_rwlock_unlock(&(subset_cache_to->rwlock));
        pthread_rwlock_unlock(&(htbp_to->rwlock));
    }

    msgp->error = rc;

    SKYFS_MSG("__skyfs_MS_rename:type:%d,id:%d,type:%d,id:%d.\n", 
        req->req_remote_type, req->req_remote_id, req_type, req_id);

    SKYFS_MSG("__skyfs_MS_rename:sender_handle:%llu\n", 
        req->req_msg->amh_sender_handle);

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req_type, 
             req_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_rename:send reply failed.rc:%d\n", rc); 
    } 


ERR_NONE:
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_rename:exit\n\n");
}

void __skyfs_MS_readdir_next(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t          kiov;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_bmeta_t     *real_bmeta = NULL;
    skyfs_M_subset_cache_t     *subset_cache;
    skyfs_htb_t         *htbp_sub = NULL;
    skyfs_htb_t         *htbp_bmeta = NULL;

    skyfs_u32_t         dir_id;
    skyfs_s32_t         subset_id;
    skyfs_s32_t         real_subset_id;
    skyfs_u32_t         bmeta_id;
    skyfs_u32_t         max_subset_id;
    skyfs_u32_t         max_bmeta_id;
    skyfs_u32_t         req_size;
    skyfs_u32_t         c_mds_id;
    skyfs_ino_t         ino;
    skyfs_u32_t         size = sizeof(skyfs_M_bmeta_t);
    skyfs_u32_t         hashvalue;
    skyfs_s32_t         relocate = 0;
    skyfs_s32_t         rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    ino = msgp->u.readdirReq.dino;
    subset_id = msgp->u.readdirReq.subset_id;
    bmeta_id = msgp->u.readdirReq.chunk_id;
    bmeta_id = bmeta_id & (skyfs_u32_t)0x0ffff;

    SKYFS_ERROR("__skyfs_MS_readdir_next:enter:ino:%llu,subset_id:%d,bmeta_id:%d\n", 
        ino, subset_id, bmeta_id);
    dir_id = __skyfs_MS_get_dir_id(ino, 0);
 
relocate_subset:
    if(relocate){
	real_subset_id = locate_next_subset_id(dir_id, subset_id);
	if(relocate){
	    SKYFS_ERROR("locate_next_subset , after locate next_subsetid\n");
    	}

	relocate = 1;
	if(real_subset_id <0){
		rc = -ENOENT;
		goto ERR;
	}

	subset_id = real_subset_id;
	bmeta_id = 0;
	//relocate = 0;

    }
    c_mds_id = __skyfs_MS_judge_mdsid(dir_id, subset_id);
    if(c_mds_id != mds_this_id){
           __skyfs_MS_forward_request(req, SKYFS_MDS, c_mds_id);
           goto ERR_NONE;
    }

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
    htbp_sub = &skyfs_subset_cache_htbbase[hashvalue];
   
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , before lock htbp\n");
    }

    pthread_rwlock_rdlock(&(htbp_sub->rwlock));
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after lock htbp\n");
    }

    c_mds_id = __skyfs_MS_check_htbcache(htbp_sub);
    if(c_mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, c_mds_id);
        goto ERR_NONE;
    }
    
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , before get  subset\n");
    }

    subset_cache = __skyfs_MS_get_subset(htbp_sub, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_readdir:error:canot get subset %d in OSD\n", 
            subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        goto ERR;
    }
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after get  subset\n");
    }

    pthread_rwlock_rdlock(&(subset_cache->rwlock));
    
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after rwlock  subset\n");
    }

   
    max_subset_id = (skyfs_u32_t)(1 << subset_cache->split_depth) - 1; 
    max_bmeta_id = (skyfs_u32_t)(1 << subset_cache->subset_depth) - 1; 

    SKYFS_MSG("__skyfs_MS_readdir:subdepth:%d,spdepth:%d,max_subset:%d,max_bmeta:%d\n",
        subset_cache->subset_depth,
        subset_cache->split_depth,
        max_subset_id,
        max_bmeta_id);

    if(subset_id >max_subset_id || bmeta_id >max_bmeta_id){
        rc = -EINVAL;
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        goto ERR;
    }

    if(bmeta_id == max_bmeta_id && ! relocate){

        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
	relocate = 1;
	//SKYFS_ERROR_1(" GET next subset_id for read_dir_next \n");
	goto relocate_subset;

    }else if(!relocate){
	    /* mayl: keep subset_id and increase bmeta_id */
	    bmeta_id ++;
	    SKYFS_ERROR_1(" GET next bmeta_id for read_dir_next \n");
    }


    
    pthread_mutex_lock(&subset_cache->lock);
    hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_id);
    htbp_bmeta = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp_bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_readdir:hash table:%d NULL\n", 
            hashvalue);
        goto ERR;
    }
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after Lock  subset %d, bmeta %d\n",
			    subset_id, bmeta_id);
    }

    bmeta = __skyfs_MS_find_bmeta(htbp_bmeta, bmeta_id);
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after find  subset %d, bmeta %d\n",
			    subset_id, bmeta_id);
    }

    if(bmeta != NULL){
        pthread_mutex_lock(&bmeta->lock);
        pthread_mutex_unlock(&subset_cache->lock);
    }else{
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR_1("__skyfs_MS_readdir:error:cannot read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp_sub->rwlock));
            goto ERR;
        }
    }
    
    if(relocate){
	    SKYFS_ERROR("locate_next_subset , after get  bmeta\n");
    }

    real_bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
    memcpy(real_bmeta, bmeta, sizeof(skyfs_M_bmeta_t));
    /*mayl, record subset_id */
    real_bmeta->last_one = (skyfs_s32_t)subset_id;   
    pthread_mutex_unlock(&(bmeta->lock));
    pthread_rwlock_unlock(&(subset_cache->rwlock));
    pthread_rwlock_unlock(&(htbp_sub->rwlock));

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;

    if(rc >= 0){
        SKYFS_ERROR("__skyfs_MS_readdir:init iov,size:%d,addr:%p.\n", size, bmeta);
        __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_DATA, 0, NULL, req_size);
        kiov.ak_addr = real_bmeta;
        kiov.ak_len = size;
        kiov.ak_offset = 0;
        kiov.ak_flag = 0;

        req->req_niov = 1;
        req->req_iov = &kiov;
        msgp->error = size;
    }else{
        __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);
        msgp->error = rc;
        SKYFS_ERROR("__skyfs_MS_readdir:read error:%d.\n", rc);
    }
 
    rc = amp_send_sync(mds_comp_context, req,
            req->req_remote_type, 
            req->req_remote_id, 
            0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_readdir:send failed.rc:%d\n", rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    if(real_bmeta){
        free(real_bmeta);
    } 

    if(relocate){
	    SKYFS_ERROR("locate_next_subset , relocate, return\n");
    }


    SKYFS_LEAVE("__skyfs_MS_readdir:exit,rc:%d\n", rc);
}


void __skyfs_MS_readdir(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t          kiov;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_bmeta_t     *real_bmeta = NULL;
    skyfs_M_subset_cache_t     *subset_cache;
    skyfs_htb_t         *htbp_sub = NULL;
    skyfs_htb_t         *htbp_bmeta = NULL;

    skyfs_u32_t         dir_id;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         bmeta_id;
    skyfs_u32_t         max_subset_id;
    skyfs_u32_t         max_bmeta_id;
    skyfs_u32_t         req_size;
    skyfs_u32_t         c_mds_id;
    skyfs_ino_t         ino;
    skyfs_u32_t         size = sizeof(skyfs_M_bmeta_t);
    skyfs_u32_t         hashvalue;
    skyfs_s32_t         rc = 0;
    skyfs_u32_t 	real_bmeta_id;

    msgp = __skyfs_get_msg(req->req_msg);
    ino = msgp->u.readdirReq.dino;
    subset_id = msgp->u.readdirReq.subset_id;
    bmeta_id = msgp->u.readdirReq.chunk_id;
    real_bmeta_id = bmeta_id;

    if((real_bmeta_id & (skyfs_u32_t)(1<<16)) != 0){
	    /* get next bmeta  in this dir -- by mayl */
	    __skyfs_MS_readdir_next(req);
	    goto ERR_EXIT;
    }

    SKYFS_ERROR("__skyfs_MS_readdir:enter:ino:%llu,subset_id:%d,bmeta_id:%d\n", 
        ino, subset_id, bmeta_id);
    dir_id = __skyfs_MS_get_dir_id(ino, 0);
    c_mds_id = __skyfs_MS_judge_mdsid(dir_id, subset_id);
    if(c_mds_id != mds_this_id){
           __skyfs_MS_forward_request(req, SKYFS_MDS, c_mds_id);
           goto ERR_NONE;
    }

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
    htbp_sub = &skyfs_subset_cache_htbbase[hashvalue];

    pthread_rwlock_rdlock(&(htbp_sub->rwlock));

    c_mds_id = __skyfs_MS_check_htbcache(htbp_sub);
    if(c_mds_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        __skyfs_MS_forward_request(req, SKYFS_MDS, c_mds_id);
        goto ERR_NONE;
    }

    subset_cache = __skyfs_MS_get_subset(htbp_sub, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_readdir:error:canot get subset %d in OSD\n", 
            subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(subset_cache->rwlock));
   
    max_subset_id = (skyfs_u32_t)(1 << subset_cache->split_depth) - 1; 
    max_bmeta_id = (skyfs_u32_t)(1 << subset_cache->subset_depth) - 1; 

    SKYFS_MSG("__skyfs_MS_readdir:subdepth:%d,spdepth:%d,max_subset:%d,max_bmeta:%d\n",
        subset_cache->subset_depth,
        subset_cache->split_depth,
        max_subset_id,
        max_bmeta_id);
    if(subset_id >max_subset_id || bmeta_id >max_bmeta_id){
        rc = -EINVAL;
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp_sub->rwlock));
        goto ERR;
    }
    
    pthread_mutex_lock(&subset_cache->lock);
    hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_id);
    htbp_bmeta = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp_bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_readdir:hash table:%d NULL\n", 
            hashvalue);
        goto ERR;
    }
    
    bmeta = __skyfs_MS_find_bmeta(htbp_bmeta, bmeta_id);
    if(bmeta != NULL){
        pthread_mutex_lock(&bmeta->lock);
        pthread_mutex_unlock(&subset_cache->lock);
    }else{
        bmeta = __skyfs_MS_get_bmeta(subset_cache, bmeta_id);
        if(bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_readdir:error:cannot read bmeta in OSD\n");
            pthread_rwlock_unlock(&(subset_cache->rwlock));
            pthread_rwlock_unlock(&(htbp_sub->rwlock));
            goto ERR;
        }
    }
    real_bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
    memcpy(real_bmeta, bmeta, sizeof(skyfs_M_bmeta_t));
    
    pthread_mutex_unlock(&(bmeta->lock));
    pthread_rwlock_unlock(&(subset_cache->rwlock));
    pthread_rwlock_unlock(&(htbp_sub->rwlock));

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;

    if(rc >= 0){
        SKYFS_MSG("__skyfs_MS_readdir:init iov,size:%d,addr:%p.\n", size, bmeta);
        __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_DATA, 0, NULL, req_size);
        kiov.ak_addr = real_bmeta;
        kiov.ak_len = size;
        kiov.ak_offset = 0;
        kiov.ak_flag = 0;

        req->req_niov = 1;
        req->req_iov = &kiov;
        msgp->error = size;
    }else{
        __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);
        msgp->error = rc;
        SKYFS_ERROR("__skyfs_MS_readdir:read error:%d.\n", rc);
    }
 
    rc = amp_send_sync(mds_comp_context, req,
            req->req_remote_type, 
            req->req_remote_id, 
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_readdir:send failed.rc:%d\n", rc);
    }

ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    if(real_bmeta){
        free(real_bmeta);
    } 

 ERR_EXIT:
    SKYFS_LEAVE("__skyfs_MS_readdir:exit,rc:%d\n", rc);
}

void __skyfs_MS_init_dcache(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_M_cmeta_t    cmeta;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.initdircReq.dir_id;
    memcpy(&cmeta, &msgp->u.initdircReq.dir_cmeta, sizeof(skyfs_M_cmeta_t));

    SKYFS_ENTER("__skyfs_MS_init_dcache:enter:%u\n", dir_id);

    rc = __skyfs_MS_do_init_dcache(dir_id, &cmeta, 1);
    
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_initdirc_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    
    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_init_dcache:send reply failed.rc:%d\n", rc); 
    } 
    
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_init_dcache:exit,rc:%d\n", rc);

}

void __skyfs_MS_get_dcache(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_M_dir_cache_t dir_cache;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.getdircReq.dir_id;

    SKYFS_ENTER("__skyfs_MS_get_dcache:enter:dir_id:%u\n", dir_id);

    SKYFS_ERROR("__skyfs_MS_get_dcache:dir_cache:%p\n", &dir_cache);
    rc = __skyfs_MS_do_get_dcache(dir_id, &dir_cache);
    
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_M_dir_cache_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    
    SKYFS_MSG("__skyfs_MS_get_dcache:before cp,rc:%d,msize:%d,dcache:%lu\n",
        rc, size, sizeof(skyfs_M_dir_cache_t));
    if(rc >= 0){
        memcpy(msgp->u.mtext, &dir_cache, sizeof(skyfs_M_dir_cache_t));    
    }

    msgp->error = rc;

    SKYFS_MSG("__skyfs_MS_get_dcache:before send\n");
    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_get_dcache:send reply failed.rc:%d\n", rc); 
    } 
    
    SKYFS_MSG("__skyfs_MS_get_dcache:after send\n");
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_get_dcache:exit,rc:%d\n", rc);

}

void __skyfs_MS_update_dcache(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_M_cmeta_t    dir_cmeta;
    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_s32_t        update;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.updatedircReq.dir_id;
    subset_id = msgp->u.updatedircReq.subset_id;
    update = msgp->u.updatedircReq.update;

    SKYFS_ENTER("__skyfs_MS_update_dcache:enter:dir:%u,subset_id:%d,update:%d,id:%d\n", 
        dir_id, subset_id, update, req->req_remote_id);

    SKYFS_MSG("__skyfs_MS_update_dcache:dir_cmeta:%p\n", &dir_cmeta);
    rc = __skyfs_MS_do_update_dcache(dir_id, subset_id, update, &dir_cmeta);
    
    size = AMP_SKYFS_MSGHEAD_SIZE + SKYFS_MAX_MSGBODY_SIZE;
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    
    if(rc >= 0){
        memcpy(msgp->u.mtext, &dir_cmeta, sizeof(skyfs_M_cmeta_t));    
    }

    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_update_dcache:send reply failed.rc:%d\n", rc); 
    } 
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_update_dcache:exit,rc:%d\n", rc);

}

void __skyfs_MS_update_ddepth(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        split_depth;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.updatedirdReq.dir_id;
    subset_id = msgp->u.updatedirdReq.subset_id;
    split_depth = msgp->u.updatedirdReq.split_depth;

    SKYFS_ENTER("__skyfs_MS_update_ddepth:enter:dir:%u,subset_id:%d,splitd:%d,id:%d\n", 
        dir_id, subset_id, split_depth, req->req_remote_id);

    rc = __skyfs_MS_do_update_ddepth(dir_id, subset_id, split_depth);
    
    size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    
    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_update_ddepth:send reply failed.rc:%d\n", rc); 
    } 
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_update_ddepth:exit,rc:%d\n", rc);
}

void __skyfs_MS_create_subindex(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_u32_t        dir_id;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        subset_depth;
    skyfs_u32_t        nlink;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dir_id = msgp->u.createsubiReq.dir_id;
    subset_id = msgp->u.createsubiReq.subset_id;
    subset_depth = msgp->u.createsubiReq.subset_depth;
    nlink = msgp->u.createsubiReq.nlink;

    SKYFS_ENTER("__skyfs_MS_create_subindex:enter:dir:%u,subset:%d,subdept:%d,nlink:%d\n",
        dir_id, subset_id, subset_depth, nlink);

    rc = __skyfs_MS_do_create_subindex(dir_id, subset_id, subset_depth, nlink);
    
    size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    
    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_create_subindex:send reply failed.rc:%d\n", rc); 
    } 
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_MS_create_subindex:exit,rc:%d\n", rc);

}

void __skyfs_MS_get_state(amp_request_t *req)
{
    skyfs_state_info_t state_info;
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_u32_t        layout_version;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_MS_get_state:enter,fromid:%d\n", req->req_remote_id);

    msgp = __skyfs_get_msg(req->req_msg);
    layout_version = msgp->ver;

    if(mds_this_id != SKYFS_MASTER_MDS_ID){
        __skyfs_MS_check_layout_version(layout_version, req->req_remote_id);
    }

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getstate_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    rc = __skyfs_MS_collect_state(&state_info);

    msgp->error = rc;
    if(rc >= 0){
        memcpy(&(msgp->u.getstateAck.state_info), &state_info, 
            sizeof(skyfs_state_info_t));
    }

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_get_state:send reply failed.rc:%d\n", rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_get_state:exit,rc:%d\n", rc);

}

void __skyfs_MS_add_htbcache(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        index;
    skyfs_u32_t        last_flag;
    skyfs_u32_t        size;
    skyfs_u32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    index = msgp->u.addhtbReq.index;
    last_flag = msgp->u.addhtbReq.last_flag;

    SKYFS_ENTER("__skyfs_MS_add_hashcache:enter,add_index:%d\n", index);

    mds_layout[index].id = mds_this_id;
    skyfs_subset_cache_htbbase[index].length = 0;
    if(last_flag > 0){
        //mds_layout_version ++;
    }

    size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_add_htbcache:send reply failed.rc:%d\n", rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_add_htbcache:exit,rc:%d\n", rc);
}

void __skyfs_MS_get_layout(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_MS_get_layout:enter,remote_id:%d,ver:%d\n",
        req->req_remote_id, mds_layout_version);

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getlayout_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);


    pthread_mutex_lock(&mds_layout_lock);

    msgp->error = rc;
    if(rc >= 0){
        memcpy(&(msgp->u.getlayoutAck.layout), &mds_layout, 
            sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);
        msgp->u.getlayoutAck.layout_version = mds_layout_version;
    }

    pthread_mutex_unlock(&mds_layout_lock);

    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_get_layout:send reply to %d %d failed.rc:%d\n", 
            req->req_remote_type, req->req_remote_id, rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_get_layout:exit,rc:%d\n", rc);

}

void __skyfs_MS_init_config(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_MS_init_config:enter,remote_id:%d\n",
        req->req_remote_id);

    msgp = __skyfs_get_msg(req->req_msg);

	memcpy(&arch_info, 
		&(msgp->u.initconfigReq.arch_info), 
		sizeof(skyfs_arch_info_t));

	rc = __skyfs_init_nodes(&arch_info, &mds_info, &osd_info, &client_info);

    sem_post(&mds_config_sem);

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_initconfig_ack_t);
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;
    rc = amp_send_sync(mds_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_MS_init_config:send reply %d %d err:%d\n", 
            req->req_remote_type, req->req_remote_id, rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_MS_init_config:exit,rc:%d\n", rc);

}
/*This is end of mds_op.c*/
