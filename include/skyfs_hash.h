/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: skyfs_hash.h 
 */

#ifndef __SKYFS_HASH_H
#define __SKYFS_HASH_H

#include<skyfs_sha1.h>

typedef struct __skyfs_htb{
    struct list_head    head;
#ifdef __KERNEL__
    struct mutex            lock;
#else
    pthread_mutex_t     lock;
    pthread_rwlock_t    rwlock;
#endif
    skyfs_s32_t            length;
}skyfs_htb_t;

#ifndef __KERNEL__
static inline skyfs_s32_t 
__skyfs_init_htb(skyfs_u32_t len, skyfs_htb_t **htbp)
{
    skyfs_htb_t *tbp= NULL, *tmp = NULL;
    skyfs_s32_t err = 0;
    skyfs_s32_t i;
    skyfs_u32_t size;

    size = len * sizeof(skyfs_htb_t);

    tbp = malloc(size);
    if (!tbp) { 
        err = -errno;
        goto EXIT;
    }

    memset(tbp, 0, size);

    for(i = 0; i < len; i++)
    {
        tmp = &tbp[i];
        INIT_LIST_HEAD(&(tmp->head));
        pthread_mutex_init(&(tmp->lock), NULL);
        pthread_rwlock_init(&(tmp->rwlock), NULL);
    }
    *htbp = tbp;

    err = 0;

EXIT:
    return err;
}
#else
static inline skyfs_s32_t 
__skyfs_init_htb(skyfs_u32_t len, skyfs_htb_t **htbp)
{
    skyfs_htb_t *tbp= NULL, *tmp = NULL;
    skyfs_s32_t err = 0;
    skyfs_s32_t i;
    skyfs_u32_t size;

    size = len * sizeof(skyfs_htb_t);

    tbp = kmalloc(size, GFP_KERNEL);
    if (!tbp) { 
        err = -ENOMEM;
        goto EXIT;
    }

    memset(tbp, 0, size);

    for(i = 0; i < len; i++)
    {
        tmp = &tbp[i];
        INIT_LIST_HEAD(&(tmp->head));
        //spin_lock_init(&(tmp->lock));
        mutex_init(&(tmp->lock));
    }
    *htbp = tbp;

    err = 0;

EXIT:
    return err;
}
#endif

static inline skyfs_u32_t
__skyfs_name_hashkey(skyfs_ino_t dir_ino, skyfs_s8_t *name, skyfs_s32_t hash_len)
{
    skyfs_u32_t    hashkey, name_len;
    skyfs_s8_t    c;
    
    name_len = strlen(name);
   
    hashkey = 0;
    while (name_len) {
        c = *name;
        hashkey = (hashkey << 4) | (hashkey >> (8 * sizeof(skyfs_u32_t) - 4));
        hashkey = hashkey ^ c;
        name ++;
        name_len --;
    }   
   
    hashkey += (skyfs_u32_t) dir_ino;
        hashkey = hashkey ^ (hashkey >>3) ^ (hashkey >>5);
        hashkey = hashkey % hash_len;
    return hashkey;
}

static inline skyfs_u64_t 
__skyfs_name2hashvalue1(skyfs_s8_t *name)
{
    skyfs_u64_t    hashkey = 0; 
    skyfs_u32_t name_len;
    skyfs_u8_t    c;
    
    name_len = strlen(name);
   
    hashkey = 0;
    while (name_len) {
        c = *name;
        hashkey = (hashkey << 4) | (hashkey >> (8 * sizeof(skyfs_u64_t) - 4));
        hashkey = hashkey ^ c;
        //hashkey = (hashkey + (c << 4) + (c >> 4)) * 11;
        name ++;
        name_len --;
    }

    return  hashkey;
}

/*The below hash is similar with linux full_name_hash in dhash.h*/

static inline skyfs_u32_t 
__skyfs_name2hashvalue2(skyfs_s8_t *name)
{
    skyfs_u64_t hash = 0;
    skyfs_u32_t name_len;
    skyfs_u8_t    c;

    name_len = strlen(name);

    while (name_len){
        c = *name;
        hash = (hash + (c << 4) + (c >> 4)) * 11;
        name ++;
        name_len --;
    }
    return (skyfs_u32_t)hash;
}

static inline skyfs_u64_t 
__skyfs_name2hashvalue(skyfs_s8_t *name)
{
    skyfs_u64_t hash = 0;
    SHA1Context sha;

    SHA1Reset(&sha);

    SHA1Input(&sha, name, strlen(name));

    if (!SHA1Result(&sha)){
        SKYFS_ERROR("sha:error\n");
    }else{
        hash = sha.Message_Digest[0];
    }

    return hash;
}

static inline skyfs_u32_t
__skyfs_num2hashvalue(skyfs_u32_t num)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];

    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%d", num);

    hashvalue = __skyfs_name2hashvalue(string);
    
    return hashvalue;

}

static inline skyfs_u32_t  __skyfs_ino2hashvalue(skyfs_ino_t ino, skyfs_u32_t conflict)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_u32_t mask;
    skyfs_u32_t mask2;

    mask = (skyfs_u32_t)~0;
    hashvalue = (skyfs_u32_t)(ino & (skyfs_u64_t)mask);

    mask = ~((skyfs_u32_t)~0 >> 1);
    mask2 = (skyfs_u32_t)1 << (SKYFS_LONG_BITS - 2);
    if((hashvalue & mask) || (hashvalue & mask2)){
        hashvalue = conflict;
    }

    return  hashvalue;
}

static inline skyfs_u32_t 
__skyfs_get_subset_hashvalue(skyfs_u32_t dir_id, skyfs_u32_t subset_id)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];

    if(dir_id == 0){
        return subset_id;
    }

    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%d-%d", dir_id, subset_id);

    hashvalue = __skyfs_name2hashvalue(string);

    SKYFS_MSG("__skyfs_get_subset_hashvalue:dir_id:%d,subset_id:%d,hashvalue:%u\n",
        dir_id, subset_id, hashvalue);

    return hashvalue;
}

static inline skyfs_u32_t 
__skyfs_get_dpartition_hashvalue(skyfs_ino_t ino, 
		skyfs_u32_t partition_id, 
		skyfs_u32_t replica_id)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];

    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%llu-%u-%u", ino, partition_id, replica_id);

    hashvalue = __skyfs_name2hashvalue(string);

    SKYFS_MSG("__skyfs_get_dpartition_hashvalue:ino:%llu,partition_id:%u,replica_id:%u,hashvalue:%u\n",
        ino, partition_id, replica_id, hashvalue);

    return hashvalue;
}
static inline skyfs_u32_t 
__skyfs_get_obj_hashvalue(skyfs_ino_t ino, skyfs_u64_t obj_id)
{
    skyfs_u32_t hashvalue = 0;
    //skyfs_u32_t mask;
    //skyfs_u32_t inoh;
    //skyfs_u32_t inoh_low;
    //skyfs_u32_t inoh_high;
    //skyfs_u32_t objh;
    //skyfs_u32_t objh_low;
    //skyfs_u32_t objh_high;
    //skyfs_u32_t tmp1;
    //skyfs_u32_t tmp2;

    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];
    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%llu0312%llu", ino, obj_id);
    hashvalue = __skyfs_name2hashvalue(string);
#if 0
    sprintf(string, "%llu", ino);
    inoh = __skyfs_name2hashvalue(string);
    //inoh = ino;
    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%llu", obj_id);
    objh = __skyfs_name2hashvalue(string);
    //objh = obj_id;

    mask = (skyfs_u32_t)~0;
    mask = ~((skyfs_u32_t)mask << 10);

    hashvalue = (skyfs_u32_t)inoh << 10 | (objh & mask);

#endif
#if 0

    sprintf(string, "%llu", ino);
    inoh = __skyfs_name2hashvalue(string);
    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "%llu", obj_id);
    objh = __skyfs_name2hashvalue(string);

    mask = (skyfs_u32_t)~0;
    mask = ~((skyfs_u32_t)mask << 16);

    tmp1 = inoh & mask;
    tmp2 = objh & mask;

    mask = (skyfs_u32_t)~0;
    mask = ~((skyfs_u32_t)mask << 12);

    inoh_low = tmp1 & mask;
    inoh_high = (skyfs_u32_t)tmp1 >> 12;

    mask = (skyfs_u32_t)~0;
    mask = ~((skyfs_u32_t)mask << 6);

    objh_low = tmp2 & mask;
    objh_high = (skyfs_u32_t)tmp2 >> 6;

    hashvalue = ((skyfs_u32_t) objh_high << 22)|((skyfs_u32_t)inoh_low <<10)| ((skyfs_u32_t)inoh_high << 6) | objh_low;

#endif    
    SKYFS_MSG("__skyfs_get_obj_hashvalue:ino:%llu,obj_id:%llu,hashvalue:%u\n",
        ino, obj_id, hashvalue);

    return hashvalue;
}

static inline skyfs_u32_t __skyfs_get_bmeta_hashvalue(skyfs_u32_t bmeta_id)
{
    skyfs_u32_t hashvalue = 0;
    skyfs_s8_t string[SKYFS_MAX_NAME_LEN];

    bzero(string, SKYFS_MAX_NAME_LEN);

    sprintf(string, "skyfs%d", bmeta_id);

    hashvalue = __skyfs_name2hashvalue(string);
    hashvalue = hashvalue % (SKYFS_BMETA_HASH_LEN - 1) + 1;

    SKYFS_MSG("__skyfs_get_bmeta_hashvalue:bmeta_id:%d,hashvalue:%d\n",
        bmeta_id, hashvalue);

    return hashvalue;
}

static inline skyfs_u32_t
__skyfs_get_subset_id(skyfs_u32_t hashvalue, skyfs_u32_t split_depth)
{
    skyfs_u32_t subset_id;
    skyfs_u32_t tmp;
    skyfs_u64_t filter;

    tmp = hashvalue;
    tmp = tmp >> SKYFS_AVA_HASH_BITS;

    filter = ~((skyfs_u32_t)~0 << split_depth);
    subset_id = tmp & filter;

    return subset_id;
}

#endif 
/*end of skyfs_hash.h*/
