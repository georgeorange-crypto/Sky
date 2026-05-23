/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_meta.c $
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
#include "mds_help.h"
#include "mds_meta.h"
#include "mds_state.h"

#include "mds_ito.h"

skyfs_u32_t skyfs_free_dir_id_head;
skyfs_htb_t skyfs_free_dir_id;

skyfs_u32_t skyfs_conflict_ino_head;
skyfs_htb_t skyfs_conflict_ino;
int alloc_meta_cnt = 0;
int free_meta_cnt = 0;

/*dir_id is composed of 1 bit flag, 7 bit mds_id and 24 bit dir_id*/
skyfs_u32_t
__skyfs_MS_compose_dirid(skyfs_u32_t meta_id)
{
    skyfs_u32_t dir_id;
    skyfs_u32_t mds_id;
    skyfs_u32_t mask;

    SKYFS_MSG("__skyfs_MS_compose_dirid:%d.\n", meta_id);

    mds_id = mds_this_id;
    mds_id = (skyfs_u32_t) mds_this_id << 24;
    mask = ~((skyfs_u32_t)~0 >> 1);
    mds_id = mds_id | mask;
    mask = ((skyfs_u32_t)~0) >> 8;
    meta_id = meta_id & mask;
    dir_id = mds_id | meta_id;

    return dir_id;
}

skyfs_ino_t
__skyfs_MS_compose_ino(skyfs_u32_t dir_id, skyfs_u32_t meta_id)
{
    skyfs_ino_t ino;

    ino = (skyfs_u64_t)dir_id << 32;
    ino = ino | meta_id;

    SKYFS_MSG("__skyfs_MS_compose_ino:dir_id:%d,meta_id:%u,ino:%llu\n",
        dir_id, meta_id, ino);

    return ino;
}

skyfs_ino_t
__skyfs_MS_alloc_ino(skyfs_ino_t dir_ino,
                skyfs_s8_t *name,
                skyfs_u32_t mode)
{
    skyfs_ino_t ino = 0;
    skyfs_u32_t dir_id;
    skyfs_u32_t meta_id;
    skyfs_u32_t mask;
    skyfs_M_dir_id_t *dir_id_entry = NULL;
    struct list_head *head = NULL, *index = NULL;

    SKYFS_ENTER("__skyfs_MS_alloc_ino:enter, dir_ino:%llu, name:%s, mode:%d",
        dir_ino, name, mode);
    dir_id = __skyfs_MS_get_dir_id(dir_ino, 0);

    if(S_ISDIR(mode)){
        pthread_mutex_lock(&(skyfs_free_dir_id.lock));
        head = &(skyfs_free_dir_id.head);
        if(list_empty(head)){
            SKYFS_MSG("__skyfs_MS_alloc_ino: no free dir_id, need to alloc one\n");
            if(skyfs_free_dir_id_head < SKYFS_MAX_DIR_PER_MDS){
                meta_id = skyfs_free_dir_id_head;
                skyfs_free_dir_id_head ++;
                meta_id = __skyfs_MS_compose_dirid(meta_id);
            }else{
                SKYFS_ERROR("__skyfs_MS_alloc_ino:no free dir_id can alloc\n");
                pthread_mutex_unlock(&(skyfs_free_dir_id.lock));
                goto ERR;
            }
        }else{
            /*add dir_id_entry only when release a dir*/
            index = head->next;
            dir_id_entry = list_entry(index, skyfs_M_dir_id_t, dir_head);
            list_del(&(dir_id_entry->dir_head));
            meta_id = dir_id_entry->dir_id;
            free(dir_id_entry);
        }
        pthread_mutex_unlock(&(skyfs_free_dir_id.lock));
        SKYFS_MSG("__skyfs_MS_alloc_ino:dir mode,dir_id:%u\n", meta_id);
    }else{
        SKYFS_MSG("__skyfs_MS_alloc_ino:general mode:%s\n", name);
        meta_id = __skyfs_name2hashvalue(name);
        SKYFS_MSG("__skyfs_MS_alloc_ino:hashvalue:%d\n", meta_id);
        mask = (skyfs_u32_t)~0 >> 2;
        meta_id = mask & meta_id;
        SKYFS_MSG("__skyfs_MS_alloc_ino:mask:%u,meta_id:%u\n", mask, meta_id);
    }
    
    ino = __skyfs_MS_compose_ino(dir_id, meta_id);
ERR:

    SKYFS_ERROR("__skyfs_MS_alloc_ino:exit, ino:%llu, NAME %s, meta_id, dir %llu\n", ino, name ,dir_id);
    return ino;
}

skyfs_ino_t
__skyfs_MS_alloc_conflict_ino(skyfs_ino_t dir_ino, skyfs_u32_t mode)
{
    skyfs_ino_t ino = 0;
    skyfs_u32_t meta_id;
    skyfs_u32_t dir_id;
    skyfs_u32_t mask;

    dir_id = __skyfs_MS_get_dir_id(dir_ino, 0);
    mask = (skyfs_u32_t)1 << 30;

    pthread_mutex_lock(&skyfs_conflict_ino.lock);
    meta_id = skyfs_conflict_ino_head;
    skyfs_conflict_ino_head ++;
    if(skyfs_conflict_ino_head > mask){
        SKYFS_ERROR("__skyfs_MS_alloc_conflict_ino:not enough left:%d\n", 
            skyfs_conflict_ino_head);
        exit(1);
    }
    pthread_mutex_unlock(&skyfs_conflict_ino.lock);

    meta_id = meta_id | mask;

    ino = __skyfs_MS_compose_ino(dir_id, meta_id);

    SKYFS_MSG("__skyfs_MS_alloc_conflict_ino:meta_id:%d, ino:%llu\n",
        meta_id, ino);

    return ino;

}
void __skyfs_MS_copy_meta(skyfs_meta_t *meta, skyfs_M_cmeta_t *cmeta)
{
    SKYFS_MSG("__skyfs_MS_copy_meta:ino:%llu, nlink:%d, mode:%d, size:%llu\n",
        cmeta->ino, 
		cmeta->nlink, 
		cmeta->mode, 
		cmeta->size);
 
    meta->ino = cmeta->ino;
    meta->size = cmeta->size;
    meta->space = cmeta->space;
    meta->mode = cmeta->mode;
    meta->nlink = cmeta->nlink;
    meta->conflict_index = cmeta->conflict_index;
    meta->uid = cmeta->uid;
    meta->gid = cmeta->gid;
    meta->dev = cmeta->dev;
    meta->type= cmeta->type;
    meta->algorithm = cmeta->eattr.comp_algorithm;
    meta->atime = cmeta->atime;
    meta->ctime = cmeta->ctime;
    meta->mtime = cmeta->mtime;
}

skyfs_s32_t
__skyfs_MS_set_cmeta(skyfs_m_setmeta_args_t *args,
                skyfs_M_cmeta_t *cmeta)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t valid;

    valid = args->valid;
    
    SKYFS_MSG("__skyfs_MS_set_cmeta: enter\n");

    /*
     * This is very simple, without permision checking.
     */    

    if(valid & SKYFS_ATTR_EATTR_FLAG){
	    cmeta->eattr.comp_algorithm = args->algorithm;
	    // added by mayl just set comp_algorithm , then return;

	    SKYFS_ERROR_1("skyfs set file aalgorithm to %d\n", cmeta->eattr.comp_algorithm);
	    return rc;
    }
    if (valid & SKYFS_ATTR_UID)
        cmeta->uid = args->uid;
    if (valid & SKYFS_ATTR_GID)
        cmeta->gid = args->gid;
    if (valid & SKYFS_ATTR_SIZE) {
        SKYFS_ERROR ("__skyfs_MS_set_cmeta:cmeta->size: %llu, args->size:%llu, flag:%d, ctime %lu, mtime %lu, valid %x  \n", 
           cmeta->size, args->size, args->truncate_flag, args->ctime.tv_sec, args->mtime.tv_sec, valid);
        if(args->truncate_flag){
	    SKYFS_ERROR_1("MS_truncate file %s   ino %llu old_size %llu new_size %llu\n ",
			    cmeta->name, cmeta->ino, cmeta->size, args->size);
	    size_t old_size = cmeta->size;
            cmeta->size = args->size;
	    if(S_ISREG(cmeta->mode) ){
		    // added by mayl for truncate obj
		    rc = __skyfs_M2O_truncate_storage(cmeta->ino, old_size, cmeta->size);
	    }



        }
        else if(cmeta->size < args->size){
            cmeta->size = args->size;
        }
	// added by mayl
	if(args->space_changed != 0){
		skyfs_s64_t space = (skyfs_s64_t)cmeta->space;
		space += args->space_changed;
		if(space <0){
			SKYFS_ERROR_1("ERROR:set file space lower than zero! %ld, changed %ld\n", space, args->space_changed);
			space = 0;
		}
		cmeta->space = space;
		//SKYFS_ERROR_1("ERROR:set file space %ld, changed %ld, size %ld\n", space, args->space_changed, cmeta->size);
	}
        SKYFS_MSG ("__skyfs_MS_set_cmeta: cmeta->size: %llu, args->size:%llu\n", 
           cmeta->size, args->size);
        rc = 1;
    }
    if (valid & SKYFS_ATTR_ATIME)
        cmeta->atime = args->atime;
    if (valid & SKYFS_ATTR_MTIME)
        cmeta->mtime = args->mtime;
    if (valid & SKYFS_ATTR_CTIME)
        cmeta->ctime = args->ctime;
    if (valid & SKYFS_ATTR_MODE) 
        cmeta->mode = args->mode;

    if (valid & SKYFS_ATTR_SIZE) {
        gettimeofday(&(cmeta->ctime), NULL);
        //gettimeofday(&(cmeta->atime), NULL);
        gettimeofday(&(cmeta->mtime), NULL);
    }
    if (valid & (SKYFS_ATTR_MODE | SKYFS_ATTR_UID | SKYFS_ATTR_GID)){
        gettimeofday(&(cmeta->ctime), NULL);
        gettimeofday(&(cmeta->atime), NULL);
        gettimeofday(&(cmeta->mtime), NULL);
    }

    if (rc == 1){
        SKYFS_MSG("__skyfs_MS_set_cmeta:finished setattr, and it affects file size\n");
    }
    
    SKYFS_MSG("__skyfs_MS_set_cmeta: leave,rc:%d\n",rc);
 
    return rc;
}

skyfs_s32_t
__skyfs_MS_release_meta(skyfs_M_bmeta_t *bmeta, skyfs_M_mmeta_t *mmeta)
{
    skyfs_s32_t rc = 0;
    skyfs_M_cmeta_t *cmeta = mmeta->cmetap;
    
    SKYFS_ENTER("__skyfs_MS_release_meta:enter,ino:%llu,nlink:%d\n", 
        cmeta->ino, cmeta->nlink);
    
    if(!__skyfs_MS_is_set(mmeta->open_clt, SKYFS_NODE_BM_LEN) && !cmeta->nlink){
    	SKYFS_ERROR("__skyfs_MS_release_meta:ino:%llu,type:%d\n", cmeta->ino, cmeta->type);
        if(S_ISREG(cmeta->mode) ){
		    rc = __skyfs_M2O_free_storage(cmeta->ino, cmeta->size);
        }
        cmeta->ino = 0;
        cmeta->conflict_index = 0;
        rc = 1;    
    }

    SKYFS_LEAVE("__skyfs_MS_release_meta:exit.rc:%d\n", rc);

    return rc;
}

skyfs_s32_t
__skyfs_MS_free_meta(skyfs_M_bmeta_t *bmeta, skyfs_M_mmeta_t *mmeta)
{
    skyfs_M_cmeta_t *cmeta = NULL;
    skyfs_s32_t     firstfree;
    skyfs_s32_t     id = 0;
    skyfs_s32_t     rc = 0;

    SKYFS_ENTER("__skyfs_MS_free_meta:enter\n");

    cmeta = mmeta->cmetap;
    firstfree = bmeta->firstfree;
    id = mmeta->id;
    bmeta->firstfree = id;
    bmeta->nfree ++;
    bzero(cmeta, sizeof(skyfs_M_cmeta_t));
    cmeta->nextfree = firstfree;
    cmeta->type = SKYFS_NULL_META;
    free_meta_cnt ++;

    SKYFS_MSG("__skyfs_MS_free_meta:bmeta_id:%d,firstfree:%d,mmeta_id:%d\n", 
        bmeta->box_id, bmeta->firstfree, mmeta->id);

    bzero(mmeta, sizeof(skyfs_M_mmeta_t));
    pthread_mutex_init(&(mmeta->lock), NULL);
    mmeta->id = id;
    mmeta->cmetap = cmeta;

    SKYFS_LEAVE("__skyfs_MS_free_meta:exit.rc:%d\n",rc);

    return rc;
}

skyfs_s32_t
__skyfs_MS_unlink_meta(skyfs_M_mmeta_t *mmeta)
{
    skyfs_s32_t rc = 0;
    skyfs_M_cmeta_t *cmeta = NULL;

    cmeta = mmeta->cmetap;
    
    SKYFS_ENTER("__skyfs_MS_unlink_meta:enter:ino:%llu,nlink:%d\n", 
        cmeta->ino, cmeta->nlink);

    //mmeta->cmetap->nextfree = -1;
    if(cmeta->nlink > 0){
        cmeta->nlink --;
    }

    if(S_ISDIR(cmeta->mode) && cmeta->nlink < 2){
        cmeta->nlink = 0;
    }

    /*Added for the consideriation of link*/
    if(cmeta->type == SKYFS_LINK){
        rc = cmeta->type;
        goto EXIT;
    }else{
        if(cmeta->nlink == 0){
            mmeta->cmetap->nextfree = -1;
        }
    }

EXIT:
    SKYFS_LEAVE("__skyfs_MS_unlink_meta:exit,rc:%d,nlink:%d\n", 
        rc, cmeta->nlink);
    return rc;
}


skyfs_s32_t
__skyfs_MS_alloc_rename_meta(skyfs_M_bmeta_t *bmeta,
                skyfs_M_cmeta_t **cmeta,
                skyfs_M_mmeta_t **mmeta,
                int ino)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t firstfree;

    SKYFS_MSG("__skyfs_MS_alloc_meta:enter\n");
    //firstfree = bmeta->firstfree;
    firstfree = ino % SKYFS_MAX_META_PER_BOX; // added by mayl , for rename ops, locate the free meta directly
    *cmeta = &(bmeta->cmetap[firstfree]);
    *mmeta = &(bmeta->mmetap[firstfree]);
    SKYFS_ERROR_1("alloc rename meata at bmeta %p , pos %d, cmeta %p ,mmeta %p , ino %lx\n", bmeta, firstfree, *cmeta, *mmeta, ino);
    if((*cmeta)->type != SKYFS_NULL_META){
	SKYFS_ERROR_1("__skyfs_MS_alloc_rename_meta Failed, the meta ino %lx ino have occupied : type %d , ori_ino %lx\n",
		ino, (*cmeta)->type, (*cmeta)->ino);
	bmeta->cmetap[firstfree].type = SKYFS_NULL_META;
	alloc_meta_cnt++;
	//rc = -EEXIST;
	return rc;
    }

   // bmeta->firstfree = (*cmeta)->nextfree;
    alloc_meta_cnt++;
    bmeta->nfree --;

    SKYFS_MSG("__skyfs_MS_alloc_meta:firstfree:%d,nextfree:%d\n", 
        firstfree, bmeta->firstfree);

    SKYFS_LEAVE("__skyfs_MS_alloc_meta:exit,index:%d,nfree:%d\n", 
        firstfree, bmeta->nfree);
    return rc;
}


skyfs_s32_t
__skyfs_MS_alloc_meta(skyfs_M_bmeta_t *bmeta,
                skyfs_M_cmeta_t **cmeta,
                skyfs_M_mmeta_t **mmeta)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t firstfree;

    SKYFS_MSG("__skyfs_MS_alloc_meta:enter\n");
    firstfree = bmeta->firstfree;
    *cmeta = &(bmeta->cmetap[firstfree]);
    *mmeta = &(bmeta->mmetap[firstfree]);

    bmeta->firstfree = (*cmeta)->nextfree;
    alloc_meta_cnt++;
    bmeta->nfree --;

    SKYFS_MSG("__skyfs_MS_alloc_meta:firstfree:%d,nextfree:%d\n", 
        firstfree, bmeta->firstfree);

    SKYFS_LEAVE("__skyfs_MS_alloc_meta:exit,index:%d,nfree:%d\n", 
        firstfree, bmeta->nfree);
    return rc;
}

skyfs_s32_t
__skyfs_MS_init_file(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args)
{
    skyfs_s32_t    rc = 0;

    cmeta->ino= ino;
    cmeta->type = SKYFS_FILE;
    memcpy(cmeta->name, args->name, strlen(args->name));
    cmeta->name[strlen(args->name)] = '\0';
    cmeta->size = 0;
    cmeta->gid = args->gid;
    cmeta->uid = args->uid;
    cmeta->mode = args->mode;
    cmeta->nlink = 1;
    cmeta->dev = 0;
    cmeta->space = 0;
    cmeta->eattr.comp_algorithm = 0;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;
	INIT_LIST_HEAD(&mmeta->posix_lock_head);
	INIT_LIST_HEAD(&mmeta->flock_head);


	SKYFS_ERROR("__skyfs_MS_init_file:ino:%llu,name:%s\n",
		ino, args->name);

    return rc;
}

skyfs_s32_t
__skyfs_MS_init_dir(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args)
{
    skyfs_s32_t    rc = 0;
    skyfs_u32_t osd_id, dir_id;

    cmeta->ino = ino;
    cmeta->type = SKYFS_DIR;
    memcpy(cmeta->name, args->name, strlen(args->name));
    cmeta->name[strlen(args->name)] = '\0';
    cmeta->size = SKYFS_DIR_BLK_SIZE;
    cmeta->space = SKYFS_DIR_BLK_SIZE;
    cmeta->eattr.comp_algorithm  = 0;
    cmeta->gid = args->gid;
    cmeta->uid = args->uid;
    cmeta->mode = args->mode;
    cmeta->nlink = 2;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;
	INIT_LIST_HEAD(&mmeta->posix_lock_head);
	INIT_LIST_HEAD(&mmeta->flock_head);

    dir_id = __skyfs_MS_get_dir_id(ino, 0);
    osd_id = __skyfs_MS_judge_osdid(dir_id, 0);    
    rc = __skyfs_M2O_create_subset_file(osd_id, dir_id);

	SKYFS_ERROR("__skyfs_MS_init_dir:dino:%llu,name:%s\n",
		ino, args->name);

    return rc;
}

skyfs_s32_t
__skyfs_MS_init_node(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_create_args_t *args)
{
    skyfs_s32_t    rc = 0;

    cmeta->ino= ino;
    cmeta->type = SKYFS_NODE;
    memcpy(cmeta->name, args->name, strlen(args->name));
    cmeta->name[strlen(args->name)] = '\0';
    cmeta->size = 0;
    cmeta->gid = args->gid;
    cmeta->uid = args->uid;
    cmeta->mode = args->mode;
    cmeta->nlink = 1;
    cmeta->dev = args->dev;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;

    return rc;
}

skyfs_s32_t
__skyfs_MS_init_rename_cmeta(skyfs_M_cmeta_t *meta,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_ino_t ino,
                skyfs_s8_t *name)
{
    skyfs_s32_t    rc = 0;

    cmeta->ino= ino;
    cmeta->type = meta->type;
    //memcpy(cmeta->name, name, strlen(name));
    //cmeta->name[strlen(name)] = '\0';
    cmeta->size = meta->size;
    cmeta->gid = meta->gid;
    cmeta->uid = meta->uid;
    cmeta->mode = meta->mode;
    cmeta->nlink = meta->nlink;
    cmeta->dev = meta->dev;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;

    SKYFS_MSG("__skyfs_MS_int_cmeta:ino:%llu\n", meta->ino);    
    SKYFS_MSG("__skyfs_MS_int_cmeta:nlink:%u\n", meta->nlink);    
    return rc;
}


skyfs_s32_t
__skyfs_MS_init_cmeta(skyfs_meta_t *meta,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_ino_t ino,
                skyfs_s8_t *name)
{
    skyfs_s32_t    rc = 0;

    cmeta->ino= ino;
    cmeta->type = meta->type;
    memcpy(cmeta->name, name, strlen(name));
    cmeta->name[strlen(name)] = '\0';
    cmeta->size = meta->size;
    cmeta->gid = meta->gid;
    cmeta->uid = meta->uid;
    cmeta->mode = meta->mode;
    cmeta->nlink = meta->nlink;
    cmeta->dev = meta->dev;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;

    SKYFS_MSG("__skyfs_MS_int_cmeta:ino:%llu\n", meta->ino);    
    SKYFS_MSG("__skyfs_MS_int_cmeta:nlink:%u\n", meta->nlink);    
    return rc;
}

skyfs_s32_t
__skyfs_MS_init_symlink(skyfs_ino_t ino,
                skyfs_M_mmeta_t *mmeta,
                skyfs_M_cmeta_t *cmeta,
                skyfs_m_symlink_args_t *args)
{
    skyfs_s32_t    rc = 0;
    skyfs_s32_t    fd = 0;
    skyfs_s8_t  symlinkdir[SKYFS_MAX_NAME_LEN];
    struct stat buf;

    cmeta->ino= ino;
    cmeta->type = SKYFS_FILE;
    memcpy(cmeta->name, args->name, strlen(args->name));
    cmeta->name[strlen(args->name)] = '\0';
    cmeta->size = strlen(args->target) + 1;
    cmeta->gid = args->gid;
    cmeta->uid = args->uid;
    cmeta->mode = args->mode;
    cmeta->nlink = 1;
    cmeta->dev = 0;
    gettimeofday(&(cmeta->atime), NULL);
    cmeta->ctime = cmeta->mtime = cmeta->atime;
    mmeta->cmetap = cmeta;
     
    sprintf(symlinkdir, "%s/%s", SKYFS_LOCAL_META_PATH, "symlink/");
    if((rc = stat(symlinkdir, &buf)) == -1){
        SKYFS_ERROR("__skyfs_MS_int_symlink:%s not exist\n", symlinkdir);    
        if((rc = mkdir(symlinkdir, 0666)) < 0){
            rc = -errno;
            SKYFS_ERROR("__skyfs_create_chunkdir:mk chunkdir:%s err:%d\n", 
                symlinkdir, rc);
            goto ERR;
        }
    }
   
    bzero(symlinkdir,SKYFS_MAX_NAME_LEN);
    sprintf(symlinkdir, "%s/%s/%llu", SKYFS_LOCAL_META_PATH, "symlink/", ino);
    fd = open(symlinkdir, O_RDWR|O_CREAT, 0666);
    if(fd > 0){
        rc = write(fd, args->target, strlen(args->target)+1);
        if(rc < 0){
            SKYFS_ERROR("__skyfs_MS_init_symlink:write %s in %s err:%d\n", 
                args->target, symlinkdir, rc);
        }
        close(fd);
    }else{
        SKYFS_ERROR("__skyfs_MS_init_symlink:open %s err:%d\n", 
            symlinkdir, rc);
        rc = -errno;
    }

ERR:

    return rc;
}

skyfs_s32_t
__skyfs_MS_get_symlink(skyfs_M_cmeta_t *cmeta, skyfs_s8_t *buf)
{
    skyfs_s8_t  symlinkdir[SKYFS_MAX_NAME_LEN]; 
    skyfs_s32_t fd = 0;
    skyfs_s32_t rc = 0;

    sprintf(symlinkdir, "%s/%s/%llu", SKYFS_LOCAL_META_PATH, "symlink/", cmeta->ino);

    fd = open(symlinkdir, O_RDONLY);
    if(fd > 0){
        rc = read(fd, buf, cmeta->size + 1);
        if(rc < 0){
            rc = -errno;
            SKYFS_ERROR("__skyfs_MS_get_symlink:read :%s err:%d\n", 
                symlinkdir, rc);
        }
        close(fd);
    }else{
        rc = -errno;
    }

    return rc;
}
skyfs_s32_t
__skyfs_MS_enlarge_subset(skyfs_M_subset_cache_t * subset_cache)
{
    amp_request_t *req = NULL;
    struct list_head *head = NULL, *index = NULL, *tmp = NULL;
    skyfs_M_bmeta_t *bmeta = NULL;
    skyfs_u32_t dir_id, subset_id, osd_id;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR_1("__skyfs_MS_enlarge_subset:enter.dir_id:%d,sbid:%d,sbdepth:%d\n",
        subset_cache->dir_id,subset_cache->subset_id,subset_cache->subset_depth);

    pthread_mutex_lock(&subset_cache->lock);

    __skyfs_MS_profile_enlarge();

    /*1.write back the subset' bmeta to the disk*/
    rc = __skyfs_MS_writeback_subset(subset_cache);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_enlarge_subset:writeback subset err:%d\n", rc);
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

    /*2.enlarge the subset file in OSD*/
    dir_id = subset_cache->dir_id;
    subset_id = subset_cache->subset_id;
    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    req = __skyfs_M2O_enlarge_subset_file(osd_id, dir_id, subset_id);

    amp_sem_down(&(req->req_waitsem));


    /*3.enlarge the subset cache and related cache*/
    subset_cache->subset_depth ++;

ERR:

    pthread_mutex_unlock(&subset_cache->lock);
    SKYFS_LEAVE("__skyfs_MS_enlarge_subset:exit\n");
    return rc;
}

skyfs_s32_t
__skyfs_MS_split_subset(skyfs_M_subset_cache_t *subset_cache)
{
    struct list_head *head = NULL, *index = NULL, *tmp = NULL;
    skyfs_M_bmeta_t *bmeta = NULL;
    amp_request_t *req = NULL;
    skyfs_u32_t dir_id, subset_id, osd_id;
    skyfs_u32_t split_depth;
    skyfs_u32_t subset_depth;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR_1("__skyfs_MS_split_subset:enter.dir_id:%d, subset_id:%d, sp depth %d\n",
        subset_cache->dir_id, subset_cache->subset_id,  subset_cache->split_depth);

    pthread_mutex_lock(&subset_cache->lock);

    /*1.write back the subset' bmeta to the disk*/
    rc = __skyfs_MS_writeback_subset(subset_cache);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_split_subset:writeback subset err:%d\n", rc);
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

    /*2.split the subset file in OSD*/
    dir_id = subset_cache->dir_id;
    subset_id = subset_cache->subset_id;
    split_depth = subset_cache->split_depth;
    subset_depth = subset_cache->subset_depth;
    osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
    req = __skyfs_M2O_split_subset_file(osd_id, dir_id, subset_id, 
            split_depth, subset_depth);

    amp_sem_down(&(req->req_waitsem));

    /*3.modify the split op relasted cache */
    split_depth = subset_cache->split_depth;
    split_depth ++;

    rc = __skyfs_MS_update_dir_depth(dir_id, subset_id, split_depth);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_split_subset:update dir depth failed\n");
        goto ERR;
    }

    subset_cache->split_depth ++;

    __skyfs_MS_profile_split();
    //rc = __skyfs_MS_write_subset(subset_cache);
    //if(rc < 0){
    //    SKYFS_ERROR("__skyfs_MS_split_subset:write back subset_head error!!\n");
    //    goto ERR;
    //}
ERR:

    pthread_mutex_unlock(&subset_cache->lock);

    SKYFS_LEAVE("__skyfs_MS_split_subset:exit.rc:%d\n", rc);
    return rc;
}

skyfs_s32_t
__skyfs_MS_update_dir_info(skyfs_M_dir_cache_t *dir_cache)
{
    skyfs_s32_t rc = 0;

    dir_cache->depth ++;
    dir_cache->cmeta.depth ++;

    /*update the cmeta?*/

    return rc;
}

skyfs_s32_t
__skyfs_MS_do_split_dir(skyfs_M_dir_cache_t *dir_cache)
{
    struct list_head *index = NULL, *head = NULL;
    amp_request_t **req = NULL;
    skyfs_M_subset_index_t *subset_index = NULL;
    skyfs_u32_t osd_id;
    skyfs_u32_t dir_id;
    skyfs_u32_t subset_id;
    skyfs_u32_t split_depth = 0;
    skyfs_u32_t subset_depth = 0;
    skyfs_s32_t rc = 0;
    skyfs_u32_t i = 0, j = 0;

    dir_id = dir_cache->dir_id;

    SKYFS_ENTER("__skyfs_MS_do_split_dir:enter,dir_id:%d\n", dir_id);
    head = &(dir_cache->subset_head);
    list_for_each(index, head){
        subset_index = list_entry(index, skyfs_M_subset_index_t, subset_list);
        subset_id = subset_index->subset_id;
        osd_id = __skyfs_MS_judge_osdid(dir_id, subset_id);
        req[i] = __skyfs_M2O_split_subset_file(osd_id, dir_id, subset_id, 
            split_depth, subset_depth);
        i ++;
    }

    for(j = 0; j < i; j ++){
        amp_sem_down(&(req[j]->req_waitsem));
    }

    SKYFS_LEAVE("__skyfs_MS_do_split_dir:exit,rc:%d\n", rc);
    return rc;
}

/*This fountion is actually not been used at all*/
skyfs_s32_t
__skyfs_MS_split_dir(skyfs_ino_t dir_ino)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t mds_id;
    skyfs_u32_t dir_id;
    skyfs_u32_t hashvalue;
    skyfs_htb_t *htbp = NULL;
    skyfs_M_dir_cache_t *dir_cache = NULL;

    dir_id = __skyfs_MS_get_dir_id(dir_ino, 0);
    mds_id = __skyfs_MS_judge_mdsid(dir_id, 0);
    if(mds_id != mds_this_id){
        /*send req to mds to do split dir*/
        SKYFS_ERROR("__skyfs_MS_split_dir:send to mds %d to split dir\n",
            mds_id);
            
    }else{
        hashvalue = __skyfs_get_subset_hashvalue(dir_id, 0);
        hashvalue = hashvalue % SKYFS_DIR_HASH_LEN;
        htbp = &(skyfs_dir_cache_htbbase[hashvalue]);
        dir_cache = __skyfs_MS_find_dir_cache(htbp, dir_id);
        if(dir_cache == NULL){
            SKYFS_ERROR("__skyfs_MS_split_dir:can't get dir_cache\n");
            goto ERR;
        }
    /*1 release all the subset cache*/
        __skyfs_MS_do_clear_dir_cache(dir_cache);
    /*2 split dir at osd*/
        __skyfs_MS_do_split_dir(dir_cache);
    /*3 update the dir info*/
        __skyfs_MS_update_dir_info(dir_cache);
    }

ERR:
    return rc;
}

skyfs_s32_t __skyfs_MS_delete_obd_file(skyfs_M_cmeta_t *cmeta)
{
    skyfs_s32_t rc = 0;
    SKYFS_ENTER("__skyfs_MS_delete_obd_file:enter\n");


    SKYFS_LEAVE("__skyfs_MS_delete_obd_file:exit\n");
    return rc;
}

skyfs_s32_t __skyfs_MS_get_metaino()
{
	skyfs_M_ino_t metaino;
	skyfs_s8_t    path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t   fd;
    skyfs_s32_t   rc = 0;
	
    sprintf(path_name, "%s/%s", SKYFS_LOCAL_META_PATH,"meta_ino_info");
	fd = open(path_name, O_RDONLY);
	if(fd > 0){
        rc = read(fd, &metaino, sizeof(skyfs_M_ino_t));
		if(rc < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_MS_get_metaino,%s,rc:%d\n", 
				path_name, rc);
			goto ERR;
		}

		skyfs_free_dir_id_head = metaino.dirid_head;
	    skyfs_conflict_ino_head = metaino.conflict_ino;

	}else{
		SKYFS_ERROR("__skyfs_MS_get_metaino,err open:%s,%d\n", 
			path_name, errno);
		rc = -errno;
	}

ERR:
	if(fd){
        close(fd);
	}

	return rc;
}

skyfs_s32_t __skyfs_MS_writeback_metaino()
{
	skyfs_M_ino_t metaino;
    skyfs_s8_t    path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t   fd;
    skyfs_s32_t   rc = 0;

	metaino.dirid_head = skyfs_free_dir_id_head;
	metaino.conflict_ino = skyfs_conflict_ino_head;

    sprintf(path_name, "%s/%s", SKYFS_LOCAL_META_PATH,"meta_ino_info");
	fd = open(path_name, O_RDWR|O_CREAT, 0666);
	if(fd > 0){
        rc = write(fd, &metaino, sizeof(skyfs_M_ino_t));
		if(rc < 0){
			rc = -errno;
			SKYFS_ERROR("__skyfs_MS_writeback_metaino,%s,rc:%d\n", 
				path_name, rc);
			goto ERR;
		}
	}else{
		SKYFS_ERROR("__skyfs_MS_writeback_metaino,err open:%s,%d\n", 
			path_name, errno);
	}

ERR:
	if(fd){
        close(fd);
	}

	return rc;
}

/*This is end of mds_meta.c*/
