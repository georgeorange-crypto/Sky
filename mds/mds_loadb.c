/* 
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: mds_loadb.c $
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
#include "mds_loadb.h"
#include "mds_layout.h"
#include "mds_itm.h"
#include "mds_cache.h"
#include "mds_help.h"

/*Hard load server clear pointed hash tables 
 * and set the related hash tables in the light load server*/
void __skyfs_MS_start_balance(amp_request_t *req)
{
	skyfs_msg_t         *msgp = NULL;
	skyfs_u32_t			kind_mds_id;
	skyfs_u32_t         balance_num;
	skyfs_u32_t         num_balanced = 0;
	skyfs_u32_t			index;
	skyfs_u32_t			first_index;
	skyfs_u32_t         last_flag = 0;
	skyfs_u32_t			size;
	skyfs_u32_t			i;
	skyfs_s32_t         rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
	kind_mds_id = msgp->u.balanceloadReq.kind_mds_id;
	balance_num = msgp->u.balanceloadReq.balance_num;
	first_index = msgp->u.balanceloadReq.first_index;

	SKYFS_ERROR("__skyfs_MS_start_balance:enter,kind_mds_id:%d\n", kind_mds_id);

	for(i = first_index; num_balanced < balance_num; i ++){
		index = i % SKYFS_SUBSET_HASH_LEN;
		rc = __skyfs_MS_clear_htbcache(index, kind_mds_id);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_MS_start_balance:clear htbcache error:rc:%d\n", rc);
			goto err_out;				
		}

		if(num_balanced == balance_num - 1){
			last_flag = 1;
			//mds_layout_version ++;
		}

		rc = __skyfs_M2M_add_htbcache(kind_mds_id, index, last_flag);
		if(rc < 0){
			SKYFS_ERROR("__skyfs_MS_start_balance:add htb in %d error\n", kind_mds_id);
			goto err_out;				
		}

		num_balanced ++;

	}

err_out:
	size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_MS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

	msgp->error = rc;
    rc = amp_send_sync(mds_comp_context,
            req,
            req->req_remote_type,
            req->req_remote_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_MS_start_balance:send reply failed.rc:%d\n", rc);
    }

	if(req->req_msg){ 
         free(req->req_msg); 
    } 
   
	if(req->req_reply){ 
         free(req->req_reply); 
    } 

	__amp_free_request(req); 

	SKYFS_LEAVE("__skyfs_MS_start_balance:leave:,type:%d,id:%d, mds_layoutv:%d\n",
		req->req_remote_type, req->req_remote_id, mds_layout_version);

}

void __skyfs_MS_balance_load(amp_request_t *req)
{
	skyfs_msg_t         *msgp = NULL;
	skyfs_state_info_t  *state_info = NULL;
	skyfs_u32_t         state_version;
	skyfs_u32_t			mds_id;
	skyfs_u32_t			kind_mds_id;
	skyfs_s32_t         first_index;
	skyfs_u32_t         balance_num;
	skyfs_s32_t         rc = 0;
 	
    msgp = __skyfs_get_msg(req->req_msg);
	state_info = &(msgp->u.triggerblaReq.state_info);
	state_version = msgp->u.triggerblaReq.state_info.state_version;

	mds_id = msgp->fromid;

	SKYFS_ERROR("__skyfs_MS_balance_load:enter,mds_id:%d,statev:%d,cpu:%lld\n", 
		mds_id, state_version, state_info->cpu_usage);

	if(state_version > mds_status[mds_id].state_version){
		memcpy(&mds_status[mds_id].state_info, state_info, sizeof(skyfs_state_info_t));
		if(state_info->cpu_usage < mds_red_alarm){
			rc = __skyfs_M2M_collect_state();
			if(rc < 0){
				SKYFS_ERROR("__skyfs_MS_balance_load:collect_state error:%d\n", rc);
				goto err_out;
			}

			if(mds_status[mds_id].state_info.cpu_usage > mds_red_alarm){
				SKYFS_ERROR("__skyfs_MS_balance_load:%d not busy, need not to balance\n", mds_id);
				goto err_out;
			}

			kind_mds_id = __skyfs_MS_choose_kindone();
			if(kind_mds_id <= 0 || kind_mds_id == mds_id){
				SKYFS_ERROR("__skyfs_MS_balance_load:error mds choose to balance\n");
				goto err_out;
			}

			balance_num = mds_status[mds_id].hashnum / mds_balance_ratio;
			if(balance_num == 0){
				SKYFS_MSG("__skyfs_MS_balance_load:error balance_num0,mds:%d\n", mds_id);
				goto err_out;
			}

			first_index = __skyfs_MS_get_first_loadout(mds_id, balance_num);
			if(first_index < 0){
				SKYFS_MSG("__skyfs_MS_balance_load:error get first index\n");
				goto err_out;
			}

			rc = __skyfs_M2M_start_balance(mds_id,kind_mds_id,first_index,balance_num);
			if(rc < 0){
				SKYFS_ERROR("__skyfs_MS_balance_load:balance between %d and %d error\n",
					mds_id, kind_mds_id);
				goto err_out;
			}

			rc = __skyfs_MS_do_update_mdslayout(mds_id, kind_mds_id, 
					first_index, balance_num);
			if(rc < 0){
				SKYFS_ERROR("__skyfs_MS_balance_load:up layout error.%d,%d,%d,%d.%d\n", 
					mds_id, kind_mds_id, first_index, balance_num, rc);
				goto err_out;
			}
			
			/*used to triggle update layout,need to fix to certain function*/
			rc = __skyfs_M2M_collect_state();
			if(rc < 0){
				SKYFS_ERROR("__skyfs_MS_balance_load:collect_state error:%d\n", rc);
				goto err_out;
			}

		}
	}

err_out:
	if(req->req_msg){ 
         free(req->req_msg); 
    } 
   
	__amp_free_request(req); 

	SKYFS_LEAVE("__skyfs_MS_balance_load:exit,rc:%d\n", rc);

}

skyfs_s32_t 
__skyfs_MS_choose_kindone()
{
	skyfs_u32_t kindone = 0;
	skyfs_u32_t mds_id = 0;
	skyfs_u32_t min_usage = 0;
	skyfs_u32_t usage = 0;
	skyfs_u32_t i;

	SKYFS_ERROR("__skyfs_MS_choose_kindone:enter:id:%d\n", mds_info.mds[1].id);

	min_usage = (100 - mds_status[mds_info.mds[1].id].state_info.cpu_usage)
			+ mds_status[mds_info.mds[1].id].hashnum * 2;
	kindone = mds_info.mds[1].id;

	for(i = 1; i <= mds_num; i ++){
		mds_id = mds_info.mds[i].id;
		SKYFS_MSG("__skyfs_MS_choose_kindone:mds_id:%d,cpu:%llu, hash:%d\n",
			mds_id,
			100 - mds_status[mds_id].state_info.cpu_usage,
			mds_status[mds_id].hashnum);
		usage = (100 - mds_status[mds_id].state_info.cpu_usage) 
				+ mds_status[mds_id].hashnum * 2;
		if(usage < min_usage){
			min_usage = usage;
			kindone =  mds_id;
		}
	}


	SKYFS_ERROR("__skyfs_MS_choose_kindone:mds_id:%d,usage:%d\n", kindone, min_usage);
	return kindone;
}
/*This is end of mds_loadb.c*/
