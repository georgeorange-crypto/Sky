/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: mds.c $
 */

#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"
#include "skyfs_fs.h"
#include "skyfs_debug.h"
#include "skyfs_hash.h"
#include "amp.h"
#include "skyfs_msg.h"

#include "mds_fs.h"
#include "mds_op.h"
#include "mds_thread.h"
#include "mds_init.h"
#include "mds_log.h"

sem_t skyfs_MS_finailize_sem;
skyfs_s32_t skyfs_log_file_len;

skyfs_s32_t main(skyfs_s32_t argc, skyfs_s8_t **argv)
{
	skyfs_s32_t	rc = 0;
	
	skyfs_log_file_len = 0;

	rc = __skyfs_MS_parse_parameter(argc, argv);
	if(rc < 0){
		SKYFS_ERROR("MS_main:parameter error\n");
		goto err_out;
	}

	rc = __skyfs_MS_get_conf();
	if(rc < 0){
		SKYFS_ERROR("MS_main:get conf error\n");
		goto err_out;
	}


	rc = __skyfs_MS_init_com();
	if(rc < 0){
		SKYFS_ERROR_1("MS_main:init com error\n");
		goto err_out;
	}

	rc = __skyfs_MS_create_threads();	
	if(rc < 0){
		SKYFS_ERROR_1("MS_main:create thread error\n");
		goto err_out;
	}

	rc = __skyfs_MS_init_fs();
	if(rc < 0){
		SKYFS_ERROR_1("MS_main:init fs error\n");
		goto err_out;
	}

	rc = __skyfs_MS_init_log();
	if(rc < 0){
		SKYFS_ERROR("MS_main:init log system error\n");
		goto err_out;
	}

	rc = __skyfs_MS_init_signal();
	if(rc < 0){
		SKYFS_ERROR("MS_main:init signal error\n");
		goto err_out;
	}

	sem_init(&skyfs_MS_finailize_sem, 0, 0);
	SKYFS_ERROR("MS_main:begin to process request\n");
	amp_sem_down(&skyfs_MS_finailize_sem);
	SKYFS_ERROR("MS_main:bye bye.\n");

err_out:
	return rc;
}
/*This is end of file*/
