/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_cache.h 
 */

#ifndef __CLIENT_CACHE_H
#define __CLIENT_CACHE_H

#define MAX_CUDA_DEV_COUNT (8)
#define SKYFS_GPU_COMPRESS_BUF_SIZE (1ul << 26)
extern skyfs_htb_t         *compbuf_hash_base;

extern  size_t get_nvcomp_zstd_temp_size();
typedef struct __skyfs_C_flength{
    skyfs_ino_t      ino;
    skyfs_u64_t      flength;
    struct list_head list;
}skyfs_C_flength_t;

typedef struct __skyfs_C_bcache{
    skyfs_s64_t        start_offset;
    skyfs_s64_t        last_offset;
    skyfs_s64_t        now_offset;
    skyfs_s32_t        now_index;
    skyfs_u32_t        last_index;
    skyfs_u32_t	       is_back;
    skyfs_u32_t        subset_id;
    skyfs_u32_t        chunk_id;
    skyfs_u32_t        nfree;
    skyfs_ino_t        ino;
    struct list_head   hash;
    skyfs_M_cmeta_t    cmetap[SKYFS_MAX_META_PER_BOX];
}skyfs_C_bcache_t;

typedef struct __skyfs_C_dentry{
    skyfs_ino_t      ino;
    skyfs_u64_t      conflict_index;
    skyfs_ino_t      dino;
    skyfs_s8_t       name[SKYFS_MAX_NAME_LEN];
    struct list_head list;
}skyfs_C_dentry_t;

typedef struct __skyfs_C_meta{
    skyfs_ino_t      ino;
    skyfs_u64_t      conflict_index;
    skyfs_meta_t     meta;
    skyfs_u32_t      update_sec;
    skyfs_u32_t      update_usec;
    skyfs_u32_t      update_cnt;
    skyfs_s64_t      changed_space;

    struct list_head list;
}skyfs_C_meta_t;

typedef struct __skyfs_C_writebuf{
    skyfs_ino_t      ino;
    skyfs_u32_t		 conflict_index;
    skyfs_u32_t      count;
    skyfs_u64_t      offset;
    skyfs_s8_t       buf[SKYFS_WRITEBUF_SIZE];
    struct list_head list;
}skyfs_C_writebuf_t;

// added by mayl

typedef struct __skyfs_C_compbuf{
    //skyfs_ino_t      ino;
//	skyfs_u32_t		 conflict_index;
    skyfs_s64_t      start; // start equal end == 0  means empty_buf
    skyfs_s64_t      end;
    
    skyfs_s64_t      submit_cnt;

    skyfs_s8_t       buf[SKYFS_OBJECT_NODE_SIZE];
    //struct list_head list;
    skyfs_u32_t      comp_type;
}skyfs_C_compbuf_t;


typedef struct __skyfs_C_compbuf_unit{
    skyfs_ino_t      ino;
    //skyfs_u32_t		 conflict_index;
    skyfs_C_compbuf_t bufs[8]; // reserve 1MB for each FILE
    pthread_rwlock_t rw_lock;
    struct list_head buflist;

}skyfs_C_compbuf_unit_t;

typedef struct __skyfs_C_gcompbuf{
    //skyfs_ino_t      ino;
//	skyfs_u32_t		 conflict_index;
    skyfs_s64_t      start; // start equal end == 0  means empty_buf
    skyfs_s64_t      end;
    skyfs_s64_t      submit_cnt;
    
    int dev_id;
    int is_prefetching; // is this buffer being prefetched
    void * uncomp_dev_buf;
    void * uncomp_dev_ptrs;
    void * uncomp_dev_sizes;
    void * comp_dev_buf;
    void * comp_dev_ptrs;
    void * comp_dev_sizes;
    void * dev_streams[5];
    void * d_temp;
    size_t d_size;

    skyfs_u8_t       buf[SKYFS_GPU_COMPRESS_BUF_SIZE]; 
    //skyfs_u8_t       buf[SKYFS_GPU_COMPRESS_BUF_SIZE]; 
    skyfs_u8_t *     prop_buf;
    skyfs_s8_t       comp_buf[SKYFS_GPU_COMPRESS_BUF_SIZE*15/9 + (4096*8)]; 
    //struct list_head list;
    skyfs_u32_t      comp_type;
    pthread_rwlock_t prefetch_rwlock;
}skyfs_C_gcompbuf_t;


typedef struct __skyfs_C_gcompbuf_unit{
    skyfs_ino_t      ino;
    skyfs_s32_t      gpu_comp_ratio ; // 1000 means all in gpu , 0 means all in cpu
    skyfs_u64_t	     gpu_comp_time ; // in us
    skyfs_u64_t	     gpu_comp_bytes ; 
    skyfs_u64_t	     cpu_comp_time ; // in us
    skyfs_u64_t	     cpu_comp_bytes ; 
    skyfs_u64_t	     comp_op_count ; 
    skyfs_u64_t	     total_compressed_bytes;
    //skyfs_u32_t		 conflict_index;
    skyfs_C_gcompbuf_t bufs[2]; // reserve 2x16 MB for each FILE
    pthread_rwlock_t rw_lock;
    struct list_head buflist;

}skyfs_C_gcompbuf_unit_t;




typedef struct __skyfs_C_dlentry{
    skyfs_ino_t      ino;
    skyfs_u64_t      obj_id;
    skyfs_u32_t 	 osd_id;
	skyfs_u32_t      subset_id;
	skyfs_u32_t      chunk_id; 
    struct list_head list;
}skyfs_C_dlentry_t;

extern skyfs_layout_t mds_layout[];

extern skyfs_u32_t mds_layout_version;

extern pthread_mutex_t mds_layout_lock;

extern struct list_head client_bcache_list;
extern pthread_mutex_t client_bcache_lock;

extern struct list_head client_writebuf_head;
extern pthread_mutex_t client_writebuf_lock;
extern skyfs_u32_t client_writebuf_num;

skyfs_s32_t __skyfs_C_init_cache(void);

skyfs_s32_t __skyfs_C_init_mds_extent(void);


extern skyfs_u32_t 
__skyfs_C_get_dirid(skyfs_ino_t pino, skyfs_u32_t flag);

extern skyfs_u32_t
__sufns_C_get_subsetid(skyfs_u32_t dir_id, 
                skyfs_s8_t *name, 
                skyfs_u32_t conflict_index);

skyfs_M_dir_depth_t *
__skyfs_C_find_dir_depth(skyfs_htb_t *htbp, skyfs_u32_t dir_id);

skyfs_s32_t
__skyfs_C_release_depth(skyfs_u32_t dir_id);

extern skyfs_u32_t 
__skyfs_C_judge_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

extern skyfs_u32_t 
__skyfs_C_judge_dir_mdsid(skyfs_u32_t dir_id, skyfs_u32_t subset_id);

extern skyfs_s32_t 
__skyfs_C_judge_mds_layoutv(skyfs_u32_t mds_layoutv, 
                skyfs_u32_t mds_id);

skyfs_s32_t __skyfs_C_init_osd_dlextent(void);

skyfs_s32_t __skyfs_C_init_layout(void);

skyfs_u32_t
__skyfs_C_get_dl_subsetid(skyfs_ino_t ino, skyfs_u64_t obj_id);

skyfs_u32_t 
__skyfs_C_judge_osdid(skyfs_u32_t subset_id);

void
__skyfs_C_clear_dl_depth(void);

skyfs_htb_t *
__skyfs_C_locate_flength(skyfs_ino_t ino);

skyfs_C_flength_t *
__skyfs_C_find_flength(skyfs_htb_t *htbp, skyfs_ino_t ino);

skyfs_u64_t
__skyfs_C_get_flength(skyfs_ino_t ino);

skyfs_s32_t
__skyfs_C_release_flength(skyfs_ino_t ino);

skyfs_s32_t
__skyfs_C_update_flength(skyfs_ino_t ino, 
                skyfs_u64_t flength);
skyfs_htb_t *
__skyfs_C_locate_dentrycache(skyfs_ino_t dino, skyfs_s8_t *name);

skyfs_C_dentry_t *
__skyfs_C_find_dentrycache(skyfs_htb_t *htbp, 
            skyfs_ino_t dino,
            skyfs_s8_t *name);

skyfs_s32_t __skyfs_C_lookup(skyfs_ino_t dino,
                skyfs_s8_t *name,
                skyfs_ino_t *ino,
                skyfs_u32_t *conflict_index);

skyfs_s32_t __skyfs_C_add_dentry(skyfs_ino_t dino,
                skyfs_s8_t *name,
                skyfs_ino_t ino,
                skyfs_u32_t conflict_index);

skyfs_s32_t __skyfs_C_release_dentry(skyfs_ino_t dino,
                skyfs_s8_t *name);

skyfs_C_writebuf_t *
__skyfs_C_lookup_writebuf(skyfs_ino_t ino, 
		skyfs_s64_t offset, skyfs_u32_t flag);

skyfs_s32_t
__skyfs_C_attach_writebuf(skyfs_C_writebuf_t *writebuf, 
		const skyfs_s8_t *buf, 
		skyfs_u32_t size);

skyfs_s32_t
__skyfs_C_submit_writebuf(skyfs_ino_t ino, 
		skyfs_u32_t conflict_index,
		const skyfs_s8_t  *buf,
		skyfs_u64_t offset,
		skyfs_u32_t count);

skyfs_s32_t
__skyfs_C_release_writebuf(skyfs_C_writebuf_t *writebuf);

skyfs_htb_t *
__skyfs_C_locate_dlentry(skyfs_ino_t ino, skyfs_u64_t obj_id);

skyfs_C_dlentry_t *
__skyfs_C_find_dlentry(skyfs_htb_t *htbp, 
            skyfs_ino_t ino,
            skyfs_u64_t obj_id);

skyfs_s32_t __skyfs_C_lookup_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id,
				skyfs_u32_t *osd_id,
				skyfs_u32_t *subset_id,
				skyfs_u32_t *chunk_id);

skyfs_s32_t __skyfs_C_add_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id,
				skyfs_u32_t osd_id,
				skyfs_u32_t subset_id,
				skyfs_u32_t chunk_id);

skyfs_s32_t __skyfs_C_release_dlentry(skyfs_ino_t ino,
                skyfs_u64_t obj_id);

skyfs_htb_t *
__skyfs_C_locate_meta(skyfs_ino_t ino, skyfs_u64_t conflict_index);

skyfs_C_meta_t *
__skyfs_C_find_meta(skyfs_htb_t *htbp, 
            skyfs_ino_t ino,
            skyfs_u64_t conflict_index);

skyfs_s32_t __skyfs_C_lookup_meta(skyfs_ino_t ino,
                skyfs_u64_t conflict_index, 
				skyfs_C_meta_t *meta_out);

skyfs_s32_t __skyfs_C_add_meta(skyfs_ino_t ino, 
		skyfs_u64_t conflict_index,
		skyfs_meta_t *meta);

skyfs_s32_t __skyfs_C_release_meta(skyfs_ino_t ino, skyfs_u64_t conflict_index);

skyfs_s32_t __skyfs_C_update_meta(skyfs_ino_t ino, 
		skyfs_u64_t conflict_index,
		skyfs_s64_t changed_space,
		skyfs_meta_t *meta);

#endif
/*This is end of client_cache.h*/
