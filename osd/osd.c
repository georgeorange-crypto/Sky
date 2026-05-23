/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd.c $
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


#include "osd_init.h"
#include "osd_thread.h"

sem_t skyfs_SS_finailize_sem;
skyfs_s32_t skyfs_log_file_len;


skyfs_s32_t main(skyfs_s32_t argc, skyfs_s8_t **argv)
{
	skyfs_s32_t	rc = 0;
	
	skyfs_log_file_len = 0;

	rc = __skyfs_SS_parse_parameter(argc, argv);
	if(rc < 0){
		SKYFS_ERROR("SS_main:parameter error\n");
		goto err_out;
	}

	rc = __skyfs_SS_create_threads();	
	if(rc < 0){
		SKYFS_ERROR("SS_main:create thread error\n");
		goto err_out;
	}

#if 1
	rc = __skyfs_SS_get_conf();
	if(rc < 0){
		SKYFS_ERROR("SS_main:get conf error\n");
		goto err_out;
	}
#endif
	rc = __skyfs_SS_init_com();
	if(rc < 0){
		SKYFS_ERROR("SS_main:init com error\n");
		goto err_out;
	}


	rc = __skyfs_SS_init_signal();
	if(rc < 0){
		SKYFS_ERROR("SS_main:init signal error\n");
		goto err_out;
	}

	SKYFS_ERROR("SS_main:begin to init obd\n");
	rc = __skyfs_SS_init_osd();
	if(rc < 0){
		SKYFS_ERROR("SS_main:init obd error\n");
		goto err_out;
	}


	SKYFS_ERROR("SS_main:begin to process request\n");
	amp_sem_down(&skyfs_SS_finailize_sem);
	SKYFS_ERROR("SS_main:bye bye.\n");

err_out:
	return rc;
}
/*This is end of osd.c*/
