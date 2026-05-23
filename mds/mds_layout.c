/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_layout.c $
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
#include "mds_cache.h"
#include "mds_layout.h"
#include "mds_itm.h"

skyfs_layout_t mds_layout[SKYFS_SUBSET_HASH_LEN];
skyfs_layout_t osd_layout[SKYFS_MAX_OSD_NUM];
skyfs_layout_L1_t mds_mapping_l1[SKYFS_MDS_L1MAPPING_LEN];
skyfs_layout_L1_t osd_mapping_l1[SKYFS_OSD_L1MAPPING_LEN];
skyfs_mds_status_t mds_status[SKYFS_MAX_MDS_NUM];

skyfs_u32_t     mds_last_index;
skyfs_u32_t     mds_layout_version;
pthread_mutex_t mds_layout_lock;

skyfs_u32_t	    mds_state_version;
pthread_mutex_t mds_state_version_lock;

skyfs_u32_t     mds_consistent_hash_ok;
skyfs_u32_t     mds_average_hash_scope;


void __skyfs_MS_init_mds_extent(void)
{
	skyfs_u32_t hashvalue;
	skyfs_u32_t mds_id;
	skyfs_u32_t virtual;
	skyfs_u32_t mds_last_index = 0;
	skyfs_u32_t i;

	mds_layout_version = 0;
	mds_state_version = 0;
	pthread_mutex_init(&mds_state_version_lock, NULL);
	pthread_mutex_init(&mds_layout_lock, NULL);

	mds_average_hash_scope = SKYFS_SUBSET_HASH_LEN / mds_num;
	mds_consistent_hash_ok = 0;

	bzero(mds_layout, sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);
	for(i = 0; i < SKYFS_MAX_MDS_NUM; i ++){
		if(mds_info.mds[i].id > 0){
		mds_id = mds_info.mds[i].id;
		if(mds_consistent_hash_ok){
			hashvalue = __skyfs_num2hashvalue(mds_id);
			hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
		}else{
			hashvalue = i * mds_average_hash_scope;  
		}

		SKYFS_ERROR("__skyfs_MS_init_mds_extent:hashvalue:%d\n", hashvalue);
		mds_layout[hashvalue].id = mds_id;
		mds_layout[hashvalue].virtual = 0;
		}
	}

	/*Get the last vaild index, in fact it is the first vaild index*/
	if(mds_consistent_hash_ok){
		for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i ++){
			if(mds_layout[i].id != 0){
				mds_last_index = i;
			}
		}
	}else{
		mds_last_index = 0;
	}

	mds_id = mds_layout[mds_last_index].id;
	virtual = mds_layout[mds_last_index].virtual;

	for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i ++){
		if(mds_layout[i].id != 0){
			mds_id = mds_layout[i].id;
			virtual = mds_layout[i].virtual;
			if(mds_id == mds_this_id){
				skyfs_subset_cache_htbbase[i].length = 0;
			}
		}else{
			mds_layout[i].id = mds_id;
			mds_layout[i].virtual = virtual;
			if(mds_id == mds_this_id){
				skyfs_subset_cache_htbbase[i].length = 0;
			}
		}
	}

	if(mds_this_id == SKYFS_MASTER_MDS_ID){
		__skyfs_MS_init_status_hashnum();
	}
}

void __skyfs_MS_init_osd_extent(void)
{
	skyfs_f64_t ratio;
	skyfs_f64_t ratio_t;
	skyfs_u32_t i;

	ratio = 1 / (skyfs_f64_t)osd_num;

	ratio_t = ratio;

	for(i = 0; i < osd_num; i ++){
		osd_layout[i].id = osd_info.osd[i].id;
		osd_layout[i].ratio = ratio_t;
		ratio_t += ratio;
		SKYFS_ERROR("__skyfs_MS_init_osd_extent:i:%f\n", 
			osd_layout[i].ratio);
	}

	osd_layout[osd_num - 1].ratio = 1;
}

skyfs_s32_t __skyfs_MS_init_mdshtbp(void)
{
	skyfs_u32_t i;
	skyfs_u32_t mds_id;
	skyfs_s32_t rc = 0;

	for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i ++){
		mds_id = mds_mapping_l1[i].id;
		if(mds_id == mds_this_id){
			skyfs_subset_cache_htbbase[i].length = 0;
		}
	}

	return rc;
}

skyfs_s32_t __skyfs_MS_init_layout(void)
{
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_MS_init_layout:enter\n");

	__skyfs_init_mdsmapping(0, &mds_info, mds_mapping_l1);

	__skyfs_MS_init_mdshtbp();

	__skyfs_init_osdmapping(0, &osd_info, osd_mapping_l1);

//	__skyfs_MS_init_osd_extent();

	SKYFS_LEAVE("__skyfs_MS_init_layout:exit\n");

	return rc;
}

skyfs_s32_t __skyfs_MS_writeback_layout(void)
{
	skyfs_s32_t rc = 0;


	return rc;
}

skyfs_u32_t __skyfs_MS_search_mds_extent(skyfs_u32_t hashvalue)
{
	skyfs_u32_t mds_id = 0;
	skyfs_u32_t index;	
	
	index = hashvalue % SKYFS_MDS_L1MAPPING_LEN;

	mds_id = mds_mapping_l1[index].id;
	
	SKYFS_MSG("__skyfs_MS_search_mds_extent:hash:%d,index:%d,mds_id:%d,ver:%d\n",
		hashvalue, index, mds_id, mds_layout_version);

	return mds_id;
}

skyfs_u32_t __skyfs_MS_search_osd_extent(skyfs_u32_t hashvalue)
{
	skyfs_u32_t osd_id = 0;
	skyfs_u32_t index;

	index = hashvalue % SKYFS_SUBSET_HASH_LEN;

	osd_id = osd_mapping_l1[index].id;
	
	SKYFS_MSG("__skyfs_MS_search_osd_extent:hash:%d,index:%d,osd_id:%d\n",
		hashvalue, index, osd_id);

	return osd_id;
}

void __skyfs_MS_init_status_hashnum()
{
	skyfs_u32_t i;
	skyfs_u32_t index;
	skyfs_u32_t mds_id;
	skyfs_u32_t count = 0;
	skyfs_u32_t hashnum = 0;

	mds_id = mds_layout[mds_last_index].id;

	for( i = mds_last_index; count <= SKYFS_SUBSET_HASH_LEN ; i++){
		index = i % SKYFS_SUBSET_HASH_LEN;
		if(mds_id == mds_layout[index].id){
			hashnum ++;
		}else{
			mds_status[mds_id].hashnum = hashnum;
			mds_status[mds_id].seqnum = hashnum;
			SKYFS_ERROR("__skyfs_MS_init_status_hashnum:mds_id%d,hashnum:%d\n",
				mds_id, hashnum);
			mds_id = mds_layout[index].id;
			hashnum = 1;
		}
		count ++;
	}

}

skyfs_s32_t
__skyfs_get_mds_index(skyfs_u32_t mds_id)
{
	skyfs_s32_t index = -1;
	skyfs_u32_t i;

	for(i = 0; i < mds_num; i ++){
		if(mds_info.mds[i].id == mds_id){
			index = i;
			break;
		}
	}

	return index;
}

skyfs_s32_t 
__skyfs_MS_get_first_loadout(skyfs_u32_t mds_id, skyfs_u32_t balance_num)
{
	skyfs_s32_t first_index = 0;
	skyfs_u32_t first_mds_index = 0;
	skyfs_s32_t mds_index = 0;

	if(mds_status[mds_id].seqnum <= balance_num){
		SKYFS_MSG("__skyfs_MS_get_first_index:seqnum:%d less bnum:%d\n",
			mds_status[mds_id].seqnum, balance_num);
		first_index = -ENOENT;
		goto err_out;
	}

	if(mds_consistent_hash_ok){
		first_mds_index = __skyfs_num2hashvalue(mds_id) % SKYFS_SUBSET_HASH_LEN;
	}else{
		mds_index = __skyfs_get_mds_index(mds_id);
		if(mds_index >= 0){
			first_mds_index=__skyfs_get_mds_index(mds_id)*mds_average_hash_scope;
		}else{
			SKYFS_ERROR("__skyfs_MS_get_first_index:get mds index error\n");
			first_index = -ENOENT;
			exit(1);
			goto err_out;
		}
	}

	first_index = (first_mds_index + (mds_status[mds_id].seqnum - balance_num))
			% SKYFS_SUBSET_HASH_LEN;

	SKYFS_MSG("__skyfs_MS_get_first_i:seq:%d,bnum:%d,ask_mds:%d,origin_mds:%d\n",
		mds_status[mds_id].seqnum, balance_num, 
		mds_id, mds_layout[first_index].id);
	if(mds_id != mds_layout[first_index].id) {
		SKYFS_ERROR("__skyfs_MS_get_first_i:error!:mds_id:%d origin:%d,ver:%d\n",
			mds_id, mds_layout[first_index].id, mds_layout_version);
		SKYFS_ERROR("__skyfs_MS_get_first_i:seqnum:%d,bnum:%d,first_index:%d\n",
			mds_status[mds_id].seqnum, balance_num, first_index);
		exit(1);
	}

err_out:
	return first_index;
}

skyfs_s32_t
__skyfs_MS_show_layout(void)
{
	skyfs_u32_t i;
	skyfs_s32_t rc = 0;
	skyfs_u32_t count = 0;

	for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i++){
		if(skyfs_subset_cache_htbbase[i].length == 0){
			SKYFS_MSG("__skyfs_MS_show_lay:index:%d,mds_id:%d,ext_mds_id:%d\n",
				i, mds_this_id, mds_layout[i].id);
			count ++;
		}
	}

	SKYFS_MSG("__skyfs_MS_show_layout:ver:%d,mds_id:%d,count:%d\n",
		mds_layout_version, mds_this_id, count);

	if(count != mds_status[mds_this_id].hashnum 
		|| count != mds_status[mds_this_id].seqnum){
		SKYFS_MSG("__skyfs_MS_show_layout:count error!!:%d, seq:%d, hash:%d\n",
			count, mds_status[mds_this_id].seqnum, 
			mds_status[mds_this_id].hashnum);
	}

	if(mds_status[mds_this_id].seqnum > mds_status[mds_this_id].hashnum){
		SKYFS_ERROR("__skyfs_MS_show_layout:seq:%d larger hashnum:%d\n",
			mds_status[mds_this_id].seqnum, mds_status[mds_this_id].hashnum);
		exit(1);
	}

	return rc;
}

skyfs_u32_t
__skyfs_MS_do_update_mdslayout(skyfs_u32_t mds_id, 
				skyfs_u32_t kind_mds_id,
				skyfs_u32_t first_index,
				skyfs_u32_t balance_num)
{
	skyfs_u32_t rc = 0;
	skyfs_u32_t count = 0;
	skyfs_u32_t index = 0;
	skyfs_u32_t last_hash;
	skyfs_u32_t i;

	SKYFS_ERROR("__skyfs_MS_do_update_layout:mds_id:%d,kind_mds_id:%d,first:%d,num:%d\n",
		mds_id, kind_mds_id, first_index, balance_num);

	for(i = first_index;count < balance_num; i ++){
		index = i % SKYFS_SUBSET_HASH_LEN;
		mds_layout[index].id = kind_mds_id;
		count ++;
	}
	
	mds_status[mds_id].hashnum -= balance_num;
	mds_status[mds_id].seqnum -= balance_num;

	if(mds_status[mds_id].seqnum < 0){
		SKYFS_MSG("__skyfs_MS_do_update_layout:mds_id:%d,seq_num:%d,bnum:%d\n",
		mds_id, mds_status[mds_id].seqnum, balance_num);
	}

	mds_status[kind_mds_id].hashnum += balance_num;
	last_hash = (first_index+SKYFS_SUBSET_HASH_LEN - 1) % SKYFS_SUBSET_HASH_LEN;
	if(mds_layout[last_hash].id == kind_mds_id){
		mds_status[kind_mds_id].seqnum += balance_num;
	}

	mds_layout_version ++;

	__skyfs_MS_show_layout();

	return rc;
}

skyfs_u32_t __skyfs_MS_get_layoutv(void)
{
	return mds_layout_version;	
}

skyfs_s32_t 
__skyfs_MS_check_layout_version(skyfs_u32_t mds_layoutv, skyfs_u32_t mds_id)
{
	skyfs_s32_t rc = 0;

	if(mds_layoutv <= mds_layout_version){
		SKYFS_MSG("__skyfs_M_check_layout_ver:mine newer,mine:%d,master%d\n", 
			mds_layout_version, mds_layoutv);
	}else{
		SKYFS_ERROR("__skyfs_M_check_layout_version:oldv %d, get %d from %d\n", 
			mds_layout_version, mds_layoutv, mds_id);
		__skyfs_M2M_get_layout(mds_id);
	}

	__skyfs_MS_show_layout();

	return rc;
}
/*This is end of mds_layout.c*/
