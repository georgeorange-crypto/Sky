/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_op.h 
 */

#ifndef __CLIENT_OP_H
#define __CLIENT_OP_H

// if setlkw failed , retry 10 seconds
#define SKYFS_LOCKW_RETRY 10 
int skyfs_statfs(const char *path, struct statvfs *stbuf);

int skyfs_open(const char *path, struct fuse_file_info *fi);

int skyfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);

int skyfs_mkdir(const char *path, mode_t mode);

int skyfs_unlink(const char *path);

int skyfs_rename(const char *from, const char *to);

int skyfs_release(const char *path, struct fuse_file_info *fi);

int skyfs_getattr(const char *path, struct stat *stbuf);

int skyfs_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);

int skyfs_access(const char *path, int mask);

int skyfs_flush(const char *path, struct fuse_file_info *fi);

int skyfs_chmod(const char *path, mode_t mode);

int skyfs_chown(const char *path, uid_t uid, gid_t gid);

int skyfs_symlink(const char *from, const char *to);

int skyfs_readlink(const char *path, char *buf, size_t size);

int skyfs_link(const char *from, const char *to);

int skyfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi);

int skyfs_truncate(const char *path, off_t size);

int skyfs_utimens(const char *path, const struct timespec ts[2]);

int skyfs_read(const char *path, char *buf, size_t size, off_t offset,
				struct fuse_file_info *fi);

int skyfs_write(const char *path, const char *buf, size_t size,
				off_t offset, struct fuse_file_info *fi);

int skyfs_opendir(const char *path, struct fuse_file_info *fi);

skyfs_s32_t
__skyfs_C_path_walk(char *pathname, 
				char **lastname, 
				skyfs_ino_t *pino, 
				skyfs_ino_t *ino, 
				skyfs_u32_t *conflict);

skyfs_s32_t
__skyfs_C_path_walk_with_pconflict(char *pathname, 
				char **lastname, 
				skyfs_ino_t *pino, 
				skyfs_ino_t *ino, 
				skyfs_u32_t *conflict,
				skyfs_u32_t *pconflict);



int skyfs_writebuf(const char *path, struct fuse_bufvec *bufv,
                off_t offset, struct fuse_file_info *fi);

int skyfs_readbuf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi);


void __skyfs_C_init_config(amp_request_t *req);
extern struct fuse_operations skyfs_oper;
#endif
/*This is end of client_op.h*/
