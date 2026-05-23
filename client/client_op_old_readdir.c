/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_op.c $
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
#include <sys/syscall.h> 
// added by mayl
#include <fcntl.h>
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



extern skyfs_DL_depth_t skyfs_dl_depth;

extern struct list_head client_pending_list;
extern pthread_mutex_t         client_pending_lock; // added by mayl


static pid_t gettid()
{
	return syscall(SYS_gettid);
}

static char 
*discard_prefix(char *st)
{
    char    *p;

    p = st;
    while (*p==' ' || *p=='\t') p++;
    while (*p=='/') p++;

    return(p);
}

static char 
*next_component(char *curp, char **next)
{
    *next = discard_prefix(curp);
    curp = *next;
    
    while (strlen(curp) > 0 && *curp != '/') curp++;
    if (*curp=='/') {
        *curp = '\0';
        curp++;
    }

    return(curp);
}

static char *
__skyfs_C_get_lastname(char *pathname)
{
    char *current, *tmp;

    tmp = pathname;

    while (strlen(tmp) != 0) {
        tmp = next_component(tmp, &current);
    }

    return current;
}

skyfs_s32_t
__skyfs_C_path_walk(char *pathname, 
                char **lastname, 
                skyfs_ino_t *pino, 
                skyfs_ino_t *ino, 
                skyfs_u32_t *conflict)
{
    char    *current, *tmp;
	skyfs_s8_t root[SKYFS_MAX_NAME_LEN];
    int        rc = 0;
    skyfs_ino_t parent_ino, this_ino;
    skyfs_u32_t conflict_index;

    SKYFS_ENTER("__skyfs_C_path_walk: pathname=%s,namelen:%d\n", pathname, rc);

    tmp = pathname;
    SKYFS_MSG("__skyfs_C_path_walk:tmp=%s\n",tmp);
	strcpy(root, "/");
    current = root; 
    this_ino = 1;        /* means fs root */
    while (strlen(tmp) != 0) {
        parent_ino = this_ino;
        tmp = next_component(tmp, &current);
        SKYFS_MSG("__skyfs_C_path_walk: tmp=[%s], current=[%s] \n", tmp, current);
        if(strlen(current) == 0){
            SKYFS_MSG("__skyfs_C_path_walk: it's root:path:%s \n", pathname);
            *pino = this_ino;
            *ino = this_ino;
            *conflict = 0;
            rc = 2;
            break;
        }

		rc = __skyfs_C_lookup(parent_ino, current, &this_ino, &conflict_index);
        if(rc != 2){
            rc = __skyfs_C2M_lookup(parent_ino, current, &this_ino, &conflict_index);
            if(rc == 2){
                /*Add into dentry cache*/ 
                __skyfs_C_add_dentry(parent_ino, current, this_ino, conflict_index);
            }
        }
 
        if (strlen(tmp) != 0) {
            /* lookup for intermediate names */
            if (rc != 2) {
                /* lookup error or lookup failed */
                *pino=0;
                *lastname=NULL;
                SKYFS_DEBUG("__skyfs_C_path_walk:path:%s,tmp:%s.exit error.\n",
                    pathname,tmp);
                return(-1);
            }
            SKYFS_MSG("__skyfs_C_path_walk: next \n");
        }
        else {
            /* lookup for the last name */
            SKYFS_MSG("__skyfs_C_path_walk: last, rc=%d \n", rc);
            switch (rc) {
                case 0:
                    /* not find it */
                    *pino = parent_ino;
                    *ino = (long long int)-1;
                    *lastname = current;
                    SKYFS_DEBUG("__skyfs_C_path_walk: rc:%d,lastnaem:%s\n",
                        rc, *lastname);
                    break;
                case 2:
                    /* find it */
                    SKYFS_MSG("__skyfs_C_path_walk: rc=2 \n");
                    *pino = parent_ino;
                    *ino = this_ino;
                    *lastname = current;
                    *conflict = conflict_index;
                    break;
                default:
                    /* error occured */
                    SKYFS_ERROR("__skyfs_C_path_walk: rc=%d\n",rc);
                    *pino = parent_ino;
                    *ino = (long long int)-1;
                    *lastname = current;
                    break;
            }
        }
    }
        
    SKYFS_LEAVE("__sunC_path_walk:exit rc=%d pino=%llu ino=%llu lastname=%s,addr%p\n", 
        rc, *pino, *ino, *lastname, *lastname);
    return(rc);
}

int skyfs_statfs(const char *path, struct statvfs *stbuf)
{
    int rc = 0;
    
    (void) path;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;
    my_pending.req_pointer = (uint64_t)stbuf;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 601; // statfs op 601
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);


	//struct statvfs stbuf123;

    SKYFS_ERROR_1("skyfs_statfs:enter:%s\n", path);

    
    //rc = statvfs(path, stbuf);
    __skyfs_C2M_statfs(stbuf);
	/*
	*/
	stbuf->f_frsize = 4096;
	stbuf->f_namemax = 255;
    SKYFS_MSG("skyfs_statfs:f_bsize:%lu\n", stbuf->f_bsize);
    SKYFS_MSG("skyfs_statfs:f_frsize:%lu\n", stbuf->f_frsize);
    SKYFS_MSG("skyfs_statfs:f_blocks:%lu\n", stbuf->f_blocks);
    SKYFS_MSG("skyfs_statfs:f_bfree:%lu\n", stbuf->f_bfree);
    SKYFS_MSG("skyfs_statfs:f_bavail:%lu\n", stbuf->f_bavail);
    SKYFS_MSG("skyfs_statfs:f_files:%lu\n", stbuf->f_files);
    SKYFS_MSG("skyfs_statfs:f_ffree:%lu\n", stbuf->f_ffree);
    SKYFS_MSG("skyfs_statfs:f_ffavail:%lu\n", stbuf->f_favail);
    SKYFS_MSG("skyfs_statfs:f_fsid:%lu\n", stbuf->f_fsid);
    SKYFS_MSG("skyfs_statfs:f_flag:%lu\n", stbuf->f_flag);
    SKYFS_MSG("skyfs_statfs:f_namemax:%lu\n", stbuf->f_namemax);	
/*
*/
    SKYFS_ERROR_1("skyfs_statfs:exit,rc:%d, check long wait\n", rc);
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    
    gettimeofday(&tv, NULL);
    pthread_mutex_lock(&client_pending_lock);
    list_for_each(index,&client_pending_list){
	    
	    tmp_req = list_entry(index, struct skyfs_pending_req, pending_entry);
	    uint64_t now = (uint64_t)(tv.tv_sec)*1000000+tv.tv_usec;
	    if(now > tmp_req->start_time + 5000){
		    SKYFS_ERROR_1(" req %p  op %d, LONG pending time %llu\n", tmp_req->req_pointer ,tmp_req->op, now-tmp_req->start_time);
	    } 


    }   
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_statfs:exit,rc:%d, check long wait end\n", rc);

    return 0;
}

int skyfs_open(const char *path, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s32_t rc = 0;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;
    
    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 602; // open op 602
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);


    strcpy(path_name, path);

    SKYFS_ENTER("skyfs_open:enter:%s\n", path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);

    if(rc == 0){
        SKYFS_MSG("skyfs_open:%s does not exit\n", path);
    }else if (rc == 2){
        SKYFS_MSG("skyfs_open:has find %s.\n", lastname);
        //rc2 = __skyfs_C2M_release(ino, conflict_index);
        fi->fh = ino;
        fi->conflict_index = conflict_index;
        rc = 0;
    }else{
        SKYFS_MSG("skyfs_open:path_walk error.rc:%d\n", rc);
    }
    SKYFS_LEAVE("skyfs_open:exit\n");

    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    return rc;
}

int skyfs_opendir(const char *path, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s32_t rc = 0;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 603; // opendir op 603
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    strcpy(path_name, path);

    SKYFS_ENTER("skyfs_opendir:enter:%s\n", path);
#if 1
    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);

    if(rc == 0){
        SKYFS_MSG("skyfs_opendir:%s does not exit\n", path);
    }else if (rc == 2){
        SKYFS_MSG("skyfs_opendir:has find %s.\n", lastname);
        //rc2 = __skyfs_C2M_release(ino, conflict_index);
        fi->fh = ino;
        fi->conflict_index = conflict_index;
        rc = 0;
    }else{
        SKYFS_MSG("skyfs_opendir:path_walk error.rc:%d\n", rc);
    }
    SKYFS_LEAVE("skyfs_opendir:exit\n");
#endif
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}


int skyfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 604; // creatre op 604
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    
    //SKYFS_ERROR_1("skyfs_create:enter:%s\n", path);

    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_MSG("skyfs_create:%s does not exit\n", path);
        rc = __skyfs_C2M_create(pino, lastname, mode, &ino, &conflict_index);
        fi->fh = ino;
        fi->conflict_index = conflict_index;
    }else if (rc == 2){
        SKYFS_ERROR("skyfs_create:has find %s.\n", lastname);
        //rc2 = __skyfs_C2M_release(ino, conflict_index);
        fi->fh = ino;
        fi->conflict_index = conflict_index;
    }else{
        SKYFS_ERROR("skyfs_create:path_walk error.rc:%d\n", rc);
    }
    //SKYFS_ERROR_1("skyfs_create:exit %d, path %s\n", rc, path);

    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    
    if(rc >= 0){
#ifdef __SKYFS_DBTEST__
	    int c_fd = 0;
            memset(path_name,0,SKYFS_MAX_NAME_LEN);
	    sprintf(path_name,"%s/%s", "/mnt/local/", path);
	    c_fd = open(path_name , O_CREAT|O_RDWR, 0666);
	    if(c_fd <0){
		    SKYFS_ERROR_1("can not create local_file %s\n", path_name);
	    }else{
		    close(c_fd);
	    }

#endif
    }

    return rc;
}

int skyfs_mkdir(const char *path, mode_t mode)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 605; // mkdir 605
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_mkdir:enter:%s\n", path);
    
    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_MSG("skyfs_mkdir:%s does not exit\n", path);
        rc = __skyfs_C2M_create(pino, lastname, (S_IFDIR|mode), &ino, &conflict_index);
	}else if (rc == 2){
        SKYFS_ERROR("skyfs_mkdir:has find %s.\n", lastname);
	}else{
        SKYFS_ERROR("skyfs_mkdir:path_walk error.rc:%d\n", rc);
    }
    SKYFS_LEAVE("skyfs_mkdir:exit\n");
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    if(rc >=0 ){
 #ifdef __SKYFS_DBTEST__
	    int mkret = 0;
            memset(path_name,0,SKYFS_MAX_NAME_LEN);
	    sprintf(path_name,"%s/%s", "/mnt/local/", path);
	    mkret = mkdir(path_name, mode);
	    if(mkret <0){
		    SKYFS_ERROR_1("can not create directory %s\n", path_name);
	    }
		  
#endif

    }

    return rc;
}

int skyfs_unlink(const char *path)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    int rc2 = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 606; // unlink  606
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);


    SKYFS_ERROR_1("skyfs_unlink file :enter:%s\n", path);

    if(strstr(path, "share") && strstr(path, "wal") && strstr(path, "data")){
    	SKYFS_ERROR_1("skyfs_unlink wal file :enter:%s\n", path);
    }

    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);

    if(rc == 0){
        SKYFS_MSG("skyfs_unlink:%s does not exit\n", path);
    }else if (rc == 2){
        SKYFS_MSG("skyfs_unlink:has find %s.\n", lastname);
        rc = __skyfs_C2M_remove(pino, lastname, ino, conflict_index);
        if (rc < 0){
            SKYFS_ERROR("skyfs_unlink:unlink %s error:%d\n", path,rc);
            goto err_out;
        }
        
        rc2 = __skyfs_C2M_release(ino, conflict_index);
        if(rc2 < 0){
            SKYFS_ERROR("skyfs_unlink: release %s failed,rc:%d\n", path, rc2);
			goto err_out;
        }

		rc2 = __skyfs_C_release_dentry(pino, lastname);
        if(rc2 < 0){
            SKYFS_ERROR("skyfs_unlink: release dentry %s failed,rc:%d\n", 
				path, rc2);
			goto err_out;
        }
    }else{
        SKYFS_MSG("skyfs_unlink:path_walk error.rc:%d\n", rc);
    }

err_out:
    SKYFS_ERROR_1("skyfs_unlink:exit,%s.rc:%d\n", path, rc);
    
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    if(rc >= 0){
#ifdef __SKYFS_DBTEST__
	    int ret = 0;
            memset(path_name,0,SKYFS_MAX_NAME_LEN);
	    sprintf(path_name,"%s/%s", "/mnt/local/", path);
	    ret  = unlink(path_name);
	    if(ret <0){
		    SKYFS_ERROR_1("can not unlink local_file %s, try rmdir\n", path_name);
		    ret = rmdir(path_name);
		    if(ret < 0){
		    	SKYFS_ERROR_1("can not rmdir local_file %s, try rmdir\n", path_name);
		    }else{
		    	SKYFS_ERROR_1(" rmdir local_file %s, try rmdir\n", path_name);
		    }

	    }else{
		    	SKYFS_ERROR_1("unlink local_file %s, try rmdir\n", path_name);
	    }

#endif
    }

    return rc;
}

int skyfs_rename(const char *from, const char *to)
{
    skyfs_ino_t pino_from = (long long int)-1;
    skyfs_ino_t ino_from = (long long int)-1;
    skyfs_u32_t conflict_index_from;

    skyfs_ino_t pino_to = (long long int)-1;
    skyfs_ino_t ino_to = (long long int)-1;
    skyfs_u32_t conflict_index_to;

    skyfs_s8_t  *lastname_from = NULL;
    skyfs_s8_t  *lastname_to = NULL;
    skyfs_s8_t  path_name_from[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t  path_name_to[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t  path_lname_from[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t  path_lname_to[SKYFS_MAX_NAME_LEN];

    skyfs_s32_t rc = 0;
    skyfs_s32_t rc2 = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)to;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 607; // rename  607
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_rename:from:%s,to:%s\n", from, to);

    strcpy(path_name_from, from);
    strcpy(path_name_to, to);

    rc = __skyfs_C_path_walk(path_name_to, &lastname_to, &pino_to, 
            &ino_to, &conflict_index_to);
    if(rc == 2){
        SKYFS_ERROR("skyfs_rename:path_walk_to:%s exist,remove it.\n", path_name_to);
        rc = skyfs_unlink(to);
    }else if(rc != 0 && rc != 2){
        SKYFS_ERROR("skyfs_rename:path_walk_to:%s error.rc:%d:\n", 
            path_name_to, rc);
        goto err_out;
    }

    rc = __skyfs_C_path_walk(path_name_from, &lastname_from, &pino_from, 
            &ino_from, &conflict_index_from);
    if(rc == 2){
	    strcpy(path_lname_from, lastname_from);
	    strcpy(path_lname_to, lastname_to);

        rc = __skyfs_C2M_rename(pino_from, pino_to, path_lname_from, path_lname_to, 
                ino_from, conflict_index_from,
                &ino_to, &conflict_index_to);
        SKYFS_ERROR("skyfs_rename:lastname %s\n", path_lname_from);
		rc2 = __skyfs_C_release_dentry(pino_from, path_lname_from);
    }else if (rc == 0){
        SKYFS_ERROR("skyfs_rename:has not find %s.\n", path_lname_from);
        rc = -ENOENT;
    }else{
        SKYFS_ERROR("skyfs_rename:path_walk %s error.rc:%d\n", 
            path_name_from, rc);
    }

err_out:
    if(rc >=0 ){
#ifdef __SKYFS_DBTEST__
	    int rc_ren = 0;
	    memset(path_name_from, 0, SKYFS_MAX_NAME_LEN);
	    memset(path_name_to, 0, SKYFS_MAX_NAME_LEN);
	    sprintf(path_name_from,"/mnt/local/%s", from );
	    sprintf(path_name_to,"/mnt/local/%s", to );
	    rc_ren = rename(path_name_from, path_name_to);
	    if(rc_ren<0){
		    SKYFS_ERROR_1("falied to rename %s->%s\n", path_name_from, path_name_to);
	    }
	    SKYFS_ERROR_1("rename %s->%s\n", path_name_from, path_name_to);
#endif

    }
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_rename:exit:rc:%d,from:%s,to:%s\n", rc, from, to);

    return rc;
}

int skyfs_release(const char *path, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 608; // release  608
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_release:enter:%s\n", path);

    strcpy(path_name, path);

    //lastname = __skyfs_C_get_lastname(path_name);
	//conflict_index = __skyfs_name2hashvalue(lastname);

    if(fi->flock_release){
	    SKYFS_ERROR("should do UNLOCK FLOCK when close/release file %s\n", path_name);
	    skyfs_flock(path, fi, LOCK_UN);

    }
    if(fi->fh > 0 && fi->conflict_index > 0){
        SKYFS_ERROR("skyfs_release:%s,fh:%lu,padding:%u\n",
			path_name, fi->fh, conflict_index);
        ino = fi->fh;
		conflict_index = fi->conflict_index;
        rc = __skyfs_C2M_release(ino, conflict_index);
    }else{
        rc = __skyfs_C_path_walk(path_name, &lastname, 
				&pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_MSG("skyfs_release:%s does not exit\n", path_name);
        }else if (rc == 2){
            SKYFS_MSG("skyfs_release:find %s,%llu.\n", lastname, ino);
            rc = __skyfs_C2M_release(ino, conflict_index);
            if (rc < 0){
                SKYFS_ERROR("skyfs_release: unlink %s failed\n", path_name);
                goto err_out;
            }
            fi->fh = ino;
            fi->conflict_index = conflict_index;
        }else{
            SKYFS_MSG("skyfs_release:path_walk error.rc:%d\n", rc);
        }
    }
err_out:
    SKYFS_LEAVE("skyfs_release:exit.rc:%d,path:%s\n", rc, path_name);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}


/*flock op system call */
int skyfs_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int cmd;
	int xop;
	struct flock lock;
	skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
	int retry_cnt = 0;
    int rc = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 609; // flock 609
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_flock:enter:%s\n", path);
	/* set cmd and struct flock according to op */
	cmd = F_SETLKW;
	if(op & LOCK_NB)
		cmd = F_SETLK;
	xop = op & (~LOCK_NB);
	if(xop == LOCK_SH)
		lock.l_type = F_RDLCK;
	if(xop == LOCK_EX)
		lock.l_type = F_WRLCK;
	if(xop == LOCK_UN)
		lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_len = OFFSET_MAX;

    strcpy(path_name, path);

    //lastname = __skyfs_C_get_lastname(path_name);
	//conflict_index = __skyfs_name2hashvalue(lastname);

    if(fi->fh > 0 && fi->conflict_index > 0){
        SKYFS_ERROR("skyfs_flock:%s,fh:%lu,padding:%u\n",
			path_name, fi->fh, conflict_index);
        ino = fi->fh;
		conflict_index = fi->conflict_index;
		
		do{
			rc = __skyfs_C2M_lock(ino, conflict_index, cmd | (2<<16), fi->lock_owner, &lock);
			if(rc == EAGAIN || rc == -EAGAIN)
				usleep(1000);
			retry_cnt++;
		}while((retry_cnt < (1000*SKYFS_LOCKW_RETRY)) && (rc == -EAGAIN) && (cmd == F_SETLKW));

    }else{
        rc = __skyfs_C_path_walk(path_name, &lastname, 
				&pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_MSG("skyfs_flock:%s does not exist\n", path_name);
			rc = -ENOENT;
        }else if (rc == 2){
            SKYFS_MSG("skyfs_flock:find %s,%llu.\n", lastname, ino);
			do{
				rc = __skyfs_C2M_lock(ino, conflict_index, cmd | (2<<16), fi->lock_owner, &lock);
				if(rc == EAGAIN || rc == -EAGAIN)
					usleep(1000);
				retry_cnt++;
			}while((retry_cnt < (1000*SKYFS_LOCKW_RETRY)) && (rc == -EAGAIN) && (cmd == F_SETLKW));

            //rc = __skyfs_C2M_lock(ino, conflict_index, cmd|(1<<16), fi->lock_owner, lock);
            if (rc < 0){
                SKYFS_ERROR("skyfs_flock: %s failed, %d\n", path_name, rc);
                goto err_out;
            }
            fi->fh = ino;
            fi->conflict_index = conflict_index;
        }else{
            SKYFS_MSG("skyfs_release:path_walk error.rc:%d\n", rc);
        }
    }
err_out:
    if(rc<0 && rc != -2)
    	SKYFS_ERROR_1("skyfs_flock:exit.rc:%d,path:%s\n", rc, path_name);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    return rc;



}


/*posix lock op*/
int skyfs_lock(const char *path, struct fuse_file_info *fi, int cmd, struct flock * lock)
{
	skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
	int retry_cnt = 0;
    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 610; // lock 610
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_lock:enter:%s\n", path);

    strcpy(path_name, path);

    //lastname = __skyfs_C_get_lastname(path_name);
	//conflict_index = __skyfs_name2hashvalue(lastname);

    if(fi->fh > 0 && fi->conflict_index > 0){
        SKYFS_ERROR("skyfs_lock:%s,fh:%lu,padding:%u\n",
			path_name, fi->fh, conflict_index);
        ino = fi->fh;
		conflict_index = fi->conflict_index;
		
		do{
			rc = __skyfs_C2M_lock(ino, conflict_index, cmd | (1<<16), fi->lock_owner, lock);

			if(rc == EAGAIN || rc == -EAGAIN)
				usleep(1000);
			retry_cnt++;
		}while((retry_cnt < (1000*SKYFS_LOCKW_RETRY)) && (rc == -EAGAIN) && (cmd == F_SETLKW));

    }else{
        rc = __skyfs_C_path_walk(path_name, &lastname, 
				&pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_MSG("skyfs_lock:%s does not exist\n", path_name);
			rc = -ENOENT;
        }else if (rc == 2){
            SKYFS_MSG("skyfs_lock:find %s,%llu.\n", lastname, ino);
			do{
				rc = __skyfs_C2M_lock(ino, conflict_index, cmd | (1<<16), fi->lock_owner, lock);
				if(rc == EAGAIN || rc == -EAGAIN)
					usleep(1000);
				retry_cnt++;
			}while((retry_cnt < (1000*SKYFS_LOCKW_RETRY)) && (rc == -EAGAIN) && (cmd == F_SETLKW));

            //rc = __skyfs_C2M_lock(ino, conflict_index, cmd|(1<<16), fi->lock_owner, lock);
            if (rc < 0){
                SKYFS_ERROR("skyfs_lock: %s failed, %d\n", path_name, rc);
                goto err_out;
            }
            fi->fh = ino;
            fi->conflict_index = conflict_index;
        }else{
            SKYFS_MSG("skyfs_release:path_walk error.rc:%d\n", rc);
        }
    }
err_out:
    SKYFS_LEAVE("skyfs_release:exit.rc:%d,path:%s\n", rc, path_name);
	if(rc == 0 && cmd == F_GETLK){
		lock->l_type = F_UNLCK;
	}else if(cmd == F_GETLK){
		SKYFS_ERROR("client get lock failed , lstart %d\n",lock->l_start );
		if(rc == -EAGAIN)
			rc = 0;
	}
	
	
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    if(rc<0 && rc != -2)
    	SKYFS_ERROR_1("skyfs_lock:%s RET %d\n", path_name,rc);
    
    return rc;

}

int skyfs_getattr(const char *path, struct stat *stbuf)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    struct fuse_file_info fi;
    int rc = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)stbuf;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 611; // getattr  611
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_getattr:enter:%s\n", path);
    if(strstr(path, "share") && strstr(path, "wal") && !strstr(path ,"000")){
	    SKYFS_ERROR_1("getattr for %s\n", path);
    }
    if(strcmp(path, "/") == 0){
        rc = lstat(path, stbuf);        
        goto normal_out;
    }

	fi.fh = 0;
	fi.conflict_index = 0;
    strcpy(path_name, path);

	rc = skyfs_flush(path_name, &fi);
	if(rc < 0){
        goto err_out;
	}

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_getattr:%s does not exit\n", path);
        rc = -2;
    }else if (rc == 2){
        SKYFS_MSG("skyfs_getattr:find %s.\n", lastname);
        rc = __skyfs_C2M_getattr(ino, conflict_index, stbuf);
        if (rc != 0){
			__skyfs_C_release_dentry(pino, lastname);
            SKYFS_ERROR_1("skyfs_getattr: stat %s rpc failed,rc:%d, pino %x, ino %llx, conflict %x\n", path, rc, pino, ino, conflict_index);
            goto err_out;
        }
    }else{
        SKYFS_ERROR_1("skyfs_getattr:path_walk error.rc:%d\n", rc);
    }

err_out:
normal_out:
    SKYFS_LEAVE("skyfs_getattr:exit.rc:%d\n", rc);
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}

int skyfs_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t   *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)stbuf;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 612; // fgetattr  612
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_fgetattr:enter\n");

    if(strstr(path, "share") && strstr(path, "wal") && !strstr(path, "000")){
	    SKYFS_ERROR_1("fgetattr for %s\n", path);
    }
    strcpy(path_name, path);
	rc = skyfs_flush(path_name, fi);
	if(rc < 0){
        goto err_out;
	}

	if(fi->fh > 0 && fi->conflict_index > 0){
    	ino = fi->fh;
		conflict_index = fi->conflict_index;
	}else{
        lastname = __skyfs_C_get_lastname(path_name);
    	ino = fi->fh;
        conflict_index = __skyfs_name2hashvalue(lastname);
	}

    rc = __skyfs_C2M_getattr(ino, conflict_index, stbuf);
    if (rc != 0){
        SKYFS_ERROR_1("skyfs_fgetattr: rpc failed stat %s failed, rc:%d, ino %llx\n", path, rc, ino );
        goto err_out;
    }

err_out:
    SKYFS_LEAVE("skyfs_fgetattr:exit.rc:%d\n", rc);
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}


int skyfs_access(const char *path, int mask)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    struct stat stbuf;
    int rc = 0;
    
    SKYFS_ENTER("skyfs_access:%s,mask:%d\n", path, mask);
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 613; // access  613
    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);


    if(strcmp(path, "/") == 0){
        rc = lstat(path, &stbuf);        
        goto normal_out;
    }

    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_access:%s does not exit\n", path);
        rc = -ENOENT;
    }else if (rc == 2){
        SKYFS_MSG("skyfs_access:has find %s.\n", lastname);
        rc = __skyfs_C2M_getattr(ino, conflict_index, &stbuf);
        if (rc != 0){
			__skyfs_C_release_dentry(pino, lastname);
            SKYFS_ERROR("skyfs_access: stat %s Failed,rc:%d, pino %llx ,ino %llx, conflit index %x\n", path, rc, ino, conlict_index);
            goto err_out;
        }
        
        if((mask & stbuf.st_mode) != mask){
            SKYFS_ERROR("skyfs_access:not permit skip temprary :%u,%u,%u\n", 
                mask, stbuf.st_mode, mask & stbuf.st_mode);
            //rc = -EACCES; tmp chanbed by mayl
            rc = 0;
        }
    }else{
        SKYFS_ERROR("skyfs_access:path_walk error.rc:%d\n", rc);
    }

err_out:
normal_out:
    SKYFS_LEAVE("skyfs_access:exit.rc:%d\n", rc);
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;

}

int skyfs_flush(const char *path, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_u32_t count;
	skyfs_u64_t lastest_size;
    skyfs_s8_t  *lastname = NULL;
	skyfs_C_writebuf_t  *writebuf = NULL;
    skyfs_s32_t  rc0 = 0;
    skyfs_s32_t  rc = 0;
    skyfs_s8_t   path_name[SKYFS_MAX_NAME_LEN];
    int base_file = 0;
    skyfs_m_setmeta_args_t args;

    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)fi;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 614; // flush  614

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    bzero(&args, sizeof(skyfs_m_setmeta_args_t));

	SKYFS_ENTER("skyfs_flush:enter:%s\n", path);

    strcpy(path_name, path);
    if(strstr(path, "base"))
	    base_file = 1;

	if(fi->fh > 0 && fi->conflict_index){
        ino = fi->fh;
		conflict_index = fi->conflict_index;
	}else{
        rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_ERROR("skyfs_flush:%s does not exit\n", path);
			goto err_out;
        }else if (rc != 2){
            SKYFS_ERROR("skyfs_flush:path_walk error.rc:%d\n", rc);
			goto err_out;
		}
		fi->fh = ino;
		fi->conflict_index = conflict_index;
	}

	pthread_mutex_lock(&client_writebuf_lock);
	writebuf = __skyfs_C_lookup_writebuf(ino, -1, 1);
	while(writebuf){
		SKYFS_ERROR_1("flush write_buf ??, ino %llu\n", ino);
		rc = __skyfs_C2O_write(ino, writebuf->buf, 
		    writebuf->offset, writebuf->count, base_file);
	
        if (rc < 0){
            SKYFS_ERROR("skyfs_flush: write %s failed\n", path);
            goto err_out;
        }

		lastest_size = writebuf->offset + rc;

		rc = count;

		__skyfs_C_release_writebuf(writebuf);
		free(writebuf);
		writebuf = NULL;

        args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
        gettimeofday(&(args.atime), NULL); 
        args.ino = ino;
        args.conflict_index = conflict_index;
        args.size = lastest_size;
        SKYFS_MSG("skyfs_flush: setattr %llu\n", args.size);
        rc0 = __skyfs_C2M_setattr(ino, conflict_index, &args);
        if (rc0 < 0){
           SKYFS_ERROR("skyfs_flush:setattr %s failed\n", path);
           goto err_out;
        }

		writebuf = __skyfs_C_lookup_writebuf(ino, -1, 1);
	}

	pthread_mutex_unlock(&client_writebuf_lock);

err_out:
     pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_LEAVE("skyfs_flush:exit\n");
    return 0;
}

int skyfs_chmod(const char *path, mode_t mode)
{
   
    skyfs_ino_t  pino = (long long int)-1;
    skyfs_ino_t  ino = (long long int)-1;
    skyfs_u32_t  conflict_index;
    skyfs_s8_t   *lastname = NULL;
    skyfs_s8_t   path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t  rc = 0;
    
    skyfs_m_setmeta_args_t args;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 615; // chmod  615

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_MSG("skyfs_chmod:%s, %d\n", path, mode);
    
    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_chmod:%s does not exit\n", path);
        rc = -2;
    }else if (rc == 2){
        SKYFS_MSG("skyfs_chmod:has find %s,mods:%d.\n", lastname, mode);
        gettimeofday(&(args.atime), NULL); 
        args.valid = SKYFS_ATTR_MODE| SKYFS_ATTR_CTIME;
        args.ino = ino;
        args.conflict_index = conflict_index;
        args.mode = mode;
        rc = __skyfs_C2M_setattr(ino, conflict_index, &args);
        if (rc < 0){
            SKYFS_ERROR("skyfs_chmod: stat %s failed\n", path);
            goto err_out;
        }
    }else{
        SKYFS_ERROR("skyfs_chmod:path_walk error.rc:%d\n", rc);
    }

err_out:
    SKYFS_LEAVE("skyfs_chmod:exit.rc:%d\n", rc);
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;

}

int skyfs_chown(const char *path, uid_t uid, gid_t gid)
{
    skyfs_ino_t  pino = (long long int)-1;
    skyfs_ino_t  ino = (long long int)-1;
    skyfs_u32_t  conflict_index;
    skyfs_s8_t   *lastname = NULL;
    skyfs_s8_t   path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t  rc = 0;
    
    skyfs_m_setmeta_args_t args;
     struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 616; // chown  616

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_chown:enter:%s, uid:%d, gid:%d.\n", 
        path, uid, gid);
    
    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_chown:%s does not exit\n", path);
        rc = -2;
    }else if (rc == 2){
        SKYFS_MSG("skyfs_chown:has find %s.\n", lastname);
        args.valid = SKYFS_ATTR_UID|SKYFS_ATTR_GID|SKYFS_ATTR_CTIME;
        gettimeofday(&(args.atime), NULL); 
        args.ino = ino;
        args.conflict_index = conflict_index;
        args.uid = uid;
        args.gid = gid;
        rc = __skyfs_C2M_setattr(ino, conflict_index, &args);
        if (rc < 0){
            SKYFS_ERROR("skyfs_chown: setattr %s failed\n", path);
            goto err_out;
        }
    }else{
        SKYFS_ERROR("skyfs_chown:path_walk error.rc:%d\n", rc);
    }

err_out:
    SKYFS_LEAVE("skyfs_chown:exit.rc:%d\n", rc);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}

int skyfs_symlink(const char *from, const char *to)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)from;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 617; // symlink  617

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_symlink:enter:from:%s, to:%s\n", 
        from, to);

    strcpy(path_name, to);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_MSG("skyfs_symlink:%s does not exit\n", path_name);
        rc = __skyfs_C2M_symlink(pino, lastname, &ino, &conflict_index, from);
    }else if (rc == 2){
        SKYFS_ERROR("skyfs_symlink:has find %s.\n", lastname);
    }else{
        SKYFS_ERROR("skyfs_symlink:path_walk error.rc:%d\n", rc);
    }
    SKYFS_LEAVE("skyfs_symlink:exit\n");
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;

}

int skyfs_readlink(const char *path, char *buf, size_t size)
{
    skyfs_ino_t ino = (long long int)-1;
    skyfs_ino_t pino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 618; // readlink  618

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_readlink:enter:%s,size:%lu.\n", path, size);

    strcpy(path_name, path);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 2){
        SKYFS_MSG("skyfs_readlink:%s does not exit\n", path);
        rc = __skyfs_C2M_readlink(pino, lastname, buf, size - 1);
        if(rc < 0){
            SKYFS_ERROR("skyfs_readlink:readlink failed,rc:%d\n", rc);
        }
    }else if (rc == 0){
        SKYFS_ERROR("skyfs_readlink:%s dose not exit.\n", path);
        rc = -2;
    }else{
        SKYFS_ERROR("skyfs_readlink:path_walk error.rc:%d\n", rc);
    }

    SKYFS_LEAVE("skyfs_readlink:exit,rc:%d\n",rc);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;
}

int skyfs_link(const char *from, const char *to)
{
    skyfs_ino_t pino_from = (long long int)-1;
    skyfs_ino_t ino_from = (long long int)-1;
    skyfs_u32_t conflict_index_from;

    skyfs_ino_t pino_to = (long long int)-1;
    skyfs_ino_t ino_to = (long long int)-1;
    skyfs_u32_t conflict_index_to;

    skyfs_s8_t  *lastname_from = NULL;
    skyfs_s8_t  *lastname_to = NULL;
    skyfs_s8_t  path_name_from[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t  path_name_to[SKYFS_MAX_NAME_LEN];

    skyfs_s32_t rc = 0;
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)to;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 619; // link  619

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ENTER("skyfs_link:from:%s,to:%s\n", from, to);

    strcpy(path_name_from, from);
    strcpy(path_name_to, to);

    rc = __skyfs_C_path_walk(path_name_to, &lastname_to, &pino_to, 
            &ino_to, &conflict_index_to);
    if(rc == 2){
        SKYFS_ERROR("skyfs_link:path_walk_to:%s exist.\n", path_name_to);
        rc = -EEXIST;
        goto err_out;
    }else if(rc != 0 && rc != 2){
        SKYFS_ERROR("skyfs_link:path_walk_to:%s error.rc:%d:\n", 
            path_name_to, rc);
        goto err_out;
    }

    rc = __skyfs_C_path_walk(path_name_from, &lastname_from, &pino_from, 
            &ino_from, &conflict_index_from);
    if(rc == 2){
        SKYFS_MSG("skyfs_link:%s exist\n", path_name_from);
        rc = __skyfs_C2M_link(pino_from, pino_to, lastname_from, lastname_to, 
                ino_from, conflict_index_from,
                &ino_to, &conflict_index_to);
    }else if (rc == 0){
        SKYFS_ERROR("skyfs_link:has not find %s.\n", lastname_from);
        rc = -ENOENT;
    }else{
        SKYFS_ERROR("skyfs_link:path_walk %s error.rc:%d\n", 
            path_name_from, rc);
    }

err_out:

    SKYFS_LEAVE("skyfs_link:exit,rc:%d\n", rc);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;

}

int skyfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t  buf_page[4096];
    skyfs_s8_t  *tmp = NULL;
    struct      stat st;
    off_t       nextoff;
    skyfs_u64_t d_offset = 0;
    //skyfs_u32_t dir_flag = 0;
    skyfs_dentry_t *dentry = NULL;

    skyfs_s32_t rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 620; // readdir  620

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    if(strstr(path, "share") && strstr(path, "wal") && strstr(path, "data")){
    	SKYFS_ERROR("skyfs_readdir:enter:%s,buf:%p\n", path, buf);
    }

    strcpy(path_name, path);

    bzero(buf_page, 4096);

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_readdir:%s does not exit\n", path);
        rc = -2;
    }else if(rc == 2){
        SKYFS_ERROR("skyfs_readdir:has find %s.\n", lastname);
        pthread_mutex_lock(&client_bcache_lock);
        rc = __skyfs_C2M_readdir(ino, conflict_index, buf_page, offset, 0);
	SKYFS_ERROR("skyfs readdir C2M offset %llu, ret %d\n", offset, rc);
        pthread_mutex_unlock(&client_bcache_lock);
        if(rc < 0){
            SKYFS_ERROR("skyfs_readdir: stat %s failed\n", path);
            goto err_out;
        }else if(rc == 0){
            SKYFS_MSG("skyfs_readdir:rc:%d\n", rc);
            memset(&st, 0, sizeof(st));
            st.st_ino = ino;
            st.st_mode = S_IFDIR|0755;
            filler(buf, ".", &st, 0);

			memset(&st, 0, sizeof(st));
            st.st_ino = pino;
            st.st_mode = S_IFDIR|0755;
            filler(buf, "..", &st, 0);
            pthread_mutex_lock(&client_bcache_lock);
	    SKYFS_ERROR("readdir release bcache ino %lu , pid %lu , tid %lu\n",
			    ino, getpid(),gettid());
            __skyfs_C_release_bcache(ino, NULL);
            pthread_mutex_unlock(&client_bcache_lock);

            goto normal_out;
        }

        tmp = buf_page;
        d_offset = 0;
        while(1){
            dentry = (skyfs_dentry_t *)tmp;
            memset(&st, 0, sizeof(st));
            st.st_ino = dentry->ino;
            st.st_mode = dentry->mode|0755;
            if(rc == 0){
                nextoff = 0;
            }else{
                nextoff = dentry->offset + dentry->reclen;
            }

            SKYFS_MSG("skyfs_readdir:%s,nextoff:%llu,d_offset:%llu\n", 
                dentry->name, nextoff, d_offset);
            if(filler(buf, dentry->name, &st, nextoff))
                break;
            dentry = NULL;
            
            if(d_offset + 2*sizeof(skyfs_dentry_t) > SKYFS_DIR_BLK_SIZE){
                d_offset = SKYFS_DIR_BLK_SIZE;
            }else{
                d_offset = d_offset + sizeof(skyfs_dentry_t);
            }

            if(d_offset >= rc){
                SKYFS_ERROR("skyfs_readdir:d_offset:%llu,rc:%d\n",
                    d_offset, rc);
                break;
            }
            tmp = buf_page + d_offset;
            SKYFS_MSG("skyfs_readdir:d_offset:%llu\n", d_offset);
        }
    }else{
        SKYFS_ERROR("skyfs_readdir:path_walk error.rc:%d\n", rc);
    }

err_out:
normal_out:
    SKYFS_LEAVE("skyfs_readdir:exit.rc:%d\n", rc);
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return 0;
}

int skyfs_truncate(const char *path, off_t size)
{
    skyfs_ino_t  pino = (long long int)-1;
    skyfs_ino_t  ino = (long long int)-1;
    skyfs_u32_t  conflict_index;
    skyfs_s8_t   *lastname = NULL;
    skyfs_s8_t   path_name[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t  rc = 0;
    
    skyfs_m_setmeta_args_t args;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 621; // truncate  621

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR_1("skyfs_truncate:%s,size:%lu\n", path, size);
    
    strcpy(path_name, path);
    bzero(&args, sizeof(skyfs_m_setmeta_args_t));

    rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
    if(rc == 0){
        SKYFS_ERROR("skyfs_truncate:%s does not exit\n", path);
        rc = -2;
    }else if (rc == 2){
        SKYFS_MSG("skyfs_truncate:has find %s.\n", lastname);
        args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
        gettimeofday(&(args.atime), NULL); 
        args.ino = ino;
        args.conflict_index = conflict_index;
        args.truncate_flag = 1;
        args.size = size;
        rc = __skyfs_C2M_setattr(ino, conflict_index, &args);
        if (rc < 0){
            SKYFS_ERROR("skyfs_truncate: stat %s failed\n", path);
            goto err_out;
        }
        rc = 0;
    }else{
        SKYFS_ERROR("skyfs_truncate:path_walk error.rc:%d\n", rc);
    }

err_out:
//normal_out:
    SKYFS_ERROR_1("skyfs_truncate_to %llu path %s ino %llu :exit.rc:%d\n", size, path, ino, rc);
    if(rc == 0){
#ifdef __SKYFS_DBTEST__
            int ret = 0;
            memset(path_name,0,SKYFS_MAX_NAME_LEN);
            sprintf(path_name,"%s/%s", "/mnt/local/", path);
            ret  = truncate(path_name, size);
            if(ret <0){
                    SKYFS_ERROR_1("can not truncate local_file %s,\n", path_name);
                    
            }else{
                        SKYFS_ERROR_1("truncate local_file %s, try rmdir\n", path_name);
            }

#endif


    }
    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    return rc;

}

int skyfs_utimens(const char *path, const struct timespec ts[2])
{
    struct timeval tv[2];

    (void) path;

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;
    
    return 0;
}

static get_curtime_str(char * timestr)
{
	time_t timep;
        struct tm *p;
        time (&timep);
        p=gmtime(&timep);
	memset(timestr, 0, 128);
	sprintf(timestr,"%d--%d--%d日 %d:%d:%d",1900+p->tm_year,1+p->tm_mon,p->tm_mday,8+p->tm_hour,p->tm_min,p->tm_sec);
}

int skyfs_read(const char *path, char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_s8_t  *lastname = NULL;
    skyfs_s8_t   path_name[SKYFS_MAX_NAME_LEN];

    int rc = 0;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 622; // read  622

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_MSG("skyfs_read:enter:%s,off_t:%lu,size:%zu, fi %p , direct_io %d\n", path, offset, size, fi, fi->direct_io);

#if 1
	//sleep(10);
    //lastname = __skyfs_C_get_lastname(path);
    //conflict_index = __skyfs_name2hashvalue(lastname);
    strcpy(path_name, path);

    if(fi->fh > 0){ 
        ino = fi->fh;
    	SKYFS_ERROR("skyfs_read:fh:%ld\n", fi->fh);
        rc = __skyfs_C2O_read(ino, buf, offset, size, fi->direct_io);
        if (rc < 0){
            SKYFS_ERROR("skyfs_read: read %s failed\n", path);
            goto err_out;
        }
    }else{
        rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_MSG("skyfs_read:%s does not exit\n", path);
        }else if (rc == 2){
            SKYFS_MSG("skyfs_read:has find %s.\n", lastname);
            rc = __skyfs_C2O_read(ino, buf, offset, size, fi->direct_io);
            if (rc <= 0){
                SKYFS_ERROR_1("skyfs_read: read %s failed, offset %lu, size %lu\n", path, offset , size);
                goto err_out;
            }
            fi->fh = ino;
            fi->conflict_index = conflict_index;
        }else{
            SKYFS_MSG("skyfs_read:path_walk error.rc:%d\n", rc);
        }
    }
#endif
err_out:


    if(strstr(path, "base") && size >0){
	char timestr[128];
	get_curtime_str(timestr);
    	SKYFS_ERROR_1("skyfs_read  base file  : exit.rc:%d, ino %llu, path %s, offset %llx, count %llx  time %s \n",
			rc, ino ,path, offset, size, timestr);
    }
    if((size %4096) != 0 || (offset%4096!=0)|| strstr(path,"wal")!=0){
	      SKYFS_ERROR_1("skyfs READ  wal or unaligned  in  file %s , offset %llu, size %lu , buf %p , rc %d \n",path ,offset, size, buf, rc);
	}

    
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);
    if(rc >= 0){

#ifdef __SKYFS_DBTEST_READ
	    char local_path[256] = {0};
	    int local_fd = 0;
	    int local_ret = 0;
	    sprintf(local_path, "%s/%s","/mnt/read_local/",path );
	    local_fd = open(local_path, O_CREAT|O_RDWR, 0666);
	    if(local_fd <= 0){
		    SKYFS_ERROR_1("Failed to open write local file %s for read \n", local_path);
		    goto end_read;
	    }
	    local_ret = pwrite(local_fd, buf, rc, offset);
	    if(local_ret <= 0){
		    SKYFS_ERROR_1("failed write readbuf local file %s, off %llu , size %llu \n", local_path, offset, size);
	    }
end_read:
	    if(local_fd > 0 ){
	    	close(local_fd);
		if(rc != size){
		    SKYFS_ERROR_1(" write readbuf local file %s, off %llu , size %llu, ret %llu \n", local_path, offset, size, rc);
		}
	    }

	    


#endif
    }

    return rc;
}

int skyfs_write(const char *path, const char *buf, size_t size,
                off_t offset, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_u32_t count;
    int base_file = 0;
	skyfs_u64_t lastest_size;
    skyfs_s8_t  *lastname = NULL;
	skyfs_C_writebuf_t  *writebuf = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    int rc0 = 0;

    skyfs_m_setmeta_args_t args;
    
    
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 623; // write  623

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);


    bzero(&args, sizeof(skyfs_m_setmeta_args_t));

    /*1. get write info*/  	
    SKYFS_MSG("skyfs_write:enter:%s,offset:%lu,size:%llu\n", 
		path, offset, size);

    strcpy(path_name, path);
    if(strstr(path, "base"))
	base_file = 1;
    if(fi->fh > 0 && fi->conflict_index > 0){
        ino = fi->fh;
		conflict_index = fi->conflict_index;
	}else{
        rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_ERROR("skyfs_write:%s does not exit\n", path);
			goto err_out;
        }else if (rc != 2){
            SKYFS_ERROR("skyfs_write:path_walk error.rc:%d\n", rc);
			goto err_out;
		}

        fi->fh = ino;
        fi->conflict_index = conflict_index;
	}
    
	/*2. search write buf*/
    /*2.1 write direct to server*/
	if(size >= SKYFS_WRITEBUF_SIZE || client_writebuf_num > 200 || fi->direct_io){

		struct timeval tv;
		uint64_t stime;

	   	SKYFS_ERROR("skyfs_write: Begin write:client writebuf num:%d\n", client_writebuf_num);
		if(size == 32768 && strstr(path, "base") ){
			char pattern [128];
			memset(pattern, 0, 128);
			if(memcmp(pattern,buf, 128) == 0)
				SKYFS_ERROR_1("skyfs WRITE zero buf in base file %s , offset %llu, buf %p \n",path ,offset, buf);
		}

		gettimeofday(&tv, NULL);
		stime = tv.tv_sec;
		stime = stime*1000000+tv.tv_usec;
		
   		rc = __skyfs_C2O_write(ino, buf, offset, size, base_file);
		
		if((size %4096) != 0 || (offset%4096!=0)|| strstr(path,"wal")!=0){
			if(!strstr(path, ".log"))
	      			SKYFS_ERROR_1("skyfs WRITE  wal or unaligned  in  file %s , offset %llu, size %lu , st %llu , rc %d, tid %d \n",
					path ,offset, size, stime, rc, gettid());
		}
   		if (rc < 0){
	   		SKYFS_ERROR_1("skyfs_write: write %s failed\n", path);
	   		goto err_out;
		}
		
		fi->fh = ino;
		fi->conflict_index = conflict_index;
	   
		args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
		gettimeofday(&(args.atime), NULL);
	   	args.ino = ino;
	   	args.conflict_index = conflict_index;
	   	args.size = offset + rc;
		SKYFS_ERROR("skyfs_write: prepare to Update  file size\n");
	   	rc0 = __skyfs_C2M_setattr(ino, conflict_index, &args);
	   	if (rc0 < 0){
		   SKYFS_ERROR_1("skyfs_write: stat %s failed, rc:%d\n", path, rc0);
		   goto err_out;
		}   
		goto EXIT;
	}

    /*2.2 manage write cache*/
	if(SKYFS_WRITEBUF_SIZE){

	pthread_mutex_lock(&client_writebuf_lock);
    	writebuf = __skyfs_C_lookup_writebuf(ino, offset, 0);
	if(writebuf){
		if(writebuf->count + size < SKYFS_WRITEBUF_SIZE){
			rc = __skyfs_C_attach_writebuf(writebuf, buf, size);
			pthread_mutex_unlock(&client_writebuf_lock);
            if(rc < 0){
                goto err_out;
		    }
			rc = size;
		}else{
			count = SKYFS_WRITEBUF_SIZE - writebuf->count;
			memcpy((skyfs_s8_t *)writebuf->buf + writebuf->count, buf, count);
			if(count < size){
                rc = __skyfs_C_submit_writebuf(ino, conflict_index, buf+count, 
						offset + count, size - count);
                if(rc < 0){
					pthread_mutex_unlock(&client_writebuf_lock);
                    goto err_out;
		        }
			}

			__skyfs_C_release_writebuf(writebuf);
			pthread_mutex_unlock(&client_writebuf_lock);

            SKYFS_DEBUG("Skyfs_Write: begin writebuf\n");
        	rc = __skyfs_C2O_write(ino, writebuf->buf, 
				    writebuf->offset, SKYFS_WRITEBUF_SIZE, base_file);
            if (rc < 0){
                SKYFS_ERROR_1("skyfs_write: write %s failed\n", path);
                goto err_out;
            }
			lastest_size = writebuf->offset + rc;

			rc = size;

	        free(writebuf);
			writebuf = NULL;

            args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
   	        gettimeofday(&(args.atime), NULL); 
   	        args.ino = ino;
   	        args.conflict_index = conflict_index;
   	        //need to adjust later
   	        args.size = offset + size;
			if(args.size % (skyfs_u32_t)(1 << SKYFS_WRITEBUF_SIZE_BITS) == 0){
   	        	rc0 = __skyfs_C2M_setattr(ino, conflict_index, &args);
   	        	if (rc0 < 0){
   		        	SKYFS_ERROR("skyfs_write:setattr %s failed\n", path);
      	        	goto err_out;
   	        	}
			}
		}
	}else{
        rc = __skyfs_C_submit_writebuf(ino, conflict_index, buf, offset, size);
		pthread_mutex_unlock(&client_writebuf_lock);
		if(rc < 0){
            goto err_out;
		}

		rc = size;
	}
       

	}
EXIT:
err_out:
    if(rc<0 || (rc == 0 && size !=0)){
	    SKYFS_ERROR_1("skyfs_write_failed rc %d\n", rc);
    }else{
#ifdef __SKYFS_DBTEST__
	    char local_path[256] = {0};
	    int local_fd = 0;
	    int local_ret = 0;
	    sprintf(local_path, "%s/%s","/mnt/local/",path );
	    local_fd = open(local_path, O_CREAT|O_RDWR, 0666);
	    if(local_fd <= 0){
		    SKYFS_ERROR_1("failed to open write local file %s \n", local_path);
		    goto end_write;
	    }
	    local_ret = pwrite(local_fd, buf, size, offset);
	    if(local_ret <= 0){
		    SKYFS_ERROR_1("failed write local file %s, off %llu , size %llu \n", local_path, offset, size);
	    }
end_write:
	    if(local_fd > 0 ){
	    	close(local_fd);
	    }

	    


#endif
    }
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR("skyfs_write:exit.%lu,count:%d\n", rc, size);
    //if(rc == 0 && size != 0 || size >= 32768 && offset == 0)
    //if(rc <=  0 )
    	//SKYFS_ERROR_1("skyfs_write: zero or failed ret  ,  exit.rc:%d, ino %llu, path %s, offset %llu, count %llu , depth %u\n",
			//rc, ino ,path, offset, size, skyfs_dl_depth.depth);


	if(strstr(path, "base") && size >0){
		char timestr[128];
		get_curtime_str(timestr);
    		SKYFS_ERROR_1("skyfs_write  base file  : exit.rc:%d, ino %llu, path %s, offset %llx, count %llx  time %s tid %d\n",
			rc, ino ,path, offset, size,  timestr, gettid());
    }

    return rc;
}

int skyfs_writebuf(const char *path, struct fuse_bufvec *bufv,
                off_t offset, struct fuse_file_info *fi)
{
    skyfs_ino_t pino = (long long int)-1;
    skyfs_ino_t ino = (long long int)-1;
    skyfs_u32_t conflict_index;
    skyfs_u32_t count;
	skyfs_u64_t lastest_size;
	skyfs_u32_t size;
	skyfs_s8_t  *buf = NULL;
    skyfs_s8_t  *lastname = NULL;
    int base_file = 0;
	skyfs_C_writebuf_t  *writebuf = NULL;
    skyfs_s8_t  path_name[SKYFS_MAX_NAME_LEN];
    int rc = 0;
    int rc0 = 0;

    skyfs_m_setmeta_args_t args;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 624; // writebif  624

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    bzero(&args, sizeof(skyfs_m_setmeta_args_t));

	size = bufv->buf[0].size;
	buf = bufv->buf[0].mem;

    /*1. get write info*/  	
    SKYFS_ERROR("skyfs_writebuf:enter:%s,offset:%lu,size:%u\n", 
		path, offset, size);
    if(strstr(path, "base"))
	    base_file = 1;

    strcpy(path_name, path);

    if(fi->fh > 0 && fi->conflict_index > 0){
        ino = fi->fh;
		conflict_index = fi->conflict_index;
	}else{
        rc = __skyfs_C_path_walk(path_name, &lastname, &pino, &ino, &conflict_index);
        if(rc == 0){
            SKYFS_ERROR("skyfs_writebuf:%s does not exit\n", path);
			goto err_out;
        }else if (rc != 2){
            SKYFS_ERROR("skyfs_writebuf:path_walk error.rc:%d\n", rc);
			goto err_out;
		}

        fi->fh = ino;
        fi->conflict_index = conflict_index;
	}
#if 1
    /*2. search write buf*/
    /*2.1 write direct to server*/
	if(size >= SKYFS_WRITEBUF_SIZE || client_writebuf_num > 200){
   		rc = __skyfs_C2O_write(ino, buf, offset, size, base_file);
   		if (rc < 0){
	   		SKYFS_ERROR("skyfs_writebuf: write %s failed\n", path);
	   		goto err_out;
		}

		fi->fh = ino;
		fi->conflict_index = conflict_index;
	   
		args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
		gettimeofday(&(args.atime), NULL);
	   	args.ino = ino;
	   	args.conflict_index = conflict_index;
	   	args.size = offset + rc;
	   	rc0 = __skyfs_C2M_setattr(ino, conflict_index, &args);
	   	if (rc0 < 0){
		   SKYFS_ERROR("skyfs_writebuf: stat %s failed, rc:%d\n", path, rc0);
		   goto err_out;
		}   
		goto EXIT;
	}

    /*2.2 manage write cache*/
	if(SKYFS_WRITEBUF_SIZE){

	pthread_mutex_lock(&client_writebuf_lock);
    writebuf = __skyfs_C_lookup_writebuf(ino, offset, 0);
	if(writebuf){
		if(writebuf->count + size < SKYFS_WRITEBUF_SIZE){
			rc = __skyfs_C_attach_writebuf(writebuf, buf, size);
            if(rc < 0){
                goto err_out;
		    }
			rc = size;
		}else{
			count = SKYFS_WRITEBUF_SIZE - writebuf->count;
			memcpy((skyfs_s8_t *)writebuf->buf + writebuf->count, buf, count);
			if(count < size){
                rc = __skyfs_C_submit_writebuf(ino, conflict_index, buf+count, 
						offset + count, size - count);
                if(rc < 0){
                    goto err_out;
		        }
			}

        	rc = __skyfs_C2O_write(ino, writebuf->buf, 
				    writebuf->offset, SKYFS_WRITEBUF_SIZE, base_file);
            if (rc < 0){
                SKYFS_ERROR("skyfs_writebuf: write %s failed\n", path);
                goto err_out;
            }
			lastest_size = writebuf->offset + rc;

			rc = size;

			__skyfs_C_release_writebuf(writebuf);
	        free(writebuf);

            args.valid = SKYFS_ATTR_SIZE | SKYFS_ATTR_CTIME;
   	        gettimeofday(&(args.atime), NULL); 
   	        args.ino = ino;
   	        args.conflict_index = conflict_index;
   	        //need to adjust later
   	        args.size = offset + size;
			if(args.size % (skyfs_u32_t)(1 << SKYFS_WRITEBUF_SIZE_BITS) == 0){
   	        	rc0 = __skyfs_C2M_setattr(ino, conflict_index, &args);
   	        	if (rc0 < 0){
   		        	SKYFS_ERROR("skyfs_writebuf:setattr %s failed\n", path);
      	        	goto err_out;
   	        	}
			}
		}
	}else{
        rc = __skyfs_C_submit_writebuf(ino, conflict_index, buf, offset, size);
		if(rc < 0){
            goto err_out;
		}

		rc = size;
	}
       
	pthread_mutex_unlock(&client_writebuf_lock);

	}
#endif
EXIT:
err_out:
    pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR("skyfs_writebuf:exit.%d\n", rc);

    return rc;
}


int skyfs_readbuf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;
    struct list_head *index = NULL;
    struct skyfs_pending_req* tmp_req = NULL;
    struct timeval tv;
    struct skyfs_pending_req my_pending;

    my_pending.req_pointer = (uint64_t)path;
    gettimeofday(&tv, NULL);
    my_pending.start_time = (uint64_t)(tv.tv_sec)*1000000 + tv.tv_usec;
    my_pending.op = 625; // writebif  625

    INIT_LIST_HEAD(&my_pending.pending_entry);
    pthread_mutex_lock(&client_pending_lock);
    list_add_tail(&my_pending.pending_entry, &client_pending_list);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR("skyfs_readbuf:enter.\n");

	src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	//src->buf[0].fd = fi->fh;
	src->buf[0].fd = 0;
	src->buf[0].pos = offset;

	*bufp = src;
    
	
     pthread_mutex_lock(&client_pending_lock);
    list_del(&my_pending.pending_entry);
    pthread_mutex_unlock(&client_pending_lock);

    SKYFS_ERROR("skyfs_readbuf:exit,fh:%ld.\n", fi->fh);
	return 0;
}

void __skyfs_C_init_config(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_C_init_config:enter,remote_id:%d\n",
        req->req_remote_id);

    msgp = __skyfs_get_msg(req->req_msg);

	memcpy(&arch_info, 
		&(msgp->u.initconfigReq.arch_info), 
		sizeof(skyfs_arch_info_t));

	rc = __skyfs_init_nodes(&arch_info, &mds_info, &osd_info, &client_info);

    sem_post(&client_config_sem);

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_initconfig_ack_t);
    __skyfs_C_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;
    rc = amp_send_sync(client_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_C_init_config:send reply %d %d err:%d\n", 
            req->req_remote_type, req->req_remote_id, rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_C_init_config:exit,rc:%d\n", rc);

}

//void __skyfs_C_get_config(void)

struct fuse_operations skyfs_oper = {
    .statfs     = skyfs_statfs,
    .open       = skyfs_open,
	.opendir    = skyfs_opendir,
    .create     = skyfs_create,
    .unlink     = skyfs_unlink,
    .rename     = skyfs_rename,
    .mkdir      = skyfs_mkdir,
    .rmdir      = skyfs_unlink,
    .release    = skyfs_release,
    .getattr    = skyfs_getattr,
    .fgetattr   = skyfs_fgetattr,
    .access     = skyfs_access,
    .flush      = skyfs_flush,
    .chmod      = skyfs_chmod,
    .chown      = skyfs_chown,
    .symlink    = skyfs_symlink,
    .readlink   = skyfs_readlink,
    .link       = skyfs_link,
    .readdir    = skyfs_readdir,
    .truncate   = skyfs_truncate,
    .utimens    = skyfs_utimens,
    .read       = skyfs_read,
    .write      = skyfs_write,
	// mayl add below
	.lock		= skyfs_lock,
	.flock		= skyfs_flock,
//	.write_buf  = skyfs_writebuf,
//  .read_buf   = skyfs_readbuf,
};

/*This is end of client_op.c*/
