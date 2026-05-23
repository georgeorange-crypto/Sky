/* 
 *  Copyright (c) 2013 by XING JING 
 *  All rights reserved.
 *  Written by Xing Jing */ 
/*
 * $Id: osd_dl.c $
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

#include "osd_fs.h"
#include "osd_init.h"
#include "osd_dl.h"
#include "osd_ito.h"
#include "osd_layout.h"
#include "osd_help.h"
#include "osd_loadb.h"
#include "osd_replica.h"
#include "interval_tree.h"

skyfs_htb_t skyfs_writecache_htbbase[SKYFS_DLSUBSET_HASH_LEN];
skyfs_htb_t skyfs_dlsubset_cache_htbbase[SKYFS_DLSUBSET_HASH_LEN];
skyfs_htb_t skyfs_special_replica_htbbase[SKYFS_OSD_CONSIST_HASH_LEN];


skyfs_DL_head_t skyfs_dl_head;
skyfs_DL_head_t skyfs_replica_head;
skyfs_DL_depth_t skyfs_dl_depth;

pthread_mutex_t total_dl_subset_num_lock;
skyfs_u32_t total_dl_subset_num;

pthread_mutex_t total_dl_subseti_num_lock;
skyfs_u32_t total_dl_subseti_num;

pthread_mutex_t total_dl_chunk_num_lock;
skyfs_u32_t total_dl_chunk_num;

pthread_mutex_t osd_wb_request_queue_lock;
struct list_head osd_wb_request_queue;
sem_t osd_wb_request_queue_sem;


skyfs_u32_t   skyfs_profile_flag = 1;
skyfs_u32_t   skyfs_dlsubset_num;

skyfs_u32_t   osd_obj_index = 0;

skyfs_state_info_t osd_state_info;


skyfs_htb_t * 
get_special_replica_htbbase()
{
	return (skyfs_htb_t *)(&(skyfs_special_replica_htbbase[0]));
}

skyfs_s32_t
__skyfs_SS_init_data_layout(void)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t i;

	skyfs_DL_subset_head_t dl_subset_head;
	skyfs_DL_subset_t      *dl_subset = NULL;
	skyfs_u32_t   subset_id;
	skyfs_s8_t    string[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t   osd_id;
	skyfs_s32_t   fd;
	goto exit_init;

	skyfs_dlsubset_num = (skyfs_u32_t)1 << skyfs_default_dlsb_bits;

	bzero(&skyfs_dl_depth, sizeof(skyfs_DL_depth_t));
	pthread_mutex_init(&skyfs_dl_depth.lock, NULL);
	INIT_LIST_HEAD(&skyfs_dl_depth.depth_list);

	pthread_mutex_init(&total_dl_subset_num_lock, NULL);
	pthread_mutex_init(&total_dl_subseti_num_lock, NULL);
	pthread_mutex_init(&total_dl_chunk_num_lock, NULL);

	total_dl_subset_num =  SKYFS_MAX_DLSUBSET_NUM;
	total_dl_subseti_num =  SKYFS_MAX_DLSUBSET_INDEX_NUM;
	total_dl_chunk_num =  SKYFS_MAX_DLCHUNK_NUM;

	SKYFS_MSG("__skyfs_SS_init_dl:init htbbase.\n");
	for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i++){
		INIT_LIST_HEAD(&skyfs_dlsubset_cache_htbbase[i].head);
		pthread_mutex_init(&skyfs_dlsubset_cache_htbbase[i].lock, NULL);
		skyfs_dlsubset_cache_htbbase[i].length = -(SKYFS_MASTER_MDS_ID);
	}

	for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i++){
		INIT_LIST_HEAD(&skyfs_writecache_htbbase[i].head);
		pthread_mutex_init(&skyfs_writecache_htbbase[i].lock, NULL);
	}

	/*Create data layout file 0*/
	subset_id = 0;
	bzero(&dl_subset_head, sizeof(skyfs_DL_subset_head_t));
	if(osd_this_id == SKYFS_MASTER_OSD_ID){
		SKYFS_MSG("__skyfs_SS_init_dl:get first subset.\n");
		bzero(&skyfs_dl_head, sizeof(skyfs_DL_head_t));
		dl_subset = __skyfs_DL_alloc_subset();
		if(dl_subset == NULL){
			SKYFS_ERROR("__skyfs_SS_init_dl:new alloc subset: NULL\n");
			goto ERR;
		}

		rc = __skyfs_DL_read_subset(dl_subset, pad_id, subset_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_SS_init_dl:system not exist before\n");
			rc = __skyfs_DL_release_subset(dl_subset);

			for(i = 0; i < skyfs_dlsubset_num; i ++){
	            		bzero(&dl_subset_head, sizeof(skyfs_DL_subset_head_t));
				subset_id = i;		
				dl_subset_head.subset_id = i;
				dl_subset_head.split_depth = skyfs_default_dlsb_bits;

				osd_id = __skyfs_DL_judge_osdid(subset_id);
				if(osd_id == osd_this_id){
					rc = __skyfs_DL_do_create_subset(&dl_subset_head);
					if(rc < 0){
						SKYFS_ERROR("__skyfs_SS_init_dl:wr sub %d err:%d\n",
						    subset_id , rc);
						goto ERR;
					}
				}else{
					rc = __skyfs_O2O_create_dl_subset(osd_id,1,&dl_subset_head);
					if(rc < 0){
						SKYFS_ERROR("__skyfs_SS_init_dl:cr sub at %d err:%d\n",
							osd_id , rc);
						goto ERR;
					}
				}
				
				rc = __skyfs_set_bit(skyfs_dl_head.subset_bm, subset_id, 1);
				if(rc < 0){
					SKYFS_ERROR("__skyfs_SS_init_data_layout:set bit err:%d\n",
						rc);
					goto ERR;
				}

				if(osd_dl_version == 0){
					osd_dl_version = 1;
				}else{
					osd_dl_version ++;
				}
			}
			skyfs_dl_head.depth = skyfs_default_dlsb_bits;
			skyfs_dl_depth.depth = skyfs_default_dlsb_bits;
            skyfs_dl_depth.ver = skyfs_dl_head.ver;	
			memcpy(skyfs_dl_depth.subset_bm, skyfs_dl_head.subset_bm, 
				sizeof(skyfs_s8_t)* SKYFS_DLSUBSET_BM_LEN);

        	sprintf(string, "%s%s", SKYFS_DL_PATH, "skyfs_dl_head");
			fd = open(string ,O_RDWR|O_CREAT, 0644);
			if(fd < 0){
				SKYFS_ERROR("__skyfs_SS_init_data_layout:open %s err:%d\n",
					string, errno);
				goto ERR;
			}

            rc = write(fd, &skyfs_dl_head, sizeof(skyfs_DL_head_t));
			if(rc < 0){
              	SKYFS_ERROR("__skyfs_SS_init_data_layout:write %s err:%d\n",
					string, errno);
				goto ERR;
			}

		}else{
			/*Read skyfs_dl_depth and skyfs_dl_head from file*/
			sprintf(string, "%s%s", SKYFS_DL_PATH, "skyfs_dl_head");
			fd = open(string ,O_RDONLY);
			if(fd < 0){
				SKYFS_ERROR("__skyfs_SS_init_data_layout:open %s err:%d\n",
					string, errno);
				goto ERR;
			}

            rc = read(fd, &skyfs_dl_head, sizeof(skyfs_DL_head_t));
			if(rc < 0){
              	SKYFS_ERROR("__skyfs_SS_init_data_layout:read %s err:%d\n",
					string, errno);
				goto ERR;
			}

			skyfs_dl_depth.depth = skyfs_dl_head.depth;
		    skyfs_dl_depth.ver = skyfs_dl_head.ver;	
			memcpy(skyfs_dl_depth.subset_bm, skyfs_dl_head.subset_bm, 
				sizeof(skyfs_s8_t)* SKYFS_DLSUBSET_BM_LEN);
		}

	}else{
		osd_dl_version = 1;
	}

exit_init:
	pthread_mutex_init(&skyfs_dl_head.lock, NULL);
	__skyfs_init_htb(SKYFS_DLSUBSET_HASH_LEN, 
		&skyfs_dl_head.subset_hash_base);

	__skyfs_init_htb(SKYFS_DL_PARTITION_HASH_LEN, 
		&skyfs_dl_head.partition_hash_base);

	pthread_mutex_init(&skyfs_replica_head.lock, NULL);
	//__skyfs_init_htb(SKYFS_OSD_CONSIST_HASH_LEN, 
	//	&skyfs_special_replica_htbbase);
	for(i = 0; i <  SKYFS_OSD_CONSIST_HASH_LEN; i++){
                INIT_LIST_HEAD(&skyfs_special_replica_htbbase[i].head);
                pthread_mutex_init(&skyfs_special_replica_htbbase[i].lock, NULL);
        }
	SKYFS_ERROR_1("special replica_htbbase %p \n", get_special_replica_htbbase());

	 //skyfs_special_replica_htbbase[SKYFS_OSD_CONSIST_HASH_LEN]



ERR:

	SKYFS_ERROR("__skyfs_SS_init_data_layout:exit\n");
	return rc;
}

skyfs_s32_t
__skyfs_DL_get_head(skyfs_DL_head_t *dl_head, skyfs_u32_t pad_id)
{
	skyfs_s32_t rc = 0;
	SKYFS_ENTER("__skyfs_DL_get_head:enter,osd_this_id:%d\n", osd_this_id);

	if(osd_this_id != SKYFS_MASTER_OSD_ID){
		rc = __skyfs_O2O_get_dl_head(SKYFS_MASTER_OSD_ID, pad_id, dl_head);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_get_head:get head failed at osd %d\n", 
				SKYFS_MASTER_OSD_ID);		
			goto ERR;
		}
	}else{
		pthread_mutex_lock(&skyfs_dl_head.lock);
		memcpy(dl_head, &skyfs_dl_head, sizeof(skyfs_DL_head_t));	
		pthread_mutex_unlock(&skyfs_dl_head.lock);
	}

ERR:
	SKYFS_LEAVE("__skyfs_DL_get_head:exit.\n");
	return rc;
}

skyfs_DL_depth_t *
__skyfs_DL_get_depth(skyfs_u32_t pad_id)
{
	skyfs_DL_depth_t *dl_depth = NULL;
	skyfs_DL_head_t  *dl_head;
	skyfs_s32_t      rc = 0;

	SKYFS_MSG("__skyfs_DL_get_depth: enter.\n");


	pthread_mutex_lock(&skyfs_dl_depth.lock);

	//if(skyfs_dl_depth.ver == 0 || skyfs_dl_depth.ver < osd_dl_version){
	if(skyfs_dl_depth.ver < osd_dl_version){
		dl_head = (skyfs_DL_head_t *)malloc(sizeof(skyfs_DL_head_t));

		rc = __skyfs_DL_get_head(dl_head, pad_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_get_depth: error get dl_head\n");
			goto ERR;
		}
		SKYFS_MSG("__skyfs_DL_get_depth: after get head.size head:%lu\n",
			sizeof(skyfs_DL_head_t));

		skyfs_dl_depth.depth = dl_head->depth;
		skyfs_dl_depth.ver = osd_dl_version;
		SKYFS_MSG("__skyfs_DL_get_depth: before copy\n");
		memcpy(skyfs_dl_depth.subset_bm, dl_head->subset_bm, SKYFS_DLSUBSET_BM_LEN);
		SKYFS_MSG("__skyfs_DL_get_depth:depthver:%d,dl_ver:%d\n",
			skyfs_dl_depth.ver, osd_dl_version);
		dl_depth = &skyfs_dl_depth;
		free(dl_head);
	}else{
		dl_depth = &skyfs_dl_depth;
	}

ERR:
	SKYFS_MSG("__skyfs_DL_get_depth: exit\n");
	return dl_depth;
}

skyfs_u32_t
__skyfs_DL_get_subsetid(skyfs_DL_depth_t *dl_depth, 
				skyfs_ino_t ino, 
				skyfs_u64_t obj_id)
{
	skyfs_u32_t subset_id = 0;
	skyfs_u64_t hashvalue;
	skyfs_s32_t split_depth;
	
	hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
	split_depth = dl_depth->depth;

retry_lower:
	subset_id = __skyfs_get_subset_id(hashvalue, split_depth);
	if(__skyfs_test_bit(dl_depth->subset_bm, subset_id) == 0 && split_depth >= 0){
		SKYFS_MSG("__skyfs_DL_get_subsetid:%d not exist, split_depth:%d\n",
			subset_id, split_depth);
		split_depth --;
		goto retry_lower;
	}

	SKYFS_MSG("__skyfs_DL_get_subset_id:subset_id:%d,split_depth:%d\n", 
		subset_id, split_depth);

	return subset_id;
}

skyfs_u32_t
__skyfs_DL_get_chunkid(skyfs_u32_t subset_depth,
				skyfs_u64_t hashvalue)
{
	skyfs_u32_t chunk_id = 0;
	skyfs_u32_t filter;

	filter = ((skyfs_u32_t)1 << SKYFS_AVA_HASH_BITS);
	hashvalue = hashvalue % filter;

	chunk_id = (skyfs_u32_t)((skyfs_u32_t)hashvalue 
				>> (SKYFS_AVA_HASH_BITS - subset_depth));
	
	SKYFS_MSG("__skyfs_DL_get_chunkid:hashvalue:%lld,chunk_id:%d,subset_depth:%d\n", 
		hashvalue, chunk_id, subset_depth);

	return chunk_id;
}

skyfs_u32_t 
__skyfs_DL_judge_osdid(skyfs_u32_t subset_id)
{
	skyfs_u32_t osd_id = 0;
	skyfs_u32_t hashvalue;

	hashvalue = __skyfs_get_subset_hashvalue(pad_id, subset_id);
	osd_id = __skyfs_SS_search_osd_dlextent(hashvalue);

	return osd_id;
}

skyfs_u32_t
__skyfs_SS_check_dl_htbcache(skyfs_htb_t *htbp)
{
	skyfs_u32_t osd_id = 0;

	return osd_id;
}

skyfs_s32_t
__skyfs_DL_do_create_subset(skyfs_DL_subset_head_t *dl_subset_head)
{
	skyfs_u32_t osd_replica2;
	skyfs_u32_t osd_replica3;
	
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_DL_do_create_subset:enter\n");

	rc = __skyfs_SS_choose_replica_place(dl_subset_head->subset_id,
			&osd_replica2,
			&osd_replica3);

	dl_subset_head->fir_osd = osd_this_id;
	dl_subset_head->sec_osd = osd_replica2;
	dl_subset_head->thi_osd = osd_replica3;

	rc = __skyfs_DL_create_local_subset(dl_subset_head);

	if(osd_num > 1 && skyfs_replica > 0){
		if(osd_num >= 2){
			rc = __skyfs_O2O_create_dl_subset(osd_replica2, 2, dl_subset_head);
		}
		if(osd_num >= 3){
			rc = __skyfs_O2O_create_dl_subset(osd_replica3, 3, dl_subset_head);
		}
	}

	SKYFS_LEAVE("__skyfs_DL_do_create_subset:exit\n");

	return rc;
}

skyfs_s32_t
__skyfs_DL_create_local_subset(skyfs_DL_subset_head_t *dl_subset_head)
{
	skyfs_s8_t  dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t  obj_path_dl[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t  obj_path_dl_chunk[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t fd;
	skyfs_s32_t rc = 0;

    sprintf(dl_subset_fname, "%s%d", 
        SKYFS_DL_PATH, 
        dl_subset_head->subset_id);
 
	sprintf(obj_path_dl, "%s/%d/%d", 
        SKYFS_OBJ_PATH, 
		skyfs_lid,
        dl_subset_head->subset_id);
    
	sprintf(obj_path_dl_chunk, "%s/%d/%d/%d", 
        SKYFS_OBJ_PATH, 
		skyfs_lid,
        dl_subset_head->subset_id, 0);
 
    SKYFS_MSG("__skyfs_DL_create_local_subset:before create %s,sec_osd %d\n",
		dl_subset_fname, dl_subset_head->sec_osd);

    fd = open(dl_subset_fname, O_RDWR|O_CREAT, 0666);
    if(fd < 0){
		if(errno == EEXIST){
			SKYFS_ERROR("__skyfs_DL_create_local_subset:%s exist\n", 
				dl_subset_fname);
		}else{
        	rc = -errno;
        	SKYFS_ERROR("__skyfs_DL_create_local_subset:create %s err:%d\n", 
				dl_subset_fname, rc);
        	goto ERR;
		}
    }

	if(write(fd, dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_create_local_subset:write file %s err:%d\n",
			dl_subset_fname, rc);
		goto ERR;
	}

	if(mkdir(obj_path_dl, 0666) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_create_local_subset:create dir %s err:%d\n",
			obj_path_dl, rc);
		goto ERR;
	}

	if(mkdir(obj_path_dl_chunk, 0666) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_create_local_subset:create dir %s err:%d\n",
			obj_path_dl_chunk, rc);
		goto ERR;
	}

ERR:

	SKYFS_LEAVE("__skyfs_DL_create_local_subset:exit\n");

	if(fd){
		close(fd);
	}

	return rc;
}

skyfs_DL_subset_t *
__skyfs_DL_find_subset(skyfs_htb_t *htbp, 
				skyfs_u32_t pad_id, 
				skyfs_u32_t subset_id)
{
	skyfs_DL_subset_t *dl_subset = NULL, *tmp = NULL;
	struct list_head *head = NULL, *index = NULL;

	SKYFS_ENTER("__skyfs_DL_find_subset:enter.subset_id:%d\n",
		subset_id);

	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_MSG("__skyfs_DL_find_subset:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_subset_t, subset_hash);
		SKYFS_MSG("__skyfs_DL_find_subset:subset_id:%d\n",
			tmp->subset_id);
		if(tmp->subset_id == subset_id){
			dl_subset = tmp;
			goto OUT;
		}
	}
ERR:
OUT:

	SKYFS_LEAVE("__skyfs_DL_find_subset:leave,%p.\n", dl_subset);
	return dl_subset;
}

skyfs_DL_subset_t *
__skyfs_DL_alloc_subset()
{
	skyfs_DL_subset_t *dl_subset = NULL;
	
	SKYFS_ENTER("__skyfs_DL_alloc_subset:enter\n");

	pthread_mutex_lock(&total_dl_subset_num_lock);
	if(total_dl_subset_num > SKYFS_MAX_DLSUBSET_NUM/2){
		total_dl_subset_num --;
		dl_subset = (skyfs_DL_subset_t *)malloc(sizeof(skyfs_DL_subset_t));
	}else{
		SKYFS_ERROR("__skyfs_DL_alloc_subset:no enough space for subset:%d\n",
			total_dl_subset_num);
		total_dl_subset_num --;
		dl_subset = (skyfs_DL_subset_t *)malloc(sizeof(skyfs_DL_subset_t));
	}
	pthread_mutex_unlock(&total_dl_subset_num_lock);

	SKYFS_LEAVE("__skyfs_DL_alloc_subset:exit\n");
	return dl_subset;
}

skyfs_s32_t 
__skyfs_DL_read_subset(skyfs_DL_subset_t *dl_subset, 
				skyfs_u32_t pad_id, 
				skyfs_u32_t subset_id)
{
	skyfs_s8_t  dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;
	skyfs_u32_t osd_id;
	skyfs_DL_subset_head_t dl_subset_head;

	SKYFS_ENTER("__skyfs_DL_read_subset:enter.subset_id:%d\n", subset_id);

	osd_id = __skyfs_DL_judge_osdid(subset_id);
	if(osd_id < 0){
		SKYFS_ERROR("__skyfs_DL_read_subset:get osd_id failed\n");
		rc = osd_id;
		goto err_out;
	}

	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);	

	fd = open(dl_subset_fname, O_RDONLY);
	if(fd < 0){
		SKYFS_ERROR("__skyfs_DL_read_subset:cannot open %s, errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto err_out;
	}
	
	bzero(&dl_subset_head, sizeof(skyfs_DL_subset_head_t));
	if(read(fd, &dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		SKYFS_ERROR("__skyfs_DL_read_subset:read subset errno:%d\n", errno);
		rc = -errno;
		goto err_out;
	}

	dl_subset->subset_id = subset_id;
	dl_subset->split_depth = dl_subset_head.split_depth;
	dl_subset->subset_depth = dl_subset_head.subset_depth;
	dl_subset->nlink_origin = dl_subset_head.nlink;
    dl_subset->nlink_update = 0;
	dl_subset->fir_osd = dl_subset_head.fir_osd;
	dl_subset->sec_osd = dl_subset_head.sec_osd;
	dl_subset->thi_osd = dl_subset_head.thi_osd;

	SKYFS_MSG("__skyfs_DL_read_subset:sp_depth:%d,sb_depth:%d,nlink:%d,secosd:%d\n",
		dl_subset->split_depth, 
		dl_subset->subset_depth, 
		dl_subset_head.nlink,
		dl_subset->sec_osd);

err_out:
	SKYFS_LEAVE("__skyfs_DL_read_subset:read %s exit.\n", dl_subset_fname);

	if(fd){
		close(fd);
	}

	SKYFS_LEAVE("__skyfs_DL_read_subset:exit.subset_id:%d,rc:%d\n", 
		subset_id, rc);

	return rc;
}

skyfs_s32_t 
__skyfs_DL_write_subset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s8_t  dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;
	skyfs_u32_t osd_id;
	skyfs_DL_subset_head_t dl_subset_head;

	SKYFS_ENTER("__skyfs_DL_write_subset:enter.subset_id:%d,nlink_up:%d,nlink_or:%d\n", 
		dl_subset->subset_id, dl_subset->nlink_update, dl_subset->nlink_origin);

	osd_id = __skyfs_DL_judge_osdid(dl_subset->subset_id);
	if(osd_id < 0){
		SKYFS_ERROR("__skyfs_DL_write_subset:get osd_id failed\n");
		rc = osd_id;
		goto err_out;
	}

	bzero(&dl_subset_head, sizeof(skyfs_DL_subset_head_t));
	dl_subset_head.subset_id = dl_subset->subset_id;
	dl_subset_head.split_depth = dl_subset->split_depth; 
	dl_subset_head.subset_depth = dl_subset->subset_depth;
	dl_subset_head.nlink = dl_subset->nlink_origin + dl_subset->nlink_update; 
	dl_subset_head.fir_osd = dl_subset->fir_osd;
	dl_subset_head.sec_osd = dl_subset->sec_osd;
	dl_subset_head.thi_osd = dl_subset->thi_osd;

	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, dl_subset->subset_id);	

	fd = open(dl_subset_fname, O_RDWR);
	if(fd < 0){
		SKYFS_ERROR("__skyfs_DL_write_subset:cannot open %s, errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto err_out;
	}
	
	if(write(fd, &dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		SKYFS_ERROR("__skyfs_DL_write_subset:write subset errno:%d\n", errno);
		rc = -errno;
		goto err_out;
	}

	SKYFS_MSG("__skyfs_DL_write_subset:sp_depth:%d,sb_depth:%d,nlink:%d\n",
		dl_subset->split_depth, dl_subset->subset_depth, dl_subset_head.nlink);

err_out:
	if(fd){
		close(fd);
	}

	SKYFS_LEAVE("__skyfs_DL_write_subset:exit.subset_id:%d,rc:%d\n", 
		dl_subset->subset_id, rc);

	return rc;
}
skyfs_s32_t 
__skyfs_DL_release_subset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_DL_release_subset:enter\n");

	pthread_mutex_lock(&total_dl_subset_num_lock);
	total_dl_subset_num ++;
	pthread_mutex_unlock(&total_dl_subset_num_lock);

	SKYFS_LEAVE("__skyfs_DL_release_subset:exit.dl_subset:%p\n", dl_subset);

	free(dl_subset);
	return rc;
}

skyfs_s32_t 
__skyfs_DL_free_subset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_DL_free_subset:enter\n");

	list_del_init(&(dl_subset->chunk_head));
	list_del_init(&(dl_subset->subset_hash));

	free(dl_subset->chunk_hash_base);
	
	__skyfs_DL_release_subset(dl_subset);

	SKYFS_LEAVE("__skyfs_DL_free_subset:exit.\n");

	return rc;
}


skyfs_DL_subset_index_t *
__skyfs_DL_alloc_subset_index()
{
	skyfs_DL_subset_index_t *dl_subset_index = NULL;

	pthread_mutex_lock(&total_dl_subseti_num_lock);
	if(total_dl_subseti_num > SKYFS_MAX_DLSUBSET_INDEX_NUM/2){
		total_dl_subseti_num --;
		dl_subset_index = 
			(skyfs_DL_subset_index_t *)malloc(sizeof(skyfs_DL_subset_index_t));
	}else{
		SKYFS_ERROR("__skyfs_DL_alloc_subset_index:need to reclaim:total:%d\n\n",
			total_dl_subseti_num);
		dl_subset_index =
			(skyfs_DL_subset_index_t *)malloc(sizeof(skyfs_DL_subset_index_t));
	}
	pthread_mutex_unlock(&total_dl_subseti_num_lock);


	SKYFS_LEAVE("__skyfs_DL_alloc_subset_index:exit.dl_subset_index:%p\n", 
		dl_subset_index);
	return dl_subset_index;
}

skyfs_DL_subset_index_t *
__skyfs_DL_find_subset_index(skyfs_htb_t *htbp,
				skyfs_u32_t pad_id,
				skyfs_u32_t subset_id)
{
	skyfs_DL_subset_index_t *dl_subset_index = NULL, *tmp = NULL;
	struct list_head *head = NULL, *index = NULL;

	SKYFS_ENTER("__skyfs_DL_find_subset_index:enter.subset_id:%d\n",subset_id);

	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_ERROR("__skyfs_DL_find_subset_index:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_subset_index_t, subset_hash);
		if(tmp->subset_id == subset_id){
			dl_subset_index = tmp;
			goto OUT;
		}
	}

ERR:
OUT:
	SKYFS_LEAVE("__skyfs_DL_find_subset_index:leave.\n");
	return dl_subset_index;
}

skyfs_s32_t
__skyfs_DL_do_create_subset_index(skyfs_u32_t subset_id, 
				skyfs_u32_t subset_depth, 
				skyfs_u32_t nlink)
{
	skyfs_DL_subset_index_t *dl_subset_index = NULL;
	skyfs_htb_t *dl_subseti_htbp = NULL;
	skyfs_u64_t hashvalue;
	skyfs_s32_t rc = 0;

	pthread_mutex_lock(&skyfs_dl_head.lock);
	dl_subset_index = __skyfs_DL_alloc_subset_index();
	if(dl_subset_index == NULL){
		pthread_mutex_unlock(&skyfs_dl_head.lock);
		rc = -ENOMEM;
		goto ERR;
	}
	dl_subset_index->subset_id = subset_id;
	dl_subset_index->subset_depth = subset_depth;
	dl_subset_index->nlink_origin = nlink;
	dl_subset_index->nlink_update = 0;
	
	hashvalue = __skyfs_get_subset_hashvalue(pad_id, subset_id);
	hashvalue = hashvalue % SKYFS_DL_SUB_INDEX_HASH_LEN;
	dl_subseti_htbp = &(skyfs_dl_head.subset_hash_base[hashvalue]);
	list_add(&(dl_subset_index->subset_hash), &(dl_subseti_htbp->head));
	pthread_mutex_init(&(dl_subset_index->lock), NULL);
	pthread_rwlock_init(&(dl_subset_index->rwlock), NULL);

	pthread_mutex_unlock(&skyfs_dl_head.lock);

ERR:

	return rc;
}

skyfs_htb_t *
__skyfs_SS_locate_dl_subset(skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *osd_id)
{
	skyfs_htb_t *htbp = NULL;
	skyfs_u32_t hashvalue = 0;
	skyfs_u32_t c_osd_id;
	skyfs_DL_depth_t *dl_depth = NULL;

	dl_depth = __skyfs_DL_get_depth(pad_id);
	if(dl_depth == NULL){
		SKYFS_ERROR("__skyfs_SS_locate_dl_subset:get dir_depth NULL\n");
		goto ERR;
	}

	SKYFS_MSG("__skyfs_SS_locate_dl_subset:depth:%d\n", dl_depth->depth);

	if(dl_depth->depth == 0){
		*subset_id = 0;
	}else{
		*subset_id = __skyfs_DL_get_subsetid(dl_depth, ino, obj_id);
	}

	pthread_mutex_unlock(&skyfs_dl_depth.lock);

	c_osd_id = __skyfs_DL_judge_osdid(*subset_id);
	if(c_osd_id != osd_this_id){
		*osd_id = c_osd_id;
		goto OUT;
	}else{
		*osd_id = 0;
		hashvalue = __skyfs_get_subset_hashvalue(pad_id, *subset_id);
		hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
		htbp = &skyfs_dlsubset_cache_htbbase[hashvalue];
		if(htbp == NULL){
			SKYFS_ERROR("__skyfs_SS_locate_dl_subset:locate htbp error:%d\n",
				*subset_id);
			goto ERR;
		}
	}

OUT:
ERR:
	SKYFS_ERROR("__skyfs_SS_locate_dl_subset:hashvalue:%d,subset_id:%d\n",
		hashvalue, *subset_id);

	return htbp;
}

skyfs_DL_subset_t *
__skyfs_SS_get_dl_subset(skyfs_htb_t *htbp,
				skyfs_u32_t subset_id)
{
	skyfs_DL_subset_t *dl_subset = NULL;
	skyfs_DL_subset_index_t *dl_subset_index = NULL;
	skyfs_htb_t       *dl_subseti_htbp = NULL;
	skyfs_u32_t       hashvalue;
	skyfs_u32_t       osd_id = 0;
	skyfs_s32_t       rc = 0;

	SKYFS_ENTER("__skyfs_SS_get_dl_subset:enter.subset_id:%d\n", subset_id);

	pthread_mutex_lock(&(htbp->lock));

	dl_subset = __skyfs_DL_find_subset(htbp, pad_id, subset_id);
	if(dl_subset != NULL){
		SKYFS_MSG("__skyfs_DL_get_subset:get the right subset\n");
		goto find_dl_subset;
	}else{
		dl_subset = __skyfs_DL_alloc_subset();
		if(dl_subset == NULL){
			SKYFS_ERROR("__skyfs_DL_get_subset:new alloc subset: NULL\n");
			goto ERR;
		}

		rc = __skyfs_DL_read_subset(dl_subset, pad_id, subset_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_get_subset:read subset_head failed\n");
			rc = __skyfs_DL_release_subset(dl_subset);
			dl_subset = NULL;
			goto ERR;
		}

		if(osd_this_id == SKYFS_MASTER_OSD_ID){
			pthread_mutex_lock(&skyfs_dl_head.lock);	
			dl_subset_index = __skyfs_DL_alloc_subset_index();
			if(dl_subset_index == NULL){
				SKYFS_ERROR("__skyfs_DL_get_subset:new alloc sub_index:NULL\n");
				rc = __skyfs_DL_release_subset(dl_subset);
				dl_subset = NULL;
				pthread_mutex_unlock(&skyfs_dl_head.lock);
				goto ERR;
			}
			dl_subset_index->subset_id = subset_id;
			dl_subset_index->subset_depth = dl_subset->subset_depth;
			dl_subset_index->nlink_origin = dl_subset->nlink_origin;
			dl_subset_index->nlink_update = 0;
			hashvalue = __skyfs_get_subset_hashvalue(pad_id, subset_id);
			hashvalue = hashvalue % SKYFS_DL_SUB_INDEX_HASH_LEN;
			dl_subseti_htbp = &(skyfs_dl_head.subset_hash_base[hashvalue]);
			list_add(&(dl_subset_index->subset_hash), &(dl_subseti_htbp->head));
			pthread_mutex_init(&(dl_subset_index->lock), NULL);
			pthread_rwlock_init(&(dl_subset_index->rwlock), NULL);

			pthread_mutex_unlock(&skyfs_dl_head.lock);

			pthread_mutex_init(&(dl_subset->lock), NULL);
			pthread_rwlock_init(&(dl_subset->rwlock), NULL);
			INIT_LIST_HEAD(&(dl_subset->chunk_head));
			INIT_LIST_HEAD(&(dl_subset->subset_hash));
			list_add(&(dl_subset->subset_hash), &(htbp->head));
			__skyfs_init_htb(SKYFS_DL_CHUNK_HASH_LEN, &dl_subset->chunk_hash_base);
		}else{
			osd_id = SKYFS_MASTER_OSD_ID;
			SKYFS_ERROR("__skyfs_DL_get_subset:send to osd %d to create subi:%d\n",
				osd_id, subset_id);
			rc = __skyfs_O2O_create_dl_subset_index(osd_id, pad_id, subset_id,
					dl_subset->subset_depth, dl_subset->nlink_origin);
			if(rc < 0){
				SKYFS_ERROR("__skyfs_DL_get_subset:create sub_index failed osd %d\n",
					osd_id);
				goto ERR;
			}

			pthread_mutex_init(&(dl_subset->lock), NULL);
			pthread_rwlock_init(&(dl_subset->rwlock), NULL);
			INIT_LIST_HEAD(&(dl_subset->chunk_head));
			INIT_LIST_HEAD(&(dl_subset->subset_hash));
			list_add(&(dl_subset->subset_hash), &(htbp->head));
			__skyfs_init_htb(SKYFS_DL_CHUNK_HASH_LEN, &dl_subset->chunk_hash_base);

		}
	}
find_dl_subset:

ERR:
	pthread_mutex_unlock(&(htbp->lock));

	if(dl_subset){
		//rc = __skyfs_MS_move_wb_entry(dir_id, subset_id);
	}

	SKYFS_LEAVE("__skyfs_DL_get_subset:exit\n");

	return dl_subset;
}

skyfs_DL_chunk_t *
__skyfs_DL_find_chunk(skyfs_htb_t *htbp, skyfs_u32_t chunk_id)
{
	skyfs_DL_chunk_t *dl_chunk = NULL, *tmp = NULL;
	struct list_head *head = NULL, *index = NULL;

	SKYFS_ENTER("__skyfs_DL_find_chunk:enter.chunk_id:%d\n", chunk_id);

	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_MSG("__skyfs_DL_find_chunk:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_chunk_t, chunk_hash);
		if(tmp->chunk_id == chunk_id){
			dl_chunk = tmp;
			SKYFS_LEAVE("__skyfs_DL_find_chunk:chunk_id:%d,firstfree:%d\n",
				chunk_id, dl_chunk->firstfree);
			goto OUT;
		}
	}
OUT:
ERR:

	SKYFS_LEAVE("__skyfs_DL_find_chunk:exit.\n");
	return dl_chunk;
}

skyfs_DL_chunk_t *
__skyfs_DL_alloc_chunk(skyfs_u32_t pad_id, skyfs_u32_t subset_id)
{
	skyfs_DL_chunk_t *dl_chunk = NULL;
	skyfs_M_wb_req_t *req = NULL;
	skyfs_s32_t      tmp_chunk_num = 0;

	SKYFS_ENTER("__skyfs_DL_alloc_chunk:enter.\n");
	pthread_mutex_lock(&total_dl_chunk_num_lock);
	if(total_dl_chunk_num > SKYFS_MAX_DLCHUNK_NUM/2){
		total_dl_chunk_num --;
		dl_chunk = (skyfs_DL_chunk_t *)malloc(sizeof(skyfs_DL_chunk_t));
		if(dl_chunk == NULL){
			SKYFS_ERROR("__skyfs_DL_alloc_chunk:alloc memory errno:%d\n", errno);
			pthread_mutex_unlock(&total_dl_chunk_num_lock);
			goto ERR;
		}
		SKYFS_ERROR("__skyfs_DL_alloc_chunk:total_chunk_num:%d,subset_id:%d\n",
			total_dl_chunk_num, subset_id);
	}else{
		SKYFS_ERROR("__skyfs_DL_alloc_chunk:no enough chunk:%d\n", 
			total_dl_chunk_num);
		total_dl_chunk_num --;
		dl_chunk = (skyfs_DL_chunk_t *)malloc(sizeof(skyfs_DL_chunk_t));
		if(dl_chunk == NULL){
			SKYFS_ERROR("__skyfs_DL_allock_chunk:alloc memroy errno:%d\n", errno);
			pthread_mutex_unlock(&total_dl_chunk_num_lock);
			goto ERR;
		}
		tmp_chunk_num = total_dl_chunk_num;
	}
	pthread_mutex_unlock(&total_dl_chunk_num_lock);

	if(tmp_chunk_num){
		req = (skyfs_M_wb_req_t *)malloc(sizeof(skyfs_M_wb_req_t));
		if(req == NULL){
			SKYFS_ERROR("__skyfs_DL_alloc_chunk:alloc wb req err:%d\n", errno);
			goto ERR;
		}
		req->total_bmeta_num = tmp_chunk_num;
		INIT_LIST_HEAD(&(req->req_list));
		pthread_mutex_lock(&osd_wb_request_queue_lock);
		list_add_tail(&(req->req_list), &(osd_wb_request_queue));
		pthread_mutex_unlock(&osd_wb_request_queue_lock);
		sem_post(&osd_wb_request_queue_sem);
	}

ERR:

	SKYFS_LEAVE("__skyfs_DL_alloc_chunk:exit.tmp_bmeta:%p\n", dl_chunk);
	return dl_chunk;
}

skyfs_s32_t 
__skyfs_DL_init_chunk(skyfs_DL_chunk_t *dl_chunk, 
				skyfs_u32_t chunk_id)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t hashvalue;
	skyfs_u32_t i;

	SKYFS_MSG("__skyfs_DL_init_chunk:init %d\n", chunk_id);

	hashvalue = __skyfs_get_bmeta_hashvalue(chunk_id);
	dl_chunk->hashvalue = hashvalue;
	dl_chunk->chunk_id = chunk_id;
	dl_chunk->nfree = SKYFS_DLENTRY_PER_CHUNK;
	dl_chunk->firstfree = 0;
	dl_chunk->nlink_origin = dl_chunk->nlink_update = 0;
	//dl_chunk->first_osd = osd_this_id;
	
	pthread_mutex_init(&dl_chunk->lock, NULL);
	pthread_rwlock_init(&dl_chunk->rwlock, NULL);
	INIT_LIST_HEAD(&dl_chunk->chunk_hash);
	INIT_LIST_HEAD(&dl_chunk->chunk_list);

	for(i = 0; i < SKYFS_DLENTRY_PER_CHUNK; i ++){
		//dl_chunk->dlfile[i].id = i;
		//remove by mayl
		//pthread_mutex_init(&dl_chunk->dlfile[i].lock, NULL);
		dl_chunk->dlfile[i].nextfree = i + 1;
		dl_chunk->dlfile[i].hashkey = 0;
	}

	for(i = 0; i < SKYFS_DLENTRY_PER_CHUNK; i ++){
		//removed by mayl
		//pthread_mutex_init(&dl_chunk->dlfile[i].lock, NULL);
		dl_chunk->dlfile[i].nextfree = i + 1;
	}

	return rc;
}

skyfs_s32_t 
__skyfs_DL_read_chunk(skyfs_DL_chunk_t *dl_chunk, 
				skyfs_u32_t pad_id, 
				skyfs_u32_t subset_id, 
				skyfs_u32_t chunk_id)
{
	skyfs_s32_t rc = 0;
	skyfs_s8_t  dl_subset_fname[SKYFS_MAX_NAME_LEN];
	//skyfs_DL_file_t *dl_file= NULL;
	skyfs_u32_t offset;
	skyfs_s32_t fd;
	//skyfs_u32_t i;

	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);
	offset = chunk_id * sizeof(skyfs_DL_chunk_t) + sizeof(skyfs_DL_subset_head_t);

	fd = open(dl_subset_fname, O_RDONLY);
	if(fd < 0){
		SKYFS_ERROR("__skyfs_DL_read_chunk:can not open dlsubset file:%s, errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto ERR;
	}

	if(pread(fd, dl_chunk, sizeof(skyfs_DL_chunk_t), offset) < 0){
		SKYFS_ERROR("__skyfs_DL_read_chunk:read subfile %s failed,errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto ERR;
	}

	pthread_mutex_init(&dl_chunk->lock, NULL);
	pthread_rwlock_init(&dl_chunk->rwlock, NULL);
	INIT_LIST_HEAD(&dl_chunk->chunk_hash);
	INIT_LIST_HEAD(&dl_chunk->chunk_list);

   /*should be useless, delete it 
	for(i = 0; i < SKYFS_DLENTRY_PER_CHUNK; i ++){
		dl_entry = &dl_chunk->dlentry[i];
		dl_entry->id = i;
		pthread_mutex_init(&dl_entry->lock, NULL);
	}
	*/

	SKYFS_MSG("__skyfs_DL_read_chunk:subset_id:%d,chunk_id:%d,hashvalue:%llu.\n",
		subset_id, chunk_id, dl_chunk->hashvalue);

	if(dl_chunk->hashvalue == 0){
		rc = -1;
	}
ERR:

	if(fd){
		close(fd);
	}

	return rc;
}

skyfs_s32_t 
__skyfs_DL_write_chunk(skyfs_DL_chunk_t *dl_chunk, skyfs_u32_t subset_id)
{
	skyfs_s32_t rc = 0;
	skyfs_s8_t  dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t offset;
	skyfs_s32_t fd;

	SKYFS_MSG("__skyfs_DL_write_chunk:chunk hash:%llu\n", dl_chunk->hashvalue);
	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);
	offset = dl_chunk->chunk_id * sizeof(skyfs_DL_chunk_t) 
			+ sizeof(skyfs_DL_subset_head_t);

	fd = open(dl_subset_fname, O_WRONLY);
	if(fd < 0){
		SKYFS_ERROR("__skyfs_DL_write_chunk:can not open dlsubset file:%s, errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto ERR;
	}

	if(pwrite(fd, dl_chunk, sizeof(skyfs_DL_chunk_t), offset) < 0){
		SKYFS_ERROR("__skyfs_DL_write_chunk:write subfile %s failed,errno:%d\n",
			dl_subset_fname, errno);
		rc = -errno;
		goto ERR;
	}

ERR:

	if(fd){
		close(fd);
	}

	return rc;

}


skyfs_s32_t __skyfs_DL_release_chunk(skyfs_DL_chunk_t *dl_chunk)
{
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_DL_release_chunk:enter\n");

	pthread_mutex_lock(&total_dl_chunk_num_lock);
	total_dl_chunk_num ++;
	SKYFS_ERROR("__skyfs_DL_release_chunk:total_num:%d\n", total_dl_chunk_num);
	pthread_mutex_unlock(&total_dl_chunk_num_lock);
	return rc;
}


skyfs_s32_t __skyfs_DL_free_chunk(skyfs_DL_chunk_t *dl_chunk)
{
	skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_DL_free_chunk:enter.chunk_id:%d\n",
		dl_chunk->chunk_id);

    list_del_init(&(dl_chunk->chunk_hash));
    list_del_init(&(dl_chunk->chunk_list));

	rc = __skyfs_DL_release_chunk(dl_chunk);

	return rc;
}

skyfs_DL_chunk_t *
__skyfs_SS_locate_dl_chunk(skyfs_DL_subset_t *dl_subset,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *chunk_id)
{
	skyfs_DL_chunk_t *dl_chunk = NULL;
	skyfs_htb_t *htbp = NULL;
	skyfs_u64_t hashvalue;

	SKYFS_ENTER("__skyfs_SS_locate_dl_chunk:enter, ino:%lld, obj_id:%lld\n",
		ino, obj_id);

	pthread_mutex_lock(&dl_subset->lock);

	hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
	*chunk_id = __skyfs_DL_get_chunkid(dl_subset->subset_depth, hashvalue);
	hashvalue = __skyfs_get_bmeta_hashvalue(*chunk_id);

	SKYFS_MSG("__skyfs_SS_locate_dl_chunk:chunk_id:%d, hashvalue:%lld\n",
		*chunk_id, hashvalue);

	htbp = &(dl_subset->chunk_hash_base[hashvalue]);
	if(htbp == NULL){
		SKYFS_ERROR("__skyfs_SS_locate_dl_chunk: hash table NULL\n");
		goto ERR;
	}

	dl_chunk = __skyfs_DL_find_chunk(htbp, *chunk_id);
	if(dl_chunk == NULL){
		SKYFS_MSG("__skyfs_SS_locate_dl_chunk:can not find chunk\n");
		goto ERR;
	}

	pthread_mutex_lock(&dl_chunk->lock);

ERR:
	if(dl_chunk !=NULL){
		pthread_mutex_unlock(&dl_subset->lock);
	}

	SKYFS_LEAVE("__skyfs_SS_locate_dl_chunk:exit\n");

	return dl_chunk;
}

skyfs_DL_chunk_t *
__skyfs_SS_get_dl_chunk(skyfs_DL_subset_t *dl_subset,
				skyfs_u32_t chunk_id)
{
	skyfs_s32_t rc = 0;
	skyfs_DL_chunk_t *dl_chunk = NULL;
	skyfs_htb_t *htbp = NULL;
	skyfs_u32_t hashvalue;

	SKYFS_ENTER("__skyfs_SS_get_dl_chunk:enter.chunk_id:%d\n", chunk_id);

	dl_chunk = __skyfs_DL_alloc_chunk(pad_id, dl_subset->subset_id);
	if(dl_chunk == NULL){
		SKYFS_ERROR("__skyfs_SS_get_dl_chunk:new alloc chunk NULL\n");
		goto ERR_NONE;
	}

	bzero(dl_chunk, sizeof(skyfs_DL_chunk_t));
	if(dl_subset->subset_id < skyfs_dlsubset_num
		&& dl_subset->nlink_origin == 0
		&& dl_subset->nlink_update == 0){
		SKYFS_ERROR("__skyfs_SS_get_dl_chunk:init dl_chunk subset_id:%d, dlsubset_num:%d,chunk_id:%d,nlink_origin:%d\n",
			dl_subset->subset_id, skyfs_dlsubset_num, 
			chunk_id, dl_subset->nlink_origin);
		rc = __skyfs_DL_init_chunk(dl_chunk, chunk_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_SS_get_dl_chunk:init dl_chunk failed:%d\n", rc);
			goto ERR;
		}
	}else{
		rc = __skyfs_DL_read_chunk(dl_chunk, pad_id,
				dl_subset->subset_id, chunk_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_SS_get_dl_chunk:read chunk from OSD error\n");
			goto ERR;
		}

		if(dl_chunk->nfree == 0){
			SKYFS_MSG("__skyfs_SS_get_dl_chunk:dl_chunk:%d, nfree=0\n",
				dl_chunk->chunk_id);
		}
	}

	hashvalue = dl_chunk->hashvalue;
	htbp = &(dl_subset->chunk_hash_base[hashvalue]);
	if(htbp == NULL){
		SKYFS_ERROR("__skyfs_SS_get_dl_chunk:hash table NULL,hashvalue:%d\n",
			hashvalue);
		goto ERR;
	}

	pthread_mutex_lock(&dl_chunk->lock);

	list_add_tail(&(dl_chunk->chunk_hash), &(htbp->head));
	list_add_tail(&(dl_chunk->chunk_list), &(dl_subset->chunk_head));

	pthread_mutex_unlock(&dl_subset->lock);

	SKYFS_LEAVE("__skyfs_SS_get_dl_chunk:exit.chunk:nfree:%d,hashvalue:%lld,id:%d\n",
		dl_chunk->nfree, dl_chunk->hashvalue, dl_chunk->chunk_id);
	return dl_chunk;

ERR:
	rc = __skyfs_DL_release_chunk(dl_chunk);
	if(dl_chunk){
		free(dl_chunk);
	}

ERR_NONE:
	pthread_mutex_unlock(&dl_subset->lock);

	SKYFS_LEAVE("__skyfs_SS_get_dl_chunk:get chunk failed. exit\n");
	return NULL;
}

#if 0
skyfs_DL_entry_t *
__skyfs_SS_locate_dl_entry(skyfs_DL_chunk_t *dl_chunk,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id)
{
	skyfs_DL_entry_t *dl_entry = NULL;
	skyfs_DL_entry_t *tmp = NULL;
	skyfs_u32_t i;

	SKYFS_ENTER("__skyfs_SS_locate_dl_entry:enter:obj_id:%llu\n", obj_id);

	for(i = 0; i < SKYFS_DLENTRY_PER_CHUNK; i++){
		tmp = &(dl_chunk->dlentry[i]);
		//SKYFS_MSG("__skyfs_SS_locate_dl_entry:ino:%llu,obj_id:%llu\n", 
		//	tmp->ino, tmp->obj_id);
		if(tmp->ino == ino && tmp->obj_id == obj_id){
			SKYFS_MSG("__skyfs_SS_locate_dl_entry:find the entry:id:%d\n", i);
			dl_entry = tmp;
			break;
		}
	}

	if(dl_entry == NULL){
		SKYFS_MSG("__skyfs_SS_locate_dl_entry:not find:ino:%llu,obj:%llu\n",
			ino, obj_id);
	}


	SKYFS_MSG("__skyfs_SS_locate_dl_entry:exit:dl_entry:%p\n", dl_entry);
	return dl_entry;
}

skyfs_DL_entry_t *
__skyfs_SS_lookup_dl_entry(skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *chunk_id)
{
	skyfs_DL_entry_t *dl_entry = NULL;

	return dl_entry;
}

skyfs_s32_t 
__skyfs_SS_alloc_dl_entry(skyfs_DL_chunk_t *dl_chunk,
				skyfs_DL_entry_t **dl_entry)
{
	skyfs_s32_t rc = 0;
	skyfs_s32_t firstfree;

	SKYFS_MSG("__skyfs_SS_alloc_dl_entry:enter\n");
	firstfree = dl_chunk->firstfree;
	*dl_entry = &(dl_chunk->dlentry[firstfree]);

	dl_chunk->firstfree = (*dl_entry)->nextfree;
	dl_chunk->nfree --;
	

	SKYFS_MSG("__skyfs_SS_alloc_dl_entry:firstfree:%d,nextfree:%d\n",
		firstfree, dl_chunk->firstfree);
	SKYFS_LEAVE("__skyfs_SS_alloc_dl_entry:exit,index:%d,nfree:%d\n",
		firstfree, dl_chunk->nfree);

	return rc;
}

skyfs_s32_t
__skyfs_SS_init_dl_file(skyfs_DL_entry_t *dl_file,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t client_id)
{
	skyfs_s32_t rc = 0;
	skyfs_s32_t tmp;

	dl_entry->ino = ino;
	dl_entry->obj_id = obj_id;
	dl_entry->transfer = 0;
	dl_entry->update = 1;
	dl_entry->hashkey = __skyfs_get_obj_hashvalue(ino, obj_id);

	//dl_entry->fir_osd = osd_this_id;//(client_id) % osd_num + 1;
	//dl_entry->fir_osd = client_id % osd_num + 1;
	if(skyfs_replica == 0){
		if(skyfs_dl_type == 0){
			tmp = client_id % osd_num;
			if(tmp != 0){
				dl_entry->fir_osd = tmp;
			}else{
				dl_entry->fir_osd = osd_num;
			}
		}else if(skyfs_dl_type == 1){
			dl_entry->fir_osd = client_id % osd_num + 1;
		}else{
			dl_entry->fir_osd = osd_this_id;
		}
	}else{
		dl_file->fir_osd = osd_this_id;
	}

	dl_file->latest_osd = dl_file->fir_osd;
	osd_obj_index ++;

#if 0
	dl_entry->fir_osd = osd_this_id;

	if(osd_info.osd[osd_this_id].ip[0]->addr == 
			client_info.client[client_id].ip[0]->addr){
		dl_entry->sec_osd = client_id;
	}else{
        dl_entry->sec_osd = client_id;
	}
	dl_entry->thi_osd = 0;
#endif

	SKYFS_MSG("__skyfs_SS_init_dl_file:ino:%llu,obj_id:%llu,hashkey:%d\n",
		ino, dl_file->fir_osd, dl_file->hashkey);

	return rc;
}

skyfs_s32_t
__skyfs_SS_init_dl_entry(skyfs_DL_entry_t *dl_entry,
				skyfs_ino_t ino,
				skyfs_u64_t obj_id,
				skyfs_u32_t client_id)
{
	skyfs_s32_t rc = 0;

	dl_entry->ino = ino;
	dl_entry->obj_id = obj_id;
	dl_entry->transfer = 0;
	dl_entry->update = 1;
	dl_entry->hashkey = __skyfs_get_obj_hashvalue(ino, obj_id);

	/*Init the placement of this object*/
	if(){

	}else if{

	}else{

	}

	dl_entry->fir_osd = client_id % osd_num + 1;
	dl_entry->latest_osd = dl_entry->fir_osd;
	osd_obj_index ++;

	SKYFS_MSG("__skyfs_SS_init_dl_entry:ino:%llu,obj_id:%llu,hashkey:%d\n",
		ino, dl_entry->fir_osd, dl_entry->hashkey);

	return rc;
}

#endif

skyfs_u32_t
__skyfs_SS_check_osd_id(skyfs_DL_file_t *dl_file)
{
	skyfs_u32_t osd_id = osd_this_id;

	SKYFS_ERROR("__skyfs_SS_check_osd_id:latest_osd:%u\n",
		dl_file->real_location);

	if(dl_file->real_location){
		osd_id = dl_file->real_location;
	}

	return osd_id;
}

skyfs_s32_t
__skyfs_DL_writeback_subset(skyfs_DL_subset_t *dl_subset)
{
	struct list_head *index = NULL, *head = NULL, *tmp = NULL;
	skyfs_DL_chunk_t *dl_chunk = NULL;

	skyfs_u32_t subset_id;
	skyfs_s32_t rc = 0;

	
	subset_id = dl_subset->subset_id;

	SKYFS_ERROR("__skyfs_DL_writeback_subset:enter.subset_id:%d\n", subset_id);

	head = &(dl_subset->chunk_head);
	list_for_each(index, head){
		dl_chunk = list_entry(index, skyfs_DL_chunk_t, chunk_list);
		rc = __skyfs_DL_write_chunk(dl_chunk, subset_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_writeback_subset:writeback chunk err\n");
			goto ERR;
		}

		tmp = index->prev;
		__skyfs_DL_free_chunk(dl_chunk);
	    index = tmp;
	    free(dl_chunk);
	}


ERR:
	SKYFS_LEAVE("__skyfs_DL_writeback_subset:write %d chunk.exit\n", rc);
	return rc;
}

/* added by mayl for dl HA */
skyfs_s32_t
__skyfs_DL_writeback_subset_without_release(skyfs_DL_subset_t *dl_subset)
{
	struct list_head *index = NULL, *head = NULL, *tmp = NULL;
	skyfs_DL_chunk_t *dl_chunk = NULL;

	skyfs_u32_t subset_id;
	skyfs_s32_t rc = 0;

	
	subset_id = dl_subset->subset_id;

	SKYFS_ERROR("__skyfs_DL_writeback_subset_without_release:enter.subset_id:%d\n", subset_id);

	head = &(dl_subset->chunk_head);
	list_for_each(index, head){
		dl_chunk = list_entry(index, skyfs_DL_chunk_t, chunk_list);
		rc = __skyfs_DL_write_chunk(dl_chunk, subset_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_writeback_subset:writeback chunk err\n");
			goto ERR;
		}
	/*
		tmp = index->prev;
		__skyfs_DL_free_chunk(dl_chunk);
	    index = tmp;
	    free(dl_chunk);
	*/
	
  }


ERR:
	SKYFS_LEAVE("__skyfs_DL_writeback_subset:write %d chunk.exit\n", rc);
	return rc;
}


skyfs_s32_t
__skyfs_DL_enlarge_subset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s8_t  	dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t  	tmp_dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t 	subset_id;
	skyfs_s32_t 	fd = 0;
	skyfs_s32_t 	tmp_fd = 0;
	skyfs_s8_t  	*tmp = NULL;
	skyfs_s8_t 		*block = NULL;

	skyfs_DL_subset_head_t  dl_subset_head;
	skyfs_DL_chunk_t        *dl_chunk = NULL;
	skyfs_DL_chunk_t        *dl_chunk_tmp1 = NULL;
	skyfs_DL_chunk_t        *dl_chunk_tmp2 = NULL;
	skyfs_DL_file_t         *dl_file = NULL;
	
	skyfs_u32_t     nr_in_block, nr_in_tmp1, nr_in_tmp2;
	skyfs_u32_t     chunk_id;
	skyfs_u32_t     chunk_id1;
	skyfs_u32_t     chunk_id2;
    skyfs_u32_t     block_num;
	skyfs_u32_t     offset;
    skyfs_u64_t     judge_value;
	skyfs_u32_t     i, j;
	skyfs_s32_t		rc = 0;
	//skyfs_s32_t		rc1 = 0;

	subset_id = dl_subset->subset_id;

	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);	
	fd = open(dl_subset_fname, O_RDWR);
	if(fd <= 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:can not open %s\n", 
			dl_subset_fname);
		goto ERR;
	}
	
	sprintf(tmp_dl_subset_fname, "%s%d-tmp", SKYFS_DL_PATH, subset_id);
	tmp_fd = open(tmp_dl_subset_fname, O_RDWR|O_CREAT, 0666);
	if(tmp_fd <= 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:can not open %s\n", 
			tmp_dl_subset_fname);
		goto ERR;
	}

	if(read(fd, &dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:cannot read %s head\n", 
			dl_subset_fname);
		goto ERR;
	}

	block_num = (skyfs_u32_t)1 << dl_subset_head.subset_depth;
	SKYFS_MSG("__skyfs_DL_enlarge_subset:subset_depth:%d\n", 
		dl_subset_head.subset_depth);

	dl_subset_head.subset_depth ++;
	dl_subset_head.nlink = dl_subset->nlink_origin + dl_subset->nlink_update;
	if(write(tmp_fd, &dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:write %s head failed\n", 
			tmp_dl_subset_fname);
		goto ERR;
	}

	tmp = (skyfs_s8_t *)malloc(SKYFS_DLCHUNK_SIZE * 2);
	block = (skyfs_s8_t *)malloc(SKYFS_DLCHUNK_SIZE);

	for(i = 0; i < block_num; i ++){
		bzero(tmp, SKYFS_DLCHUNK_SIZE * 2);
		bzero(block, SKYFS_DLCHUNK_SIZE);
		offset = i * SKYFS_DLCHUNK_SIZE + sizeof(skyfs_DL_subset_head_t);
		if(pread(fd, block, SKYFS_DLCHUNK_SIZE, offset) < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_DL_enlarge_subset:read chunk:%d failed\n", i);
			goto ERR;
		}

		dl_chunk = (skyfs_DL_chunk_t *)(block);
		chunk_id = dl_chunk->chunk_id;
		chunk_id1 = 2 * chunk_id;
		chunk_id2 = 2 * chunk_id + 1;

		if(__skyfs_SS_create_chunkdir(subset_id, chunk_id1) < 0){
			goto ERR;
		}
		if(__skyfs_SS_create_chunkdir(subset_id, chunk_id2) < 0){
			goto ERR;		
		}

		if(dl_chunk->hashvalue == 0){
            SKYFS_ERROR("__skyfs_DL_enlarge_subset:%d empty, do nothing\n", i);
			rc = -1;
			goto ERR;
		}else{
			judge_value = (2 * i + 1) * SKYFS_MAX_HASH_VALUE / (block_num * 2);

			SKYFS_ERROR("__skyfs_DL_enrlage:split:chunk:%d,judge:%lld,nfree:%d\n",
				i, judge_value, dl_chunk->nfree);
			dl_chunk_tmp1 = (skyfs_DL_chunk_t *)(tmp);
			dl_chunk_tmp2 = (skyfs_DL_chunk_t *)((skyfs_s8_t *)tmp + SKYFS_DLCHUNK_SIZE);
			nr_in_block = 0;
			nr_in_tmp1 = nr_in_tmp2 = 0;
			for(nr_in_block = 0; nr_in_block <SKYFS_DLENTRY_PER_CHUNK; nr_in_block ++){
				dl_file = &(dl_chunk->dlfile[nr_in_block]);
				if((dl_file->hashkey % SKYFS_MAX_HASH_VALUE) < judge_value){
					memcpy(&(dl_chunk_tmp1->dlfile[nr_in_tmp1]),
						dl_file, sizeof(skyfs_DL_file_t));
					nr_in_tmp1 ++;
				}else{
					memcpy(&(dl_chunk_tmp2->dlfile[nr_in_tmp2]),
						dl_file, sizeof(skyfs_DL_file_t));
					nr_in_tmp2 ++;
				}
			}
			
			dl_chunk_tmp1->nfree = SKYFS_DLENTRY_PER_CHUNK - nr_in_tmp1;
			dl_chunk_tmp1->firstfree = nr_in_tmp1;
			dl_chunk_tmp1->chunk_id = chunk_id1;
			dl_chunk_tmp1->hashvalue = 
				__skyfs_get_bmeta_hashvalue(dl_chunk_tmp1->chunk_id);

			dl_chunk_tmp2->nfree = SKYFS_DLENTRY_PER_CHUNK - nr_in_tmp2;
			dl_chunk_tmp2->firstfree = nr_in_tmp2;
			dl_chunk_tmp2->chunk_id = chunk_id2;
			dl_chunk_tmp2->hashvalue =
				__skyfs_get_bmeta_hashvalue(dl_chunk_tmp2->chunk_id);

			SKYFS_MSG("__skyfs_DL_enlarge:b1:%d,nfree:%d,b2:%d,nfree:%d\n",
				dl_chunk_tmp1->chunk_id, dl_chunk_tmp1->nfree,
				dl_chunk_tmp2->chunk_id, dl_chunk_tmp2->nfree);

			for(j = nr_in_tmp1; j < SKYFS_DLENTRY_PER_CHUNK; j ++){
				dl_file = &(dl_chunk_tmp1->dlfile[j]);
				dl_file->nextfree = j + 1;
			}

			for(j = nr_in_tmp2; j < SKYFS_DLENTRY_PER_CHUNK; j ++){
				dl_file = &(dl_chunk_tmp2->dlfile[j]);
				dl_file->nextfree = j + 1;
			}
		}

		if(write(tmp_fd, tmp, SKYFS_DLCHUNK_SIZE * 2) < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_DL_enlarge_subset:write %s err:%d\n",
				tmp_dl_subset_fname, rc);
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

	if(unlink(dl_subset_fname) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:cann't unlink %s,err:%d\n",
			dl_subset_fname, rc);
		goto ERR;
	}

	if(rename(tmp_dl_subset_fname, dl_subset_fname) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_enlarge_subset:cann't rename %s to %s.err:%d\n",
			tmp_dl_subset_fname, dl_subset_fname, rc);
		goto ERR;
	}

ERR:

	if(tmp_fd){
		close(tmp_fd);
	}

	if(fd){
		close(fd);
	}

	if(block){
		free(block);
	}

	if(tmp){
		free(tmp);
	}
	
	SKYFS_LEAVE("__skyfs_DL_enlarge_subset:leave.subset_id:%d,subset_depth:%d\n\n", 
        dl_subset_head.subset_id, dl_subset_head.subset_depth);

	return rc;
}

skyfs_s32_t
__skyfs_DL_split_subset(skyfs_DL_subset_t *dl_subset)
{
    amp_request_t         **reqs = NULL;

	skyfs_DL_subset_head_t dl_subset_head;
    skyfs_DL_subset_head_t new_shead1;
    skyfs_DL_subset_head_t new_shead2;

    skyfs_DL_chunk_t    *dl_chunk= NULL;
    skyfs_DL_chunk_t    *dl_chunk_tmp1 = NULL;
    skyfs_DL_chunk_t    *dl_chunk_tmp2 = NULL;
    skyfs_DL_file_t     *dl_file= NULL;

	skyfs_s8_t          dl_subset_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t          new_dls1_fname[SKYFS_MAX_NAME_LEN];
	skyfs_s8_t          new_dls2_fname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t         subset_id;
	skyfs_u32_t         new_sid1;
	skyfs_u32_t         new_sid2;
	skyfs_u32_t         entry_sid;
	skyfs_u32_t         new_osdid;
	skyfs_u32_t         split_depth;
	skyfs_u32_t         subset_depth;
	skyfs_u32_t         offset;
	skyfs_u32_t         block_num;
	skyfs_u32_t         i = 0, j = 0;
	skyfs_u32_t         nr_in_block = 0; 
	skyfs_u32_t         nr_in_tmp1 = 0;
	skyfs_u32_t         nr_in_tmp2 = 0;
    skyfs_s32_t         fd = 0;
    skyfs_s32_t         new_fd1 = 0;
    skyfs_s32_t         new_fd2 = 0;
    skyfs_s8_t          *block = NULL;
    skyfs_s8_t          *tmp1 = NULL;
	skyfs_s8_t          *tmp2 = NULL;

    skyfs_s32_t         rc = 0;
 
	subset_id = dl_subset->subset_id;
	split_depth = dl_subset->split_depth;
	subset_depth = dl_subset->subset_depth;

	SKYFS_ERROR_1("__skyfs_DL_split:enter,s_id:%d,sp_depth:%d,sb_depth:%d\n", 
        subset_id, split_depth, subset_depth);

	block = malloc(sizeof(skyfs_DL_chunk_t));
	if(block == NULL){
		SKYFS_ERROR("__skyfs_DL_split_subset:alloc blk error\n");
		goto ERR;
	}

	new_sid1 = subset_id;
	new_sid2 = subset_id + (skyfs_u32_t)(1 << split_depth);

	new_osdid = __skyfs_DL_judge_osdid(new_sid2);

	SKYFS_ERROR("__skyfs_DL_split_subset:new_sid1:%d,new_sid2:%d,new_osdid:%d\n",
		new_sid1, new_sid2, new_osdid);

	sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);
	fd = open(dl_subset_fname, O_RDONLY);
	if(fd < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_split_subset:can not open %s,err:%d\n", 
			dl_subset_fname, rc);
		goto ERR;
	}
	
	if(read(fd, &dl_subset_head, sizeof(skyfs_DL_subset_head_t)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_split_subset:read %s head fail,err:%d\n",
			dl_subset_fname, rc);
		goto ERR;
	}

	split_depth ++;

	new_shead1.subset_id = new_sid1;
	new_shead1.split_depth = split_depth;
	new_shead1.subset_depth = subset_depth;
	new_shead1.nlink = 0;

	new_shead2.subset_id = new_sid2;
	new_shead2.split_depth = split_depth;
	new_shead2.subset_depth = subset_depth;
	new_shead2.nlink = 0;

	sprintf(new_dls1_fname, "%s%d-tmp", SKYFS_DL_PATH, new_sid1);
	new_fd1 = open(new_dls1_fname, O_RDWR|O_CREAT, 0666);
	if(new_fd1 < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_split_subset:cannot open %s\n", new_dls1_fname);
		goto ERR;
	}

	if(write(new_fd1, &new_shead1, sizeof(skyfs_DL_subset_head_t)) < 0){
		rc = -errno;
		SKYFS_ERROR("__skyfs_DL_split_subset:w %s head fail\n", new_dls1_fname);
		goto ERR;
	}

	if(new_osdid == osd_this_id){
		rc = __skyfs_DL_do_create_subset(&new_shead2);
		if(rc < 0){
			goto ERR;
		}

		sprintf(new_dls2_fname, "%s%d", SKYFS_DL_PATH, new_sid2);
		new_fd2 = open(new_dls2_fname, O_RDWR);
		if(new_fd2 < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_DL_split_subset:open %s,err:%d\n", 
				new_dls2_fname, rc);
			goto ERR;
		}

		if(lseek(new_fd2,sizeof(skyfs_DL_subset_head_t),SEEK_SET) < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_DL_split_subset:lseek %s, err:%d\n", 
				new_dls2_fname, rc);
			goto ERR;
		}
	}else{
		SKYFS_ERROR("__skyfs_DL_split_subset:the 2rd osd is %d\n", new_osdid);
		rc = __skyfs_O2O_create_dl_subset(new_osdid, 1, &new_shead2);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_split_subset:error:create sub at %d err:%d\n",
				new_osdid, rc);
			goto ERR;
		}
	}

	block_num = (skyfs_u32_t)1 << subset_depth;
	tmp1 = (skyfs_s8_t *)malloc(SKYFS_DLCHUNK_SIZE);
	tmp2 = (skyfs_s8_t *)malloc(SKYFS_DLCHUNK_SIZE);

	if(new_osdid != osd_this_id){
		reqs = malloc(sizeof(amp_request_t) * block_num);
	}

	for(i = 0; i < block_num; i ++){
		bzero(block, SKYFS_DLCHUNK_SIZE);
		bzero(tmp1, SKYFS_DLCHUNK_SIZE);
		bzero(tmp2, SKYFS_DLCHUNK_SIZE);
		offset = i * SKYFS_DLCHUNK_SIZE + sizeof(skyfs_DL_subset_head_t);
		if(pread(fd, block, SKYFS_DLCHUNK_SIZE, offset) < 0){
			rc = -errno;
			SKYFS_ERROR("%s:error:read %s bmeta:%d off:%d.rc:%d,fd:%d\n",
				__FUNCTION__, dl_subset_fname, i, offset, rc, fd);
			exit(1);
			goto ERR;
		}
		dl_chunk = (skyfs_DL_chunk_t *)(block);
		dl_chunk_tmp1 = (skyfs_DL_chunk_t *)tmp1;
		dl_chunk_tmp2 = (skyfs_DL_chunk_t *)tmp2;
		nr_in_tmp1 = nr_in_tmp2 = 0;

		if(new_osdid == osd_this_id){
			__skyfs_SS_create_chunkdir(new_sid2, dl_chunk->chunk_id);
		}else{
			SKYFS_MSG("__skyfs_DL_split_subset:create chunkdir:%d\n",
				dl_chunk->chunk_id);
		}

		if(dl_chunk->hashvalue == 0){
			SKYFS_ERROR("__skyfs_DL_split_subset:chunk:%d empty\n", i);
		}else{
		    SKYFS_ERROR("__skyfs_DL_split_subset:split chunk:%d,nfree:%d\n", 
                i, dl_chunk->nfree);
			for(nr_in_block = 0; nr_in_block < SKYFS_DLENTRY_PER_CHUNK; nr_in_block ++){
				dl_file = &(dl_chunk->dlfile[nr_in_block]);
				if(dl_file->ino){
					entry_sid = __skyfs_get_subset_id(dl_file->hashkey, split_depth);
					if(entry_sid == new_sid1){
						memcpy(&(dl_chunk_tmp1->dlfile[nr_in_tmp1]),
							dl_file, sizeof(skyfs_DL_file_t));
						nr_in_tmp1 ++;
					}else if(entry_sid == new_sid2){
						memcpy(&(dl_chunk_tmp2->dlfile[nr_in_tmp2]),
							dl_file, sizeof(skyfs_DL_file_t));
						nr_in_tmp2 ++;
					}else{
						SKYFS_ERROR("%s:error,sid:%d,bid:%d,depth:%d,fd:%d\n",
							__FUNCTION__, entry_sid, 
							dl_chunk->chunk_id, split_depth, fd);
						SKYFS_ERROR("__skyfs_split:dlfile:%lld,chunk_id:%d,nr:%d\n",
							dl_file->ino, i, nr_in_block);
						{
							close(fd);
							fd = open(dl_subset_fname, O_RDWR);
							if(pread(fd, block, SKYFS_DLCHUNK_SIZE, offset) < 0){
								SKYFS_ERROR("__skyfs_split:read error again\n");
								exit(1);
							}
							dl_chunk = (skyfs_DL_chunk_t *)(block);
							SKYFS_ERROR("__skyfs_split:firstino:%llu\n", 
								dl_chunk->dlfile[0].ino);
						}
						rc = -1;
						goto ERR;
					}

					SKYFS_MSG("__skyfs_split:ino:%llu,hash:%d,sid:%d\n",
						dl_file->ino, 
						dl_file->hashkey, 
						entry_sid);
				}

			}

			dl_chunk_tmp1->nfree = SKYFS_DLENTRY_PER_CHUNK - nr_in_tmp1;
			dl_chunk_tmp1->firstfree = nr_in_tmp1;
			dl_chunk_tmp1->chunk_id = i;
			dl_chunk_tmp1->hashvalue = __skyfs_get_bmeta_hashvalue(i);

			dl_chunk_tmp2->nfree = SKYFS_DLENTRY_PER_CHUNK - nr_in_tmp2;
			dl_chunk_tmp2->firstfree = nr_in_tmp2;
			dl_chunk_tmp2->chunk_id = i;
			dl_chunk_tmp2->hashvalue = __skyfs_get_bmeta_hashvalue(i);

			SKYFS_MSG("__skyfs_DL_split:chunk nfree:1:%d,2:%d\n",
				dl_chunk_tmp1->nfree, dl_chunk_tmp2->nfree);

			for(j = nr_in_tmp1; j < SKYFS_DLENTRY_PER_CHUNK; j ++){
				dl_file = &(dl_chunk_tmp1->dlfile[j]);
				dl_file->nextfree = j + 1;
			}

			for(j = nr_in_tmp2; j < SKYFS_DLENTRY_PER_CHUNK; j ++){
				dl_file = &(dl_chunk_tmp2->dlfile[j]);
				dl_file->nextfree = j + 1;
			}
		}

		if(write(new_fd1, tmp1, SKYFS_DLCHUNK_SIZE) < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_DL_split_subset:err:write %d err:%d\n", 
				new_fd1, rc);
			goto ERR;
		}

		if(new_osdid == osd_this_id){
			if(write(new_fd2, tmp2 ,SKYFS_DLCHUNK_SIZE) < 0){
				rc = -errno;
				SKYFS_ERROR("__skyfs_DL_split_subset:err:write %d err:%d\n", 
					new_fd1, rc);
				goto ERR;
			}
		}else{
			SKYFS_ERROR("__skyfs_DL_split_subset:write bmeta:%d to osd:%d\n",
				i, new_osdid);
			reqs[i] = __skyfs_O2O_write_dlchunk(new_osdid, new_sid2, i,
					(skyfs_DL_chunk_t *)tmp2);
			if(reqs[i] == NULL){
				SKYFS_ERROR("__skyfs_DL_split_subset:err:write bmeta to %d failed\n",
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


    if(unlink(dl_subset_fname) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_DL_split_subset:error:can't unlink %s,err:%d\n", 
			dl_subset_fname, rc);
        goto ERR;
    }

    if(rename(new_dls1_fname, dl_subset_fname) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_DL_split_subset:error:can't rename %s to %s.\n",
            new_dls1_fname, dl_subset_fname);
        goto ERR;
    }

ERR:
	if(new_fd1){
        close(new_fd1);
    }

    if(new_fd2){
        close(new_fd2);
    }

    if(fd){
        close(fd);
    }

    if(tmp1){
        free(tmp1);
    }

    if(tmp2){
        free(tmp2);
    }

    if(block){
        free(block);
    }

    SKYFS_ERROR("__skyfs_DL_split_subset:leave.i:%d\n\n", i);

	return rc;
}
skyfs_s32_t
__skyfs_DL_do_update_hdepth(skyfs_u32_t subset_id, 
				skyfs_u32_t split_depth)
{
	skyfs_s32_t rc = 0;
	skyfs_u64_t hashvalue;
	skyfs_u32_t new_subset_id;
	skyfs_htb_t *dl_subseti_htbp = NULL;
	skyfs_DL_subset_index_t *dl_subset_index = NULL;
	skyfs_s8_t  string[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t fd;

	new_subset_id = ((skyfs_u32_t)1 << (split_depth -1)) + subset_id;

	pthread_mutex_lock(&skyfs_dl_depth.lock);

	rc = __skyfs_set_bit(skyfs_dl_depth.subset_bm, new_subset_id, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_do_update_hdepth:set new_subset_id %d fail\n",
			new_subset_id);
		pthread_mutex_unlock(&skyfs_dl_depth.lock);
		goto ERR;
	}

	if(skyfs_dl_depth.depth < split_depth){
		skyfs_dl_depth.depth = split_depth;
	}

	pthread_mutex_unlock(&skyfs_dl_depth.lock);

	if(osd_this_id == SKYFS_MASTER_OSD_ID){
		pthread_mutex_lock(&skyfs_dl_head.lock);
		hashvalue = __skyfs_get_subset_hashvalue(pad_id, subset_id);
		hashvalue = hashvalue % SKYFS_DL_SUB_INDEX_HASH_LEN;
		dl_subseti_htbp = &(skyfs_dl_head.subset_hash_base[hashvalue]);

		dl_subset_index = __skyfs_DL_find_subset_index(dl_subseti_htbp,
			pad_id, subset_id);
		if(dl_subset_index == NULL){
			SKYFS_ERROR("__skyfs_DL_do_update_hdepth:subset_index not exist\n");
			rc = -ENOENT;
			pthread_mutex_unlock(&skyfs_dl_head.lock);
			goto ERR;
		}
		
		dl_subset_index->split_depth = split_depth;

		if(skyfs_dl_head.depth < split_depth){
			skyfs_dl_head.depth = split_depth;
		}

		rc = __skyfs_set_bit(skyfs_dl_head.subset_bm, new_subset_id, 1);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_DL_do_update_hdepth:set new subset failed:%d\n",
				new_subset_id);
			pthread_mutex_unlock(&skyfs_dl_head.lock);
			goto ERR;
		}
		osd_dl_version ++;

        sprintf(string, "%s%s", SKYFS_DL_PATH, "skyfs_dl_head");
	    fd = open(string ,O_RDWR|O_CREAT, 0644);
		if(fd < 0){
			SKYFS_ERROR("__skyfs_SS_do_udpate_hdepth:open %s err:%d\n",
				string, errno);
			goto ERR;
		}

        rc = write(fd, &skyfs_dl_head, sizeof(skyfs_DL_head_t));
		if(rc < 0){
           	SKYFS_ERROR("__skyfs_SS_do_update_hdepth:write %s err:%d\n",
				string, errno);
			goto ERR;
		}

		pthread_mutex_unlock(&skyfs_dl_head.lock);
	}

ERR:
	SKYFS_LEAVE("__skyfs_DL_do_update_hdepth:rc:%d,sub_id:%d,nsubset_id:%d\n", 
		rc, subset_id, new_subset_id);

	return rc;
}

skyfs_s32_t
__skyfs_DL_update_head_depth(subset_id, split_depth)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t new_subset_id;
	skyfs_u32_t new_osd_id;

	SKYFS_ENTER("__skyfs_DL_update_head_depth:subset_id:%d,split_depth:%d\n", 
		subset_id, split_depth);

	new_subset_id = ((skyfs_u32_t)1 << (split_depth -1)) + subset_id;

	if(osd_this_id == SKYFS_MASTER_OSD_ID){
		rc = __skyfs_DL_do_update_hdepth(subset_id, split_depth);
		if(rc < 0){
			goto ERR;
		}
	}else{
		rc = __skyfs_DL_do_update_hdepth(subset_id, split_depth);
		if(rc < 0){
			goto ERR;
		}

    	rc = __skyfs_O2O_update_head_depth(SKYFS_MASTER_OSD_ID, subset_id, split_depth);
		if(rc < 0){
			goto ERR;
		}
	}

	new_osd_id = __skyfs_DL_judge_osdid(new_subset_id);
	if(new_osd_id != osd_this_id && new_osd_id != SKYFS_MASTER_OSD_ID){
		SKYFS_ERROR("__skyfs_DL_update_head_depth:init new,send to osd:%d\n",
			new_osd_id);
		rc = __skyfs_O2O_update_head_depth(new_osd_id, subset_id, split_depth);
	}

ERR:

	SKYFS_LEAVE("__skyfs_DL_update_head_depth:rc:%d,sub_id:%d,nsubset_id:%d\n", 
		rc, subset_id, new_subset_id);

	return rc;
}

skyfs_s32_t
__skyfs_SS_enlarge_dlsubset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s32_t rc = 0;

	skyfs_timespec_t    start_time;
	skyfs_timespec_t    end_time;
	skyfs_u32_t         period;

	__skyfs_get_starttime(&start_time, skyfs_profile_flag);


	SKYFS_ERROR("__skyfs_SS_enlarge_dlsubset:enter:subset_id:%d,sb_depth:%d\n",
		dl_subset->subset_id, dl_subset->subset_depth);

	pthread_mutex_lock(&dl_subset->lock);
	
	rc = __skyfs_DL_writeback_subset(dl_subset);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_enlarge_dlsubset:writeback subset err:%d\n", rc);
		goto ERR;
	}

	rc = __skyfs_DL_enlarge_subset(dl_subset);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_enlarge_dlsubset:enlrage subset err:%d\n", rc);
		goto ERR;
	}

	dl_subset->subset_depth ++;

ERR:
	period = __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "enlarge");
	SKYFS_ERROR("subset_id:%u,subset_depth:%u,time:%d\n",
		dl_subset->subset_id, dl_subset->subset_depth, period);

	pthread_mutex_unlock(&dl_subset->lock);

	SKYFS_LEAVE("__skyfs_SS_enlarge_dlsubset:exit\n");
	return rc;
}

skyfs_s32_t __skyfs_SS_choose2splitdl(void)
{
	skyfs_u32_t i;
	skyfs_u32_t new_sid = 0, new_osdid = 0, sort_index= 0;
	struct list_head *index = NULL, *head = NULL;
	skyfs_DL_subset_t *tmp = NULL;
	skyfs_htb_t *htbp = NULL;

	SKYFS_ERROR("__skyfs_SS_choose2splitdl:enter\n");

	for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i++){
		htbp = NULL;
		head = NULL;
		index = NULL;
		
	    SKYFS_MSG("__skyfs_SS_choose2splitdl:check:%d\n",i);

        htbp = &skyfs_dlsubset_cache_htbbase[i];
        pthread_rwlock_wrlock(&(htbp->rwlock));

        head = &(htbp->head);
		if(list_empty(head)){
            pthread_rwlock_unlock(&(htbp->rwlock));
            continue;
		}else{
            list_for_each(index, head){
	            tmp = NULL;
                tmp = list_entry(index, skyfs_DL_subset_t, subset_hash);
                pthread_rwlock_wrlock(&(tmp->rwlock));
	            SKYFS_MSG("__skyfs_SS_choose2splitdl:check:sid:%d\n",
					tmp->subset_id);
	            new_sid = tmp->subset_id+(skyfs_u32_t)(1<<tmp->split_depth);
	            new_osdid = __skyfs_DL_judge_osdid(new_sid);
				sort_index = __skyfs_SS_get_statusindex(new_osdid);
	            SKYFS_MSG("__skyfs_SS_choose2splitdl:prepare:sid:%d,nosdid:%d\n",
					sort_index, new_osdid);
				if(sort_index > osd_num/2){
                    __skyfs_SS_split_dlsubset(tmp);
                    pthread_rwlock_unlock(&(tmp->rwlock));
                    pthread_rwlock_unlock(&(htbp->rwlock));
					break;
				}else{
                    pthread_rwlock_unlock(&(tmp->rwlock));
				}
 			}
        
			pthread_rwlock_unlock(&(htbp->rwlock));
		}
	}

	SKYFS_ERROR("__skyfs_SS_choose2splitdl:split %d, move to %d\n",
		new_sid, new_osdid);
	return new_sid;

}

skyfs_s32_t
__skyfs_SS_split_dlsubset(skyfs_DL_subset_t *dl_subset)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t split_depth;

	skyfs_timespec_t    start_time;
	skyfs_timespec_t    end_time;
	skyfs_u32_t         period;

	__skyfs_get_starttime(&start_time, skyfs_profile_flag);


	SKYFS_ERROR("__skyfs_SS_split_dlsubset:enter.subset_id:%d,split_depth:%d\n",
		dl_subset->subset_id, dl_subset->split_depth);

	pthread_mutex_lock(&dl_subset->lock);

	rc = __skyfs_DL_writeback_subset(dl_subset);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_split_dlsubset:writeback subset err:%d\n", rc);
		goto ERR;
	}

	rc = __skyfs_DL_split_subset(dl_subset);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_split_dlsubset:split subset err:%d\n", rc);
		goto ERR;
	}

	split_depth = dl_subset->split_depth;
	split_depth ++;

	rc = __skyfs_DL_update_head_depth(dl_subset->subset_id, split_depth);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_SS_split_dlsubset:update head depth err:%d\n", rc);
		goto ERR;
	}
	
	dl_subset->split_depth ++;

ERR:
	period = __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "split");
	SKYFS_ERROR("split_depth%d:time:%d\n",dl_subset->subset_depth, period);

	pthread_mutex_unlock(&dl_subset->lock);

	SKYFS_LEAVE("__skyfs_SS_split_dlsubset:exit.rc:%d\n", rc);

	return rc;
}

skyfs_s32_t __skyfs_DL_writeback_cache(void)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t i;

	/*1. write back and clear all the subset cache and attached bmeta cache*/
	for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i++){
		rc = __skyfs_DL_clear_htbcache(i);
	}

	return rc;
}

skyfs_s32_t
__skyfs_DL_clear_htbcache(skyfs_u32_t hashindex)
{
    skyfs_htb_t *htbp = NULL;
    struct list_head *head = NULL, *index = NULL, *tmp = NULL;
    struct list_head *local_head = NULL, *local_index = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t *dl_chunk = NULL;
    skyfs_u32_t rc = 0;

    SKYFS_ENTER("__skyfs_DL_clear_htbcache:hashindex:%d\n",hashindex);

    htbp = &skyfs_dlsubset_cache_htbbase[hashindex];
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_DL_clear_htbcache:locate htbp error:%d\n", 
			hashindex);
        goto err_out;
    }

    SKYFS_MSG("__skyfs_DL_clear_htbcache:before htbp lock\n");
    pthread_rwlock_wrlock(&(htbp->rwlock));
    SKYFS_MSG("__skyfs_DL_clear_htbcache:after htbp lock\n");

    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_DL_clear_htbcache:hash table NUL,still need set\n");
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto err_out;
    }

    /* writeback all the subset in this hash entry*/
    list_for_each(index, head){
        dl_subset = list_entry(index, skyfs_DL_subset_t, subset_hash);
        SKYFS_MSG("__skyfs_DL_clear_htbcache:before lock\n");
        pthread_rwlock_wrlock(&(dl_subset->rwlock));
        SKYFS_MSG("__skyfs_DL_clear_htbcache:after lock\n");
        {
            rc = __skyfs_DL_writeback_subset(dl_subset);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_DL_clear_htbcache:wb subset err:%d\n", rc);
                pthread_rwlock_unlock(&(dl_subset->rwlock));
                pthread_rwlock_unlock(&(htbp->rwlock));
                goto err_out;
            }

            local_head = &(dl_subset->chunk_head);
            list_for_each(local_index, local_head){
                dl_chunk = list_entry(local_index, skyfs_DL_chunk_t, chunk_list);
                tmp = local_index->prev;
                __skyfs_DL_free_chunk(dl_chunk);
                local_index = tmp;
	            free(dl_chunk);
            }

            rc = __skyfs_DL_write_subset(dl_subset);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_DL_clear_htbcache:wr subhead error!!:%d\n",
					rc);
                pthread_rwlock_unlock(&(dl_subset->rwlock));
                pthread_rwlock_unlock(&(htbp->rwlock));
            }
        }
        pthread_rwlock_unlock(&(dl_subset->rwlock));

		tmp = index->prev;
        __skyfs_DL_free_subset(dl_subset);
        index = tmp;

    }

    SKYFS_ERROR("__skyfs_DL_clear_htbcache:clear_index:%d\n", hashindex);

    pthread_rwlock_unlock(&(htbp->rwlock));
err_out:

    return rc;
}

skyfs_DL_file_t *
__skyfs_SS_locate_cache_file(skyfs_ino_t ino, skyfs_u64_t ino_hash)
{
	skyfs_DL_file_t  *dl_file = NULL;

	return dl_file;
}

skyfs_DL_file_t *
__skyfs_SS_locate_dl_file(skyfs_DL_chunk_t *dl_chunk, skyfs_ino_t ino)
{
	skyfs_DL_file_t *dl_file= NULL;
	skyfs_DL_file_t *tmp = NULL;
	skyfs_u32_t i;

	SKYFS_ENTER("%s:enter:ino:%llu\n", __FUNCTION__, ino);

	for(i = 0; i < SKYFS_DLENTRY_PER_CHUNK; i++){
		tmp = &(dl_chunk->dlfile[i]);
		//SKYFS_MSG("__skyfs_SS_locate_dl_entry:ino:%llu,obj_id:%llu\n", 
		//	tmp->ino, tmp->obj_id);
		if(tmp->ino == ino){
			SKYFS_MSG("__skyfs_SS_locate_dlfile:find ino:%llu,chunk_id:%u,index:%u\n", 
				ino, dl_chunk->chunk_id, i);
			dl_file= tmp;
			break;
		}
	}

	if(dl_file== NULL){
		SKYFS_ERROR("%s:not find:ino:%llu\n", __FUNCTION__, ino);
	}

	SKYFS_MSG("%s:exit:ino:%llu, %p\n", __FUNCTION__, ino,dl_file);
	return dl_file;
}

skyfs_s32_t
__skyfs_SS_alloc_dlfile(skyfs_DL_chunk_t *dl_chunk,
			skyfs_DL_file_t **dl_file)
{
	skyfs_s32_t rc = 0;
	skyfs_u32_t firstfree;

	SKYFS_MSG("%s:enter:chunk->nfree:%d\n", __FUNCTION__, dl_chunk->nfree);

	firstfree = dl_chunk->firstfree;
	*dl_file= &(dl_chunk->dlfile[firstfree]);

	dl_chunk->firstfree = (*dl_file)->nextfree;
	dl_chunk->nfree --;

	if(dl_chunk->nfree < 0){
		SKYFS_MSG("__skyfs_SS_alloc_dlfile:err:chunk->nfree:%d,not enough space\n",dl_chunk->nfree);
		exit(1);
	}

	SKYFS_MSG("%s:exit:rc:%d,file_index:%u\n", __FUNCTION__, rc, firstfree);
	return rc;
}

skyfs_s32_t
__skyfs_SS_init_dlfile(skyfs_DL_file_t *dl_file, 
		skyfs_ino_t ino, 
		skyfs_u32_t partition_num,
		skyfs_u32_t replica_num,
		skyfs_u32_t obj_size,
		skyfs_u32_t real_location,
		skyfs_u32_t hashkey)
{
	skyfs_s32_t rc = 0;

	SKYFS_MSG("%s:enter:ino:%llu\n", __FUNCTION__, ino);
	dl_file->ino = ino;
	//dl_file->write_version = 0;
	dl_file->partition_num = partition_num;
	dl_file->replica_num = replica_num;
	dl_file->obj_size = obj_size;
	dl_file->real_location = real_location;
	dl_file->hashkey = hashkey;
	/* mayl add end pos, so we can cut out the READ which over the file end pos */
	dl_file->end_pos = 0;
	//__skyfs_init_htb(SKYFS_DL_OBJBUF_HASH_LEN, &dl_file->objbuf_hash_base);

	SKYFS_MSG("%s:exit:rc:%d\n", __FUNCTION__, rc);

	return rc;
}

skyfs_u64_t __skyfs_SS_get_objid(skyfs_DL_file_t *dl_file, skyfs_u64_t offset)
{
	skyfs_u32_t obj_size;
	skyfs_u64_t obj_id;

	obj_size = dl_file->obj_size;
	SKYFS_MSG("%s:get objid,offset:%llu, obj_size:%d\n", __FUNCTION__, offset, dl_file->obj_size);	

	obj_id = offset / obj_size;

	return obj_id;
}
skyfs_s32_t __skyfs_SS_check_obj(skyfs_DL_file_t *dl_file, skyfs_u64_t obj_id)
{
	skyfs_u32_t partition_id;
	//skyfs_u32_t obj_size;
	skyfs_s32_t rc = 0;

	partition_id = obj_id / SKYFS_MAX_OBJ_PER_PART;
	

	return rc;	

}

skyfs_s32_t __skyfs_SS_locate_obj(skyfs_DL_file_t *dl_file, skyfs_u64_t obj_id)
{
	skyfs_s32_t partition_id;
	skyfs_s32_t max_partition_id;
	skyfs_s32_t rc = 0;

	SKYFS_MSG("%s:enter.ino:%llu, obj_id:%llu,partition_num:%u\n",
		__FUNCTION__, dl_file->ino, obj_id, dl_file->partition_num);

	partition_id = obj_id / SKYFS_MAX_OBJ_PER_PART;
	max_partition_id = dl_file->partition_num - 1;

	if(max_partition_id >= partition_id){
		SKYFS_MSG("%s:get the right partition_id:%d\n", __FUNCTION__, partition_id);
		rc = __skyfs_SS_check_obj(dl_file, obj_id);
	}else if(max_partition_id + 1 == partition_id){
		SKYFS_MSG("%s:need to alloc a new partition:%d\n", __FUNCTION__, partition_id);
		partition_id = -1;
	}else{
		partition_id = -2;
		SKYFS_ERROR_1("%s:exit -2 sparse file !! .partition_id:%d,max_partition_id:%d\n", 
			__FUNCTION__, partition_id, max_partition_id);
	}

	SKYFS_MSG("%s:exit.partition_id:%d,max_partition_id:%d\n", 
		__FUNCTION__, partition_id, max_partition_id);

	return partition_id;
}

int
__skyfs_DL_remove_partition_cache(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id)
{
	skyfs_DL_part_t  *dl_part = NULL, *tmp = NULL;
	struct list_head *index = NULL, *head = NULL;
	skyfs_htb_t      *htbp = NULL;
	skyfs_u32_t      hashvalue;
	int rc = 0;

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u,replica_id:%u\n", 
		__FUNCTION__, ino, partition_id, replica_id);

	hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
	hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
	htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);

	pthread_mutex_lock(&htbp->lock);
	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_ERROR("__skyfs_DL_lookup_partition:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_part_t, part_hash);
		if(tmp->ino == ino 
			&&tmp->partition_id == partition_id 
			&& tmp->replica_id == replica_id){
			dl_part = tmp;
			list_del_init(&dl_part->part_hash);
			rc = 1;
			goto OUT;
		}
	}

ERR:
OUT:
	
	pthread_mutex_unlock(&htbp->lock);
	if(dl_part != NULL){
		free(dl_part);
	}
	//TODO: MAYL
	SKYFS_ERROR("%s:exit:ino:%llu,partition_id:%u,replica_id:%u,dl_part:%p\n", 
		__FUNCTION__, ino, partition_id, replica_id, dl_part);

	return rc;
}


skyfs_DL_part_t *
__skyfs_DL_lookup_partition_with_lock(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id,
		pthread_mutex_t ** phtb_lock)
{
	skyfs_DL_part_t  *dl_part = NULL, *tmp = NULL;
	struct list_head *index = NULL, *head = NULL;
	skyfs_htb_t      *htbp = NULL;
	skyfs_u32_t      hashvalue;

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u,replica_id:%u\n", 
		__FUNCTION__, ino, partition_id, replica_id);

	hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
	hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
	htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);

	pthread_mutex_lock(&htbp->lock);
	*phtb_lock = &htbp->lock;
	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_ERROR("__skyfs_DL_lookup_partition:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_part_t, part_hash);
		if(tmp->ino == ino 
			&&tmp->partition_id == partition_id 
			&& tmp->replica_id == replica_id){
			dl_part = tmp;
			goto OUT;
		}
	}

ERR:
OUT:
	
	//pthread_mutex_unlock(&htbp->lock);
	//TODO: MAYL
	SKYFS_ERROR("%s:exit:ino:%llu,partition_id:%u,replica_id:%u,dl_part:%p\n", 
		__FUNCTION__, ino, partition_id, replica_id, dl_part);

	return dl_part;
}


skyfs_DL_part_t *
__skyfs_DL_lookup_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id)
{
	skyfs_DL_part_t  *dl_part = NULL, *tmp = NULL;
	struct list_head *index = NULL, *head = NULL;
	skyfs_htb_t      *htbp = NULL;
	skyfs_u32_t      hashvalue;
	uint64_t 	lookup_time = 0, start_lookup_time = 0;
	struct timeval	tv;

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u,replica_id:%u\n", 
		__FUNCTION__, ino, partition_id, replica_id);

	hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
	hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
	htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);
	gettimeofday(&tv, NULL);

	start_lookup_time = tv.tv_sec * 1000000 + tv.tv_usec ;
	pthread_mutex_lock(&htbp->lock);
	head = &(htbp->head);
	if(list_empty(head)){
		SKYFS_ERROR("__skyfs_DL_lookup_partition:hash table NULL\n");
		goto ERR;
	}

	list_for_each(index, head){
		tmp = list_entry(index, skyfs_DL_part_t, part_hash);
		if(tmp->ino == ino 
			&&tmp->partition_id == partition_id 
			&& tmp->replica_id == replica_id){
			dl_part = tmp;
			goto OUT;
		}
	}

ERR:
OUT:
	
	pthread_mutex_unlock(&htbp->lock);
	gettimeofday(&tv, NULL);
	lookup_time = (tv.tv_sec * 1000000 + tv.tv_usec - start_lookup_time);
	//TODO: MAYL
	SKYFS_ERROR("%s:exit:ino:%llu,partition_id:%u,replica_id:%u,dl_part:%p, hashval %llu, lookup_time %llu\n", 
		__FUNCTION__, ino, partition_id, replica_id, dl_part, hashvalue, lookup_time);

	return dl_part;
}

int  __skyfs_DL_invalid_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id, 
		size_t write_version, size_t max_write_version)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_DL_part_t tmp_dl_part;
	skyfs_htb_t     *htbp = NULL;
	skyfs_s8_t      pname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t     hashvalue;
	skyfs_s32_t     fd;
	skyfs_s32_t     rc = 0;
	pthread_mutex_t * htb_lock = NULL;

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u,replica_id:%u\n", 
		__FUNCTION__, ino, partition_id, replica_id);

	dl_part = __skyfs_DL_lookup_partition_with_lock(ino, partition_id, replica_id, &htb_lock);
	if(dl_part != NULL){
		SKYFS_MSG("%s:get right partition in cache\n", __FUNCTION__);
		dl_part->replica_write_version = write_version;
		dl_part->max_write_version = max_write_version;
		memcpy(&tmp_dl_part,dl_part, sizeof( skyfs_DL_part_t));

		//goto find_dl_partition;
	}
	else{
		tmp_dl_part.replica_write_version = write_version;
		tmp_dl_part.max_write_version = max_write_version;
		tmp_dl_part.replica_id = replica_id;
		tmp_dl_part.partition_id = partition_id;
		tmp_dl_part.ino = ino;
		dl_part = ( skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
	        memcpy(dl_part,&tmp_dl_part, sizeof( skyfs_DL_part_t));	 
		pthread_rwlock_init(&dl_part->part_lock, NULL);

		//dl_part= &tmp_dl_part;
		hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
		hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
		htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);

		//pthread_mutex_lock(&htbp->lock);
		list_add(&(dl_part->part_hash), &(htbp->head));
		//pthread_mutex_unlock(&htbp->lock);

	}
	if(htb_lock != NULL){
		pthread_mutex_unlock(htb_lock);
	}


	SKYFS_ERROR("%s: compose_part_name replica %d\n", __FUNCTION__, replica_id);
	rc = __skyfs_SS_compose_partname(ino, partition_id, replica_id, pname);		
		fd = open(pname, O_RDWR|O_CREAT, 0666);	
		if(fd > 0){
			//dl_part = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			//bzero(dl_part, sizeof(skyfs_DL_part_t));
		rc = write(fd, &tmp_dl_part, sizeof(skyfs_DL_part_t));
						close(fd);
		}else{
			SKYFS_ERROR_1("%s:open partition file %s err:errno:%d, ino %llx, partition_id,%d,replica_id %d\n", 
					__FUNCTION__, pname, errno, ino , partition_id, replica_id);
			goto ERR;
		}

ERR: 
		return rc;
	
}

skyfs_DL_part_t *
__skyfs_DL_get_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_htb_t     *htbp = NULL;
	skyfs_s8_t      pname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t     hashvalue;
	skyfs_s32_t     fd;
	skyfs_s32_t     rc = 0;

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u,replica_id:%u\n", 
		__FUNCTION__, ino, partition_id, replica_id);

	dl_part = __skyfs_DL_lookup_partition(ino, partition_id, replica_id);
	if(dl_part != NULL){
		SKYFS_MSG("%s:get right partition in cache\n", __FUNCTION__);
		goto find_dl_partition;
	}else{
		SKYFS_ERROR("%s: compose_part_name replica %d\n", __FUNCTION__, replica_id);
		rc = __skyfs_SS_compose_partname(ino, partition_id, replica_id, pname);		
		fd = open(pname, O_RDONLY);	
		if(fd > 0){
			dl_part = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			bzero(dl_part, sizeof(skyfs_DL_part_t));
			pthread_rwlock_init(&dl_part->part_lock, NULL);
			rc = read(fd, dl_part, sizeof(skyfs_DL_part_t));
			hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
			hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
			htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);
			
			pthread_mutex_lock(&htbp->lock);
			list_add(&(dl_part->part_hash), &(htbp->head));
			pthread_mutex_unlock(&htbp->lock);

			close(fd);
		}else{
			SKYFS_ERROR("%s:open partition file %s err:errno:%d, ino %llx, partition_id,%d,replica_id %d\n", 
					__FUNCTION__, pname, errno, ino , partition_id, replica_id);
			goto ERR;
		}
	}

find_dl_partition:
ERR:

	SKYFS_MSG("%s:exit:ino:%llu,partition_id:%u,replica_id:%u,dl_part:%p\n", 
		__FUNCTION__, ino, partition_id, replica_id, dl_part);

	return dl_part;
}



skyfs_s32_t 
skyfs_commit_update_patition(skyfs_ino_t ino, skyfs_u64_t partition_id, 
		skyfs_u32_t replica_idx, skyfs_u32_t replica_cnt)
{
	skyfs_DL_part_t *buf = NULL;
	skyfs_htb_t     *htbp = NULL;
	skyfs_s8_t  pname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t hashvalue;
	skyfs_s32_t replica_id = 0;
	skyfs_s32_t fd = 0;
	skyfs_s32_t rc = 0;
	skyfs_u32_t first_replica = (ino+partition_id)%osd_num;

	skyfs_u64_t p_id =  partition_id;
	replica_id = replica_idx;
	// TODO : need get_partiton at first

	
	SKYFS_MSG("%s:alloc replica_id:%u,%p\n", __FUNCTION__, replica_id, pname);
	for(replica_id = 1; replica_id <= replica_cnt; replica_id ++){
		buf  = __skyfs_DL_get_partition(ino, partition_id, replica_id);
		if(buf != NULL){
			// TODO : mayl update the write version in this partition_file
			buf->replica_write_version++;


			continue;  
		}
		SKYFS_ERROR("%s: compose_part_name replica %d\n", __FUNCTION__, replica_id);
		rc = __skyfs_SS_compose_partname(ino, p_id, replica_id, pname);		
		fd = open(pname, O_RDWR|O_CREAT, 0666);	
		if(fd > 0){
			buf = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			buf->ino = ino;
			buf->partition_id = p_id;
			buf->replica_id = replica_id;
			//buf->free = SKYFS_MAX_OBJ_PER_PART;
			bzero(buf->obj_location, sizeof(skyfs_u32_t) * SKYFS_MAX_OBJ_PER_PART);
			rc = write(fd, buf, sizeof(skyfs_DL_part_t));
			if(rc < 0){
				SKYFS_ERROR("%s:write partition err:%d, errno:%d\n", 
					__FUNCTION__, rc, errno);
				goto ERR;
			}
			close(fd);
			hashvalue = __skyfs_get_dpartition_hashvalue(ino, p_id, replica_id);
			hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
			SKYFS_MSG("%s:hashvalue:%u\n", __FUNCTION__, hashvalue);
			htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);

			// TODO : fake code by mayl
			pthread_mutex_lock(&htbp->lock);
			list_add(&(buf->part_hash), &(htbp->head));
			pthread_mutex_unlock(&htbp->lock);
			//free(buf);
			}else{
				SKYFS_MSG("%s:fd:%d,errno:%d\n", __FUNCTION__, fd, errno);
				goto ERR;
		}
	}
ERR:
		
	SKYFS_MSG("%s:exit:ino:%llu,part_id:%u\n",
		__FUNCTION__, ino,  *partition_id);
	return rc;

}

skyfs_s32_t 
__skyfs_SS_alloc_partition(skyfs_DL_file_t *dl_file, 
			skyfs_u64_t obj_id, 
			skyfs_s32_t *partition_id)
{
	SKYFS_MSG("%s:part_id:%d\n", __FUNCTION__, *partition_id);

	skyfs_s32_t max_partition_id = 0;
	skyfs_DL_part_t *buf = NULL;
	skyfs_htb_t     *htbp = NULL;
	skyfs_s8_t  pname[SKYFS_MAX_NAME_LEN];
	skyfs_u32_t hashvalue;
	skyfs_s32_t replica_id = 0;
	skyfs_s32_t p_id = 0;
	skyfs_s32_t fd = 0;
	skyfs_s32_t rc = 0;

	SKYFS_MSG("%s:enter:ino:%llu,replica_num:%u,part_id:%d\n", 
		__FUNCTION__, dl_file->ino, dl_file->replica_num, *partition_id);

	p_id = obj_id / SKYFS_MAX_OBJ_PER_PART;	
	if(dl_file->partition_num > 0){
		max_partition_id = dl_file->partition_num - 1;
	}

	SKYFS_MSG("%s:pid:%d,max_partition_id:%u\n", __FUNCTION__, p_id, max_partition_id);

	if(dl_file->partition_num == 0 
	    || max_partition_id + 1 == p_id){
		for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){
			SKYFS_MSG("%s:alloc replica_id:%u,%p\n", __FUNCTION__, replica_id, pname);
			rc = __skyfs_SS_compose_partname(dl_file->ino, p_id, replica_id, pname);		
			fd = open(pname, O_RDWR|O_CREAT, 0666);	
			if(fd > 0){
				buf = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
				buf->ino = dl_file->ino;
				buf->partition_id = p_id;
				buf->replica_id = replica_id;
				buf->free = SKYFS_MAX_OBJ_PER_PART;
				buf->replica_write_version = 0;
				buf->max_write_version = 0;
				bzero(buf->obj_location, sizeof(skyfs_u32_t) * SKYFS_MAX_OBJ_PER_PART);
				rc = write(fd, buf, sizeof(skyfs_DL_part_t));
				if(rc < 0){
					SKYFS_ERROR("%s:write partition err:%d, errno:%d\n", 
						__FUNCTION__, rc, errno);
					goto ERR;
				}
				close(fd);
				hashvalue = __skyfs_get_dpartition_hashvalue(dl_file->ino, p_id, replica_id);
				hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
				SKYFS_MSG("%s:hashvalue:%u\n", __FUNCTION__, hashvalue);
				htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);
				
				// TODO mayl fake code 
				pthread_mutex_lock(&htbp->lock);
				list_add(&(buf->part_hash), &(htbp->head));
				pthread_mutex_unlock(&htbp->lock);
				//free(buf);
			}else{
				SKYFS_MSG("%s:fd:%d,errno:%d\n", __FUNCTION__, fd, errno);
				goto ERR;
			}
		}
		dl_file->partition_num = p_id + 1; 
		*partition_id = p_id;
	}else{
		SKYFS_ERROR("%s:err:partition_id:%d,partition_num:%u",
			__FUNCTION__, p_id, dl_file->partition_num);
		goto ERR;
	}
	
ERR:
		
	SKYFS_MSG("%s:exit:ino:%llu,obj_id:%llu,part_id:%u,partition_num:%u\n",
		__FUNCTION__, dl_file->ino, obj_id, *partition_id, dl_file->partition_num);
	return rc;
}

/*choose nodes for diff replica of a partition*/
skyfs_s32_t __skyfs_SS_place_partition(skyfs_DL_file_t *dl_file, 
				skyfs_u32_t partition_id,
				skyfs_dl_dest_t *dest)
{
	skyfs_DL_fileinfo_t *file_info = NULL, *tmp_file = NULL;
	skyfs_DL_nodeinfo_t *node_info = NULL, *tmp_node = NULL;
	struct list_head *file_index = NULL, *node_index = NULL;
	skyfs_s32_t node_added= 0;
	skyfs_s32_t rc = 0;
	skyfs_u32_t replica_num;
	skyfs_u32_t replica_id;
	skyfs_ino_t ino;
	skyfs_u32_t first_replica_osd, tmp_osd_id;
	// TODO mayl change this fuction here, we use fixed function to place partition 
	// so we can farward the request to other replica when local partition file lost
	// we will try to support dynamic load balance later
	
	ino = dl_file->ino;
	replica_num = dl_file->replica_num;
	first_replica_osd = (ino+partition_id)%(osd_info.osd_num) + 1;
	tmp_osd_id = first_replica_osd;
	
		
	for (int i = 1; i<=replica_num; i++){
		if(osd_info.osd_num == 1 || skyfs_replica == 1 ){
			dest->replica_location[replica_id] = osd_this_id;
		}else{
			tmp_osd_id =  ((first_replica_osd + i) % osd_info.osd_num) + 1;
			dest->replica_location[replica_id] = tmp_osd_id;
		}

	}
	return rc;
	// TODO mayl change end.

	SKYFS_MSG("%s:enter:ino:%llu,partition_id:%u\n",
		__FUNCTION__, dl_file->ino, partition_id);

	ino = dl_file->ino;
	replica_num = dl_file->replica_num;

	pthread_mutex_lock(&osd_node_head_lock);
	for(replica_id = 1; replica_id <= replica_num; replica_id ++){
		/*place each replica of a partition TODO temp changed  by mayl*/
		if(osd_info.osd_num == 1 || skyfs_replica == 1 ){
			dest->replica_location[replica_id] = osd_this_id;
		}else{
			node_added = 0;
			file_info = NULL;
			node_info = list_entry(osd_node_head.next, skyfs_DL_nodeinfo_t, node_list);
			if(node_info){
				SKYFS_MSG("%s:osd_info:osd_id:%d,access_times:%d,%p,replica_id:%d\n",
					__FUNCTION__, node_info->osd_id, node_info->access_times, node_info,replica_id);
				list_del_init(&node_info->node_list);
				dest->replica_location[replica_id] = node_info->osd_id;
			}else{
				SKYFS_ERROR("%s:osd:%d osd_node_list empty\n", __FUNCTION__, osd_this_id);
				pthread_mutex_unlock(&osd_node_head_lock);
				goto ERR;
			}

			list_for_each(file_index, &(node_info->file_head)){
				tmp_file = list_entry(file_index, skyfs_DL_fileinfo_t, file_list);
				if(tmp_file){
					if(tmp_file->ino == ino){
						file_info = tmp_file;
						file_info->access_times ++;
						gettimeofday(&file_info->last_time, NULL);
						list_del_init(&(file_info->file_list));
						break;
					}
				}else{
					SKYFS_ERROR("%s:node:%u is empty right now\n", __FUNCTION__, node_info->osd_id);
				}
			}
	
			if(file_info == NULL){
				file_info = (skyfs_DL_fileinfo_t *)malloc(sizeof(skyfs_DL_fileinfo_t));
				file_info->ino = ino;
				file_info->access_times = 1;
				gettimeofday(&file_info->last_time, NULL);
			}

			list_add(&file_info->file_list, &node_info->file_head);
			gettimeofday(&node_info->last_time, NULL);
	
			if(!list_empty(&osd_node_head)){
				list_for_each(node_index, &osd_node_head){
					tmp_node = list_entry(node_index, skyfs_DL_nodeinfo_t, node_list);
					SKYFS_MSG("%s:node list:osd_id:%u,active_files:%u\n", 
						__FUNCTION__, node_info->osd_id, node_info->active_files);
					if(tmp_node){
						if(tmp_node->active_files < node_info->active_files){
							list_add(&node_info->node_list, &tmp_node->node_list);
							node_added = 1;
							break;
						}
					}else{
						SKYFS_ERROR("%s:osd:%u skyfs_osd_node list empty\n", 
							__FUNCTION__, osd_this_id);
					}
				}
			}

			if(node_added == 0){
				list_add_tail(&node_info->node_list, &osd_node_head);
			}
		}
	}
	pthread_mutex_unlock(&osd_node_head_lock);

ERR:	
	SKYFS_MSG("%s:exit:ino:%llu, partition_id:%u\n",
		__FUNCTION__, dl_file->ino, partition_id);

	return rc;
}


 skyfs_DL_part_t  *  __skyfs_SS_check_alloc_partition(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u32_t replica_id, 
		 skyfs_u32_t location, skyfs_DL_part_t * got_part)
{
	 skyfs_u32_t     obj_index;
	 skyfs_s32_t     rc = 0;
	 skyfs_DL_part_t  *dl_part = NULL; 
	 skyfs_htb_t      *htbp = NULL;
	 skyfs_s8_t  pname[SKYFS_MAX_NAME_LEN];
	 skyfs_u32_t hashvalue;
	 skyfs_s32_t fd = 0;
	 pthread_mutex_t * htb_lock = NULL;

	 dl_part = __skyfs_DL_lookup_partition_with_lock(ino, partition_id, replica_id, &htb_lock);
	 if(dl_part != NULL){
		 memcpy(got_part, dl_part ,sizeof(skyfs_DL_part_t));
		 //SKYFS_ERROR_1("check alloc dl_partition success %p \n", dl_part);
		 if(htb_lock != NULL)
			 pthread_mutex_unlock(htb_lock);
		 return got_part;
	 }else{

		 dl_part = malloc(sizeof(skyfs_DL_part_t));
		 SKYFS_ERROR("check alloc dl_partition new alloc %p \n", dl_part);
		 memset(dl_part, 0, sizeof(skyfs_DL_part_t));
		 INIT_LIST_HEAD(&(dl_part->part_hash));
		 for(int n = 0; n<SKYFS_MAX_OBJ_PER_PART; n++){
			dl_part->interval_tree_handles[n] = (char*)get_IntervalTree_handle();
			SKYFS_ERROR("get ino %lu, partition %lu , obj %lu , intervalTree tree %p, tree %p  \n", 
					ino, partition_id, n,dl_part->interval_tree_handles[n]);
#if 0
			struct IntervalTree * tree = (struct IntervalTree *) dl_part->interval_tree_handles[n]; 
			struct interval inte;
			inte.low = 0;
			inte.high = 0;
			struct IntervalTNode * znode = do_search_exact_interval(tree, inte);
			SKYFS_ERROR("find inte 0,0 in tree %p , ret %p \n", tree, node);
#endif
		 }
		 // if part file exists ?
		  
		 SKYFS_ERROR("%s: compose_part_name replica %d\n", __FUNCTION__, replica_id);
		 rc = __skyfs_SS_compose_partname(ino, partition_id, replica_id, pname);
                  fd = open(pname, O_RDONLY);
		  if(fd > 0){
			  // read part from file
			  rc = read (fd, dl_part, sizeof(skyfs_DL_part_t));
			  if(rc <= 0){
		 	  	if(htb_lock != NULL)
					 pthread_mutex_unlock(htb_lock);
				free(dl_part);
                                SKYFS_ERROR_1("patition dev error, cannot read  partition\n");
				close(fd);
                                return NULL;
			   
			   }
			    //close(fd);
                            hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
                            hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
                            SKYFS_MSG("%s:hashvalue:%u\n", __FUNCTION__, hashvalue);
                            htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);
			    pthread_rwlock_init(&dl_part->part_lock, NULL);

			    //pthread_mutex_lock(&htbp->lock);
                            list_add(&(dl_part->part_hash), &(htbp->head));
		 	    if(htb_lock != NULL)
				pthread_mutex_unlock(htb_lock);
			    close(fd);
			    
		 	    memcpy(got_part, dl_part ,sizeof(skyfs_DL_part_t));
			    //pthread_mutex_unlock(&htbp->lock);

			    goto FINISH;
		  }else{
			   fd = open(pname, O_RDWR|O_CREAT, 0666);
			   if(fd <= 0){
				   
		 	      if(htb_lock != NULL)
					pthread_mutex_unlock(htb_lock);
				free(dl_part);
				SKYFS_ERROR_1("patition dev error, cannot create new partition\n");
				return NULL;
			   }
			   // init and write to part file
                           dl_part->ino = ino;
                           dl_part->partition_id = partition_id;
                           dl_part->replica_id = replica_id;
                           dl_part->free = SKYFS_MAX_OBJ_PER_PART;
                           dl_part->replica_write_version = 0;
                           dl_part->max_write_version = 0;
                           //bzero(dl_part->obj_location, sizeof(skyfs_u32_t) * SKYFS_MAX_OBJ_PER_PART);
		           for(int i = 0 ; i< SKYFS_MAX_OBJ_PER_PART; i++){
				dl_part->obj_location[i] = location;
			    }

			   
			   rc = write(fd, dl_part, sizeof(skyfs_DL_part_t));
			   if(rc <=  0){

		 	       if(htb_lock != NULL)
					pthread_mutex_unlock(htb_lock);
			        close(fd);
				free(dl_part);
                                SKYFS_ERROR_1("patition dev error, cannot write new partition\n");
                                return NULL;
			   
			   }
			    
                            hashvalue = __skyfs_get_dpartition_hashvalue(ino, partition_id, replica_id);
                            hashvalue = hashvalue % SKYFS_DL_PARTITION_HASH_LEN;
                            SKYFS_MSG("%s:hashvalue:%u\n", __FUNCTION__, hashvalue);
                            htbp = &(skyfs_dl_head.partition_hash_base[hashvalue]);
			    pthread_rwlock_init(&dl_part->part_lock, NULL);


			    //pthread_mutex_lock(&htbp->lock);
                            list_add(&(dl_part->part_hash), &(htbp->head));
			    //for(int n = 0; n<0; n++){
				    
				    //dl_part->interval_tree_handles[n] = get_IntervalTree_handle();
			    //}

		 	    memcpy(got_part, dl_part ,sizeof(skyfs_DL_part_t));
		 	    if(htb_lock != NULL)
					pthread_mutex_unlock(htb_lock);
			    
			    
			    // mayl fix bug , too many files opened.
			    if(fd > 0){
			    	close(fd);
				fd = 0;
			    }
			    //pthread_mutex_unlock(&htbp->lock);
			   
		  }



	 }
ERR:
FINISH:
	 	SKYFS_ERROR("alloc part ret %p fpname %s\n", dl_part,pname);
		 return  got_part;
	 

}
skyfs_s32_t __skyfs_SS_alloc_objs(skyfs_DL_file_t *dl_file, skyfs_u32_t partition_id)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_dl_dest_t des;
	//skyfs_u32_t     osd_id;
	skyfs_u32_t     replica_id;
	//skyfs_u32_t     free_index;
	skyfs_u32_t     obj_index;
	skyfs_s32_t     rc = 0;
	skyfs_s32_t     fd = 0;
	skyfs_s8_t  pname[SKYFS_MAX_NAME_LEN];

	SKYFS_MSG("%s:enter:ino:%llu, partition_id:%u\n", 
		__FUNCTION__, dl_file->ino, partition_id);

	rc = __skyfs_SS_place_partition(dl_file, partition_id, &des);
	if(rc < 0){
		SKYFS_ERROR("%s:obj placement err,errno:%d\n", __FUNCTION__, errno);
		goto ERR;
	}

	for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){
		dl_part = __skyfs_DL_get_partition(dl_file->ino, partition_id, replica_id);
		for(obj_index =0; obj_index < SKYFS_MAX_OBJ_PER_PART; obj_index ++){
			dl_part->obj_location[obj_index] = des.replica_location[replica_id];
		}
#if 1
		/*write partition back*/
		SKYFS_ERROR("%s: compose_part_name replica %d\n", __FUNCTION__, replica_id);
		rc = __skyfs_SS_compose_partname(dl_file->ino, partition_id, replica_id, pname);		
		fd = open(pname, O_RDWR|O_CREAT, 0666);	
		if(fd > 0){
			rc = write(fd, dl_part, sizeof(skyfs_DL_part_t));
			if(rc < 0){
				SKYFS_ERROR("%s:write partition err:%d, errno:%d\n", 
					__FUNCTION__, rc, errno);
				goto ERR;
			}
			close(fd);
		}else{
			SKYFS_MSG("%s:fd:%d,errno:%d\n", __FUNCTION__, fd, errno);
			goto ERR;
		}
#endif
	}


ERR:

	SKYFS_MSG("%s:exit:ino: %llu, partition_id:%u\n", 
		__FUNCTION__, dl_file->ino, partition_id);

	return rc;
}

skyfs_s32_t __skyfs_SS_alloc_obj(skyfs_DL_file_t *dl_file, 
				skyfs_u64_t obj_id, 
				skyfs_u32_t partition_id)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_dl_dest_t des;
	//skyfs_u32_t     osd_id;
	skyfs_u32_t     replica_id;
	//skyfs_u32_t     free_index;
	skyfs_u32_t     obj_index;
	skyfs_s32_t     rc = 0;

	SKYFS_MSG("%s:enter :ino: %llu, obj_id:%llu partition_id:%u\n", 
		__FUNCTION__, dl_file->ino, obj_id, partition_id);

	obj_index = obj_id % SKYFS_MAX_OBJ_PER_PART;
	if(obj_index > 0){
		for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){
			/*1.alloc obj for all the partition*/
			dl_part = __skyfs_DL_get_partition(dl_file->ino, partition_id, replica_id);

			/*2.set obj location*/
			dl_part->obj_location[obj_index] = dl_part->obj_location[obj_index - 1];
			SKYFS_MSG("%s:osd_id:%u\n", __FUNCTION__, dl_part->obj_location[obj_index]);
		}
	}else{//if obj_index == 0, we need to alloc a new partition
		rc = __skyfs_SS_place_partition(dl_file, partition_id, &des);
		if(rc < 0){
			SKYFS_ERROR("%s:obj placement err,errno:%d\n", __FUNCTION__, errno);
			goto ERR;
		}

		for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){
			/*1.alloc obj for all the partition*/
			dl_part = __skyfs_DL_get_partition(dl_file->ino, partition_id, replica_id);

			/*2.set obj location*/
			dl_part->obj_location[obj_index] = des.replica_location[replica_id];
		}
	}


ERR:

	SKYFS_MSG("%s:exit:ino: %llu, obj_id:%llu partition_id:%u\n", 
		__FUNCTION__, dl_file->ino, obj_id, partition_id);

	return rc;
}

skyfs_s32_t __skyfs_SS_read_fill_des(skyfs_DL_file_t *dl_file, 
			skyfs_u64_t obj_id, 
			skyfs_u32_t partition_id,
			skyfs_dl_dest_t *des,
			int prefer_replica, 
			skyfs_DL_part_t ** pdl_part)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_u32_t replica_id = 0;
	skyfs_u32_t obj_index;
	//skyfs_u32_t osd_id;
	skyfs_s32_t rc = 0;

	SKYFS_MSG("%s:enter.ino:%llu, partition_id:%u, max part:%u, obj_id:%llu\n",
			__FUNCTION__, dl_file->ino, partition_id, SKYFS_MAX_OBJ_PER_PART, obj_id);

	des->replica_num = dl_file->replica_num;
	obj_index = obj_id % SKYFS_MAX_OBJ_PER_PART;

	for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){

		if(replica_id != prefer_replica)
			continue;
		dl_part = __skyfs_DL_get_partition(dl_file->ino, partition_id, replica_id);
		if(replica_id == prefer_replica)
			*pdl_part = dl_part;
		if(dl_part){
			SKYFS_MSG("%s:ino:%llu,pid:%u,replica_id:%u,obj_index:%u,osd_id:%u\n", 
				__FUNCTION__, dl_file->ino, partition_id, 
				replica_id, obj_index, dl_part->obj_location[obj_index]);
			des->replica_location[replica_id] = dl_part->obj_location[obj_index];
			des->max_write_version = dl_part->max_write_version;
			des->write_version[replica_id] = dl_part->replica_write_version;
		}else if(replica_id == prefer_replica){
			SKYFS_ERROR_1("%s:get partition %llu %u %u err,errno:%d\n",
				__FUNCTION__, dl_file->ino, partition_id, replica_id, errno);
			rc = -ENOENT;
			des->replica_location[replica_id] = -1;
                        //des->max_write_version = 0;
                        des->write_version[replica_id] = 0;
			goto ERR;
		}
		else{
			continue;
		}
	}

ERR:
	SKYFS_ERROR("%s:exit.ino:%llu, partition_id:%u, des:%p, part:%p\n",
			__FUNCTION__, dl_file->ino, partition_id, des, dl_part);


	return rc;
}


skyfs_s32_t __skyfs_SS_fill_des(skyfs_DL_file_t *dl_file, 
			skyfs_u64_t obj_id, 
			skyfs_u32_t partition_id,
			skyfs_dl_dest_t *des)
{
	skyfs_DL_part_t *dl_part = NULL;
	skyfs_u32_t replica_id = 0;
	skyfs_u32_t obj_index;
	//skyfs_u32_t osd_id;
	skyfs_s32_t rc = 0;

	SKYFS_MSG("%s:enter.ino:%llu, partition_id:%u, max part:%u, obj_id:%llu\n",
			__FUNCTION__, dl_file->ino, partition_id, SKYFS_MAX_OBJ_PER_PART, obj_id);

	des->replica_num = dl_file->replica_num;
	obj_index = obj_id % SKYFS_MAX_OBJ_PER_PART;

	for(replica_id = 1; replica_id <= dl_file->replica_num; replica_id ++){
		dl_part = __skyfs_DL_get_partition(dl_file->ino, partition_id, replica_id);
		if(dl_part){
			SKYFS_MSG("%s:ino:%llu,pid:%u,replica_id:%u,obj_index:%u,osd_id:%u\n", 
				__FUNCTION__, dl_file->ino, partition_id, 
				replica_id, obj_index, dl_part->obj_location[obj_index]);
			des->replica_location[replica_id] = dl_part->obj_location[obj_index];
			des->max_write_version = dl_part->max_write_version;
			des->write_version[replica_id] = dl_part->replica_write_version;
		}else{
			SKYFS_ERROR("%s:get partition %llu %u %u err,errno:%d\n",
				__FUNCTION__, dl_file->ino, partition_id, replica_id, errno);
			rc = -ENOENT;
			goto ERR;
		}
	}

ERR:
	SKYFS_MSG("%s:exit.ino:%llu, partition_id:%u, des:%p, part:%p\n",
			__FUNCTION__, dl_file->ino, partition_id, des, dl_part);

	return rc;
}

/*This is the end of osd_dl.h*/
