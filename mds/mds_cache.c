/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_cache.c $
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

#include "mds_fs.h"
#include "mds_op.h"
#include "mds_thread.h"
#include "mds_init.h"
#include "mds_cache.h"
#include "mds_layout.h"
#include "mds_help.h"

#include "mds_ito.h"
#include "mds_itm.h"

static int stop_fs = 0;
static int crash_recover = 0;
skyfs_M_cmeta_t root_cmeta;

skyfs_htb_t skyfs_dir_cache_htbbase[SKYFS_DIR_HASH_LEN];
skyfs_htb_t skyfs_subset_cache_htbbase[SKYFS_SUBSET_HASH_LEN];
skyfs_htb_t skyfs_flock_cache_htbbase[SKYFS_LOCK_HASH_LEN];

skyfs_htb_t skyfs_dir_depth_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_s32_t total_bmeta_num;
skyfs_s32_t total_subset_cache_num;
skyfs_s32_t total_subset_index_num;
skyfs_s32_t total_dir_depth_num;
skyfs_s32_t total_dir_cache_num;
pthread_mutex_t total_bmeta_num_lock;
pthread_mutex_t total_subset_cache_num_lock;
pthread_mutex_t total_subset_index_num_lock;
pthread_mutex_t total_dir_depth_num_lock;
pthread_mutex_t total_dir_cache_num_lock;

skyfs_u32_t total_access_bmeta_num = 0;
pthread_mutex_t total_access_bmeta_num_lock;

skyfs_u32_t total_read_bmeta_num = 0;
pthread_mutex_t total_read_bmeta_num_lock;

struct list_head mds_wb_subset_list;
pthread_mutex_t  mds_wb_subset_list_lock;

skyfs_u32_t __skyfs_MS_get_dir_id(skyfs_ino_t ino, skyfs_u32_t flag)
{
    skyfs_u32_t dir_id = 0;
    skyfs_u64_t mask;

    if(flag == 0){
        mask = ~((skyfs_u64_t)~0 << 32);
        dir_id = (skyfs_u32_t)(mask & ino);
    }else if(flag == 1){
        mask = ((skyfs_u64_t)~0 << 32);
        dir_id = (skyfs_u32_t)((mask & ino) >> 32);
    }
    
    SKYFS_MSG("__skyfs_MS_get_dir_id:ino:%llu,flag:%d,dir_id:%d\n",
        ino, flag, dir_id);
    return dir_id;
}

/*added by mayl*/
skyfs_s32_t locate_next_subset_id(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
	skyfs_s32_t next_subsetid = -1;
	skyfs_s32_t tmp_subsetid = 0;
	skyfs_M_dir_depth_t *dir_depth = NULL;
	skyfs_s32_t max_subsetid = 0;

	SKYFS_ERROR("SKYFS—locate next subset %d -> %d\n", dir_id, subset_id);
    	dir_depth = __skyfs_MS_get_dir_depth(dir_id);
	if(dir_depth == NULL){
        	SKYFS_ERROR_1("__skyfs_MS_locate_next_subset_id:get dir_depth NULL\n");
        	goto ERR;
    	}

	max_subsetid = (skyfs_s32_t)((1<<dir_depth->depth)-1);
	for(tmp_subsetid = subset_id+1; tmp_subsetid <= max_subsetid ; tmp_subsetid ++){
		if(__skyfs_MS_test_bit(dir_depth->subset_bm, tmp_subsetid) != 0){
			next_subsetid = tmp_subsetid;
			break;
		}
	}

	pthread_mutex_unlock(&(dir_depth->lock));

	SKYFS_ERROR("SKYFS—locate next subset %d -> %d, %d\n", dir_id, subset_id, next_subsetid);

ERR:
	return next_subsetid;

}

skyfs_u32_t __skyfs_MS_get_subsetid_by_name(skyfs_M_dir_depth_t *dir_depth, 
                skyfs_s8_t *name)
{
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t    hashvalue = 0;
    skyfs_s32_t split_depth = 0;

    hashvalue = __skyfs_name2hashvalue(name);
    split_depth = dir_depth->depth;

retry_lower:
    subset_id = __skyfs_get_subset_id(hashvalue, split_depth);
    if(__skyfs_MS_test_bit(dir_depth->subset_bm, subset_id) == 0 && split_depth >= 0){
        SKYFS_MSG("__skyfs_MS_get_subsetid_by_name:%d not exist,retry\n", subset_id);
        split_depth --;
        goto retry_lower;
    }
    SKYFS_MSG("__skyfs_MS_get_subsetid_by_name:subset_id:%d\n", 
        subset_id);

    return subset_id;
}

skyfs_u32_t    __skyfs_MS_get_subsetid_by_ino(skyfs_M_dir_depth_t *dir_depth, 
                skyfs_ino_t ino, 
                skyfs_u32_t conflict_index)
{
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t hashvalue = 0;
    skyfs_s32_t split_depth = 0;

    hashvalue =  __skyfs_ino2hashvalue(ino, conflict_index);
    split_depth = dir_depth->depth;

retry_lower:
    subset_id = __skyfs_get_subset_id(hashvalue, split_depth);
    if(__skyfs_MS_test_bit(dir_depth->subset_bm, subset_id) == 0 && split_depth >= 0){
        SKYFS_MSG("__sunfget_subsetid_ino:%d not exist,retry,ino:%lld\n", 
            subset_id, ino);
        split_depth --;
        goto retry_lower;
    }

    SKYFS_MSG("__skyfs_MS_get_subsetid_by_ino:subset_id:%d\n", 
        subset_id);

    return subset_id;
}

skyfs_u32_t    __skyfs_MS_get_bmetaid(skyfs_u32_t subset_depth, skyfs_u32_t hashvalue)
{
    skyfs_u32_t    bmeta_id = 0;
    skyfs_u32_t filter;

    filter = ((skyfs_u32_t)1 << SKYFS_AVA_HASH_BITS);
    hashvalue = hashvalue % filter;

    bmeta_id = (skyfs_u32_t)((skyfs_u32_t)hashvalue 
                >> (SKYFS_AVA_HASH_BITS - subset_depth));
    
    SKYFS_MSG("__skyfs_MS_get_bmetaid:hashvalue:%u,bmeta_id:%d,subset_depth:%d\n", 
        hashvalue, bmeta_id, subset_depth);
    return bmeta_id;
}

skyfs_u32_t __skyfs_MS_judge_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_u32_t mds_id = 0;
    skyfs_u32_t hashvalue;

    /* changed by mayl for rename */
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1));
    mds_id = __skyfs_MS_search_mds_extent(hashvalue);

    return mds_id;
}

skyfs_u32_t __skyfs_MS_judge_dir_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
	return __skyfs_MS_judge_mdsid(dir_id, subset_id);
}

skyfs_u32_t __skyfs_MS_judge_osdid(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_u32_t osd_id = 0;
    skyfs_u32_t hashvalue;  

    /* changed by mayl for rename */
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id & (SKYFS_MAX_SUBSET_PER_DIR-1));
    //hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    osd_id = __skyfs_MS_search_osd_extent(hashvalue);

    SKYFS_ERROR("__skyfs_MS_judge_osdid:hash:%d,osd_id:%d\n",
        hashvalue, osd_id);

    return osd_id;
}

skyfs_htb_t *
__skyfs_MS_locate_subset_by_name(skyfs_ino_t dir_ino, 
                skyfs_s8_t *name,
                skyfs_u32_t    *mds_id,
                skyfs_u32_t    *dir_id,
                skyfs_u32_t *subset_id)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue = 0;
    skyfs_u32_t c_mds_id;
    skyfs_M_dir_depth_t *dir_depth = NULL;

    *dir_id = __skyfs_MS_get_dir_id(dir_ino, 0);
    dir_depth = __skyfs_MS_get_dir_depth(*dir_id);
    if(dir_depth == NULL){
        SKYFS_ERROR("__skyfs_MS_locate_subset_by_name:get dir_depth NULL\n");
        goto ERR;
    }

    
    SKYFS_ERROR("__skyfs_MS_locate_sbt_by_name:%d,depth:%d\n", *dir_id, dir_depth->depth);

    if(dir_depth->depth == 0){
        *subset_id = 0;
    }else{
        *subset_id = __skyfs_MS_get_subsetid_by_name(dir_depth, name);
    }
    
    pthread_mutex_unlock(&dir_depth->lock);

    c_mds_id = __skyfs_MS_judge_mdsid(*dir_id, *subset_id);
    if(c_mds_id != mds_this_id){
        *mds_id = c_mds_id;
        goto OUT;
    }else{
        *mds_id = 0;
        hashvalue = __skyfs_get_subset_hashvalue(*dir_id, *subset_id);
        hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
        htbp = &skyfs_subset_cache_htbbase[hashvalue];
        if(htbp == NULL){
            SKYFS_ERROR("__skyfs_MS_locae_subset_by_name:locate htbp error:%d\n",
                *subset_id);
            goto ERR;
        }
    }

OUT:
ERR:

    SKYFS_MSG("__skyfs_MS_locate_subset_by_name:hashvalue:%d,subset_id:%d\n", 
        hashvalue, *subset_id);
    return htbp;
}

skyfs_htb_t *
__skyfs_MS_locate_rename_subset_by_ino(skyfs_ino_t ino, 
                skyfs_u32_t conflict_index,
                skyfs_u32_t    *mds_id,
                skyfs_u32_t *dir_id,
                skyfs_u32_t    *subset_id)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t    hashvalue = 0;
    skyfs_u32_t c_mds_id;
    skyfs_M_dir_depth_t *dir_depth = 0;
    
    
    SKYFS_MSG("__skyfs_MS_locate_sbt_by_ino:%d,depth:%d\n", *dir_id, dir_depth->depth);

        

    c_mds_id = __skyfs_MS_judge_mdsid(*dir_id, *subset_id);
    /* by mayl set mds_id to mds_this_id */
    if(c_mds_id != mds_this_id){
        *mds_id = c_mds_id;
        goto OUT;
    }else{
        *mds_id = 0;
        hashvalue = __skyfs_get_subset_hashvalue(*dir_id, *subset_id);
        hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
        htbp = &skyfs_subset_cache_htbbase[hashvalue];
        if(htbp == NULL){
            SKYFS_ERROR_1("__skyfs_MS_locae_rename_subset_by_ino:locate htbp error%d\n", 
                *subset_id);
            goto ERR;
        }
    }

OUT:
ERR:

    SKYFS_MSG("__skyfs_MS_locate_subset_by_ino:hashvalue:%d\n", hashvalue);
    return htbp;
}


skyfs_htb_t *
__skyfs_MS_locate_subset_by_ino(skyfs_ino_t ino, 
                skyfs_u32_t conflict_index,
                skyfs_u32_t    *mds_id,
                skyfs_u32_t *dir_id,
                skyfs_u32_t    *subset_id)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t    hashvalue = 0;
    skyfs_u32_t c_mds_id;
    skyfs_M_dir_depth_t *dir_depth = 0;
    
    *dir_id = __skyfs_MS_get_dir_id(ino, 1);
    dir_depth = __skyfs_MS_get_dir_depth(*dir_id);
    if(dir_depth == NULL){
        SKYFS_ERROR("__skyfs_MS_locate_subset_by_ino:get dir_depth NULL\n");
        goto ERR;
    }

    SKYFS_MSG("__skyfs_MS_locate_sbt_by_ino:%d,depth:%d\n", *dir_id, dir_depth->depth);

    if(dir_depth->depth == 0){
        *subset_id = 0;
    }else{
        *subset_id = __skyfs_MS_get_subsetid_by_ino(dir_depth, ino, conflict_index);
    }
    
    pthread_mutex_unlock(&dir_depth->lock);

    c_mds_id = __skyfs_MS_judge_mdsid(*dir_id, *subset_id);
    if(c_mds_id != mds_this_id){
        *mds_id = c_mds_id;
        goto OUT;
    }else{
        *mds_id = 0;
        hashvalue = __skyfs_get_subset_hashvalue(*dir_id, *subset_id);
        hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
        htbp = &skyfs_subset_cache_htbbase[hashvalue];
        if(htbp == NULL){
            SKYFS_ERROR("__skyfs_MS_locae_subset_by_ino:locate htbp error%d\n", 
                *subset_id);
            goto ERR;
        }
    }

OUT:
ERR:

    SKYFS_MSG("__skyfs_MS_locate_subset_by_ino:hashvalue:%d\n", hashvalue);
    return htbp;
}


skyfs_M_bmeta_t *
__skyfs_MS_locate_bmeta_by_name(skyfs_M_subset_cache_t *subset_cache,
                skyfs_s8_t *name,
                skyfs_u32_t    *bmeta_id)
{
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;

    SKYFS_ENTER("__skyfs_MS_locate_bmeta_by_name:enter,name:%s\n", name);

    pthread_mutex_lock(&subset_cache->lock);

    hashvalue = __skyfs_name2hashvalue(name);
    *bmeta_id = __skyfs_MS_get_bmetaid(subset_cache->subset_depth, hashvalue);    
    hashvalue = __skyfs_get_bmeta_hashvalue(*bmeta_id);
    htbp = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_locate_bmeta_by_name:hash table:%d NULL\n", hashvalue);
        goto ERR;
    }
    
    bmeta = __skyfs_MS_find_bmeta(htbp, *bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_locate_bmeta_by_name:can not find bmeta\n");
        goto ERR;
    }
    pthread_mutex_lock(&bmeta->lock);

ERR:

    if(bmeta != NULL){
        pthread_mutex_unlock(&subset_cache->lock);
    }

    SKYFS_LEAVE("__skyfs_MS_locate_bmeta_by_name:exit,bmeta_id:%d,depth:%d\n", 
        *bmeta_id, subset_cache->subset_depth);
    pthread_mutex_lock(&total_access_bmeta_num_lock);
    total_access_bmeta_num ++;
    pthread_mutex_unlock(&total_access_bmeta_num_lock);

    return bmeta;
}

/* TODO  added by mayl*/
skyfs_M_bmeta_t *
__skyfs_MS_locate_rename_bmeta_by_ino(skyfs_M_subset_cache_t *subset_cache,
                skyfs_ino_t ino,
                skyfs_u32_t    conflict_index,
                skyfs_u32_t *bmeta_id)
{
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;
	skyfs_u32_t tmp_bmeta_id;
    
    SKYFS_ENTER("__skyfs_MS_locate_bmeta_by_ino:enter:ino:%lld\n", ino);

    pthread_mutex_lock(&subset_cache->lock);

    //hashvalue = __skyfs_ino2hashvalue(ino, conflict_index);
    //*bmeta_id = __skyfs_MS_get_bmetaid(subset_cache->subset_depth, hashvalue);
    tmp_bmeta_id = (skyfs_u32_t)((ino & ((skyfs_u64_t)((1<<30)-1))) / SKYFS_MAX_META_PER_BOX);
	*bmeta_id = tmp_bmeta_id;
    hashvalue = __skyfs_get_bmeta_hashvalue(*bmeta_id);

    SKYFS_MSG("__skyfs_MS_locate_bmeta_by_ino:bmeta_id:%d\n, hashvalue:%d",
        *bmeta_id, hashvalue);

    htbp = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_locate_bmeta_by_ino:hash table NULL\n");
        goto ERR;
    }

    bmeta = __skyfs_MS_find_bmeta(htbp, *bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_locate_bmeta_by_ino:can not find bmeta\n");
        goto ERR;
    }
    pthread_mutex_lock(&bmeta->lock);

ERR:

    if(bmeta != NULL){
        pthread_mutex_unlock(&subset_cache->lock);
    }

    SKYFS_LEAVE("__skyfs_MS_locate_bmeta_by_ino:exit\n");
    
    pthread_mutex_lock(&total_access_bmeta_num_lock);
    total_access_bmeta_num ++;
    pthread_mutex_unlock(&total_access_bmeta_num_lock);
    return bmeta;
}


skyfs_M_bmeta_t *
__skyfs_MS_locate_bmeta_by_ino(skyfs_M_subset_cache_t *subset_cache,
                skyfs_ino_t ino,
                skyfs_u32_t    conflict_index,
                skyfs_u32_t *bmeta_id)
{
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;
    
    SKYFS_ENTER("__skyfs_MS_locate_bmeta_by_ino:enter:ino:%lld\n", ino);

    pthread_mutex_lock(&subset_cache->lock);

    hashvalue = __skyfs_ino2hashvalue(ino, conflict_index);
    *bmeta_id = __skyfs_MS_get_bmetaid(subset_cache->subset_depth, hashvalue);
    hashvalue = __skyfs_get_bmeta_hashvalue(*bmeta_id);

    SKYFS_MSG("__skyfs_MS_locate_bmeta_by_ino:bmeta_id:%d\n, hashvalue:%d",
        *bmeta_id, hashvalue);

    htbp = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_locate_bmeta_by_ino:hash table NULL\n");
        goto ERR;
    }

    bmeta = __skyfs_MS_find_bmeta(htbp, *bmeta_id);
    if(bmeta == NULL){
        SKYFS_MSG("__skyfs_MS_locate_bmeta_by_ino:can not find bmeta\n");
        goto ERR;
    }
    pthread_mutex_lock(&bmeta->lock);

ERR:

    if(bmeta != NULL){
        pthread_mutex_unlock(&subset_cache->lock);
    }

    SKYFS_LEAVE("__skyfs_MS_locate_bmeta_by_ino:exit\n");
    
    pthread_mutex_lock(&total_access_bmeta_num_lock);
    total_access_bmeta_num ++;
    pthread_mutex_unlock(&total_access_bmeta_num_lock);
    return bmeta;
}

skyfs_M_mmeta_t *
__skyfs_MS_locate_mmeta_by_name(skyfs_M_bmeta_t *bmeta,
                skyfs_s8_t    *name)
{
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_u32_t i;
    
    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
        cmeta = &(bmeta->cmetap[i]);
#if 1
        if(strlen(cmeta->name)){
            SKYFS_MSG("__skyfs_lote_name:i:%d,name:%s,ino:%lld\n", 
                i, cmeta->name, cmeta->ino);
        }
#endif
        if(strcmp(cmeta->name, name) == 0 && (cmeta->type != SKYFS_RENAME)){
            SKYFS_MSG("__skyfs_lote_m_name:bmeta_id:%d,i:%d,name:%s,ino:%lld\n",
                bmeta->box_id, i, cmeta->name, cmeta->ino);
            if(cmeta->nextfree != -1){
                mmeta = &(bmeta->mmetap[i]);
            }else{
                SKYFS_MSG("__skyfs_locate_mmeta_by_name:%s %lld is unlink\n", 
                    cmeta->name, cmeta->ino);
                exit(1);
            }
            break;
        }
    }

    SKYFS_LEAVE("__skyfs_MS_locate_mmeta_by_name:exit\n");
    return mmeta;
}


/*When the metadata is unlink, inode should be accessed through ino*/
skyfs_M_mmeta_t *
__skyfs_MS_locate_rename_mmeta_by_ino(skyfs_M_bmeta_t *bmeta,
                skyfs_ino_t ino,
                skyfs_u32_t    conflict_index)
{
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_u32_t i;
    int recheck = 0;
    SKYFS_ERROR("locate_renamed mmeta in ino %lx, bmeta %p \n", ino, bmeta);
#if 0  
re_check:

    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
        cmeta = &(bmeta->cmetap[i]);
        if(cmeta->ino == ino 
          && cmeta->type == SKYFS_RENAME){
            mmeta = &(bmeta->mmetap[i]);
            SKYFS_MSG("__skyfs_lote_ino:bmeta_id:%d,i:%d,ino:%lld,id:%d\n",
                bmeta->box_id, i, ino, mmeta->id);
            if(mmeta->id != i) exit(1);
            break;
		}else if(cmeta->conflict_index == conflict_index && recheck == 1){
			SKYFS_ERROR_1("locate_Rename_mmeta_by_ino recheck  %d, only conlict_index match %x, ino Faied %llx:%llx , type %d\n ", 
				i, conflict_index, ino, cmeta->ino, cmeta->type);
		}
    }
#endif
    i = ((skyfs_u32_t)(ino))%(SKYFS_MAX_META_PER_BOX);
    cmeta = &(bmeta->cmetap[i]);
    if(cmeta->ino == ino &&  cmeta->type == SKYFS_RENAME){
	mmeta = &(bmeta->mmetap[i]);
    }
    SKYFS_ERROR("locate_renamed mmeta in ino %lx, bmeta %p , at pos %d ,get cmeta %p , mmeta %p ,type %d, cino %lx\n", 
		    ino, bmeta, i, cmeta, mmeta, cmeta->type, cmeta->ino);
 #if 0
    if(mmeta == NULL){
        SKYFS_ERROR("__skyfs_locate_ino:error:wanted:ino:%llu,conflict:%u\n",
            ino, conflict_index);
        for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
            cmeta = &(bmeta->cmetap[i]);
            if(cmeta->ino > 0){
                SKYFS_ERROR("__skyfs_lote_ino:bmeta_id:%d,i:%d,ino:%llu,con:%u,name:%s,type:%d\n",
                    bmeta->box_id, i, cmeta->ino, cmeta->conflict_index, cmeta->name, cmeta->type);
            }
        }
        //exit(1);
    }
#endif
    return mmeta;
}


/*When the metadata is unlink, inode should be accessed through ino*/
skyfs_M_mmeta_t *
__skyfs_MS_locate_mmeta_by_ino(skyfs_M_bmeta_t *bmeta,
                skyfs_ino_t ino,
                skyfs_u32_t    conflict_index)
{
    skyfs_M_mmeta_t    *mmeta = NULL;
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_u32_t i;
    int recheck = 2;
  re_check:
    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
        cmeta = &(bmeta->cmetap[i]);
        if(cmeta->ino == ino 
          && cmeta->conflict_index == conflict_index 
          && cmeta->type != SKYFS_LINK){
            mmeta = &(bmeta->mmetap[i]);
            SKYFS_MSG("__skyfs_lote_ino:bmeta_id:%d,i:%d,ino:%lld,id:%d\n",
                bmeta->box_id, i, ino, mmeta->id);
            if(mmeta->id != i) exit(1);
            break;
	}else if(cmeta->conflict_index == conflict_index && recheck == 1){
		SKYFS_ERROR_1("locate_mmeta_by_ino recheck  %d, Only conlict_index match %x, ino Faied %llx:%llx , type %d, name %s, hashkey %lx, bmeta_id %d\n ", 
				i, conflict_index, ino, cmeta->ino, cmeta->type, cmeta->name, cmeta->hashkey, bmeta->box_id);
	}
    }

    if(mmeta == NULL && recheck == 0){
	    recheck ++;
	    if(recheck == 1)
	    	goto re_check;
    }

    if(mmeta == NULL){
        SKYFS_ERROR("__skyfs_locate_ino:error:wanted:ino:%llu,conflict:%u\n",
            ino, conflict_index);
        for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
            cmeta = &(bmeta->cmetap[i]);
            if(cmeta->ino > 0){
                SKYFS_ERROR("__skyfs_lote_ino:bmeta_id:%d,i:%d,ino:%llu,con:%u,name:%s,type:%d\n",
                    bmeta->box_id, i, cmeta->ino, cmeta->conflict_index, cmeta->name, cmeta->type);
            }
        }
        //exit(1);
    }

    return mmeta;
}



skyfs_s32_t
__skyfs_MS_check_mmeta_exist(skyfs_M_bmeta_t *bmeta,
                skyfs_s8_t *name,
                skyfs_ino_t ino)
{
    skyfs_M_cmeta_t    *cmeta = NULL;
    skyfs_u32_t     i;
    skyfs_s32_t     rc = 0;

    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i++){
        cmeta = &(bmeta->cmetap[i]);
        if((strcmp(cmeta->name, name) == 0) && (cmeta->nextfree != -1) 
             && (cmeta->type != SKYFS_RENAME)){
            SKYFS_ERROR("__skyfs_MS_chek_mmeta_exist:the name %s eixst\n", name);
            rc = -EEXIST;
            break;
        }
        if(cmeta->ino == ino){
            SKYFS_ERROR("__skyfs_MS_chek_mmeta_exist:the ino %lld eixst\n", ino);
            rc = -EAGAIN;
            break;
        }
    }

    cmeta = NULL;
    SKYFS_MSG("__skyfs_MS_check_mmeta_exist:exit.rc:%d\n",rc);
    return rc;

}

skyfs_M_bmeta_t *
__skyfs_MS_alloc_bmeta(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_M_bmeta_t  *tmp_bmeta = NULL;
    skyfs_M_wb_req_t *req = NULL;
    skyfs_s32_t      tmp_bmeta_num = 0;

    SKYFS_ENTER("__skyfs_MS_alloc_bmeta:enter.\n");
    pthread_mutex_lock(&total_bmeta_num_lock);
    if(total_bmeta_num > SKYFS_MAX_BMETA_NUM/2){
        total_bmeta_num --;
        tmp_bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
        if(tmp_bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_alloc_bmeta:alloc memory errno:%d\n",errno);
            pthread_mutex_unlock(&total_bmeta_num_lock);
            goto ERR;
        }
        SKYFS_ERROR("__skyfs_MS_alloc_bmeta:total_bmeta_num:%d,subset_id:%d\n", 
            total_bmeta_num, subset_id);
    }else{
        /*Free some bmeta to get enough space*/
        SKYFS_ERROR("__skyfs_MS_alloc_bmeta:no enough bmeta:%d\n", total_bmeta_num);
        total_bmeta_num --;
        tmp_bmeta = (skyfs_M_bmeta_t *)malloc(sizeof(skyfs_M_bmeta_t));
        if(tmp_bmeta == NULL){
            SKYFS_ERROR("__skyfs_MS_alloc_bmeta:alloc memory errno:%d\n",errno);
            pthread_mutex_unlock(&total_bmeta_num_lock);
            goto ERR;
        }
        tmp_bmeta_num = total_bmeta_num;
    }
    pthread_mutex_unlock(&total_bmeta_num_lock);

    if(tmp_bmeta_num){
        req = (skyfs_M_wb_req_t *)malloc(sizeof(skyfs_M_wb_req_t));
        if(req == NULL){
            SKYFS_ERROR("__skyfs_MS_alloc_bmeta:alloc wb req err:%d\n", errno);
            goto ERR;
        }
        req->total_bmeta_num = tmp_bmeta_num;
        INIT_LIST_HEAD(&(req->req_list));
        pthread_mutex_lock(&mds_wb_request_queue_lock);
        list_add_tail(&(req->req_list), &mds_wb_request_queue);
        pthread_mutex_unlock(&mds_wb_request_queue_lock);
        sem_post(&mds_wb_request_queue_sem);
    }

ERR:
    SKYFS_LEAVE("__skyfs_MS_alloc_bmeta:exit.tmp_bmeta:%p\n", tmp_bmeta);
    return tmp_bmeta;
}

skyfs_s32_t
__skyfs_MS_release_bmeta(skyfs_M_bmeta_t *bmeta)
{
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("__skyfs_MS_release_bmeta:enter\n");

    //free(bmeta);
    
    pthread_mutex_lock(&total_bmeta_num_lock);
    total_bmeta_num ++;
    SKYFS_ERROR("__skyfs_MS_release_bmeta:total_num:%d\n", total_bmeta_num);
    pthread_mutex_unlock(&total_bmeta_num_lock);

    SKYFS_LEAVE("__skyfs_MS_release_bmeta:exit\n");
    return rc;
}

skyfs_s32_t
__skyfs_MS_free_bmeta(skyfs_M_bmeta_t *bmeta)
{
    skyfs_s32_t rc = 0;
    
    SKYFS_ENTER("__skyfs_MS_free_bmeta:enter,bmeta_id:%d\n", bmeta->box_id);

    list_del_init(&(bmeta->bmeta_hash));
    list_del_init(&(bmeta->bmeta_list));

    rc = __skyfs_MS_release_bmeta(bmeta);

    SKYFS_LEAVE("__skyfs_MS_free_bmeta:exit\n");
    return rc;
}

skyfs_M_bmeta_t *
__skyfs_MS_find_bmeta(skyfs_htb_t *htbp,
                skyfs_u32_t    bmeta_id)
{
    skyfs_M_bmeta_t *bmeta = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_find_bmeta:enter.bmeta_id:%d\n",bmeta_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_find_bmeta:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_bmeta_t, bmeta_hash);
        if(tmp->box_id == bmeta_id){
            bmeta = tmp;
            SKYFS_LEAVE("__skyfs_MS_find_bmeta:bmeta_id:%d,firstfree:%d\n",
                bmeta_id, bmeta->firstfree);
            goto OUT;
        }
    }

OUT:
ERR:

    SKYFS_LEAVE("__skyfs_MS_find_bmeta:exit,%p\n", bmeta);
    return bmeta;
}

skyfs_s32_t
__skyfs_MS_init_bmeta(skyfs_M_bmeta_t *bmeta, skyfs_u32_t bmeta_id)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t hashvalue;
    skyfs_u32_t i;

    SKYFS_MSG("__skyfs_MS_init_bmeta:init %d\n", bmeta_id);

    hashvalue = __skyfs_get_bmeta_hashvalue(bmeta_id);
    bmeta->hashvalue = hashvalue;
    bmeta->box_id = bmeta_id;
    bmeta->nfree = SKYFS_MAX_META_PER_BOX;
    bmeta->firstfree = 0;
    bmeta->nlink_orign = bmeta->nlink_update = 0;
    gettimeofday(&bmeta->first_time, NULL);
    bmeta->last_time = bmeta->first_time;

    pthread_mutex_init(&bmeta->lock, NULL);
    pthread_rwlock_init(&bmeta->rwlock, NULL);
    INIT_LIST_HEAD(&bmeta->bmeta_hash);
    INIT_LIST_HEAD(&bmeta->bmeta_list);

    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i ++){
        //bmeta->mmetap[i]->cmetap = bmeta->cmetap[i];
        bmeta->mmetap[i].id = i;
        pthread_mutex_init(&(bmeta->mmetap[i].lock), NULL);
        bmeta->cmetap[i].nextfree = i + 1;
        bmeta->cmetap[i].hashkey = 0;
		// added by mayl for flock
		INIT_LIST_HEAD(&bmeta->mmetap[i].flock_head);
		INIT_LIST_HEAD(&bmeta->mmetap[i].posix_lock_head);
		bmeta->mmetap[i].lock_htb_head = NULL;
        //bmeta->cmetap[i].id = i;
    }

    return rc;
}

skyfs_M_bmeta_t *
__skyfs_MS_get_bmeta(skyfs_M_subset_cache_t *subset_cache,
                skyfs_u32_t    bmeta_id)
{
    skyfs_s32_t rc = 0;
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;

    SKYFS_ENTER("__skyfs_MS_get_bmeta:enter.bmeta_id:%d\n", bmeta_id);
    /*Need to judge if there is enough memory*/

    //pthread_mutex_lock(&subset_cache->lock);
    bmeta = __skyfs_MS_alloc_bmeta(subset_cache->dir_id, subset_cache->subset_id);
    if(bmeta == NULL){
        SKYFS_ERROR("__skyfs_MS_get_bmeta:new alloc bmeta NULL\n");
        goto ERR_NONE;
    }

    bzero(bmeta, sizeof(skyfs_M_bmeta_t));
    if(subset_cache->subset_id == 0 
        && !subset_cache->nlink_orign 
        && !subset_cache->nlink_update){
        /*Only the first bmeta of the dir should be init, 
         * other bmeta need to be read from osd*/
        SKYFS_ERROR("__skyfs_MS_get_bmeta:init bmeta subset_id:%d, bmeta_id:%d\n",
            subset_cache->subset_id, bmeta_id);
        rc = __skyfs_MS_init_bmeta(bmeta, bmeta_id);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_get_bmeta:init bmeta failed:%d\n", rc);
            goto ERR;
        }
    }else{
        rc = __skyfs_MS_read_bmeta(bmeta, subset_cache->dir_id,
            subset_cache->subset_id, bmeta_id);
	if(subset_cache >= SKYFS_MAX_SUBSET_PER_DIR && rc == -ENOENT 
			&& bmeta->nfree == -ENOENT && bmeta->firstfree == -ENOENT){
		rc = __skyfs_MS_init_bmeta(bmeta, bmeta_id);
        	if(rc < 0){
            		SKYFS_ERROR_1("__skyfs_MS_get_bmeta:init rename bmeta %u faild %d\n", bmeta_id, rc);
            		goto ERR;
        	}else{
			SKYFS_ERROR("__skyfs_MS_get_bmeta:init rename bmeta  %d,, success \n", bmeta_id);

		}
				
        }else if(rc < 0){
            	SKYFS_ERROR("__skyfs_MS_get_bmeta:read bmeta from OSD error\n");
            	goto ERR;
			
        }

        if(bmeta->nfree == 0){
            SKYFS_MSG("__skyfs_MS_get_bmeta:bmeta:%d nfree=0\n", bmeta->box_id);
            //exit(1);
        }
    }

    hashvalue = bmeta->hashvalue;
    htbp = &(subset_cache->bmeta_hash_base[hashvalue]);
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_get_bmeta:hash table NULL,hashvalue:%d\n",
            hashvalue);
        goto ERR;
    }

    pthread_mutex_lock(&bmeta->lock);

    list_add_tail(&(bmeta->bmeta_hash), &(htbp->head));
    list_add_tail(&(bmeta->bmeta_list), &(subset_cache->bmeta_head));

    pthread_mutex_unlock(&subset_cache->lock);

    SKYFS_LEAVE("__skyfs_MS_get_bmeta:exit.bmeta:nfree:%d,hash_value:%lld,id:%d\n", 
        bmeta->nfree, bmeta->hashvalue, bmeta->box_id);
    return bmeta;
    
ERR:
    rc = __skyfs_MS_release_bmeta(bmeta);
    if(bmeta){
        free(bmeta);
    }
ERR_NONE:

    pthread_mutex_unlock(&subset_cache->lock);

    SKYFS_LEAVE("__skyfs_MS_get_bmeta:get bmeta failed.exit\n");
    return NULL;
}

skyfs_s32_t 
__skyfs_MS_read_bmeta(skyfs_M_bmeta_t *bmeta, 
                skyfs_u32_t dir_id, 
                skyfs_u32_t subset_id, 
                skyfs_u32_t bmeta_id)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t recover_cnt = 0;
    skyfs_s32_t do_recover_cnt = 0;
    skyfs_u32_t osd_id;
    skyfs_u32_t i;
    skyfs_meta_vector_t vector;
    skyfs_M_mmeta_t *mmeta = NULL;
    skyfs_M_cmeta_t *cmeta = NULL;

    SKYFS_ERROR("__skyfs_MS_read_bmeta:enter,dir_id:%d, subset:%d, bmeta:%d\n", 
        dir_id, subset_id, bmeta_id);

    pthread_mutex_lock(&total_read_bmeta_num_lock);
    total_read_bmeta_num ++;
    pthread_mutex_unlock(&total_read_bmeta_num_lock);

    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    if(osd_id < 0){
        SKYFS_ERROR("__SKYFS_MS_read_bmeta:get osd_id failed\n");
        rc = osd_id;
        goto ERR;
    }

    vector.bmeta = bmeta;
    vector.dir_id = dir_id;
    vector.subset_id = subset_id;
    vector.bmeta_id = bmeta_id;
    vector.size = sizeof(skyfs_M_bmeta_t);

    rc = __skyfs_M2O_read_bmeta(osd_id, &vector);
    if(rc < 0){
        SKYFS_ERROR("__SKYFS_MS_read_bmeta:read bmeta failed\n");
        goto ERR;
    }
	// added by mayl
	if(bmeta->nfree == -ENOENT && bmeta->firstfree == -ENOENT){
		SKYFS_ERROR("__SKYFS_MS_read_bmeta:read bmeta failed, empty bmeta\n");
		rc = -ENOENT;
        goto ERR;
	}
    
    pthread_mutex_init(&bmeta->lock, NULL);
    pthread_rwlock_init(&bmeta->rwlock, NULL);
    INIT_LIST_HEAD(&bmeta->bmeta_hash);
    INIT_LIST_HEAD(&bmeta->bmeta_list);

     SKYFS_ERROR("__SKYFS_MS_read_bmeta:bmeta id:%d, first_time %lu.%lu, lasttime %lu.%lu, crash_recover %d, stop_fs %d\n", 
			bmeta->box_id, bmeta->first_time.tv_sec,  bmeta->first_time.tv_usec,
			 bmeta->last_time.tv_sec,  bmeta->last_time.tv_usec, crash_recover, stop_fs);
    for(i = 0; i < SKYFS_MAX_META_PER_BOX; i ++){
        mmeta = &bmeta->mmetap[i];
        cmeta = &bmeta->cmetap[i];
        mmeta->cmetap = cmeta;
        mmeta->id = i;
		mmeta->status = 0;
        bzero(mmeta->open_clt, sizeof(skyfs_u8_t) * SKYFS_NODE_BM_LEN);
        pthread_mutex_init(&mmeta->lock, NULL);
		// added by mayl for flock
		INIT_LIST_HEAD(&mmeta->flock_head);
		INIT_LIST_HEAD(&mmeta->posix_lock_head);
		/* check if need to recover lock_htb_head */
		if(crash_recover){
			/* do not recover the lock list if fs is waked from crash */
			bmeta->last_time = bmeta->first_time;
			mmeta->lock_htb_head = NULL;
		}
		if(bmeta->last_time.tv_sec == bmeta->first_time.tv_sec 
			&& bmeta->last_time.tv_usec == bmeta->first_time.tv_usec || cmeta->type == SKYFS_LINK){
			mmeta->lock_htb_head = NULL;
		}else{
			recover_cnt++;
			if(mmeta->lock_htb_head != NULL){
				do_recover_cnt++;
				/* do recover flock_head and posix_lock_head read bmeta after split or enlarge  */
				SKYFS_ERROR("start recover inode lock lists for ino %lu, htb_head %p\n", cmeta->ino, mmeta->lock_htb_head);
				__skyfs_recover_inode_lock_lists(cmeta->ino, mmeta);
			}
		}

    }

ERR:

    SKYFS_ERROR("__skyfs_MS_read_bmeta:exit.bmeta.nfree:%d,firstfree:%d, recover_cnt %d ,do_recover %d\n", 
        bmeta->nfree, bmeta->firstfree, recover_cnt, do_recover_cnt);
    return rc;
}

skyfs_M_subset_cache_t *
__skyfs_MS_alloc_subset(void)
{
    skyfs_M_subset_cache_t *subset_cache = NULL;

    SKYFS_ENTER("__skyfs_MS_alloc_subset:enter\n");
    pthread_mutex_lock(&total_subset_cache_num_lock);
    if(total_subset_cache_num > SKYFS_MAX_SUBSET_CACHE_NUM/2){
        total_subset_cache_num --;
        subset_cache = (skyfs_M_subset_cache_t *)malloc(sizeof(skyfs_M_subset_cache_t));
    }else{
        /*Free some subset_cache to get enough space*/
        SKYFS_ERROR("__skyfs_MS_alloc_subset:no enough space for subset:%d\n", 
            total_subset_cache_num);
        total_subset_cache_num --;
        subset_cache = (skyfs_M_subset_cache_t *)malloc(sizeof(skyfs_M_subset_cache_t));
    }
    pthread_mutex_unlock(&total_subset_cache_num_lock);

    SKYFS_LEAVE("__skyfs_MS_alloc_subset:exit\n");
    return subset_cache;
}

skyfs_s32_t
__skyfs_MS_release_subset(skyfs_M_subset_cache_t *subset_cache)
{
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_release_subset:enter\n");

    pthread_mutex_lock(&total_subset_cache_num_lock);
    total_subset_cache_num ++;
    pthread_mutex_unlock(&total_subset_cache_num_lock);

    SKYFS_LEAVE("__skyfs_MS_release_subset:exit.subset_cache:%p\n", subset_cache);

    return rc;
}

skyfs_s32_t
__skyfs_MS_free_subset(skyfs_M_subset_cache_t *subset_cache)
{
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_free_subset:enter\n");

    free(subset_cache->bmeta_hash_base);

    list_del_init(&(subset_cache->subset_hash));

    rc = __skyfs_MS_release_subset(subset_cache);

    SKYFS_LEAVE("__skyfs_MS_free_subset:exit\n");

    return rc;
}

skyfs_s32_t
__skyfs_recover_inode_lock_lists(skyfs_u64_t ino, skyfs_M_mmeta_t *mmeta)
{

	skyfs_htb_t  * htbp;
	int ret = 0;
	struct list_head * posix_lock_head , * flock_head;
	skyfs_M_inode_lock_t * node = NULL;
	htbp = &skyfs_flock_cache_htbbase[ino % SKYFS_LOCK_HASH_LEN];
    	pthread_mutex_lock(&(htbp->lock));
	node = list_entry(mmeta->lock_htb_head, skyfs_M_inode_lock_t, hash_tab_head);
	if(node->ino != ino){
		SKYFS_ERROR("recover inode lock cache error, head %p ,node  %p ino %lu: %lu  not match \n", 
				mmeta->lock_htb_head, node , node->ino, ino );
		ret = -EINVAL;
		goto ERR_OUT;
	}
	posix_lock_head = node->posix_lock_head;
	flock_head = node->flock_head;

	// replace the lock_list's head is enough?
	memcpy(&mmeta->posix_lock_head, posix_lock_head, sizeof(struct list_head) );
	SKYFS_ERROR("try to recover posix_lock_head %p %p \n ", mmeta, &mmeta->posix_lock_head);
	if(!list_empty(&mmeta->posix_lock_head)){

		SKYFS_ERROR("try to Adjust posix_lock_head %p %p \n ", mmeta, &mmeta->posix_lock_head);
	 	/* reset next->prev and prev->next*/
		struct list_head * posix_next = ((struct list_head *)(&mmeta->posix_lock_head))->next;
		struct list_head * posix_prev = ((struct list_head *)(&mmeta->posix_lock_head))->prev;
		SKYFS_ERROR(" posix_lock_head prev %p next %p \n ", posix_prev, posix_next);


		posix_next->prev = &mmeta->posix_lock_head;
		posix_prev->next = &mmeta->posix_lock_head;
		SKYFS_ERROR("adjust posix lock  next prev success\n ");
	}
	memcpy(&mmeta->flock_head, flock_head, sizeof( struct list_head) );
	SKYFS_ERROR("try to recover flock_head %p %p \n ", mmeta, &mmeta->flock_head);
	if(!list_empty(&mmeta->flock_head)){
	 	/* reset next->prev and prev->next*/
		SKYFS_ERROR("try to Adjust flock_head %p %p \n ", mmeta, &mmeta->flock_head);
		struct list_head * flock_next = ((struct list_head *)(&mmeta->flock_head))->next;
		struct list_head * flock_prev = ((struct list_head *)(&mmeta->flock_head))->prev;
		SKYFS_ERROR(" flock_head prev %p next %p \n ", flock_prev, flock_next);
		flock_next->prev = &mmeta->flock_head;
		flock_prev->next = &mmeta->flock_head;
		SKYFS_ERROR("adjust flock next prev success\n ");
	}

	// set the new posix_lock_head  and flock_head ptr to the node_lock_htb
	node->posix_lock_head = &mmeta->posix_lock_head;
	node->flock_head = &mmeta->flock_head;
	

ERR_OUT:
    pthread_mutex_unlock(&(htbp->lock));
return ret;
	// TODO : complete this funciton!
	return 0;
}

/* added by mayl for flock */ 
skyfs_s32_t
__skyfs_add_inode_lock_cache(skyfs_u64_t ino, struct list_head * flock_head,struct list_head * posix_lock_head, skyfs_M_mmeta_t * mmeta)
{
	
	skyfs_htb_t  * htbp;
	htbp = &skyfs_flock_cache_htbbase[ino % SKYFS_LOCK_HASH_LEN];
	skyfs_M_inode_lock_t * node = (skyfs_M_inode_lock_t*)calloc(1,sizeof (skyfs_M_inode_lock_t));
	if (!node){
		SKYFS_ERROR("alloc skyfs_M_inode_lock failed !\n");
		return -ENOMEM;
	}
	node->ino = ino;
	node->flock_head = flock_head;
	node->posix_lock_head = posix_lock_head;
	INIT_LIST_HEAD(&node->hash_tab_head);
    	pthread_mutex_lock(&(htbp->lock));
	/* add node to the tail of flock list */ 
	list_add_tail(&node->hash_tab_head, &htbp->head);
	mmeta->lock_htb_head = &node->hash_tab_head; // record the list_head for quick removing  
    	pthread_mutex_unlock(&(htbp->lock));
	SKYFS_ERROR("add inode lock cache node %p, lock_htb_head %p,, ino %lu\n ", node, mmeta->lock_htb_head, ino);
	return 0;
	
}

/* added by mayl for flock */ 
skyfs_s32_t
__skyfs_remove_inode_lock_cache(skyfs_u64_t ino, struct list_head * node_lock_head, skyfs_M_mmeta_t * mmeta)
{
	
	skyfs_s32_t ret = 0; 
	skyfs_htb_t  * htbp;
	skyfs_M_inode_lock_t * node = NULL;
	SKYFS_ERROR("skyfs remove_inode_lock_cache , ino %lu\n", ino);
	htbp = &skyfs_flock_cache_htbbase[ino % SKYFS_LOCK_HASH_LEN];
	if(mmeta->lock_htb_head == NULL){
		SKYFS_ERROR("skyfs_M_inode_lock failed , mmeta node_lock_head is NULL! \n");
		return -ENOENT;
	}
	
    	pthread_mutex_lock(&(htbp->lock));
	if (node_lock_head == NULL){
		SKYFS_ERROR("skyfs_remove_inode_lock_cache failed , node_lock_head is NULL! \n");
		ret =  -ENOENT;
		goto ERR_OUT;
	}
	node = list_entry(node_lock_head, skyfs_M_inode_lock_t, hash_tab_head);
	if(node->ino != ino){
		SKYFS_ERROR("remove inode lock cache error, node ino %x: %x  not match \n", node->ino, ino );
		ret = -EINVAL;
		goto ERR_OUT;
	}
	/* delete the lock node */
	list_del_init(node_lock_head);
	free(node);
	mmeta->lock_htb_head = NULL;
ERR_OUT:
    pthread_mutex_unlock(&(htbp->lock));
	
	return ret;
	
} 
 

skyfs_M_subset_cache_t *
__skyfs_MS_find_subset(skyfs_htb_t *htbp,
                skyfs_u32_t dir_id, 
                skyfs_u32_t    subset_id)
{
    skyfs_M_subset_cache_t *subset_cache = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_find_subset:enter.dir_id:%d,subset_id:%d\n",
        dir_id, subset_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_find_subset:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_subset_cache_t, subset_hash);
        if(tmp->dir_id == dir_id && tmp->subset_id == subset_id){
            SKYFS_MSG("__skyfs_MS_find_subset:find the subset\n");
            subset_cache = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_MS_find_subset:leave.\n");
    return subset_cache;
}

skyfs_s32_t 
__skyfs_MS_do_create_subindex(skyfs_u32_t dir_id,
                skyfs_u32_t subset_id,
                skyfs_u32_t subset_depth,
                skyfs_u32_t nlink)
{
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_M_dir_cache_t    *dir_cache = NULL;
    skyfs_htb_t *dir_htbp = NULL;
    skyfs_htb_t *subset_index_htbp = NULL;
    skyfs_u32_t hashvalue;
    skyfs_s32_t rc = 0;

    /*1.get the dir cache*/
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
    dir_htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
    pthread_mutex_lock(&dir_htbp->lock);
    dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
    if(dir_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_do_create_subindex:can't get dir_cache.hash:%d\n",
            hashvalue);
        pthread_mutex_unlock(&dir_htbp->lock);
        goto ERR_NONE;
    }

    pthread_mutex_lock(&dir_cache->lock);
    pthread_mutex_unlock(&dir_htbp->lock);

    /*2.create related subset_index at dir_cache side*/
    subset_index = __skyfs_MS_alloc_subset_index();
    if(subset_index == NULL){
        SKYFS_ERROR("__skyfs_MS_do_create_subindex:new subset_index:NULL\n");
        goto ERR;
    }

    subset_index->dir_id = dir_id;
    subset_index->subset_id = subset_id;
    subset_index->subset_depth = subset_depth;
    subset_index->nlink_orign = nlink;
    subset_index->nlink_update = 0;
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
    subset_index_htbp = &(dir_cache->subset_hash_base[hashvalue]);
    list_add(&(subset_index->subset_hash), &(subset_index_htbp->head));
    pthread_mutex_init(&(subset_index->lock), NULL);
    pthread_rwlock_init(&(subset_index->rwlock), NULL);
        
ERR:
    pthread_mutex_unlock(&dir_cache->lock);
ERR_NONE:

    return rc;
}

/*Find the subset, if not exist, read it from osd,
 * init it and add the subset_index to the dir which
 * it is belong to*/
skyfs_M_subset_cache_t *
__skyfs_MS_get_subset(skyfs_htb_t *htbp,
                skyfs_u32_t dir_id,
                skyfs_u32_t subset_id)
{
    skyfs_M_subset_cache_t *subset_cache = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_htb_t *dir_htbp = NULL;
    skyfs_htb_t *subset_index_htbp = NULL;
    skyfs_u32_t hashvalue;
    skyfs_u32_t mds_id;
    skyfs_s32_t rc = 0;
    int renamed_sb = 0;
    
    SKYFS_ENTER("__skyfs_MS_get_subset:enter.dir_id:%d,subset_id:%d\n", 
        dir_id, subset_id);


    if(subset_id >= SKYFS_MAX_SUBSET_PER_DIR){
	    renamed_sb ++ ;
	    SKYFS_ERROR("__skyfs_MS_get_subset:enter.dir_id:%d,subset_id:%d\n", 
        	dir_id, subset_id);

    }

    pthread_mutex_lock(&(htbp->lock));
    if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    }
    
    subset_cache = __skyfs_MS_find_subset(htbp, dir_id, subset_id);
    if(subset_cache != NULL){
        SKYFS_MSG("__skyfs_MS_get_subset:get the right subset\n");
        goto find_subset;
    }else{
        subset_cache = __skyfs_MS_alloc_subset();
        if(subset_cache == NULL){
            SKYFS_ERROR("__skyfs_MS_get_subset:new alloc subset:NULL\n");
            goto ERR;
        }

    if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    }
        rc = __skyfs_MS_read_subset(subset_cache, dir_id, subset_id);
        if(rc < 0){
	    if(renamed_sb)
            	SKYFS_ERROR_1("__skyfs_MS_get_subset:read subset_head failed\n");
            rc = __skyfs_MS_release_subset(subset_cache);
			free(subset_cache);
            subset_cache = NULL;
            goto ERR;
        }
	
    if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    }
        mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);
        if(mds_id == mds_this_id){
            /*1.get the dir cache*/
            hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
            hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
            dir_htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
            pthread_mutex_lock(&dir_htbp->lock);
            dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
            if(dir_cache == NULL){
                SKYFS_ERROR("__skyfs_MS_get_subset:can't get dir_cache.hash:%d\n",
                    hashvalue);
                rc = __skyfs_MS_release_subset(subset_cache);
			    free(subset_cache);
                subset_cache = NULL;
                pthread_mutex_unlock(&dir_htbp->lock);
                goto ERR;
            }

            pthread_mutex_lock(&dir_cache->lock);
            pthread_mutex_unlock(&dir_htbp->lock);


    	if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    	}

            /*2.create related subset_index at dir_cache side*/
            subset_index = __skyfs_MS_alloc_subset_index();
            if(subset_index == NULL){
                SKYFS_ERROR("__skyfs_MS_get_subset:new alloc subset_index:NULL\n");
                rc = __skyfs_MS_release_subset(subset_cache);
			    free(subset_cache);
                subset_cache = NULL;
                pthread_mutex_unlock(&dir_cache->lock);
                goto ERR;
            }
            subset_index->dir_id = dir_id;
            subset_index->subset_id = subset_id;
            subset_index->subset_depth = subset_cache->subset_depth;
            subset_index->nlink_orign = subset_cache->nlink_orign;
            subset_index->nlink_update = 0;
            hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
            hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
            subset_index_htbp = &(dir_cache->subset_hash_base[hashvalue]);
            list_add(&(subset_index->subset_hash), &(subset_index_htbp->head));
            pthread_mutex_init(&(subset_index->lock), NULL);
            pthread_rwlock_init(&(subset_index->rwlock), NULL);
        
            pthread_mutex_unlock(&dir_cache->lock);

            /*3.init subset*/
            pthread_mutex_init(&(subset_cache->lock), NULL);
            pthread_rwlock_init(&(subset_cache->rwlock), NULL);
            INIT_LIST_HEAD(&(subset_cache->bmeta_head));
            INIT_LIST_HEAD(&(subset_cache->subset_hash));
            list_add(&(subset_cache->subset_hash), &(htbp->head));
            __skyfs_init_htb(SKYFS_BMETA_HASH_LEN , &subset_cache->bmeta_hash_base);
            
        }else{
            /*1.Send req to mds to create subset index*/
            SKYFS_ERROR("__skyfs_MS_get_subset: send to mds %d to create sub index\n",
                mds_id);
            rc = __skyfs_M2M_create_subset_index(mds_id, dir_id, subset_id, 
                    subset_cache->subset_depth, subset_cache->nlink_orign);    
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_get_subset:create sub_index failed mds %d\n",
                    mds_id);
                goto ERR;
            }

            /*2.Init subset*/
            pthread_mutex_init(&(subset_cache->lock), NULL);
            pthread_rwlock_init(&(subset_cache->rwlock), NULL);
            INIT_LIST_HEAD(&(subset_cache->bmeta_head));
            INIT_LIST_HEAD(&(subset_cache->subset_hash));
            list_add(&(subset_cache->subset_hash), &(htbp->head));
            __skyfs_init_htb(SKYFS_BMETA_HASH_LEN , &subset_cache->bmeta_hash_base);
    
        }

    	if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    	}
    }

find_subset:
    if(renamed_sb)
	    renamed_sb = 100;

    if(renamed_sb){
	    renamed_sb ++;
	    SKYFS_ERROR("get subset %d\n", renamed_sb);
    }
    
ERR:
    
    pthread_mutex_unlock(&(htbp->lock));

    if(subset_cache){
        rc = __skyfs_MS_move_wb_entry(dir_id, subset_id);
    }

    SKYFS_LEAVE("__skyfs_MS_get_subset:exit\n");
    return subset_cache;
}

skyfs_s32_t
__skyfs_MS_read_subset(skyfs_M_subset_cache_t *subset_cache,
                skyfs_u32_t dir_id,
                skyfs_u32_t subset_id)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t osd_id;
    skyfs_M_subset_head_t subset_head;

    SKYFS_ENTER("__skyfs_MS_read_subset:enter.dir_id:%d,subset_id:%d\n", 
        dir_id, subset_id);

    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    if(osd_id < 0){
        SKYFS_ERROR_1("__SKYFS_MS_read_subset:get osd_id failed\n");
        rc = osd_id;
        goto err_out;
    }

    bzero(&subset_head, sizeof(skyfs_M_subset_head_t));
    subset_head.dir_id = dir_id;
    subset_head.subset_id = subset_id;

    rc = __skyfs_M2O_read_subset(osd_id, &subset_head);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_MS_read_subset:read subset failed\n");
    //    exit(1);
        goto err_out;
    }

    subset_cache->dir_id = dir_id;
    subset_cache->subset_id = subset_id;
    subset_cache->split_depth = subset_head.split_depth;
    subset_cache->subset_depth = subset_head.subset_depth;
    subset_cache->nlink_orign = subset_cache->nlink_update = subset_head.nlink;    

    SKYFS_ERROR("__skyfs_MS_read_subset:subset_id %d , sp_depth:%d,sb_depth:%d\n",
        subset_id, subset_cache->split_depth, subset_cache->subset_depth);

err_out:
    SKYFS_LEAVE("__skyfs_MS_read_subset:exit.\n");
    return rc;
}

skyfs_s32_t
__skyfs_MS_write_subset(skyfs_M_subset_cache_t *subset_cache)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id;
    skyfs_u32_t osd_id;
    skyfs_M_subset_head_t subset_head;

    dir_id = subset_cache->dir_id;
    subset_id = subset_cache->subset_id;

    SKYFS_ENTER("__skyfs_MS_write_subset:enter.dir_id:%d,subset_id:%d\n", 
        dir_id, subset_id);

    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    if(osd_id < 0){
        SKYFS_ERROR("__SKYFS_MS_write_subset:get osd_id failed\n");
        rc = osd_id;
        goto err_out;
    }

    bzero(&subset_head, sizeof(skyfs_M_subset_head_t));
    subset_head.dir_id = dir_id;
    subset_head.subset_id = subset_id;
    subset_head.split_depth = subset_cache->split_depth;
    subset_head.subset_depth = subset_cache->subset_depth;
    subset_head.nlink = subset_cache->nlink_orign;

    rc = __skyfs_M2O_write_subset(osd_id, subset_head);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_write_subset:write subset failed\n");
        exit(1);
        goto err_out;
    }

    SKYFS_MSG("__skyfs_MS_write_subset:sp_depth:%d,sb_depth:%d\n",
        subset_cache->split_depth, subset_cache->subset_depth);

err_out:
    SKYFS_LEAVE("__skyfs_MS_write_subset:exit.\n");
    return rc;
}

skyfs_M_subset_index_t *
__skyfs_MS_alloc_subset_index(void)
{
    skyfs_M_subset_index_t *subset_index = NULL;
    
    SKYFS_ENTER("__skyfs_MS_alloc_subset_index:enter\n");

    pthread_mutex_lock(&total_subset_index_num_lock);
    if(total_subset_index_num > SKYFS_MAX_SUBSET_INDEX_NUM/2){
        total_subset_index_num --;
        subset_index = (skyfs_M_subset_index_t *)malloc(sizeof(skyfs_M_subset_index_t));
    }else{
        /*Free some subset_index to get enough space*/
        SKYFS_ERROR("__skyfs_MS_alloc_subset_index:need to reclaim:total:%d\n\n",
            total_subset_index_num);
        subset_index = (skyfs_M_subset_index_t *)malloc(sizeof(skyfs_M_subset_index_t));
    }
    pthread_mutex_unlock(&total_subset_index_num_lock);

    SKYFS_LEAVE("__skyfs_MS_alloc_subset_index:exit.subset_index:%p\n", subset_index);
    return subset_index;
}

skyfs_s32_t 
__skyfs_MS_release_subset_index(skyfs_M_subset_index_t *subset_index)
{
    skyfs_s32_t rc = 0;

	list_del_init(&(subset_index->subset_hash));


    pthread_mutex_lock(&total_subset_index_num_lock);
    total_subset_index_num ++;
    pthread_mutex_unlock(&total_subset_index_num_lock);

    return rc;
}

skyfs_s32_t
__skyfs_MS_free_subset_index(skyfs_M_subset_index_t *subset_index)
{
    skyfs_s32_t rc = 0;


    return rc;
}

skyfs_M_subset_index_t *
__skyfs_MS_find_subset_index(skyfs_htb_t *htbp,
                skyfs_u32_t dir_id, 
                skyfs_u32_t    subset_id)
{
    skyfs_M_subset_index_t *subset_index = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_find_subset_index:enter.dir_id:%d,subset_id:%d\n",
        dir_id, subset_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_ERROR("__skyfs_MS_find_subset_index:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_subset_index_t, subset_hash);
        if(tmp->dir_id == dir_id && tmp->subset_id == subset_id){
            SKYFS_MSG("__skyfs_MS_find_subset_index:find the subset\n");
            subset_index = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_MS_find_subset_index:leave.\n");
    return subset_index;
}

skyfs_M_dir_depth_t *
__skyfs_MS_alloc_dir_depth(void)
{
    skyfs_M_dir_depth_t *dir_depth = NULL;

    SKYFS_ENTER("__skyfs_MS_alloc_dir_depth:enter\n");
    pthread_mutex_lock(&total_dir_depth_num_lock);
    if(total_dir_depth_num > SKYFS_MAX_DIR_DEPTH_NUM/2){
        total_dir_depth_num --;
        dir_depth = (skyfs_M_dir_depth_t *)malloc(sizeof(skyfs_M_dir_depth_t));
    }else{
        /*Free some dir_depth to get enough space*/
        SKYFS_ERROR("__skyfs_MS_alloc_dir_depth:no enough space for dir_depth:%d\n", 
            total_dir_depth_num);
        total_dir_depth_num --;
        dir_depth = (skyfs_M_dir_depth_t *)malloc(sizeof(skyfs_M_dir_depth_t));
    }
    pthread_mutex_unlock(&total_dir_depth_num_lock);
    SKYFS_LEAVE("__skyfs_MS_alloc_dir_depth:exit\n");

    return dir_depth;
}

skyfs_s32_t
__skyfs_MS_release_dir_depth(skyfs_M_dir_depth_t *dir_depth)
{
    skyfs_s32_t rc = 0;

    free(dir_depth);

    pthread_mutex_lock(&total_dir_depth_num_lock);
    total_dir_depth_num ++;
    pthread_mutex_unlock(&total_dir_depth_num_lock);

    return rc;
}

skyfs_M_dir_depth_t*
__skyfs_MS_find_dir_depth(skyfs_htb_t *htbp,
                skyfs_u32_t dir_id)
{
    skyfs_M_dir_depth_t *dir_depth= NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_find_dir_depth:enter.dir_id:%d\n", dir_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_find_dir_depth:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_dir_depth_t, dir_depth_hash);
        SKYFS_MSG("__skyfs_MS_find_dir_depth:tmp %d\n", tmp->dir_id);
        if(tmp->dir_id == dir_id){
            SKYFS_MSG("__skyfs_MS_find_dir_depth:find the dir depth\n");
            dir_depth = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_MS_find_dir_depth:leave.\n");
    return dir_depth;
}

skyfs_M_dir_depth_t * __skyfs_MS_get_dir_depth(skyfs_u32_t dir_id)
{
    skyfs_u32_t hashvalue;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_dir_depth_t *dir_depth = NULL;
    skyfs_M_dir_cache_t dir_cache;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("__skyfs_MS_get_dir_depth,dir_id:%d\n", dir_id);

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_DEPTH_HASH_LEN;
    htbp = &skyfs_dir_depth_htbbase[hashvalue];
    SKYFS_MSG("__skyfs_MS_get_dir_depth:before get htbp lock:%p\n", htbp);
    pthread_mutex_lock(&htbp->lock);
    SKYFS_MSG("__skyfs_MS_get_dir_depth:after get htbp lock:%p\n", htbp);
    dir_depth = __skyfs_MS_find_dir_depth(htbp, dir_id);
    if(dir_depth == NULL){
        dir_depth = __skyfs_MS_alloc_dir_depth();
        if(dir_depth == NULL){
            SKYFS_ERROR("__skyfs_MS_get_dir_depth:alloc new dir_depth NULL\n");
            goto ERR;
        }

        rc = __skyfs_MS_get_dir_cache(&dir_cache, dir_id);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_get_dir_depth: get dir cache failed\n");
            /*need to release the dir_depth*/
            goto ERR;
        }
        SKYFS_ERROR("__skyfs_MS_get_dir_depth: get dir cache success, dir_id \n",dir_id );
        bzero(dir_depth, sizeof(skyfs_M_dir_depth_t));
        dir_depth->dir_id = dir_id;
        dir_depth->depth = dir_cache.depth;
        dir_depth->ver = mds_layout_version;
        pthread_mutex_init(&dir_depth->lock, NULL);
        memcpy(dir_depth->subset_bm, dir_cache.subset_bm, SKYFS_SUBSET_BM_LEN);
        list_add(&(dir_depth->dir_depth_hash), &(htbp->head));
    }else if(dir_depth->ver < mds_layout_version){
        rc = __skyfs_MS_get_dir_cache(&dir_cache, dir_id);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_get_dir_depth: error get dir cache failed\n");
            //need to release the dir_depth
            goto ERR;
        }
        dir_depth->dir_id = dir_id;
        dir_depth->depth = dir_cache.depth;
        dir_depth->ver = mds_layout_version;
        memcpy(dir_depth->subset_bm, dir_cache.subset_bm, SKYFS_SUBSET_BM_LEN);
        SKYFS_MSG("__skyfs_MS_get_dir_depth:fresh dirdepth,depthver:%d,ver:%d",
            dir_depth->ver, mds_layout_version);
    }

    SKYFS_MSG("__skyfs_MS_get_dir_depth:before get depth lock:%p\n", dir_depth);
    pthread_mutex_lock(&dir_depth->lock);
    pthread_mutex_unlock(&htbp->lock);

    SKYFS_LEAVE("__skyfs_MS_get_dir_depth:exit:%p\n", dir_depth);
    return dir_depth;

ERR:

    pthread_mutex_unlock(&htbp->lock);

    return NULL;
}

skyfs_s32_t
__skyfs_MS_do_update_ddepth(skyfs_u32_t dir_id,
                skyfs_u32_t subset_id,
                skyfs_u32_t split_depth)
{
    skyfs_u32_t hashvalue;
    skyfs_u32_t dcache_hash;
    skyfs_htb_t *dir_htbp = NULL;
    skyfs_htb_t *sub_index_htbp = NULL;
    skyfs_M_dir_depth_t *dir_depth = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_u32_t new_subset_id;
    skyfs_u32_t mds_id;
    skyfs_s32_t rc = 0;

    /*1. update dir depth*/
    new_subset_id = ((skyfs_u32_t)1 << (split_depth - 1)) + subset_id;

    dir_depth = __skyfs_MS_get_dir_depth(dir_id);
    if(dir_depth == NULL){
        SKYFS_ERROR("__skyfs_MS_do_update_dir_depth:can't get dir_depth:%d\n", dir_id);
        goto ERR;
    }

    rc = __skyfs_MS_set_bit(dir_depth->subset_bm, new_subset_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_do_update_dir_depth:set new_subset_id failed:%d\n", 
            new_subset_id);
        pthread_mutex_unlock(&dir_depth->lock);
        goto ERR;
    }
    
    if(dir_depth->depth < split_depth){
        dir_depth->depth = split_depth;
    }

    pthread_mutex_unlock(&dir_depth->lock);

    /*2. update dir cache if dir caceh in this mds*/
    mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);
    if(mds_id != mds_this_id){
        SKYFS_MSG("__skyfs_MS_do_update_dir_depth,mds_id:%d,mds_this_id:%d, jump\n",
            mds_id, mds_this_id);
        goto OUT;
    }

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);
    dcache_hash = hashvalue % SKYFS_DIR_HASH_LEN;
    dir_htbp = &(skyfs_dir_cache_htbbase[dcache_hash]);
    pthread_mutex_lock(&dir_htbp->lock);
    dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
    if(dir_cache != NULL){
        pthread_mutex_lock(&dir_cache->lock);
        hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
        hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
        sub_index_htbp = &(dir_cache->subset_hash_base[hashvalue]);

        subset_index = __skyfs_MS_find_subset_index(sub_index_htbp, 
            dir_id, subset_id);
        if(subset_index == NULL){
            SKYFS_ERROR("__skyfs_MS_do_update_ddepth: subset_index not exist\n");
            rc = -EEXIST;
            pthread_mutex_unlock(&dir_cache->lock);
            goto ERR;
        }
        subset_index->split_depth = split_depth;

        if(dir_cache->depth < split_depth){
            dir_cache->depth = split_depth;
        }
        rc = __skyfs_MS_set_bit(dir_cache->subset_bm, new_subset_id, 1);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_update_dir_depth:set new sub_id failed:%d\n", 
                new_subset_id);
            pthread_mutex_unlock(&dir_cache->lock);
            goto ERR;
        }

        pthread_mutex_unlock(&dir_cache->lock);
    }else{
        SKYFS_ERROR("__skyfs_MS_do_update_ddepth:can not get dir cache.%d\n",dir_id);
        //exit(1);
    }

ERR:
    if(dir_htbp){
        pthread_mutex_unlock(&dir_htbp->lock);
    }
OUT:
    return rc;
}

skyfs_s32_t
__skyfs_MS_update_dir_depth(skyfs_u32_t dir_id, 
                skyfs_u32_t subset_id, 
                skyfs_u32_t split_depth)
{
    skyfs_u32_t hashvalue;
    skyfs_htb_t *dir_depth_htbp = NULL;
    skyfs_htb_t *dir_htbp = NULL;
    skyfs_htb_t *sub_index_htbp = NULL;
    skyfs_M_dir_depth_t *dir_depth = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_u32_t mds_id;
    skyfs_u32_t new_mds_id;
    skyfs_u32_t new_subset_id;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("__skyfs_MS_update_dir_depth:in.dir_id:%u,subset_id:%d,split_depth:%d\n",
        dir_id, subset_id, split_depth);

    new_subset_id = ((skyfs_u32_t)1 << (split_depth - 1)) + subset_id;

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_DEPTH_HASH_LEN;
    dir_depth_htbp = &skyfs_dir_depth_htbbase[hashvalue];
    pthread_mutex_lock(&dir_depth_htbp->lock);
    dir_depth = __skyfs_MS_find_dir_depth(dir_depth_htbp, dir_id);
    if(dir_depth == NULL){
        SKYFS_ERROR("__skyfs_MS_update_dir_depth:can't find dir_depth:%d\n", dir_id);
        pthread_mutex_unlock(&dir_depth_htbp->lock);
        goto ERR;
    }

    pthread_mutex_lock(&dir_depth->lock);
    pthread_mutex_unlock(&dir_depth_htbp->lock);

    rc = __skyfs_MS_set_bit(dir_depth->subset_bm, new_subset_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_update_dir_depth:set new_subset_id failed:%d\n", 
            new_subset_id);
        pthread_mutex_unlock(&dir_depth->lock);
        goto ERR;
    }
    
    if(dir_depth->depth < split_depth){
        dir_depth->depth = split_depth;
    }

    pthread_mutex_unlock(&dir_depth->lock);

    mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);
    if(mds_id == mds_this_id){
        hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
        hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
        dir_htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
        pthread_mutex_lock(&dir_htbp->lock);
        dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
        if(dir_cache != NULL){
            pthread_mutex_lock(&dir_cache->lock);
            hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
            hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
            sub_index_htbp = &(dir_cache->subset_hash_base[hashvalue]);

            subset_index = __skyfs_MS_find_subset_index(sub_index_htbp, 
                dir_id, subset_id);
            if(subset_index == NULL){
                SKYFS_ERROR("__skyfs_MS_update_ddepth:sub_index not exist\n");
                rc = -ENOENT;
                pthread_mutex_unlock(&dir_cache->lock);
                pthread_mutex_unlock(&dir_htbp->lock);
                goto ERR;
            }

            subset_index->split_depth = split_depth;

            if(dir_cache->depth < split_depth){
                dir_cache->depth = split_depth;
		/* mayl : update local dir cmeta */
		SKYFS_ERROR("update dir depth and cmeta, dir_id %d depth %d\n", dir_cache->dir_id,dir_cache->depth);
		dir_cache->cmeta.depth = dir_cache->depth;
		if(dir_cache->dir_id == 1){
			/* root dir depth updated */
			SKYFS_ERROR("update dir depth for root dir, depth %d\n", dir_cache->depth);
			root_cmeta.depth = dir_cache->depth;

		}
            }
            rc = __skyfs_MS_set_bit(dir_cache->subset_bm, new_subset_id, 1);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_update_dir_depth:set  new_subset_id failed:%d\n", 
                    new_subset_id);
                pthread_mutex_unlock(&dir_cache->lock);
                pthread_mutex_unlock(&dir_htbp->lock);
                goto ERR;
            }

            pthread_mutex_unlock(&dir_cache->lock);
        }else{
            SKYFS_ERROR("__skyfs_MS_update_dir_depth:can not get dir cache\n");
        }

        pthread_mutex_unlock(&dir_htbp->lock);
    }else{
        /*Send req to the dir, update it's nlink and get the dir_cmeta back*/
        SKYFS_ERROR("__skyfs_MS_update_dir_depth:send to mds %d\n", mds_id);
        rc = __skyfs_M2M_update_dir_depth(mds_id, dir_id, subset_id, split_depth);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_update_dir_depth:update dcache failed mds %d\n",
                mds_id);
            goto ERR;
        }
    }

    new_mds_id = __skyfs_MS_judge_mdsid(dir_id, new_subset_id);
    if(new_mds_id != mds_this_id && new_mds_id != mds_id){
        SKYFS_ERROR("__skyfs_MS_update_dir_depth:init new,send to mds %d\n", 
            new_mds_id);
        rc = __skyfs_M2M_update_dir_depth(new_mds_id, dir_id, subset_id, split_depth);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_update_dir_depth:update dcache failed mds %d\n",
                new_mds_id);
            goto ERR;
        }
    }

ERR:

    SKYFS_LEAVE("__skyfs_MS_update_dir_depth:exit,rc:%d,subset_id:%d,nsubset_id:%d\n", 
        rc, subset_id, new_subset_id);

    return rc;
}

skyfs_M_dir_cache_t *
__skyfs_MS_alloc_dir_cache(skyfs_u32_t dir_id)
{
    skyfs_M_dir_cache_t *dir_cache = NULL;

    pthread_mutex_lock(&total_dir_cache_num_lock);
    if(total_dir_cache_num > SKYFS_MAX_DIR_CACHE_NUM/2){
        total_dir_cache_num --;
        dir_cache = (skyfs_M_dir_cache_t *)malloc(sizeof(skyfs_M_dir_cache_t));
    }else{
        /*Free some dir_cache to get enough space*/
        SKYFS_ERROR("__skyfs_MS_alloc_dir_cache:not enough:%d\n", 
            total_dir_cache_num);
        total_dir_cache_num --;
        dir_cache = (skyfs_M_dir_cache_t *)malloc(sizeof(skyfs_M_dir_cache_t));
    }
    SKYFS_ERROR("__skyfs_MS_alloc_dir_cache:dir_cache used:%d,size:%lu\n", 
        SKYFS_MAX_DIR_CACHE_NUM - total_dir_cache_num, sizeof(skyfs_M_dir_cache_t));

    pthread_mutex_unlock(&total_dir_cache_num_lock);

    return dir_cache;

}

skyfs_s32_t
__skyfs_MS_release_dir_cache(skyfs_M_dir_cache_t *dir_cache)
{
    skyfs_s32_t rc = 0;

	SKYFS_MSG("__skyfs_MS_release_dir_cache:enter:\n");

	list_del_init(&dir_cache->dir_hash);
	list_del_init(&dir_cache->dir_head);
	free(dir_cache->subset_hash_base);

    pthread_mutex_lock(&total_dir_cache_num_lock);
    total_dir_cache_num ++;
    pthread_mutex_unlock(&total_dir_cache_num_lock);

    return rc;
}

skyfs_M_dir_cache_t *
__skyfs_MS_find_dir_cache(skyfs_htb_t *htbp,
                skyfs_u32_t dir_id) 
{
    skyfs_M_dir_cache_t *dir_cache = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_find_dir_cache:enter.dir_id:%u\n", dir_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_ERROR("__skyfs_MS_find_dir_cache:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_dir_cache_t, dir_hash);
        SKYFS_MSG("__skyfs_MS_find_dir_cache:tmp dir_id:%d\n", tmp->dir_id);
        if(tmp->dir_id == dir_id){
            SKYFS_MSG("__skyfs_MS_find_dir_cache:find the dir_cache\n");
            dir_cache = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_MS_find_dir_cache:leave.\n");
    return dir_cache;
}

skyfs_s32_t
__skyfs_MS_do_init_dcache(skyfs_u32_t dir_id, skyfs_M_cmeta_t *cmeta, skyfs_u32_t needlock)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_htb_t *htbp = NULL;    
    skyfs_s32_t rc = 0;
    skyfs_s32_t fd = 0;
    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];
     skyfs_M_cmeta_t cmeta_t;

    SKYFS_ENTER("__skyfs_MS_do_init_dcache:init %d\n", dir_id);

    dir_cache = __skyfs_MS_alloc_dir_cache(dir_id);
    if(dir_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_do_init_dcache:new alloc dir_cache NULL\n");
        rc = -ENOMEM;
        goto ERR;
    }

    bzero(string, SKYFS_MAX_NAME_LEN);
    sprintf(string , "%s/dcache-%u", SKYFS_LOCAL_META_PATH,dir_id);
    if((fd = open(string, O_RDONLY)) > 0){

	rc = read(fd, &cmeta_t, sizeof(skyfs_M_cmeta_t));
	if(rc > 0 && cmeta_t.depth >= cmeta->depth && cmeta->ino == cmeta_t.ino ){
		/* mayl read persis c,eta file */
		SKYFS_ERROR("copy persist cmeta from local file, ino %d\n", cmeta->ino);
		memcpy(cmeta, &cmeta_t,sizeof(skyfs_M_cmeta_t));
		
	}
	close(fd);
	rc = 0;
    }

    bzero(dir_cache, sizeof(skyfs_M_dir_cache_t));
    memcpy(&(dir_cache->cmeta), cmeta, sizeof(skyfs_M_cmeta_t));
    dir_cache->dir_id = dir_id;
    dir_cache->nlink_orign = cmeta->nlink;
    dir_cache->nlink_update = cmeta->nlink;
    pthread_mutex_init(&(dir_cache->lock), NULL);
    pthread_rwlock_init(&(dir_cache->rwlock), NULL);
    INIT_LIST_HEAD(&(dir_cache->subset_head));
    INIT_LIST_HEAD(&(dir_cache->dir_hash));
    INIT_LIST_HEAD(&(dir_cache->dir_head));

    dir_cache->depth = cmeta->depth;
    if(cmeta->depth == 0){
        rc = __skyfs_MS_set_bit(dir_cache->subset_bm, 0, 1);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_do_init_dcache:set dir:%d subset_bm err\n",
                dir_id);
            goto ERR;
        }
    }else{
        /*need to read subset_bm from file*/
        if((fd = open(string, O_RDONLY)) > 0){
			rc = lseek(fd, sizeof(skyfs_M_cmeta_t), SEEK_SET);
            SKYFS_MSG("__skyfs_MS_do_init_dcache:lseek fd.\n");
			if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_do_init_dcache:lseek %s err,%d\n", 
			        string, errno);
                goto ERR;
			}
		    rc = read(fd, dir_cache->subset_bm, 
					sizeof(skyfs_u8_t) * SKYFS_SUBSET_BM_LEN);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_do_init_dcache:read bm %s err,%d\n", 
			        string, errno);
		    }

			close(fd);
	    }else{
		    rc = errno;
    	    SKYFS_ERROR("__skyfs_MS_do_init_dcache:open %s err,%d\n", 
		    	string, errno);
	    }
    }

    __skyfs_init_htb(SKYFS_SUB_INDEX_HASH_LEN , &dir_cache->subset_hash_base);
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
    htbp = &(skyfs_dir_cache_htbbase[hashvalue]);

	rc = __skyfs_MS_do_write_dcache(dir_cache, cmeta);

    SKYFS_ERROR("TRY to lock htbp %p, pthread %lu, needlock %d\n", htbp, pthread_self(), needlock);
    if(needlock)
    	pthread_mutex_lock(&(htbp->lock));
    SKYFS_ERROR(" locked htbp %p, pthread %lu, needlock\n", htbp, pthread_self(), needlock );
    list_add_tail(&(dir_cache->dir_hash), &(htbp->head));
    if(needlock)
    	pthread_mutex_unlock(&(htbp->lock));

ERR:
    SKYFS_LEAVE("__skyfs_MS_do_init_dcache:init %d, rc:%d, exit\n", 
		dir_id, rc);
    return rc;
}

/*Since cmeta is lock, there is only one process to init dir cache,
 * so do not need to search the dir cache befor insert*/
skyfs_s32_t
__skyfs_MS_init_dir_cache(skyfs_M_cmeta_t *cmeta)
{
    skyfs_s32_t rc = 0;
    skyfs_ino_t ino;
    skyfs_u32_t mds_id;
    skyfs_u32_t dir_id;

    ino = cmeta->ino;
    dir_id = __skyfs_MS_get_dir_id(ino, 0);

    SKYFS_ERROR("__skyfs_MS_init_dir_cache:enter:%lld,%d\n",ino,dir_id);
    mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);

    if(mds_id != mds_this_id){
        /*send req to mds to init dir cache*/
        SKYFS_ERROR("__skyfs_MS_init_dir_cache:send mds %d to init dir_cache\n",
            mds_id);
        rc = __skyfs_M2M_init_dir_cache(mds_id, dir_id, cmeta);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_init_dir_cache:init dcache err at mds %d\n",
                mds_id);
            goto ERR;
        }
    }else{
        /*init dir cache in local machine*/
	rc = __skyfs_MS_do_init_dcache(dir_id, cmeta, 1);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_init_dir_cache:init dcache err:%d\n",
                rc);
            goto ERR;
		}
    }

ERR:

    SKYFS_LEAVE("__skyfs_MS_init_dir_cache:exit.rc:%d\n", rc);
    return rc;
}

skyfs_s32_t
__skyfs_MS_do_get_dcache(skyfs_u32_t dir_id, skyfs_M_dir_cache_t *dir_cache)
{
    skyfs_M_dir_cache_t *real_dir_cache = NULL;
    skyfs_u32_t hashvalue;
    skyfs_htb_t *htbp = NULL;
	skyfs_M_cmeta_t cmeta;
    skyfs_s32_t rc = 0;
	skyfs_s32_t fd = 0;
	skyfs_s8_t string[SKYFS_MAX_NAME_LEN];

    SKYFS_DEBUG("__skyfs_MS_do_get_dcache:dir_cache:%p, dir_id  %d , thread_id %llu\n", dir_cache, dir_id, pthread_self());

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
    htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
    SKYFS_DEBUG("get TRY to lock htbp %p, pthread %lu\n", htbp, pthread_self());
    pthread_mutex_lock(&htbp->lock);
    SKYFS_ERROR("get  locked htbp %p, pthread %lu\n", htbp, pthread_self());

    real_dir_cache = __skyfs_MS_find_dir_cache(htbp, dir_id);
    SKYFS_ERROR("__skyfs_MS_do_get_dcache:found dir_cache:%p, dir_id  %d \n", real_dir_cache, dir_id);
    if(real_dir_cache != NULL){
        pthread_mutex_lock(&real_dir_cache->lock);
        memcpy(dir_cache, real_dir_cache, sizeof(skyfs_M_dir_cache_t));
        pthread_mutex_unlock(&real_dir_cache->lock);
    }else{
    	SKYFS_DEBUG("__skyfs_MS_do_get_dcache:dir_cache:%d not in cache.\n", 
		    dir_id);
        sprintf(string , "%s/dcache-%u", SKYFS_LOCAL_META_PATH,dir_id);
	    if((fd = open(string, O_RDONLY)) > 0){
		    rc = read(fd, &cmeta, sizeof(skyfs_M_cmeta_t));
            if(rc > 0){
		SKYFS_DEBUG("read and init dcache ,dir_id %d, cmeta.depth %d\n", dir_id, cmeta.depth);
                rc = __skyfs_MS_do_init_dcache(dir_id, &cmeta, 0);
		    }else{
                SKYFS_ERROR("__skyfs_MS_do_get_dcache:read cmeta %s err,%d\n", 
				string, errno);
	    	}

			close(fd);
	    }else{
		    rc = errno;
    	    SKYFS_ERROR("__skyfs_MS_do_get_dcache:open %s err,%d\n", 
		    	string, errno);
	    }

	}

    pthread_mutex_unlock(&htbp->lock);

    SKYFS_ERROR("__skyfs_MS_do_get_dcache:leave., %p\n", dir_cache);

    return rc;
}

skyfs_s32_t
__skyfs_MS_get_dir_cache(skyfs_M_dir_cache_t *dir_cache, skyfs_u32_t dir_id)
{
    //skyfs_M_dir_cache_t *real_dir_cache = NULL;
    skyfs_u32_t mds_id;
    //skyfs_u32_t hashvalue;
    //skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("__skyfs_MS_get_dir_cache:enter,dir_id:%d, thread_id %llu\n", dir_id, pthread_self());

    mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);

    if(mds_id != mds_this_id){
        /*send req to mds to get dir_cache*/
        SKYFS_ERROR("__skyfs_MS_get_dir_cache:send to mds %d to get dir_cache\n",
            mds_id);
        rc = __skyfs_M2M_get_dir_cache(mds_id, dir_id, dir_cache);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_get_dir_cache:get dcache err at mds %d\n",
                mds_id);
            goto ERR;
        }
    }else{
	rc = __skyfs_MS_do_get_dcache(dir_id, dir_cache);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_get_dir_cache:get dcache err:%d\n",
                rc);
            goto ERR;
        }
    }    

ERR:
    SKYFS_LEAVE("__skyfs_MS_get_dir_cache:exit:%d\n", rc);
    return rc;
}

skyfs_s32_t
__skyfs_MS_do_update_dcache(skyfs_u32_t dir_id,
                skyfs_u32_t subset_id,
                skyfs_s32_t update,
                skyfs_M_cmeta_t *dir_cmeta)
{
    skyfs_u32_t hashvalue;
    skyfs_htb_t *dir_htbp = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_s32_t rc = 0;

    SKYFS_MSG("__skyfs_MS_do_update_dcache:dir_cmeta:%p\n", dir_cmeta);

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
    hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
    dir_htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
    pthread_mutex_lock(&dir_htbp->lock);
    dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
    if(dir_cache != NULL){
        pthread_mutex_lock(&dir_cache->lock);
        hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
        hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
        htbp = &(dir_cache->subset_hash_base[hashvalue]);

        subset_index = __skyfs_MS_find_subset_index(htbp, dir_id, subset_id);
        if(subset_index == NULL){
            SKYFS_ERROR("__skyfs_MS_do_update_dcache: subset_index not exist\n");
            rc = -EEXIST;
            goto ERR;
        }
        subset_index->nlink_update += update;

        memcpy(dir_cmeta, &(dir_cache->cmeta), sizeof(skyfs_M_cmeta_t));

        pthread_mutex_unlock(&dir_cache->lock);
    }else{
        SKYFS_ERROR("__skyfs_MS_do_update_dcache:error can not get dir cache\n");
        //exit(1);
    }

ERR:
    pthread_mutex_unlock(&dir_htbp->lock);


    return rc;
}
/* This funcation just update dir nlink and get dir_cmeta. 
 * As the subset_cache is in the cache, 
 * the dir_cache and subset_index should be
 * in the cache, or error will be returned.
 */
skyfs_s32_t
__skyfs_MS_update_dir_cache(skyfs_M_subset_cache_t *subset_cache,
                skyfs_M_cmeta_t *dir_cmeta,
                skyfs_u32_t flag)
{
    skyfs_s32_t    rc = 0;
    skyfs_s32_t update;
    //skyfs_u32_t mds_id;
    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id;
    //skyfs_u32_t hashvalue;
    //skyfs_htb_t *dir_htbp = NULL;
    //skyfs_htb_t *htbp = NULL;
    //skyfs_M_dir_cache_t *dir_cache = NULL;
    //skyfs_M_subset_index_t *subset_index = NULL;

    SKYFS_ENTER("__skyfs_MS_update_dir_cache:enter.flag:%d\n", flag);
    
    if(flag > 0){
        update = 1;
    }else{
        update = -1;
    }

    dir_id = subset_cache->dir_id;
    subset_id = subset_cache->subset_id;
    subset_cache->nlink_update = subset_cache->nlink_update + update;
    subset_cache->nlink_orign = subset_cache->nlink_orign + update;

#if 0
    mds_id = __skyfs_MS_judge_dir_mdsid(dir_id, 0);
    if(mds_id == mds_this_id){
        hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);    
        hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
        dir_htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
        pthread_mutex_lock(&dir_htbp->lock);
        dir_cache = __skyfs_MS_find_dir_cache(dir_htbp, dir_id);
        if(dir_cache != NULL){
            pthread_mutex_lock(&dir_cache->lock);
            hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
            hashvalue = hashvalue % SKYFS_SUB_INDEX_HASH_LEN;
            htbp = &(dir_cache->subset_hash_base[hashvalue]);

            subset_index = __skyfs_MS_find_subset_index(htbp, dir_id, subset_id);
            if(subset_index == NULL){
                SKYFS_ERROR("__skyfs_MS_update_dir_cache: subset_index not exist\n");
                rc = -1;
                goto ERR;
            }
            subset_index->nlink_update += update;

            memcpy(dir_cmeta, &(dir_cache->cmeta), sizeof(skyfs_M_cmeta_t));

            pthread_mutex_unlock(&dir_cache->lock);
        }else{
            SKYFS_ERROR("__skyfs_MS_update_dir_cache:can not get dir cache\n");
        }

        pthread_mutex_unlock(&dir_htbp->lock);
    }else{
        /*Send req to the dir, update it's nlink and get the dir_cmeta back*/
        SKYFS_MSG("__skyfs_MS_update_dir_cache:send to mds %d\n", mds_id);
        rc = __skyfs_M2M_update_dir_cache(mds_id, dir_id, subset_id, update, dir_cmeta);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_update_dir_cache:update dcache failed at mds %d\n",
                mds_id);
            goto ERR;
        }
        SKYFS_MSG("__skyfs_MS_update_dir_cache:back mds %d,update:%d\n", 
            mds_id, update);
    }
ERR:
#endif

    SKYFS_LEAVE("__skyfs_MS_update_dir_cache:leave.rc:%d\n", rc);
    return rc;
}

skyfs_s32_t
__skyfs_MS_writeback_subset(skyfs_M_subset_cache_t *subset_cache)
{
    skyfs_s32_t      rc = 0;
    skyfs_u32_t      max_bmeta_num = ((skyfs_u32_t)1 << subset_cache->subset_depth);
    struct list_head *index = NULL, *head = NULL;
    skyfs_M_bmeta_t  *bmeta = NULL;
    amp_request_t    *req[max_bmeta_num];
    skyfs_meta_vector_t vector[max_bmeta_num];
    skyfs_u32_t dir_id, subset_id, osd_id;
    skyfs_u32_t i = 0, j = 0;

    SKYFS_ERROR("__skyfs_MS_writeback_subset:enter.dir_id:%d, subset_id:%d\n",
        subset_cache->dir_id, subset_cache->subset_id);

    memset(req, 0, max_bmeta_num * sizeof(amp_request_t *));
    dir_id = subset_cache->dir_id;
    subset_id = subset_cache->subset_id;

    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    if(osd_id < 0){
        SKYFS_ERROR("__SKYFS_MS_writeback_subset:get osd_id failed\n");
        rc = osd_id;
        goto ERR;
    }

    head = &(subset_cache->bmeta_head);
    list_for_each(index, head){
        SKYFS_MSG("__SKYFS_MS_writeback_subset:i:%d\n", i);
        bmeta = list_entry(index, skyfs_M_bmeta_t, bmeta_list);
		// added by mayl to mark if the bmeta has been written back 
        SKYFS_ERROR("__SKYFS_MS_writeback_subset:bmeta id:%d, stop_fs %d\n", bmeta->box_id, stop_fs);
	if(stop_fs){
		bmeta->last_time = bmeta->first_time;
			// set mmeta=>lock_htb_head == NULL here?
	}else{
			/* mayl: remember the last_time , if last_time != first_time , need  try to recover flocks when read_bmeta  */
		gettimeofday(&bmeta->last_time, NULL);
	}

        SKYFS_ERROR("__SKYFS_MS_writeback_subset:bmeta id:%d, first_time %lu.%lu, lasttime %lu.%lu\n", 
			bmeta->box_id, bmeta->first_time.tv_sec,  bmeta->first_time.tv_usec,
			 bmeta->last_time.tv_sec,  bmeta->last_time.tv_usec);
        vector[i].dir_id = dir_id;
        vector[i].subset_id = subset_id;
        vector[i].bmeta_id = bmeta->box_id;
        vector[i].size = sizeof(skyfs_M_bmeta_t);
        SKYFS_MSG("__skyfs_MS_writeback_subset:bmeta:%p\n", bmeta);
        vector[i].bmeta = bmeta;
        req[i] = __skyfs_M2O_write_bmeta(osd_id, &vector[i]);
        SKYFS_MSG("__skyfs_MS_writeback_subset:req:%p\n", req[i]);
        i ++;
    }

    SKYFS_MSG("__skyfs_MS_writeback_subset:begin to down\n");
    for(j = 0; j < i; j ++){
        amp_sem_down(&(req[j]->req_waitsem));
    }
    
    rc = i;

ERR:
    for(j = 0; j < i; j ++){
        if(req[j]->req_msg){
            amp_free(req[j]->req_msg, req[j]->req_msglen);
        }

        if(req[j]->req_reply){
            amp_free(req[j]->req_msg, req[j]->req_msglen);
        }

        if(req[j]->req_iov){
            amp_free(req[j]->req_iov, sizeof(amp_kiov_t));
        }
        if(req[j]){
            __amp_free_request(req[j]);
        }
    }

    SKYFS_LEAVE("__skyfs_MS_writeback_subset:exit,%d\n",rc);
    return rc;
}


skyfs_s32_t
__skyfs_MS_do_clear_subset(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t hashvalue;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_subset_cache_t *subset_cache = NULL;

    SKYFS_ENTER("__skyfs_MS_do_clear_subset:%d,%d\n", dir_id, subset_id);
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
    htbp = &skyfs_subset_cache_htbbase[hashvalue];
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_do_clear_subset:locate htbp error%d\n", subset_id);
        goto ERR;
    }

    pthread_rwlock_wrlock(&(htbp->rwlock));
    
    subset_cache = __skyfs_MS_find_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_do_clear_subset:can not find subset_cache\n");
        pthread_rwlock_unlock(&(htbp->rwlock));
        rc = -1;
        goto ERR;
    }

    rc = __skyfs_MS_writeback_subset(subset_cache);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_do_clear_subset:writeback subset_cache failed\n");
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    rc = __skyfs_MS_free_subset(subset_cache);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_do_clear_subset:free subset_cache failed\n");
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    free(subset_cache);

    pthread_rwlock_unlock(&(htbp->rwlock));

ERR:

    SKYFS_LEAVE("__skyfs_MS_do_clear_subset:exit\n");
    return rc;
}

skyfs_s32_t
__skyfs_MS_do_clear_dir_cache(skyfs_M_dir_cache_t *dir_cache)
{
    skyfs_M_subset_index_t *subset_index = NULL;
    struct list_head *index = NULL, *head = NULL;
    skyfs_u32_t mds_id, dir_id, subset_id;
    skyfs_s32_t rc = 0;

    dir_id = dir_cache->dir_id;

    head = &(dir_cache->subset_head);
    list_for_each(index, head){
        subset_index = list_entry(index, skyfs_M_subset_index_t, subset_list);
        subset_id = subset_index->subset_id;
        mds_id = __skyfs_MS_judge_mdsid(dir_id, subset_id);
        if(mds_id != mds_this_id){
            /*send req to mds release the subset_cache*/
            /*Do not process this*/
            SKYFS_ERROR("__skyfs_MS_do_clear_dir_cache:send to mds %d to release\n",
                mds_id);
        }else{
            rc = __skyfs_MS_do_clear_subset(dir_id, subset_id);
            if(rc < 0){
                goto ERR;
            }
        }
    }
ERR:

    return rc;
}

skyfs_s32_t __skyfs_MS_init_cache(void)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t i;
    
    SKYFS_ENTER("__skyfs_MS_init_cache:enter\n");
    
    total_bmeta_num = SKYFS_MAX_BMETA_NUM;
    total_subset_cache_num = SKYFS_MAX_SUBSET_CACHE_NUM;
    total_subset_index_num = SKYFS_MAX_SUBSET_INDEX_NUM;
    total_dir_depth_num = SKYFS_MAX_DIR_DEPTH_NUM;
    total_dir_cache_num = SKYFS_MAX_DIR_CACHE_NUM;

    pthread_mutex_init(&total_bmeta_num_lock, NULL);
    pthread_mutex_init(&total_subset_cache_num_lock, NULL);
    pthread_mutex_init(&total_subset_index_num_lock, NULL);
    pthread_mutex_init(&total_dir_depth_num_lock, NULL);
    pthread_mutex_init(&total_dir_cache_num_lock, NULL);

    INIT_LIST_HEAD(&mds_wb_subset_list);
    pthread_mutex_init(&mds_wb_subset_list_lock, NULL);

    /*1.init dir cache */
    for(i = 0; i < SKYFS_DIR_HASH_LEN; i++){
        INIT_LIST_HEAD(&skyfs_dir_cache_htbbase[i].head);
        pthread_mutex_init(&skyfs_dir_cache_htbbase[i].lock, NULL);
    }

    /*2.init subset cache*/
    for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i++){
        INIT_LIST_HEAD(&skyfs_subset_cache_htbbase[i].head);
        pthread_mutex_init(&skyfs_subset_cache_htbbase[i].lock, NULL);
        skyfs_subset_cache_htbbase[i].length = -(SKYFS_MASTER_MDS_ID);

    }
    /*3.init dir depth */
    for(i = 0; i < SKYFS_DIR_DEPTH_HASH_LEN; i++){
        INIT_LIST_HEAD(&skyfs_dir_depth_htbbase[i].head);
        pthread_mutex_init(&skyfs_dir_depth_htbbase[i].lock, NULL);
    }

	/*4.init flock cache by mayl */
    for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i++){
        INIT_LIST_HEAD(&skyfs_flock_cache_htbbase[i].head);
        pthread_mutex_init(&skyfs_flock_cache_htbbase[i].lock, NULL);
        skyfs_flock_cache_htbbase[i].length = -(SKYFS_MASTER_MDS_ID);

    }


    pthread_mutex_init(&total_access_bmeta_num_lock, NULL);
    pthread_mutex_init(&total_read_bmeta_num_lock, NULL);

    SKYFS_LEAVE("__skyfs_MS_init_cache:exit\n");
    return rc;
}

skyfs_s32_t __skyfs_MS_finalize_cache(void)
{
    skyfs_s32_t rc = 0;


    return rc;
}

skyfs_s32_t __skyfs_MS_writeback_root_meta(void)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t fd = 0;
    skyfs_s8_t pathname[SKYFS_MAX_NAME_LEN];
    
    sprintf(pathname, "%s%s", SKYFS_LOCAL_META_PATH, "root");
    fd = open(pathname, O_RDWR|O_CREAT, 0666);
    if(fd > 0){
        write(fd, &root_cmeta, sizeof(skyfs_M_cmeta_t));
        close(fd);
        SKYFS_ERROR("__skyfs_MS_writeback_root_meta %s ino %lu , depth %d\n", pathname, root_cmeta.ino , root_cmeta.depth);
    }else{
        rc = -1;
        SKYFS_ERROR("__skyfs_MS_writeback_root_meta %s failed\n", pathname);
        goto err_out;
    }

err_out:
    return rc;
}

/* mayl : add function writeback root cmeta */

skyfs_s32_t __skyfs_MS_writeback_root_cmeta(void)
{
	
}


skyfs_s32_t __skyfs_MS_writeback_cache(void)
{
    skyfs_s32_t rc = 0;
	skyfs_u32_t i;
	/* mayl : set the flag , stop the fs now */ 
	stop_fs = 1;

	/*1. write back and clear all the subset cache and attached bmeta cache*/
	for(i = 0; i < SKYFS_SUBSET_HASH_LEN; i++){
		rc = __skyfs_MS_clear_htbcache(i, 0);
	}

	/*2. write back and clear all the dir cache*/
	for(i = 0; i < SKYFS_DIR_HASH_LEN; i++){
		rc = __skyfs_MS_clear_dcache(i);
	}

    return rc;
}

skyfs_s32_t __skyfs_MS_move_wb_entry(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_M_wb_entry_t *tmp_entry = NULL, *wb_entry = NULL;
    struct list_head   *index = NULL, *head = NULL;
    skyfs_s32_t        rc = 0;

    SKYFS_MSG("__skyfs_MS_move_wb_entry:enter:%d,%d\n", dir_id, subset_id);

    pthread_mutex_lock(&mds_wb_subset_list_lock);

    head = &mds_wb_subset_list;
    list_for_each(index, head){
        tmp_entry = list_entry(index, skyfs_M_wb_entry_t, wb_list);
        if(tmp_entry->dir_id == dir_id && tmp_entry->subset_id == subset_id){
            wb_entry = tmp_entry;
        }
    }

    if(wb_entry){
        list_move_tail(&(wb_entry->wb_list), &mds_wb_subset_list);
    }else{
        wb_entry = (skyfs_M_wb_entry_t *)malloc(sizeof(skyfs_M_wb_entry_t));
        if(wb_entry == NULL){
            SKYFS_ERROR("__skyfs_MS_mv_wb_entry:alloc mem err:%d,dir_id:%d,subset_id:%d",
                errno, dir_id, subset_id);
            rc = -errno;
            goto ERR;
        }
        /*remember to free it when subset released*/
        wb_entry->dir_id = dir_id;
        wb_entry->subset_id = subset_id;
        INIT_LIST_HEAD(&(wb_entry->wb_list));
        list_add_tail(&(wb_entry->wb_list), &mds_wb_subset_list);
    }

    pthread_mutex_unlock(&mds_wb_subset_list_lock);
ERR:

    SKYFS_LEAVE("__skyfs_MS_move_wb_entry:exit:rc:%d\n", rc);
    return rc;
}

skyfs_s32_t __skyfs_MS_writeback(void)
{
    skyfs_M_subset_cache_t *subset_cache = NULL;
    skyfs_M_bmeta_t     *bmeta = NULL;
    skyfs_M_wb_entry_t  *wb_entry = NULL;
    struct list_head    *index = NULL, *head = NULL, *tmp = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t dir_id, subset_id;
    skyfs_u32_t hashvalue = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_writeback:enter\n");

retry_writeback:
    /*1,decide writeback which subset*/
    pthread_mutex_lock(&mds_wb_subset_list_lock);

    wb_entry = list_entry(mds_wb_subset_list.next, skyfs_M_wb_entry_t, wb_list);
    list_move_tail(&(wb_entry->wb_list), &mds_wb_subset_list);

    dir_id = wb_entry->dir_id;
    subset_id = wb_entry->subset_id;

    pthread_mutex_unlock(&mds_wb_subset_list_lock);

    /*2. writeback the subset*/
    SKYFS_ERROR("__skyfs_MS_writeback:dir_id:%u,subset_id:%d\n", 
		dir_id, subset_id);
    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);
    hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
    htbp = &skyfs_subset_cache_htbbase[hashvalue];
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_writeback:locate htbp error:%d\n", hashvalue);
        goto ERR;
    }

    pthread_rwlock_rdlock(&(htbp->rwlock));
    subset_cache = __skyfs_MS_get_subset(htbp, dir_id, subset_id);
    if(subset_cache == NULL){
        SKYFS_ERROR("__skyfs_MS_writeback:get subset:%u,%d failed\n", dir_id, subset_id);
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    pthread_rwlock_wrlock(&(subset_cache->rwlock));

    rc = __skyfs_MS_writeback_subset(subset_cache);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_writeback:writeback subset err:%d\n", rc);
        pthread_rwlock_unlock(&(subset_cache->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    head = &(subset_cache->bmeta_head);
    list_for_each(index, head){
        bmeta = list_entry(index, skyfs_M_bmeta_t, bmeta_list);
        tmp = index->prev;
        __skyfs_MS_free_bmeta(bmeta);
        index = tmp;
        free(bmeta);
    }

    pthread_rwlock_unlock(&(subset_cache->rwlock));

    pthread_rwlock_unlock(&(htbp->rwlock));

    if(rc == 0){
        SKYFS_ERROR("__skyfs_MS_writeback:rc:%d,retry\n", rc);
        goto retry_writeback;
    }
ERR:

    SKYFS_ERROR("__skyfs_MS_writeback:exit.rc:%d\n", rc);

    return rc;
}

skyfs_s32_t
__skyfs_MS_clear_htbcache(skyfs_u32_t hashindex, skyfs_u32_t kind_mds_id)
{
    skyfs_htb_t *htbp = NULL;
    struct list_head *head = NULL, *index = NULL, *tmp = NULL;
    struct list_head *local_head = NULL, *local_index = NULL;
    skyfs_M_subset_cache_t *subset_cache = NULL;
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_u32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_clear_htbcache:hashindex:%d, kind_mds:%d\n",
        hashindex, kind_mds_id);

    htbp = &skyfs_subset_cache_htbbase[hashindex];
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_clear_htbcache:locate htbp error:%d\n", hashindex);
        goto err_out;
    }

    pthread_rwlock_wrlock(&(htbp->rwlock));

    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_clear_htbcache:hash table NUL,still need set\n");
        htbp->length = - kind_mds_id;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto err_out;
    }

    /*1. writeback all the subset in this hash entry*/
    list_for_each(index, head){
        subset_cache = list_entry(index, skyfs_M_subset_cache_t, subset_hash);
        pthread_rwlock_wrlock(&(subset_cache->rwlock));
        {
            rc = __skyfs_MS_writeback_subset(subset_cache);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_clear_htbcache:wb subset err:%d\n", rc);
                pthread_rwlock_unlock(&(subset_cache->rwlock));
                pthread_rwlock_unlock(&(htbp->rwlock));
                goto err_out;
            }

            local_head = &(subset_cache->bmeta_head);
            list_for_each(local_index, local_head){
                bmeta = list_entry(local_index, skyfs_M_bmeta_t, bmeta_list);
                tmp = local_index->prev;
                __skyfs_MS_free_bmeta(bmeta);
                local_index = tmp;
                free(bmeta);
            }

            rc = __skyfs_MS_write_subset(subset_cache);
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_clear_htbcache:write subhead error!!:%d\n",rc);
                pthread_rwlock_unlock(&(subset_cache->rwlock));
                pthread_rwlock_unlock(&(htbp->rwlock));
            }
        }
        pthread_rwlock_unlock(&(subset_cache->rwlock));
    }

    /*2, free all the subset in this hash entry*/
    list_for_each(index, head){
        subset_cache = list_entry(index, skyfs_M_subset_cache_t, subset_hash);
        tmp = index->prev;
        __skyfs_MS_free_subset(subset_cache);
        index = tmp;
        free(subset_cache);
    }

    htbp->length = - kind_mds_id;
    SKYFS_ERROR("clear_htbcache:clear_index:%d,addto_mds:%d\n", hashindex, kind_mds_id);

    pthread_rwlock_unlock(&(htbp->rwlock));
err_out:

    return rc;
}

skyfs_s32_t
__skyfs_MS_check_htbcache(skyfs_htb_t *htbp)
{
    skyfs_s32_t mds = 0;

    if(htbp->length < 0){
        mds = -(htbp->length);
        SKYFS_ERROR("__skyfs_MS_check_htbcache:should be mds:%d,layoutv:%d\n", 
            mds, mds_layout_version);
    }else{
        SKYFS_MSG("__skyfs_MS_check_htbcache:htbp layout ver:%d ok\n", 
            mds_layout_version);
    }

    return mds;
}

skyfs_s32_t
__skyfs_MS_clear_dcache(skyfs_u32_t hashindex)
{
    skyfs_htb_t *htbp = NULL;
    struct list_head *head = NULL, *index = NULL, *tmp = NULL;
    struct list_head *local_head = NULL, *local_index = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_u32_t rc = 0;

    SKYFS_ENTER("__skyfs_MS_clear_dcache:hashindex:%d\n", hashindex);

    htbp = &skyfs_dir_cache_htbbase[hashindex];
    if(htbp == NULL){
        SKYFS_ERROR("__skyfs_MS_clear_htbcache:locate htbp error:%d\n", 
			hashindex);
        goto err_out;
    }

    pthread_rwlock_wrlock(&(htbp->rwlock));

    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_MS_clear_dcache:hash table NUL\n");
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto err_out;
    }

    /*1. writeback all the subset in this hash entry*/
    list_for_each(index, head){
        dir_cache = list_entry(index, skyfs_M_dir_cache_t, dir_hash);
        SKYFS_ERROR("__skyfs_MS_clear_dcache:dir_cache:%p\n",dir_cache);
        pthread_rwlock_wrlock(&(dir_cache->rwlock));
        {
            local_head = &(dir_cache->subset_head);
            list_for_each(local_index, local_head){
                subset_index = list_entry(local_index, skyfs_M_subset_index_t, 
					subset_list);
                tmp = local_index->prev;
                __skyfs_MS_release_subset_index(subset_index);
                free(subset_index);
                local_index = tmp;
            }

            rc = __skyfs_MS_do_write_dcache(dir_cache, &(dir_cache->cmeta));
            if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_clear_dcache:write dcache err!:%d\n",rc);
                pthread_rwlock_unlock(&(dir_cache->rwlock));
                pthread_rwlock_unlock(&(htbp->rwlock));
				goto err_out;
            }
        }
        pthread_rwlock_unlock(&(dir_cache->rwlock));
		dir_cache = NULL;
    }

    /*2, free all the subset in this hash entry*/
    list_for_each(index, head){
        dir_cache = list_entry(index, skyfs_M_dir_cache_t, dir_hash);
        tmp = index->prev;
        __skyfs_MS_release_dir_cache(dir_cache);
    	free(dir_cache);
        index = tmp;
    }

    //htbp->length = - kind_mds_id;
    SKYFS_ERROR("__skyfs_MS_clear_dcache:clear_index:%d\n", hashindex);

    pthread_rwlock_unlock(&(htbp->rwlock));
err_out:

    return rc;
}

skyfs_s32_t
__skyfs_MS_do_write_dcache(skyfs_M_dir_cache_t *dcache, 
		skyfs_M_cmeta_t *cmeta)
{
	skyfs_s8_t string[SKYFS_MAX_NAME_LEN];
	skyfs_s32_t fd;
	skyfs_s32_t rc;

	bzero(string, SKYFS_MAX_NAME_LEN);
    sprintf(string , "%s/dcache-%u", SKYFS_LOCAL_META_PATH, dcache->dir_id);
    fd = open(string, O_WRONLY|O_CREAT, 0666);
	if(fd  > 0){
		rc = write(fd, cmeta, sizeof(skyfs_M_cmeta_t));
        if(rc >= 0){
            SKYFS_MSG("__skyfs_MS_do_write_dcache:write %d %s suc:%d\n", 
			    fd, string, rc);
		    rc = write(fd, dcache->subset_bm, 
					sizeof(skyfs_u8_t)*SKYFS_SUBSET_BM_LEN);
			if(rc < 0){
                SKYFS_ERROR("__skyfs_MS_do_write_dcache:write bm %s err,%d\n", 
			        string, errno);
			}
		}else{
            SKYFS_ERROR("__skyfs_MS_do_write_dcache:write cmeta %s err,%d\n", 
			    string, errno);
		}
		
		close(fd);
	}else{
		rc = errno;

    	SKYFS_ERROR("__skyfs_MS_do_write_dcache:open %s err,%d\n", 
			string, errno);
	}

	return rc;
}
/*This is end of mds_cache.c*/
