/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_cache.c $
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

#include "osd_fs.h"


#include "client_help.h"
#include "client_init.h"
#include "client_op.h"
#include "client_cache.h"
#include "client_itm.h"
#include "client_ito.h"
#include "gpu_compress.h"
#include "client_compress_thread.h"


//static uint64_t gpu_comp_time = 0;

extern void do_pre_split_v0(double * data, size_t num_doubles , uint8_t * lanes);
extern void do_pre_split_p_v0(double * data, size_t num_doubles , uint8_t * lanes, size_t start_index, size_t len);
extern void do_post_merge_v0( uint8_t * lanes, size_t num_doubles, double * ori_data);

extern unsigned char *  prefetch_init_ctx(void);

extern int lock_gpu_comp_task(int gtask_num);
extern void unlock_gpu_comp_task(int gtask_index);


extern int lock_gpu_init_task(int gtask_num);
extern void unlock_gpu_init_task(int gtask_index);

int all_gpu_comp_ratio = 500;
int enable_prop = 0;
static uint64_t gpu_comp_cnt = 0;
uint64_t gpu_comp_init_time = 0;

static uint64_t gpu_compress_bytes = 0; 
static uint64_t gpu_compress_time = 0;

static uint64_t cpu_compress_bytes = 0; 
static uint64_t cpu_compress_time = 0; 

static uint64_t gpu_comp_submit_time = 0;
static uint64_t gpu_comp_submit_cnt = 0;
static uint64_t total_compressed_bytes= 0;

static size_t gpu_write_buf_size = (size_t)(SKYFS_GPU_COMPRESS_BUF_SIZE);
static int gpu_compress = 0;

extern void free_cpu_batch(void * cpu_batch);

uint64_t get_gpu_comp_init_time()
{
	return gpu_comp_init_time;
}
void clear_gpu_comp_init_time()
{
	gpu_comp_init_time = 0;
}

void stat_gpu_cpu_compress()
{
	float compress_rate = 1.0;
	float gpu_cr_bw = 0.0;
	float cpu_cr_bw = 0.0;
	float all_cr_bw = 0.0;
	float gpu_weight = 0.0;


	SKYFS_ERROR_1("Total compress_time: %lu us, ori compress_bytes %lu , total compressed bytes %lu  \n", 
			(gpu_compress_time+cpu_compress_time),(gpu_compress_bytes+cpu_compress_bytes),total_compressed_bytes);
	
	SKYFS_ERROR_1("Gpu compress_time: %lu us, compress_bytes %lu \n", 
			(gpu_compress_time),(gpu_compress_bytes));
	
	SKYFS_ERROR_1("Cpu compress_time: %lu us, compress_bytes %lu \n", 
			(cpu_compress_time),(cpu_compress_bytes));


	SKYFS_ERROR_1("compress summary:\n");
	if((total_compressed_bytes) >0){
		compress_rate = (float)(gpu_compress_bytes+cpu_compress_bytes);
		compress_rate = compress_rate / (float)total_compressed_bytes;
	}
	if(gpu_compress_time >0){
		gpu_cr_bw = (float)gpu_compress_bytes;
		gpu_cr_bw = (gpu_cr_bw/(float)gpu_compress_time)/1000.0;
	}
	
	if(cpu_compress_time >0){
		cpu_cr_bw = (float)cpu_compress_bytes;
		cpu_cr_bw = (cpu_cr_bw/(float)cpu_compress_time)/1000.0;
	}
	if(gpu_compress_time + cpu_compress_time >0){
		all_cr_bw  = (float)cpu_compress_bytes+(float)gpu_compress_bytes;
		all_cr_bw =  all_cr_bw/((float)cpu_compress_time+(float)gpu_compress_time)/1000.0;
	}
	if(gpu_compress_bytes + cpu_compress_bytes > 0){
		gpu_weight = (float)gpu_compress_bytes;
		gpu_weight = gpu_weight/( (float)gpu_compress_bytes + (float)cpu_compress_bytes) *100.0;
	}
	SKYFS_ERROR_1("compress rate : %f, gpu compress bw: %f GB/s, cpu compress bw: %f GB/s, total compress bw : %f, GPU weight: %f prcent\n",
			compress_rate, gpu_cr_bw, cpu_cr_bw, all_cr_bw, gpu_weight);


	gpu_compress_time = 0;
	cpu_compress_time = 0;
	gpu_compress_bytes = 0;
	cpu_compress_bytes = 0;
	total_compressed_bytes = 0;
}


extern skyfs_s32_t 
__skyfs_C2O_submit_compbuf(skyfs_ino_t ino,
		const skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
		int comp_type,
		skyfs_s64_t * changed_space
		);

skyfs_htb_t         *compbuf_hash_base;
skyfs_htb_t         *gcompbuf_hash_base;
skyfs_htb_t client_depth_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_htb_t client_length_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_htb_t client_dentrycache_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_htb_t client_dlentry_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_htb_t client_meta_htbbase[SKYFS_DIR_DEPTH_HASH_LEN];
skyfs_layout_t  mds_layout[SKYFS_SUBSET_HASH_LEN];
skyfs_u32_t     mds_layout_version;
pthread_mutex_t mds_layout_lock;

skyfs_u32_t     mds_consistent_hash_ok;
skyfs_u32_t     mds_average_hash_scope;

skyfs_layout_t  osd_data_layout[SKYFS_DLSUBSET_HASH_LEN];
skyfs_u32_t     osd_dl_version;
pthread_mutex_t osd_dl_lock;

skyfs_u32_t     osd_consistent_hash_ok;
skyfs_u32_t     osd_dl_average_hash_scope;

skyfs_DL_depth_t skyfs_dl_depth;

skyfs_u32_t     pad_id = 1;

struct list_head client_bcache_list;
pthread_mutex_t client_bcache_lock;
struct list_head client_writebuf_head;
pthread_mutex_t client_writebuf_lock;
skyfs_u32_t client_writebuf_num;

skyfs_layout_L1_t mds_mapping_l1[SKYFS_MDS_L1MAPPING_LEN];
skyfs_layout_L1_t osd_mapping_l1[SKYFS_OSD_L1MAPPING_LEN];

skyfs_s32_t __skyfs_C_init_cache(void)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t i;

    SKYFS_ENTER("__skyfs_C_init_cache:enter\n");

    for(i = 0; i < SKYFS_DIR_DEPTH_HASH_LEN; i++){
        INIT_LIST_HEAD(&client_depth_htbbase[i].head);
        pthread_mutex_init(&client_depth_htbbase[i].lock, NULL);

        INIT_LIST_HEAD(&client_length_htbbase[i].head);
        pthread_mutex_init(&client_length_htbbase[i].lock, NULL);

	    INIT_LIST_HEAD(&client_dentrycache_htbbase[i].head);
        pthread_mutex_init(&client_dentrycache_htbbase[i].lock, NULL);

		INIT_LIST_HEAD(&client_dlentry_htbbase[i].head);
        pthread_mutex_init(&client_dlentry_htbbase[i].lock, NULL);

		INIT_LIST_HEAD(&client_meta_htbbase[i].head);
        pthread_mutex_init(&client_meta_htbbase[i].lock, NULL);

    }

    bzero(&skyfs_dl_depth, sizeof(skyfs_DL_depth_t));
    osd_dl_version = 1;

    INIT_LIST_HEAD(&client_bcache_list);
    pthread_mutex_init(&client_bcache_lock, NULL);

    INIT_LIST_HEAD(&client_writebuf_head);
    pthread_mutex_init(&client_writebuf_lock, NULL);
	client_writebuf_num = 0;
    
     __skyfs_init_htb(SKYFS_DL_PARTITION_HASH_LEN,
                &compbuf_hash_base);
     __skyfs_init_htb(SKYFS_DL_PARTITION_HASH_LEN,
                &gcompbuf_hash_base);


    SKYFS_LEAVE("__skyfs_C_init_cache:exit\n");

    return rc;
}

/*mds layout below*/
skyfs_s32_t __skyfs_C_init_layout(void)
{
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_C_init_layout:enter\n");

	__skyfs_init_mdsmapping(0, 
			&mds_info, 
			mds_mapping_l1);

	__skyfs_init_osdmapping(0, 
			&osd_info, 
			osd_mapping_l1);

	SKYFS_LEAVE("__skyfs_C_init_layout:exit\n");

	return rc;
}

skyfs_s32_t __skyfs_C_init_mds_extent(void)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t hashvalue;
    skyfs_u32_t mds_id;
    skyfs_u32_t virtual;
    skyfs_u32_t mds_last_index = 0;
    skyfs_u32_t i;

    mds_layout_version = 0;
    pthread_mutex_init(&mds_layout_lock, NULL);

    mds_average_hash_scope = SKYFS_SUBSET_HASH_LEN / mds_num;
    mds_consistent_hash_ok = 0;

    bzero(mds_layout, sizeof(skyfs_layout_t) * SKYFS_SUBSET_HASH_LEN);
    for(i = 0; i < mds_num; i ++){
        mds_id = mds_info.mds[i].id;
        if(mds_consistent_hash_ok){
            hashvalue = __skyfs_num2hashvalue(mds_id);
            hashvalue = hashvalue % SKYFS_SUBSET_HASH_LEN;
        }else{
            hashvalue = i * mds_average_hash_scope;  
        }

        SKYFS_ERROR("__skyfs_C_init_mds_extent:hashvalue:%d,mds_id:%d\n", 
            hashvalue, mds_id);
        mds_layout[hashvalue].id = mds_id;
        mds_layout[hashvalue].virtual = 0;
    }

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
        }else{
            mds_layout[i].id = mds_id;
            mds_layout[i].virtual = virtual;
        }
        SKYFS_MSG("__skyfs_C_init_mds_extent:i:%d,mds_id:%d\n",
            i, mds_layout[i].id);
    }

    return rc;
}


skyfs_u32_t __skyfs_C_get_dirid(skyfs_ino_t ino, skyfs_u32_t flag)
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

    return dir_id;
}

skyfs_u32_t __sufns_C_get_subsetid(skyfs_u32_t dir_id, 
                skyfs_s8_t *name, 
                skyfs_u32_t hash)
{
    skyfs_u32_t subset_id = 0;
    skyfs_u32_t name_hash;
    skyfs_u32_t depth_hash;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_dir_depth_t *dir_depth = NULL;
    skyfs_M_dir_cache_t dir_cache;
    skyfs_s32_t split_depth = 0;

    skyfs_s32_t rc;
    
    if(name){
        SKYFS_MSG("__skyfs_C_get_subsetid:en:name:%s,subset_id:%d,depth:%d,size:%u\n", 
            name, subset_id, split_depth, strlen(name));
    }

    if(name){
        name_hash = __skyfs_name2hashvalue(name);
    }else{
        name_hash = hash;
    }

    depth_hash  = __skyfs_get_subset_hashvalue(dir_id, 0);
    depth_hash = depth_hash % SKYFS_DIR_DEPTH_HASH_LEN;
    if(depth_hash > SKYFS_DIR_DEPTH_HASH_LEN){
        SKYFS_ERROR("__skyfs_C_get_subsetid:detph_hash:%u\n", depth_hash);
        exit(1);
    }
    htbp = &client_depth_htbbase[depth_hash];

    pthread_mutex_lock(&htbp->lock);
    dir_depth = __skyfs_C_find_dir_depth(htbp, dir_id);
    if(dir_depth == NULL){
        dir_depth = (skyfs_M_dir_depth_t *)malloc(sizeof(skyfs_M_dir_depth_t));
        rc = __skyfs_C2M_get_dcache(&dir_cache, dir_id);
        if(rc < 0){
            SKYFS_ERROR("__sufns_C_get_subsetid:failed,rc:%d\n", rc);
            exit(1);
        }
        dir_depth->dir_id = dir_id;
        dir_depth->depth = dir_cache.depth;
        memcpy(dir_depth->subset_bm, dir_cache.subset_bm, SKYFS_SUBSET_BM_LEN);
        list_add(&(dir_depth->dir_depth_hash), &(htbp->head));
        pthread_mutex_init(&dir_depth->lock, NULL);
    }

    pthread_mutex_lock(&dir_depth->lock);

    split_depth = dir_depth->depth;

retry_lower:
    subset_id = __skyfs_get_subset_id(name_hash, split_depth);
    if(__skyfs_C_test_bit(dir_depth->subset_bm, subset_id) == 0 && split_depth >= 0){
        split_depth --;
        goto retry_lower;
    }

    pthread_mutex_unlock(&dir_depth->lock);

    if(name){
        SKYFS_MSG("__skyfs_C_get_subsetid:name:%s,subset_id:%d,depth:%d,size:%u\n", 
            name,subset_id, split_depth,strlen(name));
    }

    pthread_mutex_unlock(&htbp->lock);

    return subset_id;

}

skyfs_M_dir_depth_t *
__skyfs_C_find_dir_depth(skyfs_htb_t *htbp, skyfs_u32_t dir_id)
{
    skyfs_M_dir_depth_t *dir_depth= NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_C_find_dir_depth:enter.dir_id:%d\n", dir_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_find_dir_depth:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_M_dir_depth_t, dir_depth_hash);
        SKYFS_MSG("__skyfs_C_find_dir_depth:tmp %d\n", tmp->dir_id);
        if(tmp->dir_id == dir_id){
            SKYFS_MSG("__skyfs_C_find_dir_depth:find the dir depth\n");
            dir_depth = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_C_find_dir_depth:leave.\n");
    return dir_depth;
}

skyfs_s32_t
__skyfs_C_release_depth(skyfs_u32_t dir_id)
{
    skyfs_u32_t depth_hash;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_dir_depth_t *dir_depth = NULL;

    skyfs_s32_t rc = 0;

    depth_hash = __skyfs_get_subset_hashvalue(dir_id, 0);
    depth_hash = depth_hash % SKYFS_DIR_DEPTH_HASH_LEN;
    if(depth_hash > SKYFS_DIR_DEPTH_HASH_LEN){
        SKYFS_ERROR("__skyfs_C_release_depth:detph_hash:%u\n", depth_hash);
        exit(1);
    }
    htbp = &client_depth_htbbase[depth_hash];

    pthread_mutex_lock(&htbp->lock);
    dir_depth = __skyfs_C_find_dir_depth(htbp, dir_id);
    if(dir_depth == NULL){
        rc = -ENOENT;
        goto ERR;
    }
    pthread_mutex_lock(&dir_depth->lock);
    list_del(&(dir_depth->dir_depth_hash));
    pthread_mutex_unlock(&dir_depth->lock);

    free(dir_depth);
    dir_depth = NULL;

ERR:
    pthread_mutex_unlock(&htbp->lock);

    return rc;
}

skyfs_u32_t __skyfs_C_judge_mdsid(skyfs_u32_t dir_id, 
                skyfs_u32_t subset_id)
{
    skyfs_u32_t mds_id = 0;
    skyfs_u32_t index;    
    skyfs_u32_t hashvalue;

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);

    index = hashvalue % SKYFS_MDS_L1MAPPING_LEN;

    mds_id = mds_mapping_l1[index].id;
    
    SKYFS_MSG("__skyfs_C_judge_mdsid:hash:%u,index:%d,mds_id:%d,mds1:%d,mds2:%d\n",
        hashvalue, index, mds_id, 
		mds_mapping_l1[index+1].id, 
		mds_mapping_l1[index+2].id);

    return mds_id;

}

skyfs_u32_t __skyfs_C_judge_dir_mdsid(skyfs_u32_t dir_id, 
                skyfs_u32_t subset_id)
{
	
	return __skyfs_C_judge_mdsid(dir_id, subset_id);
#if 0
    skyfs_u32_t mds_id = 0;
    skyfs_u32_t index;    
    skyfs_u32_t hashvalue;

    hashvalue = __skyfs_get_subset_hashvalue(dir_id, subset_id);

    index = hashvalue % mds_info.mds_num;

    mds_id = mds_info.mds[index].id;
    
    SKYFS_MSG("__skyfs_C_judge_dir_mdsid:hash:%d,index:%d,mds_id:%d\n",
        hashvalue, index, mds_id);

    return mds_id;
#endif
}
skyfs_s32_t __skyfs_C_judge_mds_layoutv(skyfs_u32_t mds_layoutv, skyfs_u32_t mds_id)
{
    skyfs_s32_t rc = 0;

    pthread_mutex_lock(&mds_layout_lock);

    if(mds_layoutv <= mds_layout_version){
        SKYFS_MSG("__skyfs_C_judge_mds_layoutv:client version newer\n");
    }else{
        __skyfs_C2M_get_layout(mds_id);
    }

    pthread_mutex_unlock(&mds_layout_lock);

    return rc;
}

/*osd data layout below*/
skyfs_s32_t __skyfs_C_init_osd_dlextent(void)
{
    skyfs_u32_t hashvalue;
    skyfs_u32_t osd_id;
    skyfs_u32_t virtual;
    skyfs_u32_t osd_last_index = 0;
    skyfs_u32_t i;
    skyfs_s32_t rc = 0;
    
    osd_dl_version = 0;
    //osd_state_version = 0;
    //pthread_mutex_init(&osd_state_version_lock, NULL);
    pthread_mutex_init(&osd_dl_lock, NULL);

    osd_dl_average_hash_scope = SKYFS_DLSUBSET_HASH_LEN / osd_num;
    osd_consistent_hash_ok = 0;

    bzero(osd_data_layout, sizeof(skyfs_layout_t) * SKYFS_DLSUBSET_HASH_LEN);

#if 1
    for(i = 0; i < osd_num; i ++){
        osd_id = osd_info.osd[i].id;
        if(osd_consistent_hash_ok){
            hashvalue = __skyfs_num2hashvalue(osd_id);
            hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
        }else{
            hashvalue = i * osd_dl_average_hash_scope;
        }

        SKYFS_ERROR("__skyfs_SS_init_osd_dlextent:hashvalue:%d\n", hashvalue);
        osd_data_layout[hashvalue].id = osd_id;
        osd_data_layout[hashvalue].virtual = 0;
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
        }else{
            osd_data_layout[i].id = osd_id;
            osd_data_layout[i].virtual = virtual;
        }
    }
#endif
#if 0

    for(i = 0; i < SKYFS_DLSUBSET_HASH_LEN; i ++){
        osd_data_layout[i].id = (i % osd_num) + 1;
        osd_data_layout[i].virtual = 0;
    }

#endif
    return rc;
}

skyfs_u32_t
__skyfs_C_get_dl_subsetid(skyfs_ino_t ino, skyfs_u64_t obj_id)
{
    skyfs_u32_t subset_id = 0;
    skyfs_s32_t split_depth;
    skyfs_u32_t hashvalue;
    skyfs_DL_head_t *dl_head = NULL;
    skyfs_s32_t rc = 0;

    pthread_mutex_lock(&skyfs_dl_depth.lock);

    if(skyfs_dl_depth.ver < osd_dl_version){
        dl_head = (skyfs_DL_head_t *)malloc(sizeof(skyfs_DL_head_t));
        rc = __skyfs_C2O_get_dl_head(SKYFS_MASTER_OSD_ID, pad_id, dl_head);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_C_get_dl_subsetid:get dl head err:%d\n", rc);
            goto ERR;
        }
        skyfs_dl_depth.depth = dl_head->depth;
        skyfs_dl_depth.ver = osd_dl_version;
        SKYFS_MSG("__skyfs_C_get_dl_subsetid: before copy\n");
        memcpy(skyfs_dl_depth.subset_bm, dl_head->subset_bm, SKYFS_DLSUBSET_BM_LEN);
        SKYFS_MSG("__skyfs_C_get_dl_subsetid:depthver:%d,dl_ver:%d\n",
            skyfs_dl_depth.ver, osd_dl_version);
        free(dl_head);
    }

    hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    split_depth = skyfs_dl_depth.depth;        

retry_lower:
    subset_id = __skyfs_get_subset_id(hashvalue, split_depth);
    if(__skyfs_test_bit(skyfs_dl_depth.subset_bm, subset_id) == 0 && split_depth >= 0){
        SKYFS_MSG("__skyfs_C_get_dl_subsetid:%d not exist, split_depth:%d\n",
            subset_id, split_depth);
        split_depth --;
        goto retry_lower;
    }

    SKYFS_MSG("__skyfs_C_get_dl_subsetid:subset_id:%d,split_depth:%d\n", 
        subset_id, split_depth);

ERR:
    pthread_mutex_unlock(&skyfs_dl_depth.lock);

    return subset_id;
}

void
__skyfs_C_clear_dl_depth(void)
{
    pthread_mutex_lock(&skyfs_dl_depth.lock);

    skyfs_dl_depth.ver = 0;

    pthread_mutex_unlock(&skyfs_dl_depth.lock);
}

skyfs_u32_t 
__skyfs_C_judge_osdid(skyfs_u32_t subset_id)
{
    skyfs_u32_t osd_id = 0;
    skyfs_u32_t index;
    skyfs_u32_t hashvalue;

    hashvalue = __skyfs_get_subset_hashvalue(pad_id, subset_id);

    index = hashvalue % SKYFS_OSD_L1MAPPING_LEN;

    osd_id = osd_mapping_l1[index].id;

    SKYFS_ERROR("__skyfs_C_judge_osdid:hash:%d,index:%d,osd_id:%d,ver:%d\n",
        hashvalue, index, osd_id, osd_dl_version);

    return osd_id;
}

/*file length cache below*/
skyfs_htb_t *
__skyfs_C_locate_flength(skyfs_ino_t ino)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;

    hashvalue = __skyfs_ino2hashvalue(ino, 0);
    hashvalue = hashvalue % SKYFS_DIR_DEPTH_HASH_LEN;
    
    htbp = &client_length_htbbase[hashvalue];

    return htbp;
}

skyfs_C_flength_t *
__skyfs_C_find_flength(skyfs_htb_t *htbp, skyfs_ino_t ino)
{
    skyfs_C_flength_t *file_length= NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_C_find_flength:enter.ino:%llu\n", ino);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_find_flength:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_C_flength_t, list);
        SKYFS_MSG("__skyfs_C_find_flength:tmp %llu\n", tmp->ino);
        if(tmp->ino == ino){
            SKYFS_MSG("__skyfs_C_find_flength:find the flength\n");
            file_length = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_C_find_file_length:leave:%p.\n", file_length);
    return file_length;
}


skyfs_u64_t
__skyfs_C_get_flength(skyfs_ino_t ino)
{
    skyfs_u64_t flength = 0;
    skyfs_C_flength_t *file_length = NULL;
    skyfs_htb_t *htbp = NULL;

    htbp = __skyfs_C_locate_flength(ino);
    if(htbp == NULL){
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);
    file_length = __skyfs_C_find_flength(htbp, ino);
    if(file_length){
        flength = file_length->flength;

    }

    pthread_mutex_unlock(&htbp->lock);

ERR:
    return flength;
}

skyfs_s32_t
__skyfs_C_release_flength(skyfs_ino_t ino)
{
    skyfs_C_flength_t *file_length = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    SKYFS_MSG("__skyfs_C_release_flength:ino:%llu.\n",ino);

    htbp = __skyfs_C_locate_flength(ino);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);
    file_length = __skyfs_C_find_flength(htbp, ino);
    if(file_length){
        list_del_init(&file_length->list);
        free(file_length);
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:
    return rc;
}

skyfs_s32_t
__skyfs_C_update_flength(skyfs_ino_t ino, 
                skyfs_u64_t flength) 
{
    skyfs_C_flength_t *file_length = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    htbp = __skyfs_C_locate_flength(ino);
    if(htbp == NULL){
        rc = -EEXIST;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);
    file_length = __skyfs_C_find_flength(htbp, ino);
    if(file_length){
        if(flength > file_length->flength){
            file_length->flength = flength;
        }
    }else{
        file_length = (skyfs_C_flength_t *)malloc(sizeof(skyfs_C_flength_t));
        file_length->ino = ino;
        file_length->flength = flength;
        list_add(&file_length->list, &htbp->head);
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

/*Dentry cache below*/
skyfs_htb_t *
__skyfs_C_locate_dentrycache(skyfs_ino_t dino, skyfs_s8_t *name)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;

    hashvalue = __skyfs_name_hashkey(dino, name, 1024);
    
    htbp = &client_dentrycache_htbbase[hashvalue];


    return htbp;
}

skyfs_C_dentry_t *
__skyfs_C_find_dentrycache(skyfs_htb_t *htbp, 
            skyfs_ino_t dino,
            skyfs_s8_t *name)
{
    skyfs_C_dentry_t *dentrycache = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_C_find_dentry:enter.dino:%llu,name:%s\n", 
        dino, name);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_find_dentry:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_C_dentry_t, list);
        SKYFS_MSG("__skyfs_C_find_dentry:tmp %llu,name:%s\n", 
            tmp->dino, tmp->name);
        if(tmp->dino == dino && (strcmp(tmp->name, name)== 0)){
            SKYFS_MSG("__skyfs_C_find_dentry:find the dentry\n");
            dentrycache = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_C_find_dentry:leave:%p.\n", dentrycache);
    return dentrycache;
}


skyfs_s32_t __skyfs_C_lookup(skyfs_ino_t dino,
                skyfs_s8_t *name,
                skyfs_ino_t *ino,
                skyfs_u32_t *conflict_index)
{
    skyfs_C_dentry_t *dentrycache = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    htbp = __skyfs_C_locate_dentrycache(dino, name);
    if(htbp == NULL){
        rc = -EEXIST;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dentrycache = __skyfs_C_find_dentrycache(htbp, dino, name);
    if(dentrycache){
        *ino = dentrycache->ino;
        *conflict_index = dentrycache->conflict_index;
        rc = 2;
        SKYFS_MSG("__skyfs_C_lookup:get the dentry:%llu,%s\n", dino, name);
    }else{
        rc = 0;
    }

    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

skyfs_s32_t __skyfs_C_add_dentry(skyfs_ino_t dino,
                skyfs_s8_t *name,
                skyfs_ino_t ino,
                skyfs_u32_t conflict_index)
{
    skyfs_C_dentry_t *dentrycache = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;
    // skip add dentry , by mayl
    //return rc;

    htbp = __skyfs_C_locate_dentrycache(dino, name);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dentrycache = __skyfs_C_find_dentrycache(htbp, dino, name);
    if(dentrycache == NULL){
        dentrycache = (skyfs_C_dentry_t *)malloc(sizeof(skyfs_C_dentry_t));
        dentrycache->ino = ino;
        dentrycache->conflict_index = conflict_index;
        dentrycache->dino = dino;
        strcpy(dentrycache->name, name);
        list_add(&dentrycache->list, &htbp->head);
        rc = 0;
        SKYFS_MSG("__skyfs_C_add_dentry:add the dentry:%llu,%s\n", dino, name);
    }else{
        rc = 2;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

skyfs_s32_t __skyfs_C_release_dentry(skyfs_ino_t dino,
                skyfs_s8_t *name)
{
    skyfs_C_dentry_t *dentrycache = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    SKYFS_MSG("skyfs_C_release_dentry:%s\n", name);
    htbp = __skyfs_C_locate_dentrycache(dino, name);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dentrycache = __skyfs_C_find_dentrycache(htbp, dino, name);
    if(dentrycache){
        list_del(&dentrycache->list);
        free(dentrycache);
        rc = 0;
    }else{
        rc = -ENOENT;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    SKYFS_MSG("skyfs_C_release_dentry:%s.exit\n", name);
    return rc;
}


skyfs_C_writebuf_t *
__skyfs_C_lookup_writebuf(skyfs_ino_t ino, 
		skyfs_s64_t offset, skyfs_u32_t flag)
{
	skyfs_C_writebuf_t *writebuf = NULL, *tmp = NULL;
	struct list_head *head = NULL, *index = NULL;
	skyfs_u64_t size = 0;

	SKYFS_MSG("__skyfs_C_lookup_writebuf:ino:%llu,offset:%llu\n",
        ino, offset);

	head = &client_writebuf_head;		

	if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_lookup_writebuf:list NULL\n");
        goto OUT;
    }

	if(flag == 0){
    	list_for_each(index, head){
        	tmp = list_entry(index, skyfs_C_writebuf_t, list);
        	if(tmp->offset + tmp->count == offset && tmp->ino == ino){
            	writebuf = tmp;
            	SKYFS_MSG("__skyfs_C_lookup_writebuf:ino:%llu,offset:%llu.0\n",
                	ino, writebuf->offset);
            	goto OUT;
        	}
    	}
	}else if(flag == 1){
		list_for_each(index, head){
       		tmp = list_entry(index, skyfs_C_writebuf_t, list);
        	if(tmp->ino == ino){
           		writebuf = tmp;
            	SKYFS_MSG("__skyfs_C_lookup_writebuf:ino:%llu,offset:%llu.1\n",
                	ino, writebuf->offset);
            	goto OUT;
        	}
    	}
	}else if(flag == 2){
		list_for_each(index, head){
       		tmp = list_entry(index, skyfs_C_writebuf_t, list);
        	if(tmp->offset + tmp->count >size){
				size = tmp->offset + tmp->count;
           		writebuf = tmp;
            	SKYFS_MSG("__skyfs_C_lookup_writebuf:ino:%llu,offset:%llu.2\n",
                	ino, writebuf->offset);
        	}
    	}
	}

OUT:

	return writebuf;
}

skyfs_s32_t
__skyfs_C_attach_writebuf(skyfs_C_writebuf_t *writebuf, 
		const skyfs_s8_t *buf, 
		skyfs_u32_t size)
{
	skyfs_s32_t rc = 0;
	skyfs_s8_t *tmp_buf = NULL;
    
	SKYFS_MSG("__skyfs_C_attach_writebuf:ino:%llu,size:%d\n", 
		writebuf->ino, size);
	tmp_buf = (skyfs_s8_t *)writebuf->buf + writebuf->count;
	memcpy(tmp_buf, buf, size);
	writebuf->count += size;

	tmp_buf = NULL;

	return rc;
}
                
skyfs_s32_t
__skyfs_C_submit_writebuf(skyfs_ino_t ino, 
		skyfs_u32_t conflict_index,
		const skyfs_s8_t *buf,
		skyfs_u64_t offset,
		skyfs_u32_t count)
{
	skyfs_s32_t rc = 0;
    skyfs_C_writebuf_t *writebuf = NULL;

	skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;

    skyfs_u32_t skyfs_profile_flag = 1;

	writebuf = (skyfs_C_writebuf_t *)malloc(sizeof(skyfs_C_writebuf_t));
	if(writebuf == NULL){
		rc = -errno;
		SKYFS_ERROR("__skyfs_C_submit_writebuf:alloc mem err:%d\n", errno);
		goto ERR;
	}

	bzero(writebuf, sizeof(skyfs_C_writebuf_t));
	writebuf->ino = ino;
	writebuf->conflict_index = conflict_index;
	writebuf->offset = offset;
	writebuf->count = count;
    
	__skyfs_get_starttime(&start_time, skyfs_profile_flag);
	memcpy(writebuf->buf, buf, count);
	__skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, "memcpy");

	list_add(&writebuf->list, &client_writebuf_head);

	client_writebuf_num ++;

	SKYFS_MSG("__skyfs_C_submit_writebuf:writebuf count:%d\n", client_writebuf_num);

ERR:
	return rc;
}

skyfs_s32_t
__skyfs_C_release_writebuf(skyfs_C_writebuf_t *writebuf)
{
	skyfs_s32_t rc = 0;

	SKYFS_MSG("__skyfs_C_release_writebuf:enter\n");
	list_del_init(&writebuf->list);
	client_writebuf_num --;
	SKYFS_MSG("__skyfs_C_release_writebuf:exit\n");

	return rc;
}

#if 0
__skyfs_prefetch_get_data(skyfs_ino_t ino, skyfs_u64_t offset, skyfs_u32_t count, char * readbuf)
{
	int rc = 0;
	int found_buffer = 0;
	int wait_times = 0;
        int wait_prefetch = 0;
        skyfs_C_gcompbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;
	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(gcompbuf_hash_base[hashval].head);
	hash_rwlock =  &(compbuf_hash_base[hashval].rwlock);
        pthread_rwlock_rdlock(hash_rwlock);
	// 2 find buffer and try to merge
re_get:
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_compbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_wrlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}
	if(found_buffer){
		uint64_t border = 0;
		char * start_pos = NULL;
		int match = -1;

		for(int i = 0 ; i<2 ; i++){
			border = tmp->bufs[i].start / gpu_write_buf_size * gpu_write_buf_size;
			if(offset >= border && (offset < (border + gpu_write_buf_size))){
				match = i;
				break;
			}

		}
		if(match >= 0){
			start_pos = (char*)tmp->bufs[match].buf + (offset % gpu_write_buf_size);
			memcpy(readbuf,start_pos, count);
			// clear prefetching
			tmp->bufs[match].is_prefetching = 0;
			rc = count;

		}else{
			if(tmp->bufs[match].is_prefetching ){
				wait_prefetch = 1;
				//wait_times++;
			}else if(tmp->bufs[match].is_prefetching == 0){
				//pthread_rwlock_unlock(data_rwlock);
				tmp->bufs[match].is_prefetching = 1;
                                wait_prefetch = 2; // should start prefetch
			}
					
		}
		
		pthread_rwlock_unlock(data_rwlock);


	}
	else{ // no buffer found

		// create a new one
		wait_prefetch = 2;
                
		pthread_rwlock_unlock(hash_rwlock);
	}
	if(wait_prefetch == 1 ){
		wait_times++;
		if(wait_times <10){
			usleep(1000);
			goto re_get;
		}
	}else if(wait_prefetch == 2){
		// 1 start a prefetch and wait
                // 2 re_get
                wait_times = 0;
		goto re_get;
	}
	return rc;

}
#endif 
skyfs_s32_t
__skyfs_C_read_from_gpu_write_buf(skyfs_ino_t ino, skyfs_u64_t offset, skyfs_u32_t count, char * readbuf, int do_prefetch)
{
	int rc = 0;
	
	char * start_pos = NULL;
	int found_buffer = 0;
	int wait_prefetch = 0, wait_times = 0;
        skyfs_C_gcompbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;
	size_t prefetch_offset = 0;
	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(gcompbuf_hash_base[hashval].head);
	hash_rwlock =  &(compbuf_hash_base[hashval].rwlock);
	
        pthread_rwlock_rdlock(hash_rwlock);
	// 2 find buffer and try to merge
re_read:
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_compbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_rdlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}
	int match = -1;
	// TODO always found now .....
	if(found_buffer){
		uint64_t border = 0;

		for(int i = 0 ; i<2 ; i++){
			pthread_rwlock_wrlock(&tmp->bufs[i].prefetch_rwlock);
			border = tmp->bufs[i].start / gpu_write_buf_size * gpu_write_buf_size;
			if(offset >= border && (offset < (border + gpu_write_buf_size))){
				match = i;
				if(!tmp->bufs[i].is_prefetching)
					wait_prefetch = 0;
				else	
					wait_prefetch = 1;
					
				pthread_rwlock_unlock(&tmp->bufs[i].prefetch_rwlock);
				break;
			}else if(  tmp->bufs[i].start == 0 &&  tmp->bufs[i].end == 0 && ! tmp->bufs[i].is_prefetching){
				// TODO: 需要补充，防止并发读。
				match = i;
				tmp->bufs[i].start = offset /  gpu_write_buf_size * gpu_write_buf_size;
				tmp->bufs[i].end = offset /gpu_write_buf_size * gpu_write_buf_size + gpu_write_buf_size;
				
				tmp->bufs[i].is_prefetching = 1;
				
				wait_prefetch = 2; // need to start prefetching
				prefetch_offset = border; 
				pthread_rwlock_unlock(&tmp->bufs[i].prefetch_rwlock);
				break;
			}
			pthread_rwlock_unlock(&tmp->bufs[i].prefetch_rwlock);

		}
		if(match >= 0 && ! wait_prefetch){
			pthread_rwlock_rdlock(&tmp->bufs[match].prefetch_rwlock);
			start_pos = (char*)tmp->bufs[match].buf + (offset % gpu_write_buf_size);
			memcpy(readbuf,start_pos, count);
			rc = count;
			if((offset + count )%gpu_write_buf_size == 0){
                         tmp->bufs[match].start = 0;
                         tmp->bufs[match].end = 0;
                     }

			pthread_rwlock_unlock(&tmp->bufs[match].prefetch_rwlock);

		}else{
			
			pthread_rwlock_unlock(data_rwlock);
		}


	}
	else{
		pthread_rwlock_unlock(hash_rwlock);
	}
 
	if (!do_prefetch || !wait_prefetch){
		goto end_read;
	}else{
		// now wait or start prefetch....
	   if(wait_prefetch == 2){
			// start prefetch and wait
			unsigned char * ctx = NULL;
			ctx = prefetch_init_ctx();
			if(ctx == NULL){
			  SKYFS_ERROR_1("can not alloc ctx for prefetching \n");
			  return rc;
			}
		
		
#if 0
	 // start prefetch....
	 int prefetch_cache(CacheContext *ctx, int thread_count, size_t start_foffset,
        int block_size, int block_count, int comp_type, uint32_t * ret_comp_type , uint32_t * comp_sizes,
        size_t * preal_foff, size_t * preal_fsizes)

		
#endif
	int thread_count = 4;
	size_t start_foffset = prefetch_offset;
	int block_size = 128*1024*1024;
	int block_count = gpu_write_buf_size/block_size;
	int comp_type = 1;
	uint32_t ret_comp_type[block_count];
	uint32_t comp_sizes[block_count];
	uint32_t preal_foff[block_count];
	uint32_t preal_fsizes[block_count];
	int gpu_comp = 0;

	rc = prefetch_cache(ctx, thread_count, start_foffset,
        		block_size, block_count, comp_type, ret_comp_type ,  comp_sizes,
        		preal_foff,  preal_fsizes);
	// now decompress it:
	//char * decomp_buf = malloc(block_size* block_count);
	//if(decomp_buf == NULL){
	//	SKYFS_ERROR_1("can not alloc buffer for decomp \n");
                // free ctx;
	//}
	     gpu_comp = random() % 1000;
             if(gpu_comp >= tmp->gpu_comp_ratio){
		 // cpu decomp
		 // 1 set decomp task
		 // 2 call run_decomp
		 // 3 deal with nocomp
                 // 4 calc total length
                 
		pthread_rwlock_wrlock(&tmp->bufs[match].prefetch_rwlock);
		   // decompress to bufs[match]
		   
		   // read the exact data 
		     start_pos = (char*)tmp->bufs[match].buf + (offset % gpu_write_buf_size);
		     memcpy(readbuf,start_pos, count);
		     rc = count;
		     tmp->bufs[match].is_prefetching = 0;
		     if((offset + count )%gpu_write_buf_size == 0){
		         tmp->bufs[match].start = 0;
		         tmp->bufs[match].end = 0;
		     }
		     
		     // memcpy()
		pthread_rwlock_wrlock(&tmp->bufs[match].prefetch_rwlock);
		//return rc;

	     } else{
		// gpu decomp
		 // 1 set decomp task
		 // 2 call run_decomp
		 // 3 deal with nocomp
                 // 4 calc total length
                 
		pthread_rwlock_wrlock(&tmp->bufs[match].prefetch_rwlock);
		   // decompress to bufs[match]
		   // read the exact data 
		    start_pos = (char*)tmp->bufs[match].buf + (offset % gpu_write_buf_size);
		    memcpy(readbuf,start_pos, count);
		     rc = count;
		     // memcpy()
		pthread_rwlock_wrlock(&tmp->bufs[match].prefetch_rwlock);
		//return rc;

	     } 
	     goto end_read;
		
	 // wait_prefetch == 2     
	}else{
			// wait and re_read
		usleep(5000); 
		wait_times ++;
		if(wait_times >= 1000){
			SKYFS_ERROR_1("too long time for waiting prefetch , offset %lu , count %u, times %u\n ", offset, count, wait_times);
			rc = 0;
			return rc;
		}
		goto  re_read;

		
	}
     } // end  if (!do_prefetch || !wait_prefetch) 
		
end_read:
	return rc;


}



skyfs_s32_t
__skyfs_C_read_from_write_buf(skyfs_ino_t ino, skyfs_u64_t offset, skyfs_u32_t count, char * readbuf)
{
	int rc = 0;
	int found_buffer = 0;
        skyfs_C_compbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;
	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(gcompbuf_hash_base[hashval].head);
	hash_rwlock =  &(compbuf_hash_base[hashval].rwlock);
        pthread_rwlock_rdlock(hash_rwlock);
	// 2 find buffer and try to merge
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_compbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_rdlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}
	if(found_buffer){
		uint64_t border = 0;
		char * start_pos = NULL;
		int match = -1;

		for(int i = 0 ; i<8 ; i++){
			border = (tmp->bufs[i].start / SKYFS_OBJECT_NODE_SIZE)*SKYFS_OBJECT_NODE_SIZE;
			if(offset >= border && offset < (border + SKYFS_OBJECT_NODE_SIZE)){
				match = i;
				break;
			}

		}
		if(match >= 0){
			start_pos = (char*)tmp->bufs[match].buf  + (offset % SKYFS_OBJECT_NODE_SIZE);
			memcpy(readbuf,start_pos, count);
			rc = count;

		}
		pthread_rwlock_unlock(data_rwlock);


	}
	else{
		pthread_rwlock_unlock(hash_rwlock);
	}
	return rc;


}


skyfs_u64_t
__skyfs_C_clean_submit_ino_gpu_write_bufs(skyfs_ino_t ino , int64_t * total_changed_space)
{
	int64_t  rc = 0;
	int found_buffer = 0;
        skyfs_C_gcompbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;
	
	size_t *comp_bufs_sizes;
        char *  comp_bufs_ptrs;
	size_t * sizes_ptr ;
        size_t comp_bufs_count = 0;
        //void * cpu_buf_obj = NULL;


	SKYFS_ERROR("clean submit_ino_gpu start, GPU_compress_cnt %lu, compress_time %llu\n", gpu_comp_cnt, gpu_comp_time);
	SKYFS_ERROR("clean submit_ino_gpu start, GPU_compress_subcnt %lu, compress_subtime %llu\n", gpu_comp_submit_cnt, gpu_comp_submit_time);

	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(gcompbuf_hash_base[hashval].head);
	hash_rwlock =  &(gcompbuf_hash_base[hashval].rwlock);
        pthread_rwlock_rdlock(hash_rwlock);
	// 2 find buffer and try to merge
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_gcompbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_wrlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}

	SKYFS_ERROR("clean submit_ino_gpu lookup buffer %d\n", found_buffer);
	if(found_buffer){
		uint64_t border = 0;
		char * start_pos = NULL;
		//int match = -1;
		uint64_t max_end = 0;
		int64_t tmp_changed_space = 0;
		int64_t acc_changed_space = 0;

		if(gpu_compress){

			// TODO: submit  all buffers to server 
			goto clean_it;

		}

		for(int i = 0 ; i<2 ; i++){
			if(tmp->bufs[i].start >= 0 && tmp->bufs[i].end > 0){
				// TODO : mayl call gpu compress
				// do submit
#if 1
				
				size_t total_start = tmp->bufs[i].start;
				int comp_type = tmp->bufs[i].comp_type;
				int current_size =  SKYFS_OBJECT_NODE_SIZE;
				char * start_buf = tmp->bufs[i].buf;
				char * * real_comp_bufs_ptrs ;

				uint64_t dram_addr = (uint64_t)(&tmp->bufs[i].comp_buf[i]);
				uint64_t gpu_addr = (uint64_t)tmp->bufs[i].comp_dev_buf;
				uint64_t addr_diff = dram_addr-gpu_addr;
				SKYFS_ERROR("compress and submit buf[%d]\n", i);
				start_buf += (tmp->bufs[i].start % SKYFS_GPU_COMPRESS_BUF_SIZE);


				SKYFS_ERROR("clean submit_ino_gpu start GPU compress\n");
                                rc = __skyfs_C2O_gpu_compress_bufs(
						comp_type,
						&tmp->bufs[i],
						//tmp->bufs[i].buf,
						start_buf, 
						(tmp->bufs[i].end - tmp->bufs[i].start +1),
                                                &comp_bufs_count, // comp_count
                                                &sizes_ptr,
                                                &comp_bufs_ptrs
                                                );
				SKYFS_ERROR("clean submit_ino_gpu end GPU compress\n");

                                if(rc <0){
					SKYFS_ERROR("GPU compress failed when clean write buf\n");
                                        goto clean_it;
                                }
				gpu_addr = (uint64_t)tmp->bufs[i].comp_dev_buf;
				addr_diff = dram_addr-gpu_addr;

				SKYFS_ERROR("clean submit_ino_gpu  GPU compress, return %d, comp_bufs_count %d\n", rc, comp_bufs_count);
				// TODO : 
				//
				real_comp_bufs_ptrs = (char  * * )comp_bufs_ptrs;
				if(current_size > (tmp->bufs[i].end - total_start +1)){
					current_size = (tmp->bufs[i].end - total_start+1);
				}
				comp_bufs_sizes = sizes_ptr;
				//
				for(int n = 0 ; n<comp_bufs_count; n++){
					
					//SKYFS_ERROR_1("start clean C2O_submit_gpu_compbuf %d\n", n);
					char *  real_ptr = (char*)real_comp_bufs_ptrs[n] ;
					real_ptr  += addr_diff;
					
					if(comp_bufs_sizes[n] >=  current_size){
						comp_bufs_sizes[n] = current_size;
						SKYFS_ERROR_1("bigger compress data found when close file, copy original data\n");
						memcpy(real_ptr, (char*)(tmp->bufs[i].buf) + total_start, current_size);
						
					}				

					SKYFS_ERROR("start call clean C2O_submit_gpu_compbuf %d , real_ptr %p , comp_buf %p gpu_addr %p, addr_diff %lx\n", 
						n, real_ptr, dram_addr, gpu_addr, addr_diff );
					 rc = __skyfs_C2O_submit_gpu_compbuf(tmp->ino,
                                                //comp_bufs_ptrs[n],
						real_ptr,
                                                total_start,
                                                current_size,
                                                comp_bufs_sizes[n],
                                                comp_type,
                                                &tmp_changed_space);
					
					SKYFS_ERROR("end clean C2O_submit_gpu_compbuf %d, total %d, rc %d\n", n, comp_bufs_count , rc );
                                        if(rc <= 0){

						SKYFS_ERROR_1("GPU compress failed when submit clean write buf[%d]\n", n);
                                                break;
                                        }
					SKYFS_ERROR("gpu clean net req [%d], start_off %llu, size %llu, compressed size %llu , return %d\n",
							n, total_start, current_size,  comp_bufs_sizes[n], rc);
					current_size =  SKYFS_OBJECT_NODE_SIZE;
					if(current_size > (tmp->bufs[i].end - total_start +1)){
                                        	current_size = (tmp->bufs[i].end - total_start+1);
                                	}
					total_start += current_size;
					//TODO mayl to verify this logic !
					acc_changed_space = acc_changed_space + tmp_changed_space;

					if(max_end <  tmp->bufs[i].start+rc){
						max_end = tmp->bufs[i].start+rc;
					}
					
					// TODO  临时措施，仅buf边界地址清0
					tmp->bufs[i].start = 0;
					tmp->bufs[i].end = 0;
				
				}

				SKYFS_ERROR("End compress and submit buf[%d]\n", i);



#endif 
#if 0
				SKYFS_ERROR_1("clear cuda prameters for ino %llu, buf num %d, dev_id %d, dev_tmp_size %lu\n",
						tmp->ino, i, tmp->bufs[i].dev_id, tmp->bufs[i].d_size);

 				select_cuda_device(tmp->bufs[i].dev_id);
				if(tmp->bufs[i].uncomp_dev_buf != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_buf);
				}
				if(tmp->bufs[i].comp_dev_buf != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_buf);
				}
				if(tmp->bufs[i].uncomp_dev_ptrs != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_ptrs);
				}
				if(tmp->bufs[i].uncomp_dev_sizes != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_sizes);
				}
				if(tmp->bufs[i].comp_dev_ptrs != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_ptrs);
				}
				if(tmp->bufs[i].comp_dev_sizes != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_sizes);
				}
				if(tmp->bufs[i].d_temp != NULL){
					free_cuda_memory(tmp->bufs[i].d_temp);
				}
				destroy_cuda_treams(tmp->bufs[i].dev_streams, 5);

#endif
				
				
                                // release buf_obj;
                                //if(cpu_buf_obj){
                                  //       free_cpu_batch(cpu_buf_obj);
                                //}


				SKYFS_ERROR("submit clean ino %llu write buf , i= %d, offset %llu, count %llu, last rc %llu\n", 
						tmp->ino,i,   tmp->bufs[i].start, tmp->bufs[i].end -  tmp->bufs[i].start+1, rc );
#if 0
				rc = __skyfs_C2O_submit_compbuf(tmp->ino,
                		     tmp->bufs[i].buf,
				     tmp->bufs[i].start,
				     (tmp->bufs[i].end - tmp->bufs[i].start +1),
				     tmp->bufs[i].comp_type,
				     &tmp_changed_space
                		);
#endif
				if(rc <= 0 ){
					continue;
				}

				//acc_changed_space = acc_changed_space + tmp_changed_space;
			        	

			} // end if (tmp->start...)
			// clear this buf[n] parameters
#if 1
			        SKYFS_ERROR("clear cuda prameters for ino %llu, buf num %d, dev_id %d, dev_tmp_size %lu\n",
						tmp->ino, i, tmp->bufs[i].dev_id, tmp->bufs[i].d_size);

			if(1){
				// TODO: 临时措施， 关闭文件不释放资源
				if( tmp->bufs[i].prop_buf != NULL){
					free(tmp->bufs[i].prop_buf);
					tmp->bufs[i].prop_buf=NULL;
				}

 				select_cuda_device(tmp->bufs[i].dev_id);
				SKYFS_ERROR("1 try free uncomp_dev_buf for buf  %d\n", i);
				if(tmp->bufs[i].uncomp_dev_buf != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_buf);
				}
				SKYFS_ERROR("2 try free comp_dev_buf for buf   %d\n", i);
				if(tmp->bufs[i].comp_dev_buf != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_buf);
				}
				SKYFS_ERROR("3 try free uncomp_dev_ptrs for buf %d\n", i);
				if(tmp->bufs[i].uncomp_dev_ptrs != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_ptrs);
				}
				SKYFS_ERROR("4 try free uncomp_dev_sizes_buf for buf %d\n", i);
				if(tmp->bufs[i].uncomp_dev_sizes != NULL){
					free_cuda_memory(tmp->bufs[i].uncomp_dev_sizes);
				}
				SKYFS_ERROR("5 try free comp_dev_ptrs_buf for buf %d\n", i);
				if(tmp->bufs[i].comp_dev_ptrs != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_ptrs);
				}

				SKYFS_ERROR("6 try free comp_dev_sizes_buf for buf %d\n", i);
				if(tmp->bufs[i].comp_dev_sizes != NULL){
					free_cuda_memory(tmp->bufs[i].comp_dev_sizes);
				}
				SKYFS_ERROR("7 try free dev_temp_buf for buf %d\n", i);
				if(tmp->bufs[i].d_temp != NULL){
					free_cuda_memory(tmp->bufs[i].d_temp);
				}
				SKYFS_ERROR("8 try free dev_stream for buf %d\n", i);
				destroy_cuda_streams(tmp->bufs[i].dev_streams, 2);
				SKYFS_ERROR("9 end free dev_stream for buf %d\n", i);
			    }
#endif

		} // end for i
clean_it:

		SKYFS_ERROR("10 try clean inode temp buf\n");
		* total_changed_space = acc_changed_space;
		rc = max_end;
		SKYFS_ERROR("submit clean ino %llu write buf , i= %d, offset %llu, count %llu, last rc %llu\n", 
						tmp->ino,i,   tmp->bufs[i].start, tmp->bufs[i].end -  tmp->bufs[i].start+1, rc );

		pthread_rwlock_wrlock(hash_rwlock);
		if(0){
		// TODO : 临时措施，关文件不释放资源
		list_del(&tmp->buflist);
		pthread_rwlock_unlock(hash_rwlock);
		unregister_host_data((void*)tmp);
		free (tmp);
		}

		SKYFS_ERROR("11 end clean inode temp buf\n");
		//pthread_rwlock_unlock(data_rwlock);


	}else{
		pthread_rwlock_unlock(hash_rwlock);
	}

	// TODO : SHOULD clean this write buffer node 
	
	        //pthread_rwlock_wrlock(hash_rwlock);

	return rc;


}


/* return value of this function below is the max end for these write bufs */
skyfs_u64_t
__skyfs_C_clean_submit_ino_write_bufs(skyfs_ino_t ino , int64_t * total_changed_space)
{
	int64_t  rc = 0;
	int found_buffer = 0;
        skyfs_C_compbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;

	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(compbuf_hash_base[hashval].head);
	hash_rwlock =  &(compbuf_hash_base[hashval].rwlock);
        pthread_rwlock_rdlock(hash_rwlock);
	// 2 find buffer and try to merge
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_compbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_wrlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}
	if(found_buffer){
		uint64_t border = 0;
		char * start_pos = NULL;
		int match = -1;
		uint64_t max_end = 0;
		int64_t tmp_changed_space = 0;
		int64_t acc_changed_space = 0;

		if(gpu_compress){

			// TODO: submit  all buffers to server 
			goto clean_it;

		}

		for(int i = 0 ; i<8 ; i++){
			if(tmp->bufs[i].start >= 0 && tmp->bufs[i].end > 0){
				// do submit
				SKYFS_ERROR("submit clean ino %llu write buf , i= %d, offset %llu, count %llu, last rc %llu\n", 
						tmp->ino,i,   tmp->bufs[i].start, tmp->bufs[i].end -  tmp->bufs[i].start+1, rc );
				rc = __skyfs_C2O_submit_compbuf(tmp->ino,
                		     tmp->bufs[i].buf,
				     tmp->bufs[i].start,
				     (tmp->bufs[i].end - tmp->bufs[i].start +1),
				     tmp->bufs[i].comp_type,
				     &tmp_changed_space
                		);
				if(rc <= 0 ){
					continue;
				}
				if(max_end <  tmp->bufs[i].start+rc){
					max_end =  tmp->bufs[i].start+rc;
					SKYFS_ERROR_1("clean rc = %d, max_end =  %d, start = %llu , end %llu\n", rc, max_end,  tmp->bufs[i].start, tmp->bufs[i].end);
				}
				acc_changed_space = acc_changed_space + tmp_changed_space;
			        	

			}		

		}
clean_it:
		* total_changed_space = acc_changed_space;
		rc = max_end;
		pthread_rwlock_wrlock(hash_rwlock);
		list_del(&tmp->buflist);
		pthread_rwlock_unlock(hash_rwlock);
		free (tmp);

		//pthread_rwlock_unlock(data_rwlock);


	}else{
		pthread_rwlock_unlock(hash_rwlock);
	}

	// TODO : SHOULD clean this write buffer node 
	
	        //pthread_rwlock_wrlock(hash_rwlock);

	return rc;


}


/* find the proper data buf, merge the data to it return 0
 * if the buffer full , send to OSD and return submit count , >0  or -EIO, when send faild;
 * if can not find proper buffer, do nothing , just return -ENOMEM 
 * */
skyfs_s32_t 
__skyfs_C_place_submit_writebuf(skyfs_ino_t ino, skyfs_u64_t offset, 
		skyfs_u32_t count, char * databuf, int comp_type, int64_t * changed_space)
{
	int rc = 0;
	int found_buffer = 0;
        skyfs_C_compbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;
	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(compbuf_hash_base[hashval].head);
	hash_rwlock =  &(compbuf_hash_base[hashval].rwlock);
	SKYFS_ERROR("try to lock cache hash tab for ino %llu, comp_type %d\n", ino, comp_type);
        pthread_rwlock_rdlock(hash_rwlock);
	SKYFS_ERROR("got lock cache hash tab for ino %llu\n", ino);
	// 2 find buffer and try to merge
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_compbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_wrlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}

	SKYFS_ERROR("afteriteration cache hash tab for ino %llu\n", ino);
	if(found_buffer){
		SKYFS_ERROR("client wr cache found ino %llu, offset %llu,\n", ino, offset);
		int first_free = -1;
                int match = -1;
                uint64_t border = 0;
		char * start_pos = NULL;
                for(int i = 0; i<8; i++){
                        if(tmp->bufs[i].start == 0 && tmp->bufs[i].end == 0 ){
				if(first_free <0)
                                	first_free = i;
				continue;
                        }
                        border = (tmp->bufs[i].start / SKYFS_OBJECT_NODE_SIZE)*SKYFS_OBJECT_NODE_SIZE;
			if(offset >= border && offset < (border + SKYFS_OBJECT_NODE_SIZE)){
				match = i;
				break;
			}

                }


		if(match >= 0){

			
			
			// 1 merge the buffer
			tmp->bufs[match].comp_type = comp_type;
			if(tmp->bufs[match].start > offset)
				tmp->bufs[match].start = offset;
			if(tmp->bufs[match].end < (offset+count-1))
				tmp->bufs[match].end = (offset+count-1);
			start_pos = tmp->bufs[match].buf + (offset % SKYFS_OBJECT_NODE_SIZE);
			memcpy(start_pos, databuf, count);
			SKYFS_ERROR("client wr cache merged, ino %llu, offset %llu, end %llu\n", ino, offset, tmp->bufs[match].end);
			//rc = 2;
			// 2 if buffer_full ,  compress and send to OSD
			if(tmp->bufs[match].end - tmp->bufs[match].start +1 == SKYFS_OBJECT_NODE_SIZE && comp_type != COMPRESS_GZSTD_ALGORITHM){
       
				// TODO
				//

				SKYFS_ERROR("client submit ino %llu write buf , i= %d, offset %llu, count %llu, last rc %llu\n", 
						tmp->ino,match,   tmp->bufs[match].start,  SKYFS_OBJECT_NODE_SIZE, rc );
				rc = __skyfs_C2O_submit_compbuf(tmp->ino,
                		     tmp->bufs[match].buf,
				     tmp->bufs[match].start,
				     SKYFS_OBJECT_NODE_SIZE,
				     comp_type,
				     changed_space
                		);


				SKYFS_ERROR("client Submit  ino %llu write buf , i= %d, offset %llu, count %llu, result  rc %llu\n", 
						tmp->ino,match,   tmp->bufs[match].start,  SKYFS_OBJECT_NODE_SIZE, rc );
				if(rc < SKYFS_OBJECT_NODE_SIZE)
					rc = -EIO;
				// clear this buf
				tmp->bufs[match].start = 0;
				tmp->bufs[match].end = 0;

			}else if(comp_type ==  COMPRESS_GZSTD_ALGORITHM ){
				// check if (add )
				
			}
			pthread_rwlock_unlock(data_rwlock);
			goto EXIT;

		}else if(first_free >= 0){
			SKYFS_ERROR("client wr cache new, ino %llu, offset %llu,\n", ino, offset);
			// use the first_free entry

			int min_submit_entry = first_free;
			SKYFS_ERROR("client wr cache new, ino %llu, offset %llu,\n", ino, offset);
			// select the minium used buf 
			for(int buf_idx = 0; buf_idx <8; buf_idx++){
				if(tmp->bufs[buf_idx].start == 0 && tmp->bufs[buf_idx].end == 0){
					if(tmp->bufs[buf_idx].submit_cnt < tmp->bufs[min_submit_entry].submit_cnt )
						min_submit_entry = buf_idx;

				}
			}
			first_free = min_submit_entry;

			match = first_free;
			tmp->bufs[match].comp_type = comp_type;
			tmp->bufs[match].start = offset;
			tmp->bufs[match].end = (offset+count-1);
			start_pos = tmp->bufs[match].buf + (offset % SKYFS_OBJECT_NODE_SIZE);
			memcpy(start_pos, databuf, count);
			rc = 0; // merged in  proper buf
			pthread_rwlock_unlock(data_rwlock);
			goto EXIT;


		}else{
			rc = -ENOMEM; // no match and no free buf
			pthread_rwlock_unlock(data_rwlock);
			goto EXIT;
		}


		// try to merge or do sedn buffer
	}else{
		// alloc a new inode buffer and insert it to the hash list


		uint32_t buf_offset =(uint32_t)(offset % SKYFS_OBJECT_NODE_SIZE);
		
		skyfs_C_compbuf_unit_t * new_compbuf = NULL;

		pthread_rwlock_unlock(hash_rwlock);
		new_compbuf = (skyfs_C_compbuf_unit_t *)calloc(1, sizeof(skyfs_C_compbuf_unit_t ));
		if(new_compbuf == NULL){
			SKYFS_ERROR_1("can not alloc comp buffer for new ino %llu\n", ino);
			rc = -ENOMEM; // should send data to osd
			goto unlock_hash_list;
		}
		new_compbuf->ino = ino;
		pthread_rwlock_init(&new_compbuf->rw_lock, NULL);
		new_compbuf->bufs[0].start = offset;
		new_compbuf->bufs[0].end = offset + count-1;
		new_compbuf->bufs[0].comp_type = comp_type;
		INIT_LIST_HEAD(&new_compbuf->buflist);

		//memset(&(new_compbuf->bufs[0].buf[0]), 0 , SKYFS_OBJECT_NODE_SIZE);
		memcpy((char*)(new_compbuf->bufs[0].buf)+buf_offset, databuf, count);
		pthread_rwlock_wrlock(hash_rwlock);
		list_add_tail(&new_compbuf->buflist, head);
		SKYFS_ERROR("client wr cache created, ino %llu, offset %llu,\n", ino, offset);
		rc = 0; // add a free 


		pthread_rwlock_unlock(hash_rwlock);

unlock_hash_list:
		SKYFS_ERROR("cache create failed \n");
	}
        	




EXIT:        	
	
	return rc;
}

void 
adjust_comp_ratio(skyfs_C_gcompbuf_unit_t * tmp){
	uint64_t gpu_comp_bw = 0;
	uint64_t cpu_comp_bw = 0;
	uint64_t curr_ratio = 0;
	
	if(all_gpu_comp_ratio != 500)
		return;

	if(tmp->gpu_comp_bytes != 0 && tmp->gpu_comp_time != 0)
		gpu_comp_bw = tmp->gpu_comp_bytes / tmp->gpu_comp_time; //MB/s
	if(tmp->cpu_comp_bytes != 0 && tmp->cpu_comp_time)
		cpu_comp_bw = tmp->cpu_comp_bytes / tmp->cpu_comp_time; //
	
	curr_ratio = gpu_comp_bw*1000/(gpu_comp_bw + cpu_comp_bw);
	tmp->gpu_comp_ratio = curr_ratio;
	// tmp changed by mayl
	return ;


	if(gpu_comp_bw > cpu_comp_bw){
		//tmp->gpu_comp_ratio +=50;
		tmp->gpu_comp_ratio =1001;
		if(tmp->gpu_comp_ratio > 1000)
			tmp->gpu_comp_ratio = 1000; 
		SKYFS_ERROR_1("tune to gpu compressing, gpu bw %lu MB, cpu bw %lu MB , current ratio %d\n   ",
			gpu_comp_bw, cpu_comp_bw, tmp->gpu_comp_ratio);
	}
	if(gpu_comp_bw < cpu_comp_bw){

		//tmp->gpu_comp_ratio -=50;
		tmp->gpu_comp_ratio = 0;
		if(tmp->gpu_comp_ratio < 0)
			tmp->gpu_comp_ratio = 0; 

		SKYFS_ERROR_1("tune to cpu compressing, gpu bw %lu MB, cpu bw %lu  MB , current ratio %d\n   ",
			gpu_comp_bw, cpu_comp_bw, tmp->gpu_comp_ratio);
	}

									
}

skyfs_s32_t 
__skyfs_C_place_submit_gpu_writebuf(skyfs_ino_t ino, skyfs_u64_t offset, 
		skyfs_u32_t count, char * databuf, int comp_type, int64_t * changed_space)
{
	int rc = 0;
	char * d_temp = NULL;
	uint64_t total_rc = 0;
	int found_buffer = 0;
        skyfs_C_gcompbuf_unit_t * tmp;
	struct list_head * head, *pos ;
	pthread_rwlock_t * hash_rwlock;
	pthread_rwlock_t * data_rwlock;

	struct timeval t_start;

	size_t *comp_bufs_sizes;
	int cpu_compress = 0;
        char *  comp_bufs_ptrs;
	char * * real_comp_bufs_ptrs = NULL;
	size_t * sizes_ptr ;
        size_t comp_bufs_count = 0;
	int multi_submit = 1;
        //void * cpu_buf_obj = NULL;

	//1 find the hashtab list and lock it
	int hashval = ino % SKYFS_DL_PARTITION_HASH_LEN;
	head = &(gcompbuf_hash_base[hashval].head);
	hash_rwlock =  &(gcompbuf_hash_base[hashval].rwlock);
	gettimeofday(&t_start, NULL);
	SKYFS_ERROR("try to lock cache hash tab for ino %llu, comp_type %d\n", ino, comp_type);
        pthread_rwlock_rdlock(hash_rwlock);
	SKYFS_ERROR("got lock cache hash tab for ino %llu, for place_submit_Gpu_writebuf offset %llu, count %llu \n", ino, offset ,count);
	// 2 find buffer and try to merge
	list_for_each(pos, head){
		tmp = list_entry(pos, skyfs_C_gcompbuf_unit_t, buflist);
		if(tmp->ino == ino){
			found_buffer =1;
			data_rwlock = &tmp->rw_lock;
			pthread_rwlock_wrlock(data_rwlock);
			//pthread_rwlock_unlock(hash_rwlock);
			break;
		}

	}

	SKYFS_ERROR("afteriteration cache hash tab for ino %llu, found buf %d \n", ino, found_buffer);
	if(found_buffer){
		SKYFS_ERROR("client wr cache found ino %llu, offset %llu,\n", ino, offset);
		int first_free = -1;
                int match = -1;
                uint64_t border = 0;
		char * start_pos = NULL;
                for(int i = 0; i<2; i++){
                        if(tmp->bufs[i].start == 0 && tmp->bufs[i].end == 0 ){
				if(first_free <0)
                                	first_free = i;
				continue;
                        }
                        border = (tmp->bufs[i].start / gpu_write_buf_size)*(gpu_write_buf_size);
			if(offset >= border && offset < (border + gpu_write_buf_size)){
				match = i;
				break;
			}

                }


		if(match >= 0){

			
			
			// 1 merge the buffer
			tmp->bufs[match].comp_type = comp_type;
			if(tmp->bufs[match].start > offset)
				tmp->bufs[match].start = offset;
			if(tmp->bufs[match].end < (offset+count-1))
				tmp->bufs[match].end = (offset+count-1);
			start_pos = tmp->bufs[match].buf + (offset % (gpu_write_buf_size));
			memcpy(start_pos, databuf, count);
			SKYFS_ERROR("client wr Gpu cache merged, ino %llu, offset %llu,\n", ino, offset);
			//rc = 2;
			// 2 if buffer_full ,  compress and send to OSD
			if(tmp->bufs[match].end - tmp->bufs[match].start +1 == (gpu_write_buf_size) ){
       
				// TODO: compresse data first
				//

				//size_t comp_bufs_sizes[260];
				
				//char *  comp_bufs_ptrs[260];
				//size_t comp_bufs_count = 0;
				//void * cpu_buf_obj = NULL;
				//size_t * sizes_ptr = & comp_bufs_sizes[0];
				cpu_compress = 1;
				struct timeval t1, t2;

				struct timeval t3, t4;
				uint64_t Gpu_comp_time = 0;
				int Gpu_task_index = 0;
				int Gpu_task_num = ((int)random())%64;

				
				char *  cpu_comp_buf_ptr[SKYFS_GPU_COMPRESS_BUF_SIZE /SKYFS_OBJECT_NODE_SIZE];
				size_t ret_blk_sizes[SKYFS_GPU_COMPRESS_BUF_SIZE /SKYFS_OBJECT_NODE_SIZE];

				// TODO: only changed here , just for test , will be changed back by mayl
				//tmp->gpu_comp_ratio = 1000;
				if(((tmp->comp_op_count % 5)==0)&& (tmp->comp_op_count >= 5)){
					adjust_comp_ratio(tmp);
					
				}

			
				// mayl: TODO do sharpening 
				/*

				if(tmp->gpu_comp_ratio <= 250){
					tmp->gpu_comp_ratio = tmp->gpu_comp_ratio/100;
				}else if(tmp->gpu_comp_ratio >=800){
					//tmp->gpu_comp_ratio = (998+tmp->gpu_comp_ratio/500);
				}
				if(tmp->gpu_comp_ratio <=0){
					tmp->gpu_comp_ratio = 1;
				}*/
				
				int do_prop = enable_prop;
				if(((int)(random()%1000))> tmp->gpu_comp_ratio | tmp->gpu_comp_ratio == 0){


					
					compress_task_t curr_task;
					uint64_t comp_time = 0;
					gettimeofday(&t1,NULL);
					if( tmp->bufs[match].prop_buf == NULL){
						tmp->bufs[match].prop_buf = malloc(SKYFS_GPU_COMPRESS_BUF_SIZE);
					}

					if(do_prop){
						 size_t num_doubles = SKYFS_GPU_COMPRESS_BUF_SIZE / sizeof(double);
						 //do_pre_split_v0((double *)tmp->bufs[match].buf, num_doubles , tmp->bufs[match].prop_buf);
					}

					memset(&curr_task, 0, sizeof(compress_task_t));
					curr_task.ino = tmp->ino;
					curr_task.op_type = 1 ; // just zstd
					curr_task.uncompress_blk_size = SKYFS_OBJECT_NODE_SIZE;
					curr_task.compress_blk_size = SKYFS_OBJECT_NODE_SIZE*15/9;
					curr_task.compress_buf = tmp->bufs[match].comp_buf; 
					curr_task.uncompress_buf = tmp->bufs[match].buf;
					curr_task.need_prop = 0;

					if(do_prop){
						curr_task.prop_buf = tmp->bufs[match].prop_buf;
						curr_task.ori_size = SKYFS_GPU_COMPRESS_BUF_SIZE;
						curr_task.need_prop = 0x10;
						SKYFS_ERROR_1("cache do_prop %d\n", do_prop);
					} 
					curr_task.blk_cnt = SKYFS_GPU_COMPRESS_BUF_SIZE /SKYFS_OBJECT_NODE_SIZE;
					curr_task.compressed_chunk_sizes = &(ret_blk_sizes[0]);
					curr_task.compress_buf_ptrs = &(cpu_comp_buf_ptr[0]);
					
					
					tmp->comp_op_count++;

					// CPU compress and multiple submit

					 rc = run_multithead_compress(&curr_task);
					 if(rc <0)
						 goto submit_end;
					 SKYFS_ERROR_1("after cpu compress , First data are [%x: %x : %x : %x \n]",
					   curr_task.compress_buf[0],
                                           curr_task.compress_buf[1],
					   curr_task.compress_buf[2],
					   curr_task.compress_buf[3]);
					  
					SKYFS_ERROR_1("after cpu compress , First sizes are [%lld: %lld : %lld : %lld \n]",
					   curr_task.compressed_chunk_sizes[0],
                                           
					   curr_task.compressed_chunk_sizes[1],
						
					   curr_task.compressed_chunk_sizes[2],

					 
					   curr_task.compressed_chunk_sizes[3]);
 
					gettimeofday(&t2,NULL);
					comp_time += (t2.tv_sec*1000000 + t2.tv_usec);
					comp_time -= (t1.tv_sec*1000000 + t1.tv_usec);
					tmp->cpu_comp_time += comp_time;
					tmp->cpu_comp_bytes += SKYFS_GPU_COMPRESS_BUF_SIZE;
					
					cpu_compress_time += comp_time;
					cpu_compress_bytes += SKYFS_GPU_COMPRESS_BUF_SIZE;
					SKYFS_ERROR("CPu compress %lu us,  %lu bytes\n",
							cpu_compress_time, cpu_compress_bytes);

					 cpu_compress = 1;
					 comp_bufs_count = rc;
					 // TODO : multiple submit
					 					 

					if(0){
					  uint8_t*  ori_data = malloc(SKYFS_GPU_COMPRESS_BUF_SIZE);
					  size_t num_doubles = SKYFS_GPU_COMPRESS_BUF_SIZE / sizeof(double);
					  do_post_merge_v0(tmp->bufs[match].prop_buf, num_doubles, ori_data );
					  if(memcmp(ori_data, tmp->bufs[match].buf,  SKYFS_GPU_COMPRESS_BUF_SIZE)){
						SKYFS_ERROR_1("Prop v0 compare failed at serial %d !!!\n", tmp->comp_op_count );
					  }
					  free(ori_data);
					   
					}
					goto start_submit;
				}
				
				SKYFS_ERROR("start client submit gpu compress  ino %llu write buf , i= %d, offset %llu, count %llu, last rc %llu, sizes_ptr %p \n", 
						tmp->ino,match,   tmp->bufs[match].start,  SKYFS_OBJECT_NODE_SIZE, rc , sizes_ptr);
				gettimeofday(&t1, NULL);

				cpu_compress = 0;
				//Gpu_task_index = lock_gpu_comp_task(Gpu_task_num);
				rc = __skyfs_C2O_gpu_compress_bufs(comp_type, 
						&tmp->bufs[match],
						tmp->bufs[match].buf, 
						(gpu_write_buf_size),
						&comp_bufs_count, // comp_count
					       	&sizes_ptr,
						&comp_bufs_ptrs
						);
				

				//unlock_gpu_comp_task(Gpu_task_index);

				gettimeofday(&t2, NULL);
				Gpu_comp_time += (t2.tv_sec*1000000 + t2.tv_usec);
				Gpu_comp_time -= (t1.tv_sec*1000000 + t1.tv_usec);
				gpu_comp_cnt ++;
				tmp->gpu_comp_time += Gpu_comp_time;
				tmp->gpu_comp_bytes += SKYFS_GPU_COMPRESS_BUF_SIZE;
				tmp->comp_op_count++;


				gpu_compress_time += Gpu_comp_time;
				gpu_compress_bytes += SKYFS_GPU_COMPRESS_BUF_SIZE;

				SKYFS_ERROR("GPU compress %lu bufs, Used time %lu us, pre_time %lu us, compressing time %lu us, post time %lu us\n",
						comp_bufs_count, Gpu_comp_time, sizes_ptr[comp_bufs_count +2], sizes_ptr[comp_bufs_count +3],
						sizes_ptr[comp_bufs_count+4]);


				
				SKYFS_ERROR("after gpu compress ,submit ino %llu write buf , i= %d, offset %llu, comp_buf count %llu, last rc %d, first comp size %u, comp sizes ptr %p\n", 
						tmp->ino, match, tmp->bufs[match].start,  comp_bufs_count , rc ,comp_bufs_sizes[0] , sizes_ptr);
				if(rc <0){
					goto submit_end;
				}

start_submit:
				if(!cpu_compress)
					comp_bufs_sizes = sizes_ptr;
				else
					comp_bufs_sizes = &ret_blk_sizes[0];
				tmp->bufs[match].submit_cnt++;


				// now submit all comp_bufs
				size_t start = tmp->bufs[match].start;
				int64_t cur_changed_space = 0;
				int64_t total_changed_space = 0;
				uint64_t dram_addr = (uint64_t)(&tmp->bufs[match].comp_buf[0]);
				uint64_t gpu_addr = (uint64_t)tmp->bufs[match].comp_dev_buf;
				uint64_t addr_diff = dram_addr-gpu_addr;
				if(cpu_compress){
					addr_diff = 0;
				}
			        real_comp_bufs_ptrs = (char * * )comp_bufs_ptrs;
				if(cpu_compress){
					//TODO , cpu_comp_buf_ptr
					real_comp_bufs_ptrs = (char **) (cpu_comp_buf_ptr);
				}	
				SKYFS_ERROR("comp_dram_addr %lx, comp_gpu_addr %lx, addr_diff %lx, comp_buf[0] %p\n",
						dram_addr, gpu_addr, addr_diff, real_comp_bufs_ptrs[0]);
				
				gettimeofday(&t3, NULL);
				multi_submit = 1;
				if(multi_submit){
					//TODO: mayl, do mult_isumbit
#if 0
__skyfs_C2O_submit_multiple_gpu_compbuf(skyfs_ino_t ino,
		const skyfs_u8_t * * buf_ptr,
                skyfs_u64_t *  offset_ptr,
                skyfs_u32_t * size_ptr,
		size_t * gpu_comp_size_ptr,
		int buf_cnt,
		int comp_type,
		uint64_t addr_diff,
		skyfs_s64_t * changed_space
		)

#endif					
					size_t tmp_size = SKYFS_OBJECT_NODE_SIZE;
					size_t comp_submit_size = 0;
					int submit_count = 0;
					int this_submit = 0;
					int over_count = 0;
					int old_compress_size = 0;


					while(submit_count < comp_bufs_count){
						comp_submit_size = 0;
					        this_submit = 0;
						over_count = 0;
						for(int n = 0 ; n<32; n++ ){
							if((submit_count+n) >= comp_bufs_count){
								SKYFS_ERROR("count over: comp bufs count : %lu,  submit_count, %lu, index %lu\n",
										comp_bufs_count, submit_count, n);
								this_submit = n;
								over_count = 1;
								break;
							}


							if(!cpu_compress && comp_bufs_sizes[submit_count+n] >= SKYFS_OBJECT_NODE_SIZE){
								 comp_bufs_sizes[submit_count+n] =  SKYFS_OBJECT_NODE_SIZE;
								 SKYFS_ERROR_1("GPU compress find bigger result, copy original \n");
								 //copy original
						
                                                                 char * temp_comp_ptr =(char*) (real_comp_bufs_ptrs[submit_count+n])+addr_diff;
								 memcpy(temp_comp_ptr,  (char *)tmp->bufs[match].buf + (submit_count+n)*SKYFS_OBJECT_NODE_SIZE,
										SKYFS_OBJECT_NODE_SIZE);	
			
							}
							
							old_compress_size = comp_submit_size;
							comp_submit_size += comp_bufs_sizes[submit_count+n];
							if(comp_submit_size > (2*1024*1024)){
								SKYFS_ERROR("over size: old_comp_size %lu, cur_comp_size %lu , index %d comp_buf_count %lu \n ",
										old_compress_size, comp_submit_size, n, comp_bufs_count);
								comp_submit_size -= comp_bufs_sizes[submit_count+n];
								this_submit = n;
								over_count = 2;
								break;
							}
							
						}
						if(!over_count)
							this_submit = 32;

						SKYFS_ERROR("submit_count %d, this_submit %d comp_submit_size %ld, start %lu, over_count %d, cpu_comp %d\n", 
								submit_count, this_submit, comp_submit_size , start, over_count, cpu_compress);
						if(this_submit == 0){
							SKYFS_ERROR("compress data[%d] too large %d, comp_bufs_count %d\n",
									this_submit+submit_count,  comp_bufs_sizes[submit_count+this_submit], comp_bufs_count );

							break;
						}
					   

#if 0	
					    if((submit_count + 32) <= comp_bufs_count )
						    this_submit = 32;
					    else
						    this_submit = comp_bufs_count - submit_count;
#endif


					    rc = __skyfs_C2O_submit_multiple_gpu_compbuf(tmp->ino,
                		     		(char * *)&real_comp_bufs_ptrs[submit_count],
						
				     		&start,
				     		&tmp_size,
                		     		&comp_bufs_sizes[submit_count], //gpu_comp_size
						this_submit, // buf_count
				     		comp_type,
						addr_diff,
				     		&cur_changed_space);

					    if(rc <= 0){
						    break;
					    }else{
						    total_rc += rc;
						    tmp->total_compressed_bytes += comp_submit_size;
						    total_compressed_bytes += comp_submit_size;
					    }


					    submit_count += this_submit;
					    start += (SKYFS_OBJECT_NODE_SIZE*this_submit);
					    total_changed_space += &cur_changed_space;





					}
			
					 
					goto submit_end;
				}
				// calculate  the delta and modify the comp_bufs_ptrs[n]
				for(int n = 0; n<comp_bufs_count ; n++){
					char *  real_ptr = (char*)real_comp_bufs_ptrs[n] ;
					real_ptr  += addr_diff;
					SKYFS_ERROR("_skyfs_C2O_submit_gpu_compbuf for gpu place, req[%d], offset %llu, foff %lu, realptr %p , comp_buf %p, comp_size %lu\n ",
							n, start, offset, real_ptr, tmp->bufs[match].comp_buf, comp_bufs_sizes[n]);
					rc = __skyfs_C2O_submit_gpu_compbuf(tmp->ino,
                		     		//comp_bufs_ptrs[n],
						real_ptr,
				     		start,
				     		SKYFS_OBJECT_NODE_SIZE,
                		     		comp_bufs_sizes[n],
				     		comp_type,
				     		&cur_changed_space);
					if(rc <= 0){
						break;
					}else{
						total_rc += rc;
					}
					
					start += SKYFS_OBJECT_NODE_SIZE;
					total_changed_space += &cur_changed_space;

				}
				gettimeofday(&t4, NULL);

				gpu_comp_submit_time += (t4.tv_sec*1000000 + t4.tv_usec);
				gpu_comp_submit_time -= (t3.tv_sec*1000000 + t3.tv_usec);
				gpu_comp_submit_cnt ++;

				SKYFS_ERROR("Place ino %llu, offset %llu, p_start: %llu.%06u, c_start: %llu.%06u, c_end: %llu.%06lu, s_start %llu.%06lu, s_end %llu.%06u\n  ", ino, offset, t_start.tv_sec, t_start.tv_usec,
	 t1.tv_sec, t1.tv_usec, t2.tv_sec, t2.tv_usec, 
	 t3.tv_sec, t3.tv_usec, t4.tv_sec, t4.tv_usec);
				/*if(rc >0){
					total_rc += rc;
				}*/


				SKYFS_ERROR("client Submit  ino %llu write buf , i= %d, offset %llu, count %llu, result  rc %llu\n", 
						tmp->ino,match,   tmp->bufs[match].start,  SKYFS_OBJECT_NODE_SIZE, rc );
submit_end:
				if(total_rc < (gpu_write_buf_size) || rc < 0){
					SKYFS_ERROR_1("submit compress gbuf failed , clear buf, rc %ld, start %llu, offset %llu  \n", rc, start, offset);
					rc = -EIO;
					memset(tmp->bufs[match].buf, 0, gpu_write_buf_size);
				}
				// clear this buf
				// release buf_obj;
				//if(cpu_buf_obj){
				//	 free_cpu_batch(cpu_buf_obj);
				//}
				tmp->bufs[match].start = 0;
				tmp->bufs[match].end = 0;

			}
			pthread_rwlock_unlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			goto EXIT;

		}else if(first_free >= 0){
			int min_submit_entry = first_free;
			SKYFS_ERROR("client wr cache new, ino %llu, offset %llu,\n", ino, offset);
			// select the minium used buf 
			for(int gbuf_idx = 0; gbuf_idx <2; gbuf_idx++){
				if(tmp->bufs[gbuf_idx].start == 0 && tmp->bufs[gbuf_idx].end == 0){
					if(tmp->bufs[gbuf_idx].submit_cnt < tmp->bufs[min_submit_entry].submit_cnt )
						min_submit_entry = gbuf_idx;

				}
			}
			first_free = min_submit_entry;
			
			match = first_free;
			tmp->bufs[match].comp_type = comp_type;
			tmp->bufs[match].start = offset;
			tmp->bufs[match].end = (offset+count-1);
			start_pos = tmp->bufs[match].buf + (offset % (gpu_write_buf_size));
			memcpy(start_pos, databuf, count);
			rc = 0; // merged in  proper buf
			pthread_rwlock_unlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			goto EXIT;


		}else{
			rc = -ENOMEM; // no match and no free buf
			pthread_rwlock_unlock(data_rwlock);
			pthread_rwlock_unlock(hash_rwlock);
			goto EXIT;
		}


		// try to merge or do send buffer
	}else{
		// alloc a new inode buffer and insert it to the hash list


		struct timeval t1, t2;
		uint64_t buf_init_time;
		uint32_t buf_offset =(uint32_t)(offset % SKYFS_OBJECT_NODE_SIZE);
		size_t buf_unit_size = ((sizeof(skyfs_C_gcompbuf_unit_t)+255)/256)*256; 
		
		skyfs_C_gcompbuf_unit_t * new_compbuf = NULL;

		pthread_rwlock_unlock(hash_rwlock);
		new_compbuf = (skyfs_C_gcompbuf_unit_t *)calloc(1, sizeof(skyfs_C_gcompbuf_unit_t ));
		if(new_compbuf == NULL){
			SKYFS_ERROR_1("can not alloc comp buffer for new ino %llu\n", ino);
			rc = -ENOMEM; // should send data to osd
			goto unlock_hash_list;
		}

		gettimeofday(&t1, NULL);
		//new_compbuf->gpu_comp_ratio = 500; // 50% compressing with CPU, other 50% in GPU
		new_compbuf->gpu_comp_ratio = all_gpu_comp_ratio; // 50% compressing with CPU, other 50% in GPU
		// set dev_id for all 2 bufs, because other comp_buf_parameters is set to zero by calloc
		for (int n = 0 ; n< 2; n++){
			new_compbuf->bufs[n].prop_buf = NULL;
			new_compbuf->bufs[n].dev_id = (new_compbuf->ino+n) % 2;
			int dev_idx =  new_compbuf->bufs[n].dev_id;
			char * d_temp_ptr = NULL;
 			select_cuda_device(new_compbuf->bufs[n].dev_id);

			int lock_idx = lock_gpu_init_task(dev_idx);
			// important !! select cuda device and create streams 
			new_compbuf->bufs[n].d_size = get_nvcomp_zstd_temp_size();
			/*
			alloc_cuda_memory(d_temp_ptr,  new_compbuf->bufs[n].d_size);
			if(d_temp_ptr != NULL){
				SKYFS_ERROR_1("Init alloc d_temp %p \n", d_temp_ptr);
				 new_compbuf->bufs[n].d_temp = d_temp_ptr;
			}else{
				SKYFS_ERROR_1("Init alloc d_temp failed !\n");
			}*/

			pthread_rwlock_init(&new_compbuf->bufs[n].prefetch_rwlock, NULL);
			create_cuda_streams(new_compbuf->bufs[n].dev_streams, 2);
			unlock_gpu_init_task(lock_idx);


		}
		rc = register_host_data((void*)new_compbuf, sizeof(skyfs_C_gcompbuf_unit_t));
		gettimeofday(&t2,NULL);
		buf_init_time = t2.tv_sec * 1000000 + t2.tv_usec;
		buf_init_time -= (t1.tv_sec * 1000000 + t1.tv_usec);
		gpu_comp_init_time += buf_init_time;
		
		if(rc != 0){
			goto EXIT;
		}
		new_compbuf->ino = ino;
		pthread_rwlock_init(&new_compbuf->rw_lock, NULL);
		new_compbuf->bufs[0].start = offset;
		new_compbuf->bufs[0].end = offset + count;
		new_compbuf->bufs[0].comp_type = comp_type;
		INIT_LIST_HEAD(&new_compbuf->buflist);

		//memset(&(new_compbuf->bufs[0].buf[0]), 0 , SKYFS_OBJECT_NODE_SIZE);
		memcpy((char*)(new_compbuf->bufs[0].buf)+buf_offset, databuf, count);
		if( new_compbuf->bufs[0].prop_buf == NULL){
			new_compbuf->bufs[0].prop_buf = malloc(SKYFS_GPU_COMPRESS_BUF_SIZE);
		}
		pthread_rwlock_wrlock(hash_rwlock);
		list_add_tail(&new_compbuf->buflist, head);
		SKYFS_ERROR("client wr cache created, ino %llu, offset %llu,\n", ino, offset);
		rc = 0; // add a free 


		pthread_rwlock_unlock(hash_rwlock);

unlock_hash_list:
		SKYFS_ERROR("cache create failed \n");
	}
        	




EXIT:        	
	
	return rc;
}


/*data layout entry cache below*/
skyfs_htb_t *
__skyfs_C_locate_dlentry(skyfs_ino_t ino, skyfs_u64_t obj_id)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;
    
	hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    hashvalue = hashvalue % SKYFS_DIR_DEPTH_HASH_LEN;//CLIENT_DLENTRY_HASH_LEN;

    htbp = &client_dlentry_htbbase[hashvalue];

    return htbp;
}

skyfs_C_dlentry_t *
__skyfs_C_find_dlentry(skyfs_htb_t *htbp, 
            skyfs_ino_t ino,
            skyfs_u64_t obj_id)
{
    skyfs_C_dlentry_t *dlentry = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_C_find_dlentry:enter.ino:%llu,obj_id:%llu\n", 
        ino, obj_id);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_find_dlentry:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_C_dlentry_t, list);
        SKYFS_MSG("__skyfs_C_find_dlentry:tmp %llu,name:%llu\n", 
            tmp->ino, tmp->obj_id);
        if(tmp->ino == ino && tmp->obj_id == obj_id){
            SKYFS_MSG("__skyfs_C_find_dlentry:find the dlentry\n");
            dlentry = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_C_find_dlentry:leave:%p.\n", dlentry);
    return dlentry;
}


skyfs_s32_t __skyfs_C_lookup_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id,
				skyfs_u32_t *osd_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *chunk_id)
{
    skyfs_C_dlentry_t *dlentry= NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    htbp = __skyfs_C_locate_dlentry(ino, obj_id);
    if(htbp == NULL){
        rc = -ENOENT; 
		goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dlentry = __skyfs_C_find_dlentry(htbp, ino, obj_id);
    if(dlentry){
        *osd_id = dlentry->osd_id;
        *subset_id = dlentry->subset_id;
        *chunk_id = dlentry->chunk_id;
        rc = 2;
        SKYFS_MSG("__skyfs_C_lookup_dlentry:get the dentry:%llu,%llu\n", ino, obj_id);
    }else{
        rc = 0;
    }

    pthread_mutex_unlock(&htbp->lock);

ERR:

	SKYFS_LEAVE("__skyfs_C_lookup_dlentry:%llu, %llu,rc:%d\n", ino, obj_id, rc);
    return rc;
}

skyfs_s32_t __skyfs_C_add_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id,
				skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id)
{
    skyfs_C_dlentry_t *dlentry = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    htbp = __skyfs_C_locate_dlentry(ino, obj_id);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dlentry = __skyfs_C_find_dlentry(htbp, ino, obj_id);
    if(dlentry == NULL){
        dlentry = (skyfs_C_dlentry_t *)malloc(sizeof(skyfs_C_dlentry_t));
        dlentry->ino = ino;
        dlentry->obj_id = obj_id;
        dlentry->osd_id = osd_id;
        dlentry->subset_id = subset_id;
        dlentry->chunk_id = chunk_id;
        list_add(&dlentry->list, &htbp->head);
        rc = 0;
        SKYFS_MSG("__skyfs_C_add_dlentry:add the dentry:%llu,%llu\n", ino, obj_id);
    }else{
        rc = 2;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

skyfs_s32_t __skyfs_C_release_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id)
{
    skyfs_C_dlentry_t *dlentry= NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

	SKYFS_ERROR("__skyfs_C_release_dlentry:%llu, %llu\n", ino, obj_id);

    htbp = __skyfs_C_locate_dlentry(ino, obj_id);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    dlentry = __skyfs_C_find_dlentry(htbp, ino, obj_id);
    if(dlentry){
        list_del(&dlentry->list);
        free(dlentry);
        rc = 0;
    }else{
        rc = -ENOENT;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

//metacache Function
skyfs_htb_t *
__skyfs_C_locate_meta(skyfs_ino_t ino, skyfs_u64_t conflict_index)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue;
    
	hashvalue = __skyfs_get_obj_hashvalue(ino, conflict_index);
    hashvalue = hashvalue % SKYFS_DIR_DEPTH_HASH_LEN;//CLIENT_DLENTRY_HASH_LEN;

    htbp = &client_meta_htbbase[hashvalue];

    return htbp;
}

skyfs_C_meta_t *
__skyfs_C_find_meta(skyfs_htb_t *htbp, 
            skyfs_ino_t ino,
            skyfs_u64_t conflict_index)
{
    skyfs_C_meta_t *metacache = NULL, *tmp = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_C_find_meta:enter.ino:%llu,conflict_index:%llu\n", 
        ino, conflict_index);
    
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_C_find_meta:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_C_meta_t, list);
        SKYFS_MSG("__skyfs_C_find_meta:ino %llu,conflict_index:%llu\n", 
            tmp->ino, tmp->conflict_index);
        if(tmp->ino == ino && tmp->conflict_index == conflict_index){
            SKYFS_MSG("__skyfs_C_find_meta:find the meta\n");
            metacache = tmp;
            goto OUT;
        }
    }

ERR:
OUT:
    SKYFS_LEAVE("__skyfs_C_find_meta:leave:%p.\n", metacache);
    return metacache;
}


skyfs_s32_t __skyfs_C_lookup_meta(skyfs_ino_t ino,
                skyfs_u64_t conflict_index, skyfs_C_meta_t *meta_out)
{
    skyfs_C_meta_t *metacache= NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    htbp = __skyfs_C_locate_meta(ino, conflict_index);
    if(htbp == NULL){
        rc = -ENOENT; 
		goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    metacache = __skyfs_C_find_meta(htbp, ino, conflict_index);
    if(metacache){
   		memcpy(&meta_out->meta, &metacache->meta, sizeof(skyfs_meta_t)); 
		meta_out->ino = ino;
		meta_out->conflict_index = conflict_index;
		meta_out->update_sec = metacache->update_sec;
		meta_out->update_usec = metacache->update_usec;
		meta_out->update_cnt = metacache->update_cnt;
		meta_out->changed_space = metacache->changed_space;
		rc = 2;
	}

    pthread_mutex_unlock(&htbp->lock);

ERR:

	//SKYFS_ERROR_1("__skyfs_C_lookup_meta:%llu, %llu,rc:%d\n", ino, conflict_index, rc);
    return rc;
}

skyfs_s32_t __skyfs_C_add_meta(skyfs_ino_t ino, 
		skyfs_u64_t conflict_index,
		skyfs_meta_t *meta)
{
    skyfs_C_meta_t *metacache = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;
    // skip add meta by mayl
    //return rc;

    //SKYFS_ERROR_1("add meta ino %lu, conflict_index %lu\n", ino, conflict_index);
    htbp = __skyfs_C_locate_meta(ino, conflict_index);

    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    metacache = __skyfs_C_find_meta(htbp, ino, conflict_index);
    if(metacache == NULL){
	struct timeval tv;
        metacache = (skyfs_C_meta_t *)malloc(sizeof(skyfs_C_meta_t));
        metacache->ino = ino;
        metacache->conflict_index = conflict_index;
	gettimeofday(&tv , NULL);
	metacache->update_sec = tv.tv_sec;
	metacache->update_usec = tv.tv_usec;
	metacache->update_cnt = 0;
	memcpy(&(metacache->meta), meta, sizeof(skyfs_meta_t));

        list_add(&metacache->list, &htbp->head);
        rc = 0;
        SKYFS_MSG("__skyfs_C_add_meta:add the meta:%llu,%llu\n", ino, conflict_index);
    }else{
        rc = 2;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    //SKYFS_ERROR_1("add meta ino %lu, conflict_index %lu, return %d, uptime %lu.%06lu\n", ino, conflict_index, rc,
//		    metacache->update_sec, metacache->update_usec);
    return rc;
}

skyfs_s32_t __skyfs_C_release_meta(skyfs_ino_t ino, skyfs_u64_t conflict_index)
{
    skyfs_htb_t *htbp = NULL;
	skyfs_C_meta_t *metacache = NULL;
    skyfs_s32_t rc = 0;

	//SKYFS_ERROR_1("__skyfs_C_release_meta:%llu %llu\n", 
	//	ino, conflict_index);

    htbp = __skyfs_C_locate_meta(ino, conflict_index);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    metacache = __skyfs_C_find_meta(htbp, ino, conflict_index);
	if(metacache){
    	list_del_init(&(metacache->list));
    	free(metacache);
    	rc = 0;
	}	

    pthread_mutex_unlock(&htbp->lock);

ERR:

    return rc;
}

skyfs_s32_t __skyfs_C_update_meta(skyfs_ino_t ino, 
		skyfs_u64_t conflict_index,
		skyfs_s64_t changed_space,
		skyfs_meta_t *meta)
{
    skyfs_C_meta_t *metacache = NULL;
    skyfs_htb_t *htbp = NULL;
    skyfs_s32_t rc = 0;

    //SKYFS_ERROR_1("update meta ino %lu, conflict_index %lu, \n", ino, conflict_index );
    htbp = __skyfs_C_locate_meta(ino, conflict_index);
    if(htbp == NULL){
        rc = -ENOENT;
        goto ERR;
    }

    pthread_mutex_lock(&htbp->lock);

    metacache = __skyfs_C_find_meta(htbp, ino, conflict_index);
    if(metacache){
	    struct timeval tv;
	    gettimeofday(&tv , NULL);
	    metacache->update_sec = tv.tv_sec;
	    metacache->update_usec = tv.tv_usec;
	    metacache->update_cnt ++;
	    metacache->changed_space = changed_space;
	    memcpy(&(metacache->meta), meta, sizeof(skyfs_meta_t));
        rc = 0;
	if(meta->algorithm){
        	SKYFS_ERROR("__skyfs_C_update_meta:update meta:%llu,%llu, altorithm %d\n", ino, conflict_index, metacache->meta.algorithm);
	}

    }else{
        rc = 2;
    }
    pthread_mutex_unlock(&htbp->lock);

ERR:

    //SKYFS_ERROR_1("update meta ino %lu, conflict_index %lu, ret %d\n", ino, conflict_index, rc);
    return rc;
}
/*This is end of client_cache.c*/
