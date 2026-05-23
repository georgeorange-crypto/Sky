/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_layout.c $
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


#include "osd_init.h"
#include "osd_fs.h"
#include "osd_dl.h"
#include "osd_layout.h"

skyfs_layout_t osd_layout[SKYFS_MAX_OSD_NUM];

skyfs_layout_t osd_data_layout[SKYFS_DLSUBSET_HASH_LEN];

skyfs_layout_L1_t osd_mapping_l1[SKYFS_OSD_L1MAPPING_LEN];

skyfs_u32_t    osd_dl_version = 0;
skyfs_u32_t    osd_state_version = 0;

pthread_mutex_t osd_state_version_lock;
pthread_mutex_t osd_data_layout_lock;

skyfs_u32_t     osd_dl_average_hash_scope;
skyfs_u32_t     osd_consistent_hash_ok;

void __skyfs_SS_init_osd_extent(void)
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
		SKYFS_ERROR("__skyfs_SS_init_osd_extent:%d:%f\n", 
			i, osd_layout[i].ratio);
	}

	osd_layout[osd_num - 1].ratio = 1;
}

void __skyfs_SS_init_osd_dlextent(void)
{
    skyfs_u32_t hashvalue;
	skyfs_u32_t osd_id;
	skyfs_u32_t virtual;
	skyfs_u32_t osd_last_index = 0;
	skyfs_u32_t i;
	
	osd_dl_version = 0;
    osd_state_version = 0;
	pthread_mutex_init(&osd_state_version_lock, NULL);
	pthread_mutex_init(&osd_data_layout_lock, NULL);

    osd_dl_average_hash_scope = SKYFS_DLSUBSET_HASH_LEN / osd_num;
	osd_consistent_hash_ok = 0;

    bzero(osd_data_layout, sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);

	for(i = 0; i < SKYFS_MAX_OSD_NUM; i ++){
		osd_id = osd_info.osd[i].id;
		if(osd_id > 0){
			if(osd_consistent_hash_ok){
				hashvalue = __skyfs_num2hashvalue(osd_id);
				hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
			}else{
				hashvalue = (i - 1)* osd_dl_average_hash_scope;
			}

			SKYFS_ERROR("__skyfs_SS_init_osd_dlextent:hashvalue:%d\n", hashvalue);
			osd_data_layout[hashvalue].id = osd_id;
			osd_data_layout[hashvalue].virtual = 0;
		}
	}

	/*Get the last vaild index, in fact it is the first vaild index*/
	if(osd_consistent_hash_ok){
		for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i ++){
			if(osd_data_layout[i].id != 0){
				osd_last_index = i;
			}
		}
	}else{
		osd_last_index = 0;
	}

	osd_id = osd_data_layout[osd_last_index].id;
	virtual = osd_data_layout[osd_last_index].virtual;

	for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i ++){
		if(osd_data_layout[i].id != 0){
			osd_id = osd_data_layout[i].id;
			virtual = osd_data_layout[i].virtual;
			if(osd_id == osd_this_id){
				skyfs_dlsubset_cache_htbbase[i].length = 0;
			}
		}else{
			osd_data_layout[i].id = osd_id;
			osd_data_layout[i].virtual = virtual;
			if(osd_id == osd_this_id){
				skyfs_dlsubset_cache_htbbase[i].length = 0;
			}
		}
	}

}

skyfs_s32_t __skyfs_SS_init_status_hashnum(void)
{
	skyfs_s32_t rc = 0;

	return rc;
}

skyfs_s32_t __skyfs_SS_init_layout(void)
{
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_SS_init_layout:enter:%d\n", osd_info.osd[1].id);

	__skyfs_init_osdmapping(0, 
			&osd_info, 
			osd_mapping_l1);

	SKYFS_LEAVE("__skyfs_SS_init_layout:init data layout\n");

//	__skyfs_SS_init_osd_dlextent();

	SKYFS_LEAVE("__skyfs_SS_init_layout:exit\n");

	return rc;
}

skyfs_u32_t __skyfs_SS_get_subset_hashvalue(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
	skyfs_u32_t hashvalue = 0;
	skyfs_s8_t tmp[SKYFS_MAX_NAME_LEN/2], string[SKYFS_MAX_NAME_LEN];

	bzero(tmp, SKYFS_MAX_NAME_LEN/2);
	bzero(string, SKYFS_MAX_NAME_LEN);

	sprintf(string, "%d-%d", dir_id, subset_id);
	//sprintf(tmp, "%d", subset_id);

	//strcat(string,tmp);

	hashvalue = __skyfs_name2hashvalue(string);

	return hashvalue;
}

skyfs_u32_t __skyfs_SS_search_osd_dlextent(skyfs_u32_t hashvalue)
{
	return __skyfs_SS_search_osd_extent(hashvalue);
#if 0
	skyfs_u32_t osd_id = 0;
    skyfs_u32_t index;

    index = hashvalue % SKYFS_DLSUBSET_HASH_LEN;

    osd_id = osd_data_layout[index].id;

    SKYFS_MSG("__skyfs_SS_search_osd_dlextent:hash:%d,index:%d,osd_id:%d,ver:%d\n",
    	hashvalue, index, osd_id, osd_dl_version);

    return osd_id;
#endif
}


skyfs_u32_t __skyfs_SS_search_osd_extent(skyfs_u32_t hashvalue)
{
	skyfs_u32_t osd_id = 0;
	skyfs_u32_t index;

	index = hashvalue % SKYFS_OSD_L1MAPPING_LEN;

    osd_id = osd_mapping_l1[index].id;

	return osd_id;
}

skyfs_u32_t
__skyfs_SS_judge_osdid_rename(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
	skyfs_u32_t	osd_id;
	skyfs_u32_t hashvalue;  
	/* changed bg mayl for rename */
	hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1));
	osd_id = __skyfs_SS_search_osd_extent(hashvalue);

	return osd_id;
}


skyfs_u32_t
__skyfs_SS_judge_osdid(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
	skyfs_u32_t	osd_id;
	skyfs_u32_t hashvalue;  
	/* changed bg mayl for rename */
	SKYFS_ERROR_1("call %s !!!\n", __FUNCTION__);
	hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1));
	osd_id = __skyfs_SS_search_osd_extent(hashvalue);

	return osd_id;
}
/*This is end of osd_layout.c*/
