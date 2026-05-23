/* 
 *  Copyright (c) 2010  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_loadb.c $
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
#include "osd_profile.h"

skyfs_u64_t random_num;
skyfs_u32_t chooseid = 0;
skyfs_u32_t request_his[5] = {0};
skyfs_u32_t request_index = 0;

skyfs_s32_t __skyfs_SS_collect_state(skyfs_state_info_t *state_info)
{
    //skyfs_u64_t user_j, nice_j, sys_j, idle_j, iowait_j = 0;
    //skyfs_u64_t user_j1, nice_j1, sys_j1, idle_j1, iowait_j1 = 0;
    //skyfs_u64_t u, n, s, i, io = 0;
    //skyfs_u64_t cpu_usage = 0;
	//skyfs_u32_t j = 0;

	skyfs_s32_t rc = 0;

	pthread_mutex_lock(&osd_request_queue_lock);
	state_info->request_num = osd_nr_request;
    //osd_nr_request = 0;
	pthread_mutex_unlock(&osd_request_queue_lock);

	request_his[request_index] = state_info->request_num;
#if 0
	j = (request_index + 1)%5;
	for(i = 0; i< 5; i ++){
        SKYFS_DEBUG(" %2d ", request_his[j]);
		j = (j + 1)%5;
    }
	request_index = (request_index + 1) % 5;
    SKYFS_ERROR("__skyfs_SS_collect_state:end\n");

	if(__skyfs_five_cpu_numbers(&user_j,&nice_j,&sys_j,&idle_j,&iowait_j)!=0){
        SKYFS_ERROR("__skyfs_SS_collect_state:read /proc/stat error\n");
        rc = -EIO;
        goto ERR;
    }    

    sleep(1);

    if(__skyfs_five_cpu_numbers(&user_j1,&nice_j1,&sys_j1,&idle_j1,&iowait_j1)!=0){
        SKYFS_ERROR("__skyfs_SS_collect_state:read /proc/stat error\n");
        rc = -EIO;
        goto ERR;
    }

    u = user_j1 -user_j;
    n = nice_j1 -nice_j;
    s = sys_j1 - sys_j;
    i = idle_j1 -idle_j;
    io = iowait_j1 - iowait_j;

    cpu_usage = (i * 100) / (u + n + s + i + io);

    SKYFS_MSG("__skyfs_SS_collect_state:cpu_usage:%lld\n", cpu_usage);
    state_info->cpu_usage = cpu_usage;
ERR:
#endif
	//SKYFS_MSG("__skyfs_SS_collect_state:request_num:%d\n", state_info->request_num);

	return rc;
}

skyfs_s32_t __skyfs_SS_get_statusindex(skyfs_u32_t osd_id)
{
	skyfs_u32_t index = 0;
	skyfs_u32_t i;

	for(i = 1; i <= osd_num; i++){
        if(sort_osd_status[i] == osd_id){
            index = i;
			break;
		}
	}

	SKYFS_MSG("__skyfs_SS_get_statusindex:%d\n",index);

	return index;
}

skyfs_s32_t __skyfs_SS_judge_status(skyfs_u32_t osd_id)
{
	skyfs_u32_t tmp;
	skyfs_u32_t index = 0;
	skyfs_u32_t i, j;

	for(i = 1; i <= osd_num; i++){
        for(j = i+1; j <= osd_num; j++){
            if(osd_status[sort_osd_status[j]].state_info.request_num 
					> osd_status[sort_osd_status[i]].state_info.request_num){
				tmp = sort_osd_status[i];
				sort_osd_status[i] = sort_osd_status[j];
				sort_osd_status[j] = tmp;
			}
		}
	}


	index = __skyfs_SS_get_statusindex(osd_id);

    SKYFS_ERROR("sorted_osd:");

    for(i = 1; i<= osd_num; i ++){
        SKYFS_DEBUG(" %2d ", sort_osd_status[i]);
    }

	SKYFS_ERROR("__skyfs_SS_judge_status:index:%u\n", index);
	return index;

}

skyfs_u32_t __skyfs_SS_choose2read(skyfs_DL_file_t *dl_file, skyfs_dl_dest_t *des)
{
	skyfs_s32_t replica_id;
	skyfs_u32_t replica_num = 1; //? from 1

	SKYFS_MSG("%s:enter.ino:%llu, des:%p, replica_num:%u,replica1L:%u\n",
		__FUNCTION__, dl_file->ino, des, des->replica_num, des->replica_location[1]);

	//osd_id = des->replica_location[1];
	replica_id = -1;
	while(replica_num < des->replica_num){
		if(des->max_write_version <= des->write_version[replica_num]){
			replica_id =  replica_num;
			break;
		}
		replica_num++;

	}
	if(replica_id <0){
		// added by mayl
		SKYFS_ERROR_1("%s:exit FAILED.ino:%llu, replica_id:%u\n", __FUNCTION__, dl_file->ino, replica_id);
	}


	SKYFS_MSG("%s:exit.ino:%llu, replica_id:%u\n", __FUNCTION__, dl_file->ino, replica_id);

	return replica_id;
}

/*This is end of osd_loadb.c*/
