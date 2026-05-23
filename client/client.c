/* 
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client.c $
 */

//#define FUSE_USE_VERSION 26
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


#include "client_init.h"
#include "client_op.h"
#include "client_cache.h"
#include "client_thread.h"
#include "gpu_compress.h"
#include "client_compress_thread.h"

int skyfs_log_file_len;
pthread_mutex_t skyfs_debug_lock;

size_t nvcomp_zstd_temp_size = 0;
size_t get_nvcomp_zstd_temp_size()
{
	return nvcomp_zstd_temp_size;
}
//void launchKernel(void * *temp_dev_buffer);

int cuda_device_count; 
int main(int argc, char *argv[])
{
	
	int rc = 0;
	umask(0);
	void * gpu_buffer[16];

	pthread_mutex_init(&skyfs_debug_lock, NULL);

	client_this_id = atoi(argv[argc - 1]);

	SKYFS_MSG("client_main:client_id:%d\n", client_this_id);

	__skyfs_daemonize(client_this_id);

	SKYFS_ENTER("client_main:enter\n");

	rc = __skyfs_C_get_var_conf();

	skyfs_log_file_len = 0;

	rc = __skyfs_C_init_com();

	rc = __skyfs_C_create_threads();

	rc = __skyfs_C_init_layout();
	
	rc = __skyfs_C_init_cache();

	cuda_device_count = launchKernel(gpu_buffer, 128*1024, 512, &nvcomp_zstd_temp_size);
	if(1){
		SKYFS_ERROR_1("Skyfs client cuda device and init , nvcomp_zstd_temp_size %lu\n",nvcomp_zstd_temp_size );
		//nvcomp_zstd_temp_size = 0;
	}


 	init_compress_worker_threads();
	rc = fuse_main(argc - 1, argv, &skyfs_oper, NULL);

	SKYFS_MSG("client_main:begin to process request\n");
	
	return 0;
}

/*This is end of client.c*/
