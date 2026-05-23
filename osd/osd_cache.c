/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_cache.c $
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
#include "osd_cache.h"

skyfs_s32_t 
__skyfs_SS_add2objbuf(skyfs_O_objbuf_t *objbuf, 
        skyfs_O_pagebuf_t *pagebuf)
{
    skyfs_s32_t rc = 0;

    return rc;
}

skyfs_O_pagebuf_t *
__skyfs_SS_find_pagebuf(skyfs_htb_t *htbp, skyfs_u64_t page_id)
{
    skyfs_O_pagebuf_t *pagebuf= NULL, *tmp = NULL;

    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_SS_find_pagebuf:enter.page_id:%llu,htbp:%p\n", 
        page_id, htbp );

    SKYFS_MSG("__skyfs_SS_find_pagebuf:hash head %p\n", &(htbp->head));

    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_SS_find_pagebuf:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_pagebuf_t, page_hash);
        if(tmp->page_id == page_id){
            pagebuf = tmp;
            SKYFS_LEAVE("__skyfs_SS_find_pagebuf:page_id:%llu\n", page_id);
            goto OUT;
        }
    }
OUT:
ERR:

    SKYFS_LEAVE("__skyfs_SS_find_pagebuf:exit.\n");
    return pagebuf;
}


skyfs_O_pagebuf_t *
__skyfs_SS_locate_pagebuf(skyfs_O_objbuf_t *objbuf, skyfs_u32_t i)
{
    skyfs_htb_t *htbp = NULL;
    skyfs_u32_t hashvalue = 0;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    
    hashvalue = __skyfs_get_obj_hashvalue(i, 0);
    hashvalue = hashvalue % SKYFS_DL_PAGEBUF_HASH_LEN;
    SKYFS_MSG("__skyfs_SS_locate_pagebuf:hashvalue:%u\n", hashvalue);
    htbp = &(objbuf->pagebuf_hash_base[hashvalue]);

    pagebuf = __skyfs_SS_find_pagebuf(htbp, i);

    return pagebuf;
}

skyfs_O_filebuf_t * 
__skyfs_SS_find_filebuf(skyfs_ino_t ino)
{
    skyfs_O_filebuf_t *filebuf = NULL, *tmp = NULL;
    struct list_head *index = NULL, *head = NULL;

    head = &osd_filebuf_queue;

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_filebuf_t, file_list);
        if(tmp->ino == ino){
            filebuf = tmp;
            SKYFS_LEAVE("__skyfs_SS_find_filebuf:ino:%llu\n", ino);
            goto ERR_OUT;
        }
    }
ERR_OUT:    
    return filebuf;
}

skyfs_O_objbuf_t *
__skyfs_SS_find_objbuf(skyfs_htb_t *htbp, skyfs_u64_t obj_id)
{
    skyfs_O_objbuf_t *objbuf= NULL, *tmp = NULL;

    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_SS_find_objbuf:enter.obj_id:%llu\n", obj_id);

    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("__skyfs_SS_find_objbuf:hash table NULL\n");
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_objbuf_t, obj_hash);
        if(tmp->obj_id == obj_id){
            objbuf = tmp;
            SKYFS_LEAVE("__skyfs_SS_find_objbuf:obj_id:%llu\n", obj_id);
            goto OUT;
        }
    }
OUT:
ERR:

    SKYFS_LEAVE("__skyfs_SS_find_objbuf:exit.\n");
    return objbuf;
}

skyfs_s32_t 
__skyfs_SS_locate_objbuf(skyfs_ino_t ino, 
        skyfs_u64_t obj_id, 
        skyfs_u64_t offset, 
        skyfs_u32_t count, 
        skyfs_O_objbuf_t **objbuf)
{
    /*Make sure ALL the require content in the cache*/
    skyfs_u32_t page_t_plus = 0;
    skyfs_u32_t start_page = 0;
    skyfs_u32_t end_page = 0;
    skyfs_u32_t hashvalue = 0;
    skyfs_u32_t i = 0, j = 0;
    skyfs_htb_t *htbp = NULL;
    skyfs_O_filebuf_t *filebuf = NULL;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    skyfs_s32_t       rc = 0;
    skyfs_timespec_t  access_time;


    SKYFS_ENTER("%s:enter:ino:%llu,obj_id:%llu,offset:%llu\n", 
        __FUNCTION__, ino, obj_id,offset);

    pthread_mutex_lock(&osd_filebuf_queue_lock);
    filebuf = __skyfs_SS_find_filebuf(ino);
    if(filebuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:can't find filebuf:%llu\n", __FUNCTION__, ino);
        goto ERR_OUT;    
    }

    hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    hashvalue = hashvalue % SKYFS_DL_OBJBUF_HASH_LEN;
    htbp = &(filebuf->objbuf_hash_base[hashvalue]);

    *objbuf = __skyfs_SS_find_objbuf(htbp, obj_id);
    if(*objbuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:can't find objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
        goto ERR_OUT;    
    }

    gettimeofday(&access_time, NULL);
    filebuf->timelen = access_time.tv_sec * 1000000 + access_time.tv_usec;

    if((offset + count) % SKYFS_PAGE_SIZE != 0) page_t_plus = 1;
    start_page = (offset / SKYFS_PAGE_SIZE);
    end_page = (offset + count)/SKYFS_PAGE_SIZE + page_t_plus;
    SKYFS_MSG("%s:page:start:%u,end:%u\n", __FUNCTION__, start_page, end_page);

    for(i = start_page; i < end_page; i++){
        SKYFS_MSG("%s:start page:%u,index:%u\n", 
            __FUNCTION__, i, j);
        pagebuf = NULL;
        pagebuf = __skyfs_SS_locate_pagebuf(*objbuf, i);
        if(pagebuf == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("%s:can't find pagebuf:%llu,%llu,%d\n", 
                __FUNCTION__, ino, obj_id, i);
            break;
        }else{
            SKYFS_MSG("%s:find the pagebuf:%llu,%llu,%d\n", __FUNCTION__, ino, obj_id, i);
            continue;
        }
        j++;
    }

ERR_OUT:
    pthread_mutex_unlock(&osd_filebuf_queue_lock);

    SKYFS_LEAVE("%s:exit:%llu,%llu.rc:%d,objbuf:%p\n", __FUNCTION__, ino, obj_id, rc, *objbuf);
    return rc;    

}

skyfs_s32_t 
__skyfs_SS_read_objbuf(skyfs_ino_t ino, 
        skyfs_u64_t obj_id, 
        skyfs_u64_t offset, 
        skyfs_u32_t count, 
        skyfs_O_objbuf_t **objbuf,
        skyfs_s8_t  **buf)
{
    /*Make sure ALL the require content in the cache*/
    skyfs_u32_t page_t_plus = 0;
    skyfs_u32_t start_page = 0;
    skyfs_u32_t end_page = 0;
    skyfs_u32_t hashvalue = 0;
    skyfs_u32_t i = 0, j = 0;
    skyfs_htb_t *htbp = NULL;
    skyfs_O_filebuf_t *filebuf = NULL;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    skyfs_s8_t        *tmp_buf = NULL;
    skyfs_s32_t       rc = 0;
    skyfs_timespec_t  access_time;

    skyfs_u32_t in_offset = 0;
    skyfs_u32_t front_count;


    SKYFS_ERROR("%s:enter:ino:%llu,obj_id:%llu,offset:%llu,count:%d\n", 
        __FUNCTION__, ino, obj_id, offset, count);

    pthread_mutex_lock(&osd_filebuf_queue_lock);
    filebuf = __skyfs_SS_find_filebuf(ino);
    if(filebuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:can't find filebuf:%llu\n", __FUNCTION__, ino);
        pthread_mutex_unlock(&osd_filebuf_queue_lock);
        goto ERR_OUT;    
    }
    pthread_rwlock_rdlock(&filebuf->rwlock);
    pthread_mutex_unlock(&osd_filebuf_queue_lock);

    hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    hashvalue = hashvalue % SKYFS_DL_OBJBUF_HASH_LEN;
    htbp = &(filebuf->objbuf_hash_base[hashvalue]);
    pthread_mutex_lock(&htbp->lock);

    *objbuf = __skyfs_SS_find_objbuf(htbp, obj_id);
    if(*objbuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:can't find objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
        pthread_mutex_unlock(&htbp->lock);
        pthread_rwlock_unlock(&filebuf->rwlock);
        goto ERR_OUT;    
    }

    gettimeofday(&access_time, NULL);
    filebuf->timelen = access_time.tv_sec * 1000000 + access_time.tv_usec;

    if((offset + count) % SKYFS_PAGE_SIZE != 0) page_t_plus = 1;
    start_page = (offset / SKYFS_PAGE_SIZE);
    end_page = (offset + count)/SKYFS_PAGE_SIZE + page_t_plus;
    SKYFS_MSG("%s:page:start:%u,end:%u,obj:%p\n", __FUNCTION__, start_page, end_page, *objbuf);

    *buf = (skyfs_s8_t *)malloc(count);
    tmp_buf = *buf;
    rc = 0;
    j = 0;

    for(i = start_page; i < end_page; i++){
        SKYFS_MSG("%s:buf:%p, start page:%u,index:%u,in_offset:%u\n", 
            __FUNCTION__, tmp_buf, i, j, in_offset);
        in_offset = offset % SKYFS_PAGE_SIZE;
        tmp_buf = tmp_buf + in_offset;
        front_count = SKYFS_PAGE_SIZE - in_offset;
        pagebuf = NULL;

        pagebuf = __skyfs_SS_locate_pagebuf(*objbuf, i);
        if(pagebuf == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("%s:can't find pagebuf:%llu,%llu,%d\n", 
                __FUNCTION__, ino, obj_id, i);
            //pthread_mutex_unlock(&htbp->lock);
            //pthread_rwlock_unlock(&filebuf->rwlock);
            break;
        }else{
	    SKYFS_ERROR("read_objbuf cpy %p , %p , len %d\n", tmp_buf, pagebuf->page, front_count);
            memcpy(tmp_buf, pagebuf->page, front_count);    
        }

        tmp_buf = tmp_buf + front_count;
        j ++;
        rc = rc + front_count;
    }

    if(rc <= 0){
        free(*buf);
    }

    pthread_mutex_unlock(&htbp->lock);
    pthread_rwlock_unlock(&filebuf->rwlock);

ERR_OUT:

    SKYFS_ERROR("%s:exit:%llu,%llu.rc:%d,objbuf:%p, buf %p , tmp_buf %p \n", __FUNCTION__, ino, obj_id, rc, *objbuf, *buf, tmp_buf);
    return rc;    

}
skyfs_s32_t 
__skyfs_SS_fill_buffer(skyfs_s8_t *buf, 
        skyfs_O_objbuf_t *objbuf,
        skyfs_u64_t offset,
        skyfs_u32_t count)
{
    /*Fill buf with objbuf data*/
    skyfs_u32_t in_offset = 0;
    skyfs_u32_t page_t_plus = 0;
    skyfs_s8_t  *tmp_buf = NULL;
    skyfs_u32_t start_page;
    skyfs_u32_t end_page;
    skyfs_u32_t front_count;
    skyfs_u64_t obj_id;
    skyfs_ino_t ino;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    skyfs_s32_t rc = 0;
    skyfs_u32_t i = 0,j = 0;

    SKYFS_ENTER("%s:enter,objbuf:%p\n", __FUNCTION__, objbuf);
    SKYFS_ENTER("%s:enter :%p,%llu,%llu\n", __FUNCTION__, objbuf, objbuf->ino, objbuf->obj_id);


    obj_id = objbuf->obj_id;
    ino = objbuf->ino;

    if((offset + count) % SKYFS_PAGE_SIZE != 0) page_t_plus = 1;
    start_page = (offset / SKYFS_PAGE_SIZE);
    end_page = (offset + count)/SKYFS_PAGE_SIZE + page_t_plus;

    pthread_mutex_lock(&osd_filebuf_queue_lock);

    tmp_buf = buf;
    for(i = start_page; i < end_page; i++){
        SKYFS_MSG("%s:buf:%p, start page:%u,index:%u,in_offset:%u\n", 
            __FUNCTION__, tmp_buf, i, j, in_offset);
        in_offset = offset % SKYFS_PAGE_SIZE;
        tmp_buf = tmp_buf + in_offset;
        front_count = SKYFS_PAGE_SIZE - in_offset;
        pagebuf = NULL;

        pagebuf = __skyfs_SS_locate_pagebuf(objbuf, i);
        if(pagebuf == NULL){
            rc = -ENOENT;
            SKYFS_ERROR("%s:can't find pagebuf:%llu,%llu,%d\n", 
                __FUNCTION__, ino, obj_id, i);
            break;
        }else{
            memcpy(tmp_buf, pagebuf->page, front_count);    
        }

        tmp_buf = tmp_buf + front_count;
        j ++;
        rc = rc + front_count;
    }

    pthread_mutex_unlock(&osd_filebuf_queue_lock);

    SKYFS_LEAVE("%s:exit:%llu,%llu,rc:%d\n", __FUNCTION__, objbuf->ino, objbuf->obj_id, rc);

    return rc;
}

skyfs_s32_t
__skyfs_SS_insert_objbuf(skyfs_ino_t ino, 
        skyfs_u64_t obj_id, 
        skyfs_u64_t offset, 
        skyfs_u32_t count, 
        skyfs_s8_t  *buf)
{
    /*Insert content of buf into objbuf*/
    skyfs_u32_t page_t_plus = 0;
    skyfs_u32_t start_page = 0;
    skyfs_u32_t end_page = 0;
    skyfs_htb_t *htbp = NULL;
    skyfs_htb_t *htbpobj = NULL;
    skyfs_u32_t hashvalue = 0;
    
    skyfs_s8_t  *tmp_buf = NULL;
    skyfs_u32_t i,j = 0;
    skyfs_u32_t in_offset = 0;
    skyfs_u32_t front_count;
    skyfs_O_filebuf_t *filebuf = NULL;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    skyfs_O_objbuf_t *objbuf = NULL;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("%s:enter:%llu,%llu,%d,%llu,buf:%p\n", __FUNCTION__, ino, offset, count, obj_id, buf);

    pthread_mutex_lock(&osd_block_insert_lock);
    if(osd_block_insert_objbuf){
        pthread_mutex_unlock(&osd_block_insert_lock);
        goto ERR_OUT;
    }
    pthread_mutex_unlock(&osd_block_insert_lock);

    pthread_mutex_lock(&osd_filebuf_queue_lock);

    filebuf = __skyfs_SS_find_filebuf(ino);
    if(filebuf == NULL){
        filebuf = (skyfs_O_filebuf_t *)malloc(sizeof(skyfs_O_filebuf_t));
        if(filebuf == NULL){
            SKYFS_ERROR("%s:error.can't malloc filebuf:%llu\n", __FUNCTION__, ino);
            pthread_mutex_unlock(&osd_filebuf_queue_lock);
            goto ERR_OUT;
        }
        filebuf->ino = ino;
        list_add(&(filebuf->file_list), &osd_filebuf_queue);
        INIT_LIST_HEAD(&filebuf->obj_head);
        __skyfs_init_htb(SKYFS_DL_OBJBUF_HASH_LEN, &filebuf->objbuf_hash_base);
        SKYFS_ERROR("%s:insert filebuf:%llu\n", __FUNCTION__, ino);
        pthread_rwlock_init(&filebuf->rwlock, NULL);
    }
    pthread_rwlock_rdlock(&filebuf->rwlock);
    pthread_mutex_unlock(&osd_filebuf_queue_lock);

    hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    hashvalue = hashvalue % SKYFS_DL_OBJBUF_HASH_LEN;
    htbpobj = &(filebuf->objbuf_hash_base[hashvalue]);
    pthread_mutex_lock(&htbpobj->lock);    

    objbuf = __skyfs_SS_find_objbuf(htbpobj, obj_id);
    if(objbuf == NULL){
        SKYFS_ERROR("%s:alloc objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
        objbuf = (skyfs_O_objbuf_t *)malloc(sizeof(skyfs_O_objbuf_t));
        if(objbuf == NULL){
            SKYFS_ERROR("%s:can't malloc objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
            pthread_mutex_unlock(&htbpobj->lock);    
            pthread_rwlock_unlock(&filebuf->rwlock);
            goto ERR_OUT;
        }
        objbuf->ino = ino;
        objbuf->obj_id = obj_id;
        objbuf->obj_size = 0;
        list_add(&objbuf->obj_hash, &htbpobj->head);
        list_add(&objbuf->obj_list, &filebuf->obj_head);
        INIT_LIST_HEAD(&objbuf->page_head);
        rc = __skyfs_init_htb(SKYFS_DL_PAGEBUF_HASH_LEN, &objbuf->pagebuf_hash_base);
        if(rc < 0){
            SKYFS_ERROR("%s:init paghbuf htb error:%d\n", __FUNCTION__, rc);
            pthread_mutex_unlock(&htbpobj->lock);    
            pthread_rwlock_unlock(&filebuf->rwlock);
            goto ERR_OUT;
        }
    }else{
        SKYFS_MSG("%s:find objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
    }

    if((offset + count) % SKYFS_PAGE_SIZE != 0) page_t_plus = 1;
    start_page = (offset / SKYFS_PAGE_SIZE);
    end_page = (offset + count)/SKYFS_PAGE_SIZE + page_t_plus;

    tmp_buf = buf;
    for(i = start_page; i < end_page; i++){
        SKYFS_MSG("%s:buf:%p, start page:%u,index:%u,in_offset:%u,objbuf:%p\n", 
            __FUNCTION__, tmp_buf, i, j, in_offset, objbuf);
        in_offset = offset % SKYFS_PAGE_SIZE;
        tmp_buf = tmp_buf + in_offset;
        front_count = SKYFS_PAGE_SIZE - in_offset;
        pagebuf = NULL;

        pagebuf = __skyfs_SS_locate_pagebuf(objbuf, i);
        if(pagebuf == NULL){
            SKYFS_MSG("%s:start alloc page:%u\n", __FUNCTION__, i);
            pagebuf = (skyfs_O_pagebuf_t *) malloc(sizeof(skyfs_O_pagebuf_t));
            if(pagebuf == NULL){
                SKYFS_ERROR("%s:can't malloc pagebuf:%llu,%llu,%d\n", 
                    __FUNCTION__, ino, obj_id, i);
                pthread_mutex_unlock(&htbpobj->lock);    
                pthread_rwlock_unlock(&filebuf->rwlock);
                goto ERR_OUT;
            }
            bzero(pagebuf, sizeof(skyfs_O_pagebuf_t));
            pagebuf->page_id = i;
            memcpy(pagebuf->page, tmp_buf, front_count);
            hashvalue = __skyfs_get_obj_hashvalue(i, 0);
            hashvalue = hashvalue % SKYFS_DL_PAGEBUF_HASH_LEN;
            SKYFS_MSG("%s:start i:%d,startpage:%u,endpage:%u,hashvalue:%u\n", 
                __FUNCTION__, i, start_page, end_page, hashvalue);
            htbp = &(objbuf->pagebuf_hash_base[hashvalue]);
            list_add(&pagebuf->page_hash, &htbp->head);
            SKYFS_MSG("%s:add to obj_page_head:%llu,thread_id:%lu\n", __FUNCTION__, obj_id, pthread_self());
            list_add(&pagebuf->page_list, &objbuf->page_head);
            SKYFS_MSG("%s:ended add\n", __FUNCTION__);
        }else{
	    SKYFS_ERROR("insert_objbuf cpy %p , %p , len %d\n", pagebuf->page, tmp_buf, front_count);
            memcpy(pagebuf->page, tmp_buf, front_count);
        }

        tmp_buf = tmp_buf + front_count;
        j++;
    }

    pthread_mutex_unlock(&htbpobj->lock);    
    pthread_rwlock_unlock(&filebuf->rwlock);

ERR_OUT:


    SKYFS_ERROR("%s:exit insert_objbuf :%llu,%llu,%llu,rc:%d\n", __FUNCTION__, ino, offset, obj_id, rc);

    return rc;
}

skyfs_s32_t 
__skyfs_SS_release_objbuf(skyfs_ino_t ino, 
        skyfs_u32_t partition_id, 
        skyfs_u64_t obj_id, 
        skyfs_u64_t offset, 
        skyfs_u32_t count)
{
    /*Release data cache*/
    skyfs_u32_t page_t_plus = 0;
    skyfs_u32_t start_page = 0;
    skyfs_u32_t end_page = 0;
    skyfs_u32_t hashvalue = 0;
    skyfs_O_filebuf_t *filebuf = NULL;
    skyfs_O_objbuf_t *objbuf = NULL;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    skyfs_htb_t *htbp = NULL;

    skyfs_u32_t i;
    skyfs_s32_t rc = 0;            

    SKYFS_ERROR("%s:enter release_objbuf :%llu,%llu\n", __FUNCTION__, ino, obj_id);

    pthread_mutex_lock(&osd_filebuf_queue_lock);
    filebuf = __skyfs_SS_find_filebuf(ino);
    if(filebuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:error.can't find filebuf:%llu\n", __FUNCTION__, ino);
        pthread_mutex_unlock(&osd_filebuf_queue_lock);
        goto ERR_OUT;    
    }
    
    pthread_rwlock_rdlock(&filebuf->rwlock);
    pthread_mutex_unlock(&osd_filebuf_queue_lock);

    hashvalue = __skyfs_get_obj_hashvalue(ino, obj_id);
    hashvalue = hashvalue % SKYFS_DL_OBJBUF_HASH_LEN;
    htbp = &(filebuf->objbuf_hash_base[hashvalue]);
    pthread_mutex_lock(&htbp->lock);

    objbuf = __skyfs_SS_find_objbuf(htbp, obj_id);
    if(objbuf == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("%s:error.can't find objbuf:%llu,%llu\n", __FUNCTION__, ino, obj_id);
        pthread_mutex_unlock(&htbp->lock);
        pthread_rwlock_unlock(&filebuf->rwlock);
        goto ERR_OUT;    
    }

    if((offset + count) % SKYFS_PAGE_SIZE != 0) page_t_plus = 1;
    start_page = (offset / SKYFS_PAGE_SIZE);
    end_page = (offset + count)/SKYFS_PAGE_SIZE + page_t_plus;

    for(i = start_page; i < end_page; i++){
        pagebuf = NULL;
        pagebuf = __skyfs_SS_locate_pagebuf(objbuf, i);
        if(pagebuf == NULL){
            SKYFS_ERROR("%s:error.already release pagebuf:%llu,%llu,%d\n", 
                __FUNCTION__, ino, obj_id, i);
            continue;
        }else{
            list_del_init(&pagebuf->page_hash);
            list_del_init(&pagebuf->page_list);
            free(pagebuf);    
        }
    }

    pthread_mutex_unlock(&htbp->lock);
    pthread_rwlock_unlock(&filebuf->rwlock);

ERR_OUT:


    SKYFS_ERROR("%s:exit:release_objbuf :%llu,%llu.rc:%d\n", __FUNCTION__, ino, obj_id, rc);
    return rc;
}

skyfs_s32_t __skyfs_SS_release_filebuf(void)
{
    skyfs_s32_t rc = 0;
    skyfs_u64_t timelen = 0;
    skyfs_O_filebuf_t *filebuf = NULL, *tmp = NULL;
    skyfs_O_objbuf_t *objbuf = NULL;
    skyfs_O_pagebuf_t *pagebuf = NULL;
    struct list_head *tmp_index = NULL, *index = NULL, *index1 = NULL, *head = NULL, *page_head = NULL;
    
    SKYFS_ERROR("%s:enter release_filebuf\n", __FUNCTION__);

    pthread_mutex_lock(&osd_filebuf_queue_lock);
    head = &osd_filebuf_queue;
    if(list_empty(head)){
          SKYFS_ERROR("%s:error.osd filebuf empty\n", __FUNCTION__);
        pthread_mutex_unlock(&osd_filebuf_queue_lock);
        goto ERR_OUT;
    }

    filebuf = list_entry(head->next, skyfs_O_filebuf_t, file_list);
    timelen = filebuf->timelen;
    SKYFS_ERROR("%s:init loop filebuf:ino:%llu,len:%llu\n", __FUNCTION__, filebuf->ino,timelen);

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_filebuf_t, file_list);
        SKYFS_MSG("%s:loop filebuf:ino:%llu,len:%llu\n", __FUNCTION__, tmp->ino,tmp->timelen);
        if(tmp->timelen < timelen){
            SKYFS_MSG("%s:loop filebuf:succeed:ino:%llu,len:%llu\n",
                __FUNCTION__, tmp->ino,tmp->timelen);
            timelen = tmp->timelen;
            filebuf = tmp;
        }
    }
    
    list_del_init(&filebuf->file_list);
    SKYFS_ERROR("%s:before get rwlock:%llu\n", __FUNCTION__, filebuf->ino);
    pthread_rwlock_wrlock(&filebuf->rwlock);

    pthread_mutex_unlock(&osd_filebuf_queue_lock);
    SKYFS_ERROR("%s:start to free filebuf:%llu\n", __FUNCTION__, filebuf->ino);

    head = &(filebuf->obj_head);
    if(list_empty(head)){
          SKYFS_ERROR("%s:filebuf:%llu empty NULL\n", __FUNCTION__, filebuf->ino);
        goto FREE_FILE;
    }
    list_for_each(index, head){
        objbuf = list_entry(index, skyfs_O_objbuf_t, obj_list);
        page_head = &(objbuf->page_head);
        if(list_empty(page_head)){
              SKYFS_ERROR("%s:objbuf:%llu %llu empty NULL\n", __FUNCTION__, filebuf->ino, objbuf->obj_id);
            goto FREE_OBJ;
        }
          SKYFS_ERROR("%s:start to free objbuf:%llu %llu\n", __FUNCTION__, filebuf->ino, objbuf->obj_id);
        list_for_each(index1, page_head){
            pagebuf = list_entry(index1, skyfs_O_pagebuf_t, page_list);
            SKYFS_MSG("%s:pagebuf:%llu\n", __FUNCTION__, pagebuf->page_id);
            tmp_index = index1->prev;
            list_del_init(&pagebuf->page_list);
            list_del_init(&pagebuf->page_hash);
            index1 = tmp_index;
            free(pagebuf);
        }
FREE_OBJ:    
        SKYFS_MSG("%s:start to free objbuf:%llu,%llu\n", 
            __FUNCTION__, objbuf->ino, objbuf->obj_id);
        tmp_index = index->prev;
        list_del_init(&objbuf->obj_list);
        list_del_init(&objbuf->obj_hash);
        index = tmp_index;
        free(objbuf->pagebuf_hash_base);
        free(objbuf);
    }
FREE_FILE:
    free(filebuf->objbuf_hash_base);
    pthread_rwlock_unlock(&filebuf->rwlock);
    free(filebuf);

ERR_OUT:

    SKYFS_ERROR("%s:exit release_filebuf\n", __FUNCTION__);
    return rc;
}

skyfs_s32_t __skyfs_SS_test_freemem(void)
{
    skyfs_u64_t freemem;
    skyfs_u64_t usedmem;
    skyfs_u64_t alert;
    skyfs_s32_t rc = 0;


    freemem = get_sys_free_mem();
    usedmem = get_proc_used_mem();

    alert = (freemem + usedmem) / 10;

    //SKYFS_ERROR("%s:freemem:%llu, usedmem:%llu\n", __FUNCTION__, freemem, usedmem);
    pthread_mutex_lock(&osd_block_insert_lock);
    if(freemem < alert){
        rc = 1;
        osd_block_insert_objbuf = 1;
        SKYFS_ERROR("%s:need to free some cache\n", __FUNCTION__);
    }else{
        osd_block_insert_objbuf = 0;
    }
    pthread_mutex_unlock(&osd_block_insert_lock);

    return rc;
}

long long str_to_val(const char * str)
{
    char unit_flag = str[strlen(str) - 3];
    long long val = atoll(str);

    if ((unit_flag == 'k') || (unit_flag == 'K'))
        val *= (long long)(1024);

    if ((unit_flag == 'm') || (unit_flag == 'M'))
        val *= (long long)(1024*1024);
    
    return val;
}

long long get_proc_used_mem()
{
    pid_t proc_pid;
    char  status_fpath[1024] = {0};
    char  line_buf[1024] = {0};
    FILE * status_f;
    long long mem_used = 0;
    const char mem_flag_str[] = "VmRSS:";

    proc_pid = getpid();
    sprintf(status_fpath, "/proc/%lld/status", (long long)proc_pid);
    status_f = fopen(status_fpath, "r");
    if (status_f == NULL) {
        fprintf(stderr, "Open %s error. %s\n", status_fpath, strerror(errno));
        return (long long)(0);
    }

    while (fgets(line_buf, 1024, status_f) != NULL) {
        if (strncmp(line_buf, mem_flag_str, strlen(mem_flag_str)) == 0) {
            mem_used = str_to_val(line_buf + strlen(mem_flag_str));
            break;
        }
    }

    fclose(status_f);

    return mem_used;
}

long long get_sys_free_mem()
{
    const char meminfo_fpath[] = "/proc/meminfo";
    FILE  * meminfo_f;
    long long mem_free = 0;
    char line_buf[1024] = {0};
    const char mem_free_str[] = "MemFree:";
    const char buffer_str[] = "Buffers:";
    const char cached_str[] = "Cached:";

    meminfo_f = fopen(meminfo_fpath, "r");
    if (meminfo_f == NULL) {
        fprintf(stderr, "Open %s error. %s\n", meminfo_fpath, strerror(errno));
        return (long long)(0);
    }

    while (fgets(line_buf, 1024, meminfo_f)) {
        if (strncmp(line_buf, mem_free_str, strlen(mem_free_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(mem_free_str));

        if (strncmp(line_buf, buffer_str, strlen(buffer_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(buffer_str));

        if (strncmp(line_buf, cached_str, strlen(cached_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(cached_str));
    }

    fclose(meminfo_f);

    return mem_free;
}
/*This is end of osd_cache.c*/
