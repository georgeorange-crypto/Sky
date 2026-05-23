/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_help.c $
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

#include "osd_init.h"
#include "osd_fs.h"
#include "osd_ito.h"


extern skyfs_u32_t             skyfs_data_stripe_cnt;
uint32_t find_osdgid(uint32_t osd_cnt, skyfs_ino_t ino, size_t obj_id);

static int mkdir_recursive(char *path);

skyfs_s32_t    
__skyfs_SS_init_reply(amp_request_t **req, 
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
        SKYFS_MSG("__skyfs_SS_init_reply:alloc reply failed\n");
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


skyfs_s32_t __skyfs_SS_forward_request(amp_request_t *req,
				skyfs_u32_t com_type,
				skyfs_u32_t id)
{
	skyfs_s32_t rc = 0;
	skyfs_msg_t *msgp;

	msgp = __skyfs_get_msg(req->req_msg);
	SKYFS_ERROR("__skyfs_SS_forward_request:enter:type:%d,id:%d,msg_type:%d\n",
		com_type, id, msgp->type);

	SKYFS_MSG("__skyfs_SS_forward_request:sender_handle:%lld\n", 
		req->req_msg->amh_sender_handle);

	req->req_need_ack = SKYFS_NEEDNOT_ACK;

	rc = amp_send_sync(osd_comp_context,
			req,
			com_type,
			id,
			0);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_forward_request:forward to %d %d failed\n",
			com_type, id);
	}


	SKYFS_ERROR("__skyfs_SS_forward_request:exit,rc:%d\n", rc);
	return rc;

}

#if 0
skyfs_s32_t __skyfs_SS_get_osdpid(skyfs_u32_t osd_id)
{
	skyfs_u32_t i;
	skyfs_s32_t osdpid = -1;

	for(i = 0; i < osd_num; i++){
		if(osd_info.osd[i].id == osd_id){
			osdpid = osd_info.osd[i].pid;
			break;
		}
	}

	return osdpid;
}
#endif

skyfs_u32_t __skyfs_SS_get_osdid(void)
{
	skyfs_s8_t ip[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t str[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t hostname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t tmp_hostname[SKYFS_MAX_NAME_LEN];
	FILE *fp_hosts = NULL;
	skyfs_u32_t osd_id = 0;
	skyfs_u32_t i;

	SKYFS_ENTER("__skyfs_SS_get_osdid:enter\n");

	/*1. get hostname*/
	__skyfs_get_hostname(hostname, str, skyfs_ib_flag);

	SKYFS_ERROR_1("__skyfs_SS_get_osdid:get this hostname:%s\n", hostname);

	/*2. get host ip*/
	fp_hosts = fopen("/etc/hosts", "r");
	while(fgets(str, SKYFS_MAX_NAME_LEN, fp_hosts)){
		bzero(ip, SKYFS_MAX_NAME_LEN);
		bzero(tmp_hostname, SKYFS_MAX_NAME_LEN);
		sscanf(str, "%s %s", ip, tmp_hostname);
		SKYFS_ERROR_1("__skyfs_SS_get_osdid:get hostname:%s,ip:%s\n", tmp_hostname, ip);
		if(strcmp(tmp_hostname, hostname) == 0){
			SKYFS_ERROR_1("match __skyfs_SS_get_osdid:get hostname:%s,ip:%s\n", hostname, ip);
			break;
		}
		bzero(str, SKYFS_MAX_NAME_LEN);
	}

	if(strlen(ip) == 0){
		SKYFS_ERROR("__skyfs_SS_get_osdid:can't find %s in /etc/hosts\n", hostname);
		goto err_out;
	}

	/*3. get host id*/
	for(i = 0; i < SKYFS_MAX_OSD_NUM; i ++){
		if(osd_info.osd[i].id > 0){
			if(strncmp(osd_info.osd[i].ip[0]->addr, ip, strlen(ip)) == 0 &&
					osd_info.osd[i].ip[0]->lid == skyfs_lid){
				osd_id = osd_info.osd[i].id;
				SKYFS_ERROR_1("__skyfs_SS_get_osdid:osdid:%s,%d\n", hostname, osd_id);
				break;
			}
		}
	}

err_out:

	SKYFS_ERROR("__skyfs_SS_get_osdid:exit.osd_id:%d\n", osd_id);

	return osd_id;
}

skyfs_s32_t
__skyfs_SS_compose_partname(skyfs_ino_t ino,
					skyfs_u32_t partition_id,
					skyfs_u32_t replica_id,
					skyfs_s8_t  *pname)
{
	char replica_part_dir[256];
	skyfs_s32_t rc = 0;
	struct stat buf;

	SKYFS_MSG("%s:partition pathname:%p\n", __FUNCTION__, pname);
	sprintf( replica_part_dir, "%s/rep-%u-partdir",
                SKYFS_OBJ_PATH, replica_id);
 	if((rc = stat(replica_part_dir, &buf)) == -1){
                rc = mkdir(replica_part_dir, 0666);
                if(rc < 0 ){
                        SKYFS_ERROR_1("%s:create partdir:%s,err:%d, try recursive\n",
                                        __FUNCTION__, replica_part_dir, errno);
                                                rc = mkdir_recursive(replica_part_dir);

                        }
                if(rc < 0 && errno != EISDIR){
                        SKYFS_ERROR("%s:create partdir:%s,err:%d\n", __FUNCTION__, partdir, errno);
                        goto ERR;
                }
        }


	sprintf(pname, "%s/rep-%u-partdir/%llu-%u", 
		SKYFS_OBJ_PATH, replica_id,ino, partition_id);

	SKYFS_MSG("%s:partition pathname:%s\n", __FUNCTION__, pname);
ERR:
	return rc;
}

skyfs_s32_t
__skyfs_SS_compose_chunkfile_pathname(skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id, 
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_s8_t *chunkfile)
{
	skyfs_s32_t rc = 0;
	//skyfs_s8_t  tmp_path[SKYFS_MAX_NAME_LEN];
	//struct stat buf;


	sprintf(chunkfile, "%s/%d/%d/%d/%llu-%llu", 
		SKYFS_OBJ_PATH, skyfs_lid, subset_id, chunk_id, ino, obj_id);

#if 0
	sprintf(tmp_path, "%s/%d/%d", SKYFS_OBJ_PATH, subset_id, chunk_id);

	if(stat(chunkfile, &buf) == -1){
		rc = -errno;
		SKYFS_ERROR("__skyfs_SS_compose_chunfile:stat %s err:%d\n", 
			chunkfile, rc);	
		if((rc = mkdir(tmp_path, 0666)) < 0){
			rc = -errno;
			if(rc == -EEXIST){
				rc = 0;
			}
			SKYFS_ERROR("__skyfs_SS_compose_chunfile:create %s err:%d\n", 
				tmp_path, -errno);	
		}
	}
#endif
	SKYFS_MSG("__skyfs_SS_compose_chunkfile_pathname:%s\n", chunkfile);

	return rc;
}

skyfs_s32_t
__skyfs_SS_create_chunkdir(skyfs_u32_t dl_subset_id, skyfs_u32_t dl_chunk_id)
{
	skyfs_s32_t rc = 0;
	skyfs_s8_t  chunkdir[SKYFS_MAX_NAME_LEN];
	struct stat buf;

	SKYFS_ERROR("__skyfs_SS_create_chunkdir:enter,subset_id:%d,chunk_id:%d\n", 
		dl_subset_id, dl_chunk_id);

	sprintf(chunkdir, "%s/%d/%d/%d", 
		SKYFS_OBJ_PATH, skyfs_lid, dl_subset_id, dl_chunk_id);
	
	if((rc = stat(chunkdir, &buf)) == -1){
		SKYFS_ERROR("__skyfs_create_chunkdir:stat %s not exist\n", chunkdir);	
	}else{
		goto OUT;
	}

	if((rc = mkdir(chunkdir, 0666)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_create_chunkdir:mk chunkdir:%s err:%d\n", 
			chunkdir, rc);
	}
OUT:
		
	SKYFS_ERROR("__skyfs_SS_create_chunkdir:chunkdir:%s rc:%d\n", 
		chunkdir, rc);
	return rc;
}

skyfs_s32_t
__skyfs_SS_move_obj(skyfs_DL_entry_t *dl_entry, 
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id,
				skyfs_u32_t new_subset_id, 
				skyfs_u32_t new_chunk_id,
				skyfs_u32_t osd_id)
{
	skyfs_s8_t  chunkfile[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t  newpath[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t rc = 0;

	if(dl_entry->ino == 0 && dl_entry->obj_id == 0){
		goto EXIT;
	}

	if(subset_id == new_subset_id && chunk_id == new_chunk_id){
		goto EXIT;
	}

	if(osd_id == osd_this_id){
		sprintf(chunkfile, "%s/%d/%d/%d/%llu-%llu", 
			SKYFS_OBJ_PATH, skyfs_lid, subset_id, chunk_id, 
			dl_entry->ino, dl_entry->obj_id);

		sprintf(newpath, "%s/%d/%d/%d/%llu-%llu", 
			SKYFS_OBJ_PATH, skyfs_lid, new_subset_id, new_chunk_id,
			dl_entry->ino, dl_entry->obj_id);

		if(rename(chunkfile, newpath) < 0){
       		rc = -errno;
        	SKYFS_ERROR("__skyfs_SS_move_obj:error:rename %sto%s,ino:%llu,obj_id:%llu.\n",
           		chunkfile, newpath, dl_entry->ino, dl_entry->obj_id);
    	}
	}else{
		sprintf(chunkfile, "%s/%d/%d/%d/%llu-%llu",
			SKYFS_OBJ_PATH, skyfs_lid, subset_id, chunk_id,
			dl_entry->ino, dl_entry->obj_id);
		rc = __skyfs_O2O_move_obj(osd_id, new_subset_id, new_chunk_id,
				dl_entry->ino, dl_entry->obj_id, chunkfile);
		if(rc >= 0){
			if(unlink(chunkfile) < 0){
        		SKYFS_ERROR("__skyfs_SS_move_obj:err unlink old %s,%d\n",
					chunkfile, errno);
			}
		}else{
        	SKYFS_ERROR("__skyfs_SS_move_obj:err move %s to osd:%d\n", 
				chunkfile, osd_id);
		}
	}
EXIT:
    SKYFS_LEAVE("__skyfs_SS_move_obj:exit:rc:%d,ino:%llu,obj_id:%llu.\n", 
		rc, dl_entry->ino, dl_entry->obj_id);

	return rc;
}

skyfs_s32_t
__skyfs_SS_prepareopen_objfile(skyfs_ino_t ino,
		skyfs_u32_t partition_id,
		skyfs_u64_t obj_id,
		skyfs_u32_t replica_id,
		skyfs_s8_t *path)
{
	skyfs_s32_t rc = 0;
	skyfs_s8_t          objfile[SKYFS_MAX_NAME_LEN];
    	skyfs_s8_t          partdir[SKYFS_MAX_NAME_LEN];
	struct stat buf;

	//uint32_t osd_gid = find_osdgid(osd_num,ino,obj_id);
	skyfs_u32_t         stripe_num = ((ino+obj_id)% skyfs_data_stripe_cnt);
	sprintf(partdir, "%s/%d-%d/%d/%llu-%d", SKYFS_OBJ_PATH, skyfs_lid, stripe_num,replica_id,ino, partition_id);

	/* DIR / lid-strip_num / replica_id / ino-obj_id */
	sprintf(objfile, "%s/%d-%d/%d/%llu-%d/%llu-%llu", 
		SKYFS_OBJ_PATH, skyfs_lid, stripe_num,replica_id,ino,partition_id, ino, obj_id);
	
	if((rc = stat(partdir, &buf)) == -1){
		rc = mkdir(partdir, 0666);
		if(rc < 0 ){
			SKYFS_ERROR_1("%s:create partdir:%s,err:%d, try recursive\n",
                                        __FUNCTION__, partdir, errno);
						rc = mkdir_recursive(partdir);

			}
		if(rc < 0 && errno != EISDIR){
			SKYFS_ERROR("%s:create partdir:%s,err:%d\n", __FUNCTION__, partdir, errno);
			goto ERR;
		}
	}	
	memcpy(path, objfile, sizeof(skyfs_s8_t)*SKYFS_MAX_NAME_LEN);
ERR:
	SKYFS_MSG("%s:open %s exit.replica_id:%d\n",__FUNCTION__, objfile, replica_id);
	return rc;
}


static int mkdir_recursive(char *path) {
    char subpath[256];
    size_t len;
    int n = 0;

    len = strlen(path);
    if(len >= sizeof(subpath))
        return -1;

    // Create a copy of the path
    strcpy(subpath, path);

     SKYFS_ERROR_1("start mkdir %s ,  \n", subpath);

    // Iterate over each part of the path
    for(char *p = subpath; *p; p++) {
        if(*p == '/' && (n!= 0)) {
            *p = 0;
     	    SKYFS_ERROR_1("try mkdir %s ,  \n", subpath);
            if(access(subpath, F_OK) != 0) {
                if(mkdir(subpath, 0755) != 0) {
		    SKYFS_ERROR_1("mkdir %s failed, err %d\n", subpath, errno);
                    //return -1;
                }
            }
            *p = '/';
        }else{
		n++;
	}

    }

    // Create the last part of the path
    if(mkdir(subpath, 0755) != 0) {
        if(errno != EEXIST) {
            return -1;
        }
    }

    return 0;
}


skyfs_s32_t
__skyfs_SS_open_obj_file(skyfs_ino_t ino,
		skyfs_u32_t partition_id,
		skyfs_u64_t obj_id,
		skyfs_u32_t replica_id,
		skyfs_s32_t flag)
{
	skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;
	skyfs_s8_t          objfile[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          partdir[SKYFS_MAX_NAME_LEN];
    //uint32_t osd_gid = find_osdgid(osd_num,ino,obj_id);
    skyfs_u32_t         stripe_num = ((ino+obj_id)% skyfs_data_stripe_cnt);
	struct stat buf;
#if 0
	sprintf(partdir, "%s/%d-%d/%llu-%d", SKYFS_OBJ_PATH, skyfs_lid, stripe_num, ino, partition_id);

	sprintf(objfile, "%s/%d-%d/%llu-%d/%llu-%llu.%d", 
		SKYFS_OBJ_PATH, skyfs_lid, stripe_num, ino, partition_id, ino, obj_id, replica_id);

	sprintf(partdir, "%s/%d-%d-%d-%d/%llu-%d", SKYFS_OBJ_PATH, skyfs_lid, osd_gid, replica_id,stripe_num,ino, partition_id);

	sprintf(objfile, "%s/%d-%d-%d-%d/%llu-%d/%llu-%llu.%d", 
		SKYFS_OBJ_PATH, skyfs_lid, stripe_num,ino, osd_gid, replica_id,partition_id, ino, obj_id, replica_id);
#endif
	sprintf(partdir, "%s/%d-%d/%d/%llu-%d", SKYFS_OBJ_PATH, skyfs_lid, stripe_num,replica_id,ino, partition_id);

	/* DIR / lid-strip_num / replica_id / ino-obj_id */
	sprintf(objfile, "%s/%d-%d/%d/%llu-%d/%llu-%llu", 
		SKYFS_OBJ_PATH, skyfs_lid, stripe_num,replica_id,ino,partition_id, ino, obj_id);


	if(flag == 1){
		if((fd = open(objfile, O_RDWR|O_CREAT, 0666)) < 0){
       		SKYFS_ERROR("%s:can not open objfile:%s,%d\n", 
            	__FUNCTION__, objfile, errno);
        	if((rc = errno) == ENOENT){
				if((rc = stat(partdir, &buf)) == -1){
					rc = mkdir(partdir, 0666);
					if(rc < 0 ){
						 SKYFS_ERROR("%s:create partdir:%s,err:%d, try recursive\n",
                                        	__FUNCTION__, partdir, errno);
						 rc = mkdir_recursive(partdir);

					}
					if(rc < 0 && errno != EEXIST){
						SKYFS_ERROR("%s:create partdir:%s,err:%d\n", 
            				__FUNCTION__, partdir, errno);
						fd = rc;
						goto ERR;
					}
				}
        	}else{
				fd = rc;
				goto ERR;
			}
			fd = open(objfile, O_RDWR|O_CREAT, 0666);
			if(fd < 0){
				SKYFS_ERROR("%s:reopen %s err,errno:%d\n", __FUNCTION__, objfile, errno);
				goto ERR;
			}
    	}
	}else{
		if(flag == 0)
			fd = open(objfile, O_RDONLY);
		else
			fd = open(objfile, O_RDWR);
		if(fd < 0){
			SKYFS_ERROR("%s:open %s err,errno:%d\n", __FUNCTION__, objfile, errno);
			if(errno == ENOENT){
				fd = 0;
			}
			goto ERR;
		}

	}
ERR:
	SKYFS_ERROR("%s:open %s exit.fd:%d,flag:%d,replica_id:%d\n",
		__FUNCTION__, objfile, fd, flag, replica_id);
	return fd;
}

skyfs_s32_t
__skyfs_SS_open_subset_dir(skyfs_u32_t subset_id, 
		skyfs_u32_t chunk_id,
		skyfs_ino_t ino,
		skyfs_u64_t obj_id)
{
	skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;
	skyfs_s8_t          chunkfile[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          subsetdir[SKYFS_MAX_NAME_LEN];
	struct stat buf;

	sprintf(subsetdir, "%s/%d/%d", SKYFS_OBJ_PATH, skyfs_lid, subset_id);

    rc = __skyfs_SS_compose_chunkfile_pathname(subset_id, 
                    chunk_id, 
                    ino, 
                    obj_id, 
                    chunkfile);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_open_subset_dir:compose chunkfile err,rc:%d\n", 
            rc);
        goto ERR;
    }

    if((fd = open(chunkfile, O_WRONLY|O_CREAT, 0666)) < 0){
        SKYFS_ERROR("__skyfs_SS_open_subset_dir:can not open chunkfile:%s,%d\n", 
            chunkfile, errno);
        if((rc = errno) == ENOENT){
			if((rc = stat(subsetdir, &buf)) == -1){
				rc = mkdir(subsetdir, 0666);
				if(rc < 0){
					SKYFS_ERROR("__skyfs_SS_open_subset_dir:create sub:%s,err:%d\n", 
            			subsetdir, errno);
					goto ERR;
				}
			}
            if((rc = __skyfs_SS_create_chunkdir(subset_id, chunk_id)) == 0){
                fd = open(chunkfile, O_WRONLY|O_CREAT, 0666);
                if(fd < 0){
					rc = -errno;
					SKYFS_ERROR("__skyfs_SS_open_subset_dir:open chunkfile:%s,err:%d\n", 
            			subsetdir, errno);
					goto ERR;
                }else{
					goto CONT;
				}
            }
        }
        goto ERR;
    }

CONT:
	return fd;

ERR:
	return rc;
}

skyfs_s32_t
__skyfs_OSD_init_req(amp_request_t **req, 
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
    msg->fromid = osd_this_id;
    msg->fromType = SKYFS_OSD;
    // added by mayl for rdma
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
/*This is end of osd_help.c*/
