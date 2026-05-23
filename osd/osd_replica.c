/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_replica.c $
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


skyfs_s32_t 
__skyfs_SS_choose_replica_place(skyfs_u32_t subset_id,
			skyfs_u32_t *osd_replica2,
			skyfs_u32_t *osd_replica3)
{
	skyfs_s32_t rc = 0;
	//skyfs_s32_t osd_choosen = 0;
	skyfs_u32_t osd[2];
	//skyfs_s32_t i;
	//skyfs_u32_t tmp_id;
	//skyfs_timespec_t rand_time;

	osd[0] = osd[1] = 0;
/*
	for(i = 0; i < 2; i++){
		while(osd_choosen!= 1){
			gettimeofday(&rand_time, NULL);
			tmp_id = (rand() + 1)%osd_num + 1;
			if(tmp_id != osd_this_id && tmp_id != osd[1 - i]){
				osd[i] = tmp_id;
				osd_choosen = 1;
			}
		}
		osd_choosen = 0;
	}

	*osd_replica2 = osd[0];
	*osd_replica3 = osd[1];
	*/
	*osd_replica2 = (osd_this_id )%osd_num + 1;
	*osd_replica3 = (osd_this_id + 1)%osd_num + 1;
	SKYFS_ERROR("__skyfs_SS_choose_replica_place:osd2:%d,osd3:%d\n", 
		*osd_replica2, *osd_replica3);

	return rc;
}

/*This is end of osd_replica.c*/
