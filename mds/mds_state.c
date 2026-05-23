/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: mds_state.c $
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
#include "mds_state.h"
#include "mds_layout.h"


skyfs_s32_t __skyfs_MS_collect_state(skyfs_state_info_t *state_info)
{
    skyfs_u64_t user_j, nice_j, sys_j, idle_j, iowait_j = 0;
    skyfs_u64_t user_j1, nice_j1, sys_j1, idle_j1, iowait_j1 = 0;
    skyfs_u64_t u, n, s, i, io = 0;
    skyfs_u64_t cpu_usage = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_collect_state:enter:\n");

    pthread_mutex_lock(&mds_state_version_lock);
    if(__skyfs_five_cpu_numbers(&user_j, &nice_j, &sys_j, &idle_j, &iowait_j)!=0){
        SKYFS_ERROR("__skyfs_MS_collect_state:read /proc/stat error\n");
        rc = -EIO;
        goto ERR;
    }    

    sleep(5);

    if(__skyfs_five_cpu_numbers(&user_j1, &nice_j1, &sys_j1, &idle_j1, &iowait_j1)!=0){
        SKYFS_ERROR("__skyfs_MS_collect_state:read /proc/stat error\n");
        rc = -EIO;
        goto ERR;
    }

    u = user_j1 -user_j;
    n = nice_j1 -nice_j;
    s = sys_j1 - sys_j;
    i = idle_j1 -idle_j;
    io = iowait_j1 - iowait_j;

    cpu_usage = (i * 100) / (u + n + s + i + io);

    SKYFS_MSG("__skyfs_MS_collect_state:cpu_usage:%lld\n", cpu_usage);
    state_info->cpu_usage = cpu_usage;
    state_info->state_version = mds_state_version ++;
ERR:
    pthread_mutex_unlock(&mds_state_version_lock);

    SKYFS_LEAVE("__skyfs_MS_collect_state:exit:\n");
    return rc;
}

void __skyfs_MS_profile_create()
{
    skyfs_u32_t create_num = 0;
    skyfs_u32_t split_num = 0;
    skyfs_u32_t enlarge_num = 0;
    
    if(skyfs_profile_flag == 0){
    }else if(skyfs_profile_flag == 2){
        pthread_mutex_lock(&mds_profile_create_lock);
        create_num = mds_profile_create;
        mds_profile_create ++;
        pthread_mutex_unlock(&mds_profile_create_lock);
        if(create_num % 100000 == 0){
            pthread_mutex_lock(&mds_profile_split_lock);
            split_num = mds_profile_split;
            pthread_mutex_unlock(&mds_profile_split_lock);
            pthread_mutex_lock(&mds_profile_enlarge_lock);
            enlarge_num = mds_profile_enlarge;
            pthread_mutex_unlock(&mds_profile_enlarge_lock);

            SKYFS_ERROR("__skyfs_MS_profile_create: create %d split %d enlarge %d\n", 
                create_num, split_num, enlarge_num);
        }
    }

}

void __skyfs_MS_profile_split()
{
    if(skyfs_profile_flag == 0){
    }else if(skyfs_profile_flag == 2){
        pthread_mutex_lock(&mds_profile_split_lock);
        mds_profile_split ++;
        pthread_mutex_unlock(&mds_profile_split_lock);
    }

}

void __skyfs_MS_profile_enlarge()
{
    if(skyfs_profile_flag == 0){
    }else if(skyfs_profile_flag == 2){
        pthread_mutex_lock(&mds_profile_enlarge_lock);
        mds_profile_enlarge ++;
        pthread_mutex_unlock(&mds_profile_enlarge_lock);
    }

}
/*This is end of mds_state.c*/
