/* 
 *  Copyright (c) 2010  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *  *  Written by Xing Jing */ 
/*
 * $Id: client_itm.c $
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
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <zstd.h>
#include <zlib.h>

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
#include "client_ito.h"
#include "sz3c.h"
#include "gpu_compress.h"

extern int isComp_zstd();

extern pthread_mutex_t * get_dev_mutex(int dev_idx);

 extern void free_cpu_batch(void * cpu_batch);
#if 0
 extern void call_gpu_zstd_compress(char* data, int size,
                        size_t warmup_iteration_count, size_t total_iteration_count,
                        size_t * comp_bufs_count, size_t * comp_buf_sizes, char ** comp_bufs,
                        void ** pbatch);
#endif

extern char *  compress_func(char * src, size_t  src_len, size_t * dest_len, int algorithm);
extern char *  decompress_func(char * csrc, size_t csrc_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf);

extern char * adm_compress_data(char * src, size_t src_len , size_t* pdest_len);
extern char * adm_decompress_data(char * src, size_t src_len , size_t* pdest_len);


extern uint8_t *  pans_compress_data(uint8_t * src, uint32_t src_len, uint32_t * pdest_len);
extern uint8_t *  pans_decompress_data(uint8_t * src, uint32_t src_len, uint32_t * pdest_len);


char *  compress_output(char * src, size_t  src_len, size_t * dest_len, int algorithm);
char *  decompress_output(char * csrc, size_t csrc_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf);



/*TODO  mayl : only use 2 OSD for test*/
static replica_cnt = 1;
static uint64_t read_obj_cnt = 0;
static uint64_t read_obj_time = 0;
static uint64_t read_obj_compress_time = 0;
static int find_osd_cnt = 0;


static uint64_t write_obj_cnt = 0;
static uint64_t write_obj_time = 0;
static uint64_t write_obj_compress_time = 0;
static uint64_t write_obj_pre_compress_time = 0;
static uint64_t write_obj_do_compress_time = 0;
static uint64_t write_obj_post_compress_time = 0;
static uint64_t total_gpu_net_time = 0;
static uint64_t total_rdma_net_time = 0;
static uint64_t total_write_server_time = 0;

extern int get_replica();
static virtue_osd_cnt = SKYFS_OSD_CONSIST_HASH_SEGSIZE;


void clear_write_stat()
{
	SKYFS_ERROR_1("skyfs write stat: write_obj_cnt %llu, write_obj_time %llu write_compress time %llu, buf_init %llu, pre_compress %llu, do_compress %llu , post compress %llu \n",
   write_obj_cnt, write_obj_time, write_obj_compress_time, get_gpu_comp_init_time(),
   write_obj_pre_compress_time, write_obj_do_compress_time, write_obj_post_compress_time);


	SKYFS_ERROR_1("total_gpu_net_time %llu,  total_rdma_net_time %llu,  total_write_server_time %llu \n", total_gpu_net_time,  total_rdma_net_time , total_write_server_time);
	write_obj_cnt = 0;
	write_obj_time = 0;
	write_obj_compress_time = 0;
	write_obj_pre_compress_time = 0;
	write_obj_do_compress_time = 0;
	write_obj_post_compress_time = 0;
	clear_gpu_comp_init_time();
	total_gpu_net_time = 0;
}

uint32_t find_osdgid(uint32_t osd_cnt, skyfs_ino_t ino, size_t obj_id)
{
	uint32_t osdgid = 1;
	uint64_t scope;
	uint32_t region_hash = (ino + obj_id/SKYFS_MAX_OBJ_PER_PART)&0x3fffffff;
	uint64_t cur_pos;

	if(osd_cnt == 1 || region_hash == 0){
		osdgid = 0;
		goto ret_exit;
	}
	//scope = (((uint64_t)(1<<32))+osd_cnt-1)/osd_cnt;  // 10.5,2
	//cur_pos = ((region_hash / scope))*scope ;
	 
	// TODO : so cur_pos beween 0 to 128k -1 distribute in all OSDs
	cur_pos = region_hash/SKYFS_OSD_CONSIST_HASH_SEGSIZE;
	//uint64_t tmp_group_size = (((uint64_t)1)<<32)/SKYFS_OSD_CONSIST_HASH_SEGSIZE;

        osdgid = cur_pos;	
#if 0
	while(region_hash >= cur_pos ){
		osdgid ++;
		cur_pos += scope;
	}
#endif

	
ret_exit:
	find_osd_cnt++;
	if(find_osd_cnt < 30){
		SKYFS_ERROR("find osd_gid %d, ino %llu, obj_id %d, region %lu\n", 
				osdgid, ino, obj_id, region_hash);
	}
	return osdgid;
	//return (osdgid % osd_cnt)+1;
}


/* TODO : the two function below should  change when we support OSD HA or load balance  */
int find_specical_replica_osd(int osd_gid, int replica_id)
{
      
      return 0;

}

// TODO: how to suppert load balance 
int find_normal_replica_osd(int osd_cnt, int osd_gid, int replica_id)
{
     
     int ret = 0;
     int this_replica_osd = 0;
     //size_t virtue_groups_per_osd = (1ull <<32) /SKYFS_OSD_CONSIST_HASH_SEGSIZE/osd_cnt;

     size_t virtue_groups_per_osd = (((1ull <<30) / virtue_osd_cnt)+osd_cnt)/osd_cnt;

     // osd for the replica 1;
     this_replica_osd = ((int)osd_gid)/virtue_groups_per_osd;
     this_replica_osd = (this_replica_osd+replica_id-1)%osd_cnt;
     ret = this_replica_osd+1;

     ret = this_replica_osd+1;
     if(find_osd_cnt < 30){
		SKYFS_ERROR("find osd_id, gid  %d osd_cnt %d, osd_id %d,  virtue_osd_cnt %d, v_osd_per_node %llu\n", 
				osd_gid, osd_cnt, ret,  virtue_osd_cnt,  virtue_groups_per_osd);
	}


     return ret;
}

uint32_t find_replica_osd(uint32_t osd_cnt, int osd_gid, int replica_id , int replica_cnt)
{
	uint32_t   cur_osd = 0;
#if 0
	int 	   found_replica = 1;
	if(replica_id == 1)
		goto ret_exit;

	while(found_replica < replica_id){
		cur_osd = find_next_osd(osd_gid,cur_osd);
		found_replica ++;
	}
#endif
	cur_osd =  find_specical_replica_osd(osd_gid, replica_id);
	if(cur_osd >0)
		goto ret_exit;

	cur_osd =  find_normal_replica_osd(osd_cnt, osd_gid, replica_id);
	if(cur_osd == 0){
		SKYFS_ERROR_1(" FAILED to got osd_id %d, for osd_gid %d , replica_id %d\n", cur_osd, osd_gid, replica_id);
	}

ret_exit:
	return cur_osd;

}

uint32_t find_prefer_replica_osd(uint32_t osd_cnt, int this_osd, int this_replica_id , int prefer_replica_id, int replica_cnt)
{
	uint32_t   cur_osd = this_osd;
	uint32_t   cur_replica_id = this_replica_id; 
	while (cur_replica_id != prefer_replica_id){
		if(cur_replica_id < prefer_replica_id){
			cur_replica_id ++;
			cur_osd ++;
			if(cur_osd > osd_cnt)
				cur_osd = 1;
			
		}else if(cur_replica_id > prefer_replica_id){
			cur_replica_id --;
			cur_osd--;
			if(cur_osd == 0)
				cur_osd = osd_cnt;
		
		}

	}
	SKYFS_ERROR("%s : find prefer osd %d , src osd %d, src replica %d, prefer replica %d, replica_cnt %d , osd cnt %d\n",
			cur_osd, this_osd, this_replica_id, prefer_replica_id, replica_cnt, osd_cnt);


	return cur_osd;



}



#if 0
uint32_t find_osdgid_fake(uint32_t osd_cnt, skyfs_ino_t ino, size_t obj_id)
{
	uint32_t osdgid = 1;
	uint64_t scope;
	uint32_t region_hash = (ino + obj_id/SKYFS_MAX_OBJ_PER_PART)& 0xffffffff;
	uint64_t cur_pos;

	if(osd_cnt == 1 || region_hash == 0){
		osdgid = 0;
		goto ret_exit;
	}
	scope = (((uint64_t)(1<<32))+osd_cnt-1)/osd_cnt;  // 10.5,2
	cur_pos = ((region_hash / scope))*scope ; 

	while(region_hash >= cur_pos ){
		osdgid ++;
		cur_pos += scope;
	}

ret_exit:
	return (osdgid % osd_cnt)+1;
}


uint32_t find_replica_osd_fake(uint32_t osd_cnt, int osd_gid, int replica_id , int replica_cnt)
{
	uint32_t   cur_osd = osd_gid;
	cur_osd = (cur_osd + ((replica_id-1) % replica_cnt)) % osd_cnt + 1 ;
	return cur_osd;

}
#endif

skyfs_s32_t __skyfs_C2O_listxattr(skyfs_ino_t ino,
				  skyfs_s8_t * buf,
				  skyfs_u32_t * psize)
{
    skyfs_u32_t osd_id = 0;
    skyfs_u32_t osd_gid = 0;
    skyfs_u32_t obj_id = 0;
    skyfs_u32_t req_size = 0;
    skyfs_s32_t rc = 0;
    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t *kiovp = NULL;


    skyfs_u32_t size = *psize;
    int replica_cnt = get_replica();

    osd_gid =  find_osdgid(osd_num, ino, obj_id);
	// TODO : aayl for read load balance
    osd_id = find_replica_osd(osd_num, osd_gid,
	((ino + obj_id/SKYFS_MAX_OBJ_PER_PART)%replica_cnt)+1, replica_cnt);
     
    rc = __amp_alloc_request(&req);
    if(rc < 0){
       		SKYFS_ERROR_1("__skyfs_C2O_listxattr:alloc request failed\n");
        	goto err_none;
    }

    rc = -ENOMEM;
    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_listxattr_args_t);    
    req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR("__skyfs_C2O_read:alloc req_msg failed\n");
        	goto err_req;
    	}

    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_LISTXATTR,
        	client_this_id, SKYFS_CLIENT,req_size);
		bzero(&(msgp->u.readObjReq.vec), sizeof(skyfs_io_vector_t));
    	msgp->u.listxattrReq.vec.ino = ino;
    	msgp->u.listxattrReq.vec.count = size;
    	msgp->u.listxattrReq.ino = ino;
	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, req_size);

        SKYFS_ERROR_1("__skyfs_C2O_listxattr:send request . ino %lu , size %d\n", ino, size);
    	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_listxattr:send request failed.rc:%d\n", rc);
        	goto err_msg;
    	}
        SKYFS_ERROR_1("__skyfs_C2O_listxattr:send request succ. return rc:%d, \n", rc );

    	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
        SKYFS_ERROR_1("__skyfs_C2O_listxattr:send request msgp error rc:%d, \n", rc );
	if(rc < 0){
		if(rc == -ENOBUFS){
			*psize = msgp->u.listxattrAck.xattr_cnt;
			rc = -ERANGE;
		}
		goto err_msg;
	}else{
		*psize = msgp->u.listxattrAck.xattr_cnt;
		size = *psize;
        	kiovp = req->req_iov;
		char * xattr_buf = kiovp->ak_addr;
	        memcpy(buf, xattr_buf, size);
		SKYFS_ERROR_1("names: %s, ak_len %d\n", buf, kiovp->ak_len);
		for(int j = 0; j< size; j++){
			SKYFS_ERROR("%c:", buf[j]);
		}
		SKYFS_ERROR_1("\n");
		rc = size;	

	}
	if(kiovp->ak_addr){
            	free(kiovp->ak_addr);
		kiovp->ak_addr = NULL;
        }

        if(kiovp){
            	free(kiovp);
		kiovp = NULL;
        }

err_msg:
   	//SKYFS_MSG("__skyfs_C2O_read:final free req_msg:\n");
   	if(req){
		if(req->req_msg){
       		free(req->req_msg);
		}
   	}
err_req:
    	SKYFS_MSG("__skyfs_C2O_read:free req\n");
    if(req){
        __amp_free_request(req);
    }

err_none:
	return rc;

	
}

skyfs_s32_t
__skyfs_C2O_read(skyfs_ino_t ino,
                skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
		skyfs_u32_t compress_type,
		skyfs_u32_t direct_io)
{
    skyfs_s32_t rc = 0;
    skyfs_u64_t ori_len = 0;
    skyfs_s32_t total_read = 0;
    struct timeval readobj_start_tv, readobj_end_tv;
    struct timeval readobj_compstart_tv, readobj_compend_tv;

    skyfs_u32_t subset_id;
    skyfs_u32_t chunk_id;

    skyfs_u32_t replica_id;
    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t *kiovp = NULL;
    skyfs_u32_t req_size;
    
    skyfs_s8_t *read_buf = NULL;
    skyfs_s8_t *blk_buf = NULL;

    skyfs_u32_t osd_id = 0;
    skyfs_u32_t osd_gid = 0;
    skyfs_u32_t op_size;
    skyfs_u32_t op_off;
    skyfs_u32_t obj_start;
    skyfs_u32_t obj_stop;
    skyfs_u64_t obj_id;
    skyfs_u64_t last_obj_id;
    skyfs_u32_t buf_off;
    skyfs_s32_t adapt_op = 0;
    size_t loop_count = 0;
    //size_t curr_offset = 0;

    int zstd_comp = 0;

      if(isComp_zstd() && compress_type){
	      zstd_comp = 1;
      }

    replica_cnt = get_replica();

	obj_start = offset / SKYFS_OBJECT_SIZE;
	obj_stop = (offset + size - 1) / SKYFS_OBJECT_SIZE;
	buf_off = 0;

	SKYFS_ERROR("__skyfs_C2O_read:enter:ino:%llu,offset:%llu,count:%u, direct_io %d, comp %d , ctype %d \n", ino, offset, size, direct_io,zstd_comp, compress_type );
	// added by mayl , for test ualigned read, will be removed lator
#if 0
	if(offset % 0xfff){
		SKYFS_ERROR("OFFSET not aligned PAGE size , testing return 0 \n ");
		return 0;
	}
#endif
	SKYFS_ENTER("__skyfs_C2O_read:obj_start:%u,obj_stop:%u\n", obj_start, obj_stop);

    	op_off= offset % SKYFS_OBJECT_SIZE;
	//curr_offset = op_off;
	

	if(zstd_comp ){
		op_size = SKYFS_OBJECT_NODE_SIZE - (op_off % SKYFS_OBJECT_NODE_SIZE);
	}else{
		//op_size = SKYFS_OBJECT_SIZE - (op_off % SKYFS_OBJECT_SIZE);
		 op_size = SKYFS_OBJECT_NODE_SIZE - (op_off  % SKYFS_OBJECT_NODE_SIZE);

	}
	if (op_size+buf_off > size){
		op_size = size - buf_off;
	}
	obj_id = obj_start;
	last_obj_id = obj_start;

	read_obj_cnt ++;
	gettimeofday(&readobj_start_tv, NULL);
	while(obj_id <= obj_stop) 
	{
		SKYFS_ERROR("readobj, loop count %lu, buf_off %lu, op_size %lu, size %lu\n", loop_count, buf_off, op_size, size);
		if(loop_count > 0){
    		//op_off= offset % SKYFS_OBJECT_SIZE;
		    //curr_offset += op_size;
		    buf_off += op_size;
			SKYFS_ERROR("next readobj, loop count %lu, buf_off %lu, op_size %lu, size %lu\n", loop_count, buf_off, op_size, size);
		    if (buf_off >= size)
			    break;
		     op_off= (offset+buf_off) % SKYFS_OBJECT_SIZE;
		     
		     if(zstd_comp){
		     	op_size = SKYFS_OBJECT_NODE_SIZE - ((buf_off+offset) % SKYFS_OBJECT_NODE_SIZE);
		     }else{
			     op_size = SKYFS_OBJECT_SIZE - ((buf_off+offset) % SKYFS_OBJECT_SIZE);
		     }

        	     if (op_size + buf_off  > size){
                	op_size = size - buf_off;
		     }
		     obj_id = (buf_off+offset) / SKYFS_OBJECT_SIZE;
		     
		     if(last_obj_id < obj_id){
			     op_off = 0;
		     }
		     last_obj_id = obj_id;
		     
		     //op_off = curr_offset;
			     

        	}

#if 0
			if((obj_id + 1) * SKYFS_OBJECT_SIZE >= (offset +size)){
				op_size = size;
			}else{
                op_size = SKYFS_OBJECT_SIZE - op_off; 
			}
		}else{
			op_off = 0;
            if((obj_id + 1) * SKYFS_OBJECT_SIZE >= (offset +size)){
				op_size = size - buf_off;
			}else{
                op_size = SKYFS_OBJECT_SIZE;
			}
		}
#endif

		//adapt_op = __skyfs_C_lookup_dlentry(ino, obj_id, 
		//			&osd_id, &subset_id, &chunk_id);
		if(adapt_op > 0){
			goto begin_process;
		}
normal_locate:
    	subset_id = __skyfs_C_get_dl_subsetid(ino, obj_id);
	osd_gid =  find_osdgid(osd_num, ino, obj_id);
	// TODO : aayl for read load balance
	osd_id = find_replica_osd(osd_num, osd_gid,
		((ino + obj_id/SKYFS_MAX_OBJ_PER_PART)%replica_cnt)+1, replica_cnt);
    	//osd_id = __skyfs_C_judge_osdid(subset_id);

begin_process:
	replica_id = ((ino + obj_id/SKYFS_MAX_OBJ_PER_PART)%replica_cnt)+1;	
       	SKYFS_ERROR("C2O_read, ino %lu ,partition_id %lu, replica_id %d,osd_gid %lu, osd_id %u \n",
			 ino, obj_id/SKYFS_MAX_OBJ_PER_PART, replica_id, osd_gid,osd_id );
    	rc = __amp_alloc_request(&req);
    	if(rc < 0){
       		SKYFS_ERROR("__skyfs_C2O_read:alloc request failed\n");
        	goto err_none;
    	}else{
		
	}
	

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR("__skyfs_C2O_read:alloc req_msg failed\n");
        	goto err_req;
    	}

    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_READ_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
		bzero(&(msgp->u.readObjReq.vec), sizeof(skyfs_io_vector_t));
    	msgp->u.readObjReq.vec.ino = ino;
    	msgp->u.readObjReq.vec.obj_id = obj_id;
    	msgp->u.readObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.readObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.readObjReq.vec.offset = op_off;
    	msgp->u.readObjReq.vec.count = op_size;
    	msgp->u.readObjReq.vec.forward_count = 0;
	// add by mayl for non compressing  mode
    	msgp->u.readObjReq.vec.algorithm = compress_type & 0x0ffff;
	msgp->u.readObjReq.vec.replica_id =  ((ino + obj_id/SKYFS_MAX_OBJ_PER_PART)%replica_cnt)+1;
    	msgp->u.readObjReq.vec.replica_num = replica_cnt;
	if(direct_io){
		/* added by mayl, direct_op == 1 means O2O forward, 2 means direct_read*/
		msgp->u.readObjReq.vec.direct_op = 2;
	}

		if(adapt_op){
			msgp->u.readObjReq.dest = osd_id;
			msgp->u.readObjReq.subset = subset_id;
			msgp->u.readObjReq.chunk = chunk_id;
		}

	SKYFS_ERROR("send readobj, ino %lu, offset %lu, count %lu , replica_id %lu , dest %d\n",
			 msgp->u.readObjReq.vec.ino,  msgp->u.readObjReq.vec.offset, msgp->u.readObjReq.vec.count,  msgp->u.readObjReq.vec.replica_id,  msgp->u.readObjReq.dest );
    	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, req_size);

    	SKYFS_MSG("__skyfs_C2O_read:ino:%llu,obj_off:%u,obj_size:%u\n",
        	ino, op_off, op_size);
		SKYFS_MSG("__skyfs_C2O_read: obj_id:%llu,sid:%u,osd_id:%u,type:%d\n",
			obj_id, subset_id, osd_id,req->req_type);
	//gettimeofday(&readobj_start_tv, NULL);
    	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	

    	if(rc < 0){
        	SKYFS_ERROR("__skyfs_C2O_read:send request failed.rc:%d\n", rc);
        	goto err_msg;
    	}

    	msgp = __skyfs_get_msg(req->req_reply);

    	SKYFS_ERROR("__skyfs_C2O_read:rc:%d,fromid:%d,fromType:%d,type:%d\n", 
        	msgp->error, msgp->fromid, msgp->fromType, req->req_type);

    	rc = msgp->error;
	if(rc <= 0){
		SKYFS_ERROR_1("C2O read return faild,  ret %d  ino %llu , offset %lu, need count %lu, obj %llu, osdid %d \n", 
				ino, rc, offset, size, obj_id, osd_id);
	}
    	if(rc > 0){
		read_buf = (skyfs_s8_t *)(buf + buf_off);
        	kiovp = req->req_iov;
        	blk_buf = kiovp->ak_addr;
		int need_new_buf = 0;

		if(rc > size)
        		SKYFS_ERROR("__skyfs_C2O_read: comp %d ,:rc:%d,buf:%p,read_buf:%p, offset %lu , foffset %lu , op_off %lu ,compressed_size %lu, algorithm %d, need_size %lu\n", 
				zstd_comp,rc, buf, read_buf, offset, kiovp->ak_foffset, op_off, kiovp->ak_flen, msgp->u.readObjAck.algorithm, size);
		if(zstd_comp && kiovp->ak_flen != 0){

			
		// decompress and combine the buffer. by mayl
		        char * decomp_buf = NULL;
			if(msgp->u.readObjAck.algorithm != 0){

				gettimeofday(&readobj_compstart_tv, NULL);
				if(kiovp->ak_foffset == op_off && kiovp->ak_flen == op_size){
					decomp_buf =  decompress_func(blk_buf, rc, kiovp->ak_flen, &ori_len, msgp->u.readObjAck.algorithm, read_buf);
				}else{
					need_new_buf = 1;
					decomp_buf =  decompress_func(blk_buf, rc, kiovp->ak_flen, &ori_len, msgp->u.readObjAck.algorithm, NULL);
				}
				gettimeofday(&readobj_compend_tv, NULL);
				if(decomp_buf == NULL){
					rc = 0;
					SKYFS_ERROR_1("alloc memory for decompress failed %lu, %lu \n ",obj_id, op_off );
					goto next_loop;
				}

			}else{
				//SKYFS_ERROR_1("keep original data , do not decompress, buf[0]: %x\n", blk_buf[0]);
				ori_len = rc;
				decomp_buf = blk_buf;
        			memcpy(read_buf, decomp_buf, rc);
			}

        		SKYFS_ERROR("__skyfs_C2O_read: decompress get %lu bytes, from offset %lu\n", ori_len, kiovp->ak_foffset );

			if(kiovp->ak_foffset == op_off){
				size_t cp_len = ori_len ;
				if(cp_len > op_size)
					cp_len = op_size;
				rc = cp_len;
				if(need_new_buf)
        				memcpy(read_buf, decomp_buf, rc);
				

			}else if(kiovp->ak_foffset >= op_off+op_size){
				rc = 0;
			}else if(kiovp->ak_foffset + ori_len <= op_off){
				rc = 0;
			}else{
				// copy corossed part 
				size_t min_end, max_start,  copy_len, src_off, dest_off;
				max_start = kiovp->ak_foffset >= op_off ? kiovp->ak_foffset: op_off;
				min_end = kiovp->ak_foffset+kiovp->ak_flen >= op_off + op_size? op_off+op_size-1: kiovp->ak_foffset+kiovp->ak_flen-1;
				copy_len = min_end-max_start+1;

				if(max_start == kiovp->ak_foffset){
					src_off = 0;
					dest_off = kiovp->ak_foffset - op_off;
				}else{
					src_off = op_off-kiovp->ak_foffset;
					dest_off = 0;
				}
				
				SKYFS_ERROR("read data not exact match, overlaped.  foffset %lu, op_off %lu, max_start %lu, min_end %lu\n", 
						kiovp->ak_foffset, op_off, max_start, min_end);
				rc = copy_len;
				memset(read_buf, 0, op_size);
        			memcpy(read_buf+dest_off, decomp_buf+src_off, rc);



			}
			if(decomp_buf != NULL  && msgp->u.readObjAck.algorithm != 0  && need_new_buf)
				free(decomp_buf);


		}else{ // no commpress algorithm 
        		memcpy(read_buf, blk_buf, rc);
		}	
        	SKYFS_ERROR("__skyfs_C2O_read:right:rc:%d,buf:%p,read_buf:%p\n", 
				rc, buf, read_buf);

		//SKYFS_ERROR_1("READOBJ4 get req %p , reply %p \n", req, req->req_reply);
		if(rc != op_size){
			SKYFS_ERROR_1("skyfs_readobj insuff reply, want %lu bytes, got %lu bytes!\n", op_size, rc);
		}
		
		if(0){
			uint64_t * pdata = (uint64_t*)read_buf;
			uint64_t check_data1 = pdata[126];
			uint64_t check_data2 = pdata[127];
			if(msgp->u.readObjAck.check_data1 != check_data1){
				SKYFS_ERROR("skyfs_readobj data1 msmatch: , %llx, %llx\n", check_data1, msgp->u.readObjAck.check_data1);
			}
			if(msgp->u.readObjAck.check_data2 != check_data2){
				SKYFS_ERROR("skyfs_readobj data2 msmatch: , %llx, %llx\n", check_data2, msgp->u.readObjAck.check_data2);
			}

		}


next_loop:
        	if(kiovp->ak_addr){
            		free(kiovp->ak_addr);
			kiovp->ak_addr = NULL;
        	}



        	if(kiovp){
            		free(kiovp);
			kiovp = NULL;
        	}


			total_read = total_read + rc;

			//if(! do_async_write){
				subset_id = msgp->u.readObjAck.subset;
				chunk_id = msgp->u.readObjAck.chunk;
			//}
		
		if(adapt_op == 0){
			// mayl add dlentry if first read success 
			//__skyfs_C_add_dlentry(ino, obj_id, msgp->fromid, subset_id, chunk_id);
		}
	// end rc >0 
    	}else if(rc == -2 && adapt_op == 2){
			adapt_op = 0;
			goto normal_locate;
		}else{
	    	SKYFS_ERROR("__skyfs_C2O_read:read err osd_id:%d,rc:%d\n", osd_id, rc);
			//exit(1);
        	//rc = 0;
    	}

    	if(msgp->fromid != osd_id){
        	SKYFS_ERROR_1("__skyfs_C2O_read:msg forward,newosd:%d,oldosd:%d, ino %llu , obj_id %lu\n",
            	msgp->fromid, osd_id, ino, obj_id);
        	__skyfs_C_clear_dl_depth();
			if(adapt_op == 0){
				__skyfs_C_add_dlentry(ino, obj_id, msgp->fromid, subset_id, chunk_id);
			}else{
				__skyfs_C_release_dlentry(ino, obj_id);
			}
    	}

    	SKYFS_MSG("__skyfs_C2O_read:free req_reply:%p\n", req->req_reply);
    	if(req->req_reply){
        	free(req->req_reply);
			req->req_reply = NULL;
    	}
    	
		SKYFS_MSG("__skyfs_C2O_read:free req_msg:%p\n", req->req_msg);
    	if(req->req_msg){
        	free(req->req_msg);
			req->req_msg = NULL;
    	}
    
		SKYFS_MSG("__skyfs_C2O_read:free req\n");
    	if(req){
        	__amp_free_request(req);
			req = NULL;
    	}
	
		adapt_op = 0;	
		//buf_off = buf_off + op_size;
		loop_count++;
     } /* end while  */

     gettimeofday(&readobj_end_tv, NULL);
     read_obj_cnt ++;

     read_obj_time += (readobj_end_tv.tv_sec*1000000+ readobj_end_tv.tv_usec);
     read_obj_time -= (readobj_start_tv.tv_sec*1000000+ readobj_start_tv.tv_usec);


     read_obj_compress_time += (readobj_compend_tv.tv_sec*1000000+ readobj_compend_tv.tv_usec);
     read_obj_compress_time -= (readobj_compstart_tv.tv_sec*1000000+ readobj_compstart_tv.tv_usec);
     
     if(read_obj_cnt % 10000 == 0 && read_obj_cnt >100){
		SKYFS_ERROR_1("read obj cnt %llu, total access time %llu us, decompress time %llu us\n ", 
				read_obj_cnt, read_obj_time, read_obj_compress_time);
		//SKYFS_ERROR_1("current read req start time %llu, end_time %llu, send_time %llu, recv_time %llu, reply_time %llu\n",
		//		(readobj_start_tv.tv_sec*1000000+ readobj_start_tv.tv_usec),
		//	       (readobj_end_tv.tv_sec*1000000+ readobj_end_tv.tv_usec),	
		//		req->req_send_time, req->req_recv_time, req->req_reply_time);
     }

err_msg:
   	SKYFS_MSG("__skyfs_C2O_read:final free req_msg:\n");
   	if(req){
		if(req->req_msg){
       		free(req->req_msg);
		}
   	}
err_req:
    	SKYFS_MSG("__skyfs_C2O_read:free req\n");
    if(req){
        __amp_free_request(req);
    }
err_none:

    SKYFS_LEAVE("__skyfs_C2O_read:exit,rc:%d, total_read:%d\n", 
		rc, total_read);

    if(total_read == 0 && size != 0 || (size+offset >= (1<<24)) ){
	SKYFS_ERROR("__skyfs_C2O_read:exit,zero read OR long pos read , rc:%d, total_read:%d, ino %llx, offset 0x%llx,size 0x%x , osd_id %d\n", 
		rc, total_read, ino, offset, size, osd_id);

    }else if(total_read == 4096){
	    SKYFS_ERROR("client read from %llu, get first char %d\n", offset, buf[0]);
    }

    return total_read;
}



char *  decompress_nonstd_output(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char* pre_dest_buf)
{
	char * rdest = NULL;
	char * dest = pre_dest_buf;
	int need_copy = 1;
	SKYFS_ERROR("decompress nonstd , algorithm %d\n", algorithm);

	if(dest == NULL && algorithm <= COMPRESS_ZLIB_ALGORITHM)
		dest = malloc(dest_buf_len);

	if(dest == NULL && algorithm <= COMPRESS_ZLIB_ALGORITHM){
                SKYFS_ERROR_1("failed to get decompressing for memory size %d \n", src_len);
                return NULL;
        }else if(dest == NULL){
		need_copy = 0;
	}
	

	if(algorithm == COMPRESS_NONE_ALGORITHM){
		*dest_len  =  src_len;
                memcpy(dest, src, src_len);
		return dest;

	}else if(algorithm == COMPRESS_ZLIB_ALGORITHM){

		// now do ZLIB decompress
		int ret = 0;
    		z_stream stream;
		stream.zalloc = Z_NULL;
    		stream.zfree = Z_NULL;
    		stream.opaque = Z_NULL;
    		stream.avail_in = (uInt)src_len; // 设置输入数据的长度
    		stream.next_in = (Bytef *)src;   // 设置输入数据的指针
    		if (inflateInit(&stream) != Z_OK) {
        		fprintf(stderr, "inflateInit error\n");
			free(dest);
        		return NULL;
		}
		stream.avail_out = dest_buf_len ; // 设置输出缓冲区的长度
    		stream.next_out = dest;            // 设置输出数据的指针

    		// 执行压缩操作

    		ret = inflate(&stream, Z_FINISH);
		if((ret != Z_OK) && (ret != Z_STREAM_END)){
			fprintf(stderr, "inflate return failed %d\n", ret);
    			// 清理资源
    			inflateEnd(&stream);
			free(dest);
			return NULL;

		}
    		*dest_len = stream.total_out; // 设置实际的压缩数据长度

    		// 清理资源
    		inflateEnd(&stream);

    	}else if(algorithm == COMPRESS_ADM_ALGORITHM){
	   rdest =  adm_decompress_data( src, src_len , dest_len);
	   if(*dest_len <= 0){
		   SKYFS_ERROR_1("compress failed at altorithm %d\n", algorithm);
		   if(rdest != NULL){
			   free(rdest);
			   
		   }
		   if(dest != NULL){
			   free(dest);
		   }
	   }
	   if(need_copy){
		   memcpy(dest, rdest, (*dest_len));
		   free(rdest);
	   }else{
		   dest = rdest;
	   }


	}else if(algorithm == COMPRESS_PANS_ALGORITHM){
           
	   uint8_t * tmp_src = (uint8_t *)src;
	   uint32_t tmp_src_len = src_len;
	   uint32_t tmp_dest_len = 0;

	   rdest = (char* ) pans_decompress_data( tmp_src, tmp_src_len , &tmp_dest_len);
	   if(tmp_dest_len <= 0){
		   SKYFS_ERROR_1("decompress failed at altorithm %d\n", algorithm);
		   if(rdest != NULL){
			   free(rdest);
			   
		   }
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
		   return NULL;
	   }
	   *dest_len = tmp_dest_len;
	   if(need_copy){
		   memcpy(dest, rdest, (*dest_len));
		   free(rdest);
	   }else{
		   dest = rdest;
	   }


	}else if(algorithm == COMPRESS_MANS_ALGORITHM){
	   uint8_t * tmp_src = (uint8_t *)src;
	   uint32_t tmp_src_len = src_len;
	   uint32_t tmp_dest_len = 0;
	   // 1 decoresas pans
	   rdest = (char* ) pans_decompress_data( tmp_src, tmp_src_len , &tmp_dest_len);
	   if(tmp_dest_len <= 0){
		   SKYFS_ERROR_1("decompress failed at altorithm %d, step1\n", algorithm);
		   if(rdest != NULL){
			   free(rdest);
			   
		   }
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
		   return NULL;
	   }
	   SKYFS_ERROR_1("MANS, pans decompress data len %lu, buf %p\n", tmp_dest_len, rdest) ;
	   tmp_src = (uint8_t *)rdest;
	   tmp_src_len = tmp_dest_len;
	   tmp_dest_len = 0;

	   rdest = (char* ) adm_decompress_data((char*)tmp_src, (size_t)tmp_src_len , (size_t*)&tmp_dest_len);
	   free(tmp_src);

	   if(tmp_dest_len <= 0){
		   SKYFS_ERROR_1("decompress failed at altorithm %d, step 2 \n", algorithm);
		   if(rdest != NULL){
			   free(rdest);
			   
		   }
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
		   return NULL;
	   }
	   *dest_len = tmp_dest_len;
	   if(need_copy){
		   memcpy(dest, rdest, tmp_dest_len);
		   free(rdest);
	   }else{
		   dest = rdest;
	   }


	}else if(algorithm == COMPRESS_SZ3_ALGORITHM){
		   //calculate r1 .. r5
		   size_t r1 = 32;
		   size_t r2 = 32;
		   size_t r3 = 1;
		   size_t r4 = 0;
		   size_t r5 = 0;
		   // TODO should by size of (float)
		   if(dest_buf_len < (r1*r2*r3)*sizeof(int)){
			   SKYFS_ERROR_1("Bug !!, sz3 decompress need at least 4KB ori data %ld \n", dest_buf_len);
			   if(dest != NULL)
				   free(dest);
			   return NULL;
		   }
		   r3 = dest_buf_len/r1/r2/sizeof(int);
		   // now decompress sz3... ,fix data type to FLOAT
		   rdest = SZ_decompress(SZ_FLOAT, (unsigned char *)src, (size_t)src_len,
				  	r5, r4 , r3, r2, r1);
		   if(rdest != NULL){
			   *dest_len = dest_buf_len;
		      if(need_copy){
		   		memcpy(dest, rdest, dest_buf_len);
		   		free(rdest);

		      }else{
			      dest = rdest;
		      }

		   }else{
			   *dest_len = 0;
		   }



	   }else{
		SKYFS_ERROR_1("failed to support decompress algothrim %d  \n", algorithm);
		free(dest);
		return NULL;

	}
	
	



	

	return dest;
}


char *  compress_nonstd_output(char * src, size_t src_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	
	int ret = 0;
	
	if(algorithm <= COMPRESS_ZLIB_ALGORITHM ){ // get dest buf directly from ADM and later
		dest = malloc(src_len * 2);
		if(dest == NULL){
                SKYFS_ERROR_1("failed to get compressing for memory size %d \n", src_len);
                return NULL;
        	}
	}

	if(algorithm == COMPRESS_NONE_ALGORITHM){
		*dest_len  =  src_len;
                memcpy(dest, src, src_len);
		return dest;

	}else if(algorithm == COMPRESS_ZLIB_ALGORITHM){

    		z_stream stream;

    		stream.zalloc = Z_NULL;
    		stream.zfree = Z_NULL;
    		stream.opaque = Z_NULL;
    		stream.avail_in = (uInt)src_len; // 设置输入数据的长度
    		stream.next_in = (Bytef *)src;   // 设置输入数据的指针
    		//if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
    		if (deflateInit(&stream, 1) != Z_OK) {
        		fprintf(stderr, "deflateInit error\n");
			free(dest);
        		return dest;
		}
		stream.avail_out = src_len *2 ; // 设置输出缓冲区的长度
    		stream.next_out = dest;            // 设置输出数据的指针

    		// 执行压缩操作

    		ret = deflate(&stream, Z_FINISH);
		if((ret != Z_OK) && (ret != Z_STREAM_END)){
			fprintf(stderr, "deflate return failed %d\n", ret);
    			// 清理资源
    			deflateEnd(&stream);
			free(dest);
			return NULL;

		}
    		*dest_len = stream.total_out; // 设置实际的压缩数据长度

    		// 清理资源
    		deflateEnd(&stream);

    	}else if(algorithm == COMPRESS_ADM_ALGORITHM){

	   dest =  adm_compress_data( src, src_len ,  dest_len);
	   if(*dest_len <= 0){
		   SKYFS_ERROR_1("compress failed at altorithm %d\n", algorithm);
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
	   }

	 }else if(algorithm == COMPRESS_SZ3_ALGORITHM){

		 size_t r5 = 0;
		 size_t r4 = 0;
		 size_t r1 = 32;
		 size_t r2 = 32;
		 size_t r3 = 1;
		 


		 if(src_len <(r1*r2)*sizeof(int) ){
			 dest = malloc(4*1024);
			 // return a big buf, and caller will use original buffer;
			 *dest_len = 4*1024;
			 return dest;
		 }
		 r3 = src_len /r1/r2/sizeof(int);

	   	//dest =  adm_compress_data( src, src_len ,  dest_len);
		dest = SZ_compress_args(SZ_FLOAT, (void*)src, (size_t*)dest_len,REL,
				(double)0.0, (double)0.001, (double)0.0, 
				r5, r4, r3, r2, r1 );
	   	if(*dest_len <= 0){
		   SKYFS_ERROR_1("compress failed at altorithm %d\n", algorithm);
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
	    }


	}else if(algorithm == COMPRESS_PANS_ALGORITHM){
	   
	   uint8_t * tmp_src = (uint8_t *)src;
	   uint32_t tmp_len  = 0;
	   uint32_t tmp_src_len  = src_len;
	   dest =(char *) pans_compress_data( tmp_src, tmp_src_len ,  &tmp_len);
	   if(tmp_len <= 0){
		   SKYFS_ERROR_1("compress failed at altorithm %d\n", algorithm);
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
		   }
	   }
	   *dest_len = (size_t)tmp_len;
	   

	   


	}else if(algorithm == COMPRESS_MANS_ALGORITHM){
	   uint32_t  tmp_len = 0;
	   uint8_t * tmp_src = NULL;
	   uint32_t tmp_src_len  = 0;
	   uint8_t * tmp_dest = NULL;
	   uint32_t real_dest_len = 0;

	   dest = adm_compress_data( src, src_len ,  dest_len);
	   if(*dest_len <= 0){
		   SKYFS_ERROR_1("compress failed at altorithm %d at step 1\n", algorithm);
		   if(dest != NULL){
			   free(dest);
			   dest = NULL;
			   return dest;
		   }
	   }
	   tmp_src = (uint8_t *)dest;
	   tmp_src_len = (uint32_t )(*dest_len);
	   tmp_dest = pans_compress_data(tmp_src, tmp_src_len , &real_dest_len);
	   free(dest);
	   *dest_len = real_dest_len;
	   if(real_dest_len <= 0){
		SKYFS_ERROR_1("compress failed at altorithm %d at step 2\n", algorithm);
		   if(tmp_dest != NULL){
			   free(tmp_dest);
			   dest = NULL;
			   return dest;
		   }

	   }
	   dest = (char*)tmp_dest;
	   *dest_len = (size_t) real_dest_len; 

	   


	}else{
		SKYFS_ERROR_1("failed to support compress algothrim %d  \n", algorithm);
		free(dest);
		return NULL;

	}


		

	return dest;
}



char *  decompress_output(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf)
{

	char * dest = pre_dest_buf;
	size_t ori_len = 0;


	if(algorithm != COMPRESS_ZSTD_ALGORITHM){
		//SKYFS_ERROR("\n");
		return decompress_nonstd_output(src, src_len, dest_buf_len, dest_len, algorithm, pre_dest_buf);
	}

	if(dest == NULL){
		dest = malloc(dest_buf_len);
	}
	if(dest == NULL){
                SKYFS_ERROR_1("failed to get decompressing for memory size %d \n", src_len);
                return NULL;
        }

	
	ori_len =  ZSTD_decompress(dest, dest_buf_len, src, src_len);
	if(ZSTD_isError(ori_len)){
                SKYFS_ERROR_1("Compression error: %s, algorithm %d \n", ZSTD_getErrorName(ori_len), algorithm);
                free(dest);
                return NULL;
        }

	*dest_len = ori_len;
        SKYFS_ERROR("decompress src data len %lu, dest data len %lu, buf %p \n", src_len, ori_len, dest);

	return dest;
}

char *  compress_output(char * src, size_t src_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	size_t compress_len = 0;

	
	if(algorithm != COMPRESS_ZSTD_ALGORITHM){
		//SKYFS_ERROR("\n");
		return compress_nonstd_output(src, src_len, dest_len, algorithm);
	}
	//size_t compress_bound = ZSTD_compressBound((size_t) src_len);
	size_t compress_bound = src_len *2;
	dest = (char *)malloc(compress_bound);
	if(dest == NULL){
		SKYFS_ERROR_1("failed to get compressing for memory size %d \n", src_len);
		return NULL;
	}
	compress_len = ZSTD_compress(dest, compress_bound, src, src_len, 2);
	if(ZSTD_isError(compress_len)){
        	SKYFS_ERROR_1("Compression error: %s\n", ZSTD_getErrorName(compress_len));
		free(dest);
		return NULL;
	}
	*dest_len = compress_len;
	SKYFS_ERROR("compress src data len %lu, dest data len %lu\n", src_len, compress_len);
	return dest;



}

#if 0

void call_gpu_zstd_compress(char* data, size_t  size,
                        size_t warmup_iteration_count, size_t total_iteration_count,
                        size_t * comp_bufs_count, size_t * comp_buf_sizes, char ** comp_bufs,
                        void ** pbatch);

#endif 

skyfs_s32_t
__skyfs_C2O_gpu_compress_bufs(int compress_type, skyfs_C_gcompbuf_t * gpubuf,  char *buf , size_t size,
		size_t * comp_bufs_count,  size_t * * comp_bufs_sizes, char * * compress_bufs)
{
	int rc = 0;
	void * pbatch_obj = NULL;
	struct timeval tv1, tv2;
	uint64_t durtime;
	void * old_d_temp = gpubuf->d_temp;
	size_t old_d_size = gpubuf->d_size;
	int dev_idx = gpubuf->dev_id;
	pthread_mutex_t * dev_mutex = get_dev_mutex(dev_idx);  

	if (compress_type == 0x100){ // GPU_zstd_COMPRESS

		// step 1 : select cuda dev


		SKYFS_ERROR("call gpu compress, dev_id %d, uncomp_buf %p \n  ", gpubuf->dev_id, gpubuf->uncomp_dev_buf);
		gettimeofday(&tv1, NULL);
 		select_cuda_device(gpubuf->dev_id);
		// step 2 : set API call_gpu_zstd_compress :13 parameters
		call_gpu_zstd_compress(buf, gpubuf->comp_buf, size, SKYFS_OBJECT_NODE_SIZE,
				&gpubuf->d_temp, &gpubuf->d_size,
				gpubuf->dev_streams,
				&gpubuf->uncomp_dev_buf, &gpubuf->uncomp_dev_ptrs, &gpubuf->uncomp_dev_sizes,
				&gpubuf->comp_dev_buf, &gpubuf->comp_dev_ptrs, &gpubuf->comp_dev_sizes, dev_mutex
				);

		gettimeofday(&tv2, NULL);
		durtime = tv2.tv_sec * 1000000+ tv2.tv_usec;
		durtime -=(tv1.tv_sec * 1000000 +tv1.tv_usec);
		write_obj_compress_time += durtime;
		


#if 0
		call_gpu_zstd_compress(buf, size, // uncompressed buf and buf size
                        0,1, // no warmup , one compress iteration
                        comp_bufs_count, // output compressed buf count  
			comp_buf_sizes, // output compressed buf sizes
			compress_bufs, //  output compressed bufs
                        &pbatch_obj);
	
		
	void call_gpu_zstd_compress(char* data, char * ret_data, size_t size, size_t blk_size, 
		void ** d_temp,size_t * d_temp_size,
                void * * streams,
                void * *  input_uncomp_data, void **  input_uncomp_ptrs , void ** input_uncomp_sizes,
                void * *  input_comp_data, void * * input_comp_ptrs , void ** input_comp_sizes
	);


#endif
		if((pbatch_obj != NULL) ||(rc <0)){
			SKYFS_ERROR_1("GPU compress zstd failded\n, rc %d , batch_obj %p ",rc, pbatch_obj);
			rc = -EIO;
		}else{
			/*calculate build comp_buf_sizes and comp_buf_ptrs */
			size_t pos = 0;
			uint64_t blks_count;
			uint64_t pre_time, do_time, post_time; 
			uint64_t * time_pos;
			char * data_sizes ;
			char * data_ptrs = gpubuf->comp_buf;
			size_t blk_size = SKYFS_OBJECT_NODE_SIZE;
			data_ptrs +=(size*10/9);
			pos = (size_t )data_ptrs;
			pos = ((pos+(1<<12)-1) >>12)<<12;
			data_ptrs = (char *)pos;
			//data_sizes = (char *)pos;
			pos += (sizeof(char*)*(size/blk_size+1));																                     pos = ((pos+(1<<12)-1) >>12)<<12; // 4KB align
			data_sizes = (char *)pos;
			*compress_bufs = data_ptrs;
			*comp_bufs_count = (size+blk_size-1)/blk_size;
			
			blks_count = *comp_bufs_count;

			*comp_bufs_sizes = data_sizes;
			time_pos = (uint64_t*)data_sizes;
			//char ** real_data_ptrs = (char * * )data_ptrs;
			pre_time = time_pos[blks_count+2];
			do_time = time_pos[blks_count+3];
			post_time = time_pos[blks_count+4];

			write_obj_pre_compress_time +=pre_time;
			write_obj_do_compress_time += do_time;
			write_obj_post_compress_time += post_time;

			if(old_d_temp == NULL)
				SKYFS_ERROR_1("Gpu_nvcomp  ,pre_time %lu, d_tomp %p, d_size %lu, old_d_temp %p , old_d_size %lu\n ",
					pre_time, gpubuf->d_temp, gpubuf->d_size, old_d_temp, old_d_size);

			//SKYFS_ERROR("gpu nvcomp compress data_ptrs %p , [0] %p \n ", data_ptrs, real_data_ptrs[0]);
		        	

		}	
	
	}
	// TODO do compress
	if(rc >= 0){
		return *comp_bufs_count;

	}else{
		return rc;
	}

}

// multiple submit gpu compressed data

skyfs_s32_t 
__skyfs_C2O_submit_multiple_gpu_compbuf(skyfs_ino_t ino,
		const skyfs_u8_t * * buf_ptr,
                skyfs_u64_t *  offset_ptr,
                skyfs_u32_t * size_ptr,
		size_t * gpu_comp_size_ptr,
		int buf_cnt,
		int comp_type,
		uint64_t diff_offset,
		skyfs_s64_t * changed_space
		)
{
	int rc = 0;
        int zstd_comp = 0;
	struct timeval tv1, tv2;
	uint64_t this_net_time;
	
    	size_t compressed_size, size, new_changed_space;
	skyfs_u32_t osd_id = 0;
    	skyfs_u32_t osd_gid = 0;
    	replica_cnt = get_replica();
	amp_request_t *req = NULL;
    	skyfs_msg_t *msgp = NULL;
    	amp_kiov_t *kiovp = NULL;
	//skyfs_s8_t *write_buf = NULL;
    	skyfs_u32_t req_size;
        skyfs_u64_t obj_id;
	int algorithm = comp_type;
	//char * compress_buf = NULL;

	if(isComp_zstd() && comp_type){
	      zstd_comp = 1;
        }
	gettimeofday(&tv1, NULL);
	if(diff_offset == 0)
	SKYFS_ERROR("enter func submit_multiple_gpu_comp_buf,ino %llu , offset %llu, count %d, buf_ptr %p , size_ptr %p, diff_offset %llx, size[0] %d\n ",
			ino, *offset_ptr, buf_cnt, buf_ptr, size_ptr, diff_offset, size_ptr[0]);
	// changed by mayl, make sure all offsets within the range of a single object 
	obj_id = offset_ptr[0] /  SKYFS_OBJECT_SIZE;


	osd_gid =  find_osdgid(osd_num, ino, obj_id);
	osd_id = find_replica_osd(osd_num, osd_gid, 1, replica_cnt);
	rc = __amp_alloc_request(&req);
    	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_submit-write:alloc request failed\n");
        	goto err_none;
    	}

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_multi_writeobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR_1("__skyfs_C2O_write:alloc req_msg failed\n");
        	goto err_req;
    	}

    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_MULTI_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
    	msgp->u.Multi_writeObjReq.multi_vec.ino = ino;
    	msgp->u.Multi_writeObjReq.multi_vec.obj_id = obj_id;
    	msgp->u.Multi_writeObjReq.multi_vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.Multi_writeObjReq.multi_vec.obj_size = SKYFS_OBJECT_SIZE;

	// set offset and count vector
	uint64_t start_offset = 0;
	for(int n = 0 ; n < buf_cnt ; n ++){
		start_offset+=SKYFS_OBJECT_NODE_SIZE;
    		msgp->u.Multi_writeObjReq.multi_vec.offset[n] = (offset_ptr[0]+start_offset) % SKYFS_OBJECT_SIZE;
    		msgp->u.Multi_writeObjReq.multi_vec.count[n] = size_ptr[0];
	}
    	
	msgp->u.Multi_writeObjReq.multi_vec.replica_num = replica_cnt;
	msgp->u.Multi_writeObjReq.multi_vec.vector_cnt  = buf_cnt;
    	msgp->u.Multi_writeObjReq.multi_vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.Multi_writeObjReq.multi_vec.page_idx = 0;

	//compress_buf = compress_func(buf, size, &compressed_size,comp_type);
	// changed for gpu , which have already compressed.
#if 0
	compress_buf = buf;
	compressed_size = gpu_comp_size;
	SKYFS_ERROR("C2O submit cache data, size %llu, comp size %llu , comp_type %d\n ",
			size, compressed_size, comp_type);

	if(compress_buf == NULL){
		SKYFS_ERROR_1("GPU compress buffer is %p, %p !\n", compress_buf, buf);
		goto err_msg;
	}

	if(compressed_size > size){
			SKYFS_ERROR_1("compressed data is bigger, use original data\n");
			//memcpy(compress_buf, write_buf, op_size);
			algorithm = 0;
			compressed_size = size;
			free(compress_buf);
			compress_buf = NULL;

	}
#endif
	// build kiov and do amp send
    	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, req_size);

	msgp->u.Multi_writeObjReq.multi_vec.algorithm = algorithm | 0x80000000;
#if 0
	if(compress_buf){
		write_buf = compress_buf;
	}else{
		write_buf = buf;
	}
#endif
	compressed_size = 0;
	size = 0;
	start_offset = 0;
	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t) * buf_cnt);
	for(int n = 0 ; n<buf_cnt ; n++){
		start_offset += n*SKYFS_OBJECT_NODE_SIZE;
		kiovp[n].ak_addr = buf_ptr[n]+diff_offset;
		kiovp[n].ak_len = gpu_comp_size_ptr[n];

		kiovp[n].ak_offset = 0;
		kiovp[n].ak_flag = 0;

    		msgp->u.Multi_writeObjReq.multi_vec.foffset[n] = (offset_ptr[0]+start_offset) % SKYFS_OBJECT_SIZE;
    		msgp->u.Multi_writeObjReq.multi_vec.fcount[n] = gpu_comp_size_ptr[n];
		compressed_size += gpu_comp_size_ptr[n];
		size += size_ptr[0];

		if(diff_offset == 0)
			SKYFS_ERROR("buf_ptr[%d] is  %p , ak_addr is %p, diff_offset %llx , osd_id %d, comp_size %lu\n ", 
					n, buf_ptr[n], kiovp[n].ak_addr, diff_offset, osd_id,gpu_comp_size_ptr[n]);
	}
	//SKYFS_ERROR_1("Total size %d \n", size);
    	//kiovp->ak_addr = write_buf;
    	//kiovp->ak_len = compressed_size;

	//msgp->u.writeObjReq.vec.foffset = offset % SKYFS_OBJECT_SIZE;
	//msgp->u.writeObjReq.vec.fcount = compressed_size;
	//if(isComp_zstd() && comp_type){
	//kiovp->ak_len = msgp->u.writeObjReq.vec.fcount;
	//}

    	//kiovp->ak_offset = 0;
    	//kiovp->ak_flag = 0;
    	req->req_iov = kiovp;
    	req->req_niov = buf_cnt;
    	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR_1("client submit write buf net rdma FAILed %d\n", rc);
		goto err_msg;
	}
	msgp = __skyfs_get_msg(req->req_reply);

    	//SKYFS_ERROR("__skyfs_C2O_write:rc:%d,fromid:%d,fromType:%d, ori offset:%llu,size:%u, changed_space %ld\n, algo %d, compress_type %d\n", 
       	//	msgp->error, msgp->fromid, msgp->fromType,offset,size, msgp->u.writeObjAck.space_changed, algorithm, compress_type) ;

	// changed by mayl for async write
	
    	rc = msgp->error;
	*changed_space = msgp->u.Multi_writeObjAck.space_changed;
	if(rc <= 0 ){
		SKYFS_ERROR_1("client Submit compressed write buf osd return FAILED , return size %d, changed space %lu\n", rc, *changed_space);
	}	
	if(algorithm != 0){
		if(rc == compressed_size){
			rc = size;
		}
		else{
			SKYFS_ERROR_1("submit to OSD %d failed,  gpu comp size %ld, return rc %ld\n", osd_id, compressed_size, rc);
			rc = -EIO;
		}
	}else{
		if(rc != size)
			rc = -EIO;
	}

	if(kiovp){
            free(kiovp);
	    kiovp = NULL;
#if 0
	    if(compress_buf){
		free(compress_buf);
	     }
#endif
	    	
	}

    	if(req->req_reply){
        	free(req->req_reply);
		req->req_reply = NULL;
    	}

		SKYFS_MSG("__skyfs_C2O_write:free req_msg\n");
    	if(req->req_msg){
        	free(req->req_msg);
		req->req_msg = NULL;
    	}
    	
		SKYFS_MSG("__skyfs_C2O_write:free req\n");
    	if(req){
       		__amp_free_request(req);
		req = NULL;
    	}




err_msg:
	if(rc != size ){
		SKYFS_ERROR("submit gpu compressed buf %p FAILEed, rc %d, gpu comp size %lu\n",
				buf, rc, gpu_comp_size);
	}
	SKYFS_MSG("__skyfs_C2O_write:final free req_msg\n");
    if(req){
		if(req->req_msg){
        		free(req->req_msg);
		}
    }
err_req:
	SKYFS_MSG("__skyfs_C2O_write:final free req\n");
    if(req){
        __amp_free_request(req);
    }
err_none:
	if(rc != size ){
		SKYFS_ERROR_1("submit gpu compressed buf_ptr %p err FAILEed, rc %d,  size %u\n",
				buf_ptr, rc, size);
	}



	SKYFS_ERROR("submit return rc %d\n", rc);
       
	gettimeofday(&tv2, NULL);
	this_net_time = (tv2.tv_sec * 1000000 + tv2.tv_usec);
	this_net_time -= (tv1.tv_sec * 1000000 + tv1.tv_usec);
	total_gpu_net_time += this_net_time;
	return rc;
}


skyfs_s32_t 
__skyfs_C2O_submit_gpu_prefetch(skyfs_ino_t ino,
		const skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
		size_t *gpu_comp_size,
		int comp_type,
                skyfs_u32_t * preal_comp_type,
		skyfs_s32_t * preal_fsize, 
		size_t  * preal_foff
		)
{
	int rc = 0;
        int zstd_comp = 0;
        int direct_io = 1;
    	size_t compressed_size, new_changed_space;
	skyfs_u32_t osd_id = 0;
    	skyfs_u32_t osd_gid = 0;
    	replica_cnt = get_replica();
	amp_request_t *req = NULL;
    	skyfs_msg_t *msgp = NULL;
    	amp_kiov_t *kiovp = NULL;
	skyfs_s8_t *write_buf = NULL;
    	skyfs_u32_t req_size;
        skyfs_u64_t obj_id;
	int algorithm = comp_type;
	char * compress_buf = NULL;

	uint64_t gpu_net_time = 0;
	struct timeval tv1, tv2;

	gettimeofday(&tv1,NULL);
	if(isComp_zstd() && comp_type){
	      zstd_comp = 1;
        }
	obj_id = offset /  SKYFS_OBJECT_SIZE;


	osd_gid = find_osdgid(osd_num, ino, obj_id);
	osd_id = find_replica_osd(osd_num, osd_gid, 1, replica_cnt);
        // readobj osdid have certained.
	rc = __amp_alloc_request(&req);
    	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_submit-write:alloc request failed\n");
        	goto err_none;
    	}

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR_1("__skyfs_C2O_write:alloc req_msg failed\n");
        	goto err_none;
    	}

#if 0
    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_READ_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
    	msgp->u.writeObjReq.vec.ino = ino;
    	msgp->u.writeObjReq.vec.obj_id = obj_id;
    	msgp->u.writeObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.writeObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.offset = offset % SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.count = size;
    	msgp->u.writeObjReq.vec.replica_num = replica_cnt;
    	msgp->u.writeObjReq.vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.writeObjReq.vec.page_idx = 0;
#endif

#if 1
	// for read
	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_READ_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
	bzero(&(msgp->u.readObjReq.vec), sizeof(skyfs_io_vector_t));
    	msgp->u.readObjReq.vec.ino = ino;
    	msgp->u.readObjReq.vec.obj_id = obj_id;
    	msgp->u.readObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.readObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.readObjReq.vec.offset = offset;
    	msgp->u.readObjReq.vec.count = size;
    	msgp->u.readObjReq.vec.forward_count = 0;
	// add by mayl for non compressing  mode
    	msgp->u.readObjReq.vec.algorithm = comp_type & 0x0ffff;
	msgp->u.readObjReq.vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.readObjReq.vec.replica_num = replica_cnt;
	if(direct_io){
		/* added by mayl, direct_op == 1 means O2O forward, 2 means direct_read*/
		msgp->u.readObjReq.vec.direct_op = 2;
	}

#endif      
		// build kiov and do amp send
    	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST, req_size);
	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	

    	if(rc < 0){
        	SKYFS_ERROR("__skyfs_C2O_read:send request failed.rc:%d\n", rc);
        	goto err_msg;
    	}else{
		// get msg
		msgp = __skyfs_get_msg(req->req_reply);

    		SKYFS_ERROR("__skyfs_C2O_read:rc:%d,fromid:%d,fromType:%d,type:%d\n", 
        		msgp->error, msgp->fromid, msgp->fromType, req->req_type);

    		rc = msgp->error;
		if(rc <= 0){
			SKYFS_ERROR_1("C2O read return faild,  ret %d  ino %llu , offset %lu, need count %lu, obj %llu, osdid %d \n", 
				ino, rc, offset, size, obj_id, osd_id);
			*preal_fsize = rc;
		}else{

                // get rc
                // get kiov
			kiovp = req->req_iov;
        		char * blk_buf = kiovp->ak_addr;
                // copy buffer
			memset(buf,blk_buf, rc); // 复制压缩数据，
                // record size
			*preal_fsize = kiovp->ak_flen;
			*preal_foff = kiovp->ak_foffset;
			*gpu_comp_size = rc;
			
			if(kiovp->ak_addr){
                        	free(kiovp->ak_addr);
                        	kiovp->ak_addr = NULL;
                	}


                	if(kiovp){
                        	free(kiovp);
                        	kiovp = NULL;
                	}

			
			
			
		}
		// free kiov and return ;
		
	}
	



err_msg:
    	if(req->req_reply){
        	free(req->req_reply);
		req->req_reply = NULL;
    	}

		SKYFS_MSG("__skyfs_C2O_write:free req_msg\n");
    	if(req->req_msg){
        	free(req->req_msg);
		req->req_msg = NULL;
    	}

err_req:    	
		SKYFS_MSG("__skyfs_C2O_write:free req\n");
    	if(req){
       		__amp_free_request(req);
		req = NULL;
    	}



err_none:       
	return rc;
}




skyfs_s32_t 
__skyfs_C2O_submit_gpu_compbuf(skyfs_ino_t ino,
		const skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
		size_t gpu_comp_size,
		int comp_type,
		skyfs_s64_t * changed_space
		)
{
	int rc = 0;
        int zstd_comp = 0;
    	size_t compressed_size, new_changed_space;
	skyfs_u32_t osd_id = 0;
    	skyfs_u32_t osd_gid = 0;
    	replica_cnt = get_replica();
	amp_request_t *req = NULL;
    	skyfs_msg_t *msgp = NULL;
    	amp_kiov_t *kiovp = NULL;
	skyfs_s8_t *write_buf = NULL;
    	skyfs_u32_t req_size;
        skyfs_u64_t obj_id;
	int algorithm = comp_type;
	char * compress_buf = NULL;

	uint64_t gpu_net_time = 0;
	struct timeval tv1, tv2;

	gettimeofday(&tv1,NULL);
	if(isComp_zstd() && comp_type){
	      zstd_comp = 1;
        }
	obj_id = offset /  SKYFS_OBJECT_SIZE;


	osd_gid =  find_osdgid(osd_num, ino, obj_id);
	osd_id = find_replica_osd(osd_num, osd_gid, 1, replica_cnt);
	rc = __amp_alloc_request(&req);
    	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_submit-write:alloc request failed\n");
        	goto err_none;
    	}

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR_1("__skyfs_C2O_write:alloc req_msg failed\n");
        	goto err_req;
    	}

    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
    	msgp->u.writeObjReq.vec.ino = ino;
    	msgp->u.writeObjReq.vec.obj_id = obj_id;
    	msgp->u.writeObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.writeObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.offset = offset % SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.count = size;
    	msgp->u.writeObjReq.vec.replica_num = replica_cnt;
    	msgp->u.writeObjReq.vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.writeObjReq.vec.page_idx = 0;

	//compress_buf = compress_func(buf, size, &compressed_size,comp_type);
	// changed for gpu , which have already compressed.
	compress_buf = buf;
	compressed_size = gpu_comp_size;
	SKYFS_ERROR("C2O submit cache data, size %llu, comp size %llu , comp_type %d\n ",
			size, compressed_size, comp_type);

	if(compress_buf == NULL){
		SKYFS_ERROR_1("GPU compress buffer is %p, %p !\n", compress_buf, buf);
		goto err_msg;
	}
#if 0
	if(compressed_size > size){
			SKYFS_ERROR_1("compressed data is bigger, use original data\n");
			//memcpy(compress_buf, write_buf, op_size);
			algorithm = 0;
			compressed_size = size;
			free(compress_buf);
			compress_buf = NULL;

	}
#endif
	// build kiov and do amp send
    	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, req_size);

	msgp->u.writeObjReq.vec.algorithm = algorithm | 0x80000000;
	
	if(compress_buf){
		write_buf = compress_buf;
	}else{
		write_buf = buf;
	}
	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
    	kiovp->ak_addr = write_buf;
    	kiovp->ak_len = compressed_size;

	msgp->u.writeObjReq.vec.foffset = offset % SKYFS_OBJECT_SIZE;
	msgp->u.writeObjReq.vec.fcount = compressed_size;
	//if(isComp_zstd() && comp_type){
	kiovp->ak_len = msgp->u.writeObjReq.vec.fcount;
	//}

    	kiovp->ak_offset = 0;
    	kiovp->ak_flag = 0;
    	req->req_iov = kiovp;
    	req->req_niov = 1;
    	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR_1("client submit write buf net rdma FAILed %d\n", rc);
		goto err_msg;
	}
	msgp = __skyfs_get_msg(req->req_reply);

    	//SKYFS_ERROR("__skyfs_C2O_write:rc:%d,fromid:%d,fromType:%d, ori offset:%llu,size:%u, changed_space %ld\n, algo %d, compress_type %d\n", 
       	//	msgp->error, msgp->fromid, msgp->fromType,offset,size, msgp->u.writeObjAck.space_changed, algorithm, compress_type) ;

	// changed by mayl for async write
	
    	rc = msgp->error;
	if(rc <= 0){
		SKYFS_ERROR_1("client submit compressed write buf osd return FAILED , return size%d\n", rc);
	}	
	*changed_space = msgp->u.writeObjAck.space_changed;
	if(algorithm != 0){
		if(rc == compressed_size){
			rc = size;
		}
		else{
			SKYFS_ERROR_1("submit to OSD %d failed,  gpu comp size %ld, return rc %ld\n", osd_id, compressed_size, rc);
			rc = -EIO;
		}
	}else{
		if(rc != size)
			rc = -EIO;
	}

	if(kiovp){
            free(kiovp);
	    kiovp = NULL;
#if 0
	    if(compress_buf){
		free(compress_buf);
	     }
#endif
	    	
	}

    	if(req->req_reply){
        	free(req->req_reply);
		req->req_reply = NULL;
    	}

		SKYFS_MSG("__skyfs_C2O_write:free req_msg\n");
    	if(req->req_msg){
        	free(req->req_msg);
		req->req_msg = NULL;
    	}
    	
		SKYFS_MSG("__skyfs_C2O_write:free req\n");
    	if(req){
       		__amp_free_request(req);
		req = NULL;
    	}




err_msg:
	if(rc != gpu_comp_size ){
		SKYFS_ERROR("submit gpu compressed buf %p FAILEed, rc %d, gpu comp size %lu\n",
				buf, rc, gpu_comp_size);
	}
	SKYFS_MSG("__skyfs_C2O_write:final free req_msg\n");
    if(req){
		if(req->req_msg){
        		free(req->req_msg);
		}
    }
err_req:
	SKYFS_MSG("__skyfs_C2O_write:final free req\n");
    if(req){
        __amp_free_request(req);
    }
err_none:
	if(rc != size ){
		SKYFS_ERROR_1("submit gpu compressed buf %p err FAILEed, rc %d,  size %lu\n",
				buf, rc, size);
	}

	gettimeofday(&tv2,NULL);
	gpu_net_time = tv2.tv_sec*1000000 + tv2.tv_usec;
	gpu_net_time -=(tv1.tv_sec*1000000+tv1.tv_usec);
	total_gpu_net_time += gpu_net_time;

       
	return rc;
}


skyfs_s32_t 
__skyfs_C2O_submit_compbuf(skyfs_ino_t ino,
		const skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
		int comp_type,
		skyfs_s64_t * changed_space
		)
{
	int rc = 0;
        int zstd_comp = 0;
    	size_t compressed_size, new_changed_space;
	skyfs_u32_t osd_id = 0;
    	skyfs_u32_t osd_gid = 0;
    	replica_cnt = get_replica();
	amp_request_t *req = NULL;
    	skyfs_msg_t *msgp = NULL;
    	amp_kiov_t *kiovp = NULL;
	skyfs_s8_t *write_buf = NULL;
    	skyfs_u32_t req_size;
        skyfs_u64_t obj_id;
	int algorithm = comp_type;
	char * compress_buf = NULL;
	struct timeval tv1, tv2;
	uint64_t dur_time = 0;

	if(isComp_zstd() && comp_type){
	      zstd_comp = 1;
        }
	obj_id = offset /  SKYFS_OBJECT_SIZE;


	osd_gid =  find_osdgid(osd_num, ino, obj_id);
	osd_id = find_replica_osd(osd_num, osd_gid, 1, replica_cnt);
	rc = __amp_alloc_request(&req);
    	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_submit-write:alloc request failed\n");
        	goto err_none;
    	}

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR("__skyfs_C2O_write:alloc req_msg failed\n");
        	goto err_req;
    	}

    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
    	msgp->u.writeObjReq.vec.ino = ino;
    	msgp->u.writeObjReq.vec.obj_id = obj_id;
    	msgp->u.writeObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.writeObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.offset = offset % SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.count = size;
    	msgp->u.writeObjReq.vec.replica_num = replica_cnt;
    	msgp->u.writeObjReq.vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.writeObjReq.vec.page_idx = 0;

	compressed_size = size;
	gettimeofday(&tv1,NULL);
	compress_buf = compress_func(buf, size, &compressed_size,comp_type);
	gettimeofday(&tv2,NULL);
	dur_time = tv2.tv_sec * 1000000 + tv2.tv_usec;
	dur_time -= (tv1.tv_sec * 1000000 + tv1.tv_usec);
	write_obj_compress_time += dur_time;


	SKYFS_ERROR("C2O submit cache data, size %llu, comp size %llu , comp_type %d\n ",
			size, compressed_size, comp_type);

	if(compress_buf == NULL){
		goto err_msg;
	}

	if(compressed_size > size){
			SKYFS_ERROR_1("compressed data is bigger, use original data\n");
			//memcpy(compress_buf, write_buf, op_size);
			algorithm = 0;
			compressed_size = size;
			free(compress_buf);
			compress_buf = NULL;

	}
	// build kiov and do amp send
    	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, req_size);

	msgp->u.writeObjReq.vec.algorithm = algorithm | 0x80000000;
	
	if(compress_buf){
		write_buf = compress_buf;
	}else{
		write_buf = buf;
	}
	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
    	kiovp->ak_addr = write_buf;
    	kiovp->ak_len = compressed_size;

	msgp->u.writeObjReq.vec.foffset = offset % SKYFS_OBJECT_SIZE;
	msgp->u.writeObjReq.vec.fcount = compressed_size;
	//if(isComp_zstd() && comp_type){
	kiovp->ak_len = msgp->u.writeObjReq.vec.fcount;
	//}

    	kiovp->ak_offset = 0;
    	kiovp->ak_flag = 0;
    	req->req_iov = kiovp;
    	req->req_niov = 1;
    	rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
	if(rc < 0){
		SKYFS_ERROR_1("client submit write buf failed %d\n", rc);
		goto err_msg;
	}
	msgp = __skyfs_get_msg(req->req_reply);

    	//SKYFS_ERROR("__skyfs_C2O_write:rc:%d,fromid:%d,fromType:%d, ori offset:%llu,size:%u, changed_space %ld\n, algo %d, compress_type %d\n", 
       	//	msgp->error, msgp->fromid, msgp->fromType,offset,size, msgp->u.writeObjAck.space_changed, algorithm, compress_type) ;

	// changed by mayl for async write
	
    	rc = msgp->error;
	if(rc <= 0){
		SKYFS_ERROR_1("client submit write buf failed , return size%d\n", rc);
	}else{
		
		SKYFS_ERROR_1("client submit write buf  tc %d  size %d\n", rc, size);
	}	
	*changed_space = msgp->u.writeObjAck.space_changed;
	if(algorithm != 0){
		if(rc == compressed_size)
			rc = size;
		else
			rc = -EIO;
	}else{
		if(rc != size)
			rc = -EIO;
	}

	if(kiovp){
            free(kiovp);
	    kiovp = NULL;
	    if(compress_buf){
		free(compress_buf);
	     }
	    	
	}

    	if(req->req_reply){
        	free(req->req_reply);
		req->req_reply = NULL;
    	}

		SKYFS_MSG("__skyfs_C2O_write:free req_msg\n");
    	if(req->req_msg){
        	free(req->req_msg);
		req->req_msg = NULL;
    	}
    	
		SKYFS_MSG("__skyfs_C2O_write:free req\n");
    	if(req){
       		__amp_free_request(req);
		req = NULL;
    	}




err_msg:
	SKYFS_MSG("__skyfs_C2O_write:final free req_msg\n");
    if(req){
		if(req->req_msg){
        		free(req->req_msg);
		}
    }
err_req:
	SKYFS_MSG("__skyfs_C2O_write:final free req\n");
    if(req){
        __amp_free_request(req);
    }
err_none:


       
	return rc;
}

skyfs_s32_t
__skyfs_C2O_write(skyfs_ino_t ino,
                const skyfs_s8_t *buf,
                skyfs_u64_t offset,
                skyfs_u32_t size,
                skyfs_u32_t base_file,
		skyfs_u32_t compress_type,
		skyfs_u32_t * write_to_buf,
		skyfs_s64_t * changed_space)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t total_write = 0;

    struct timeval writeobj_start_tv, writeobj_end_tv;
    struct timeval writecomp_start_tv, writecomp_end_tv;

    skyfs_u32_t subset_id;
    skyfs_u32_t chunk_id;
    char pattern[128];
    size_t compressed_size, new_changed_space;

    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t *kiovp = NULL;
	skyfs_s8_t *write_buf = NULL;
    skyfs_u32_t req_size;
    
    skyfs_u32_t osd_id = 0;

    skyfs_u32_t osd_gid = 0;
    skyfs_u32_t op_size;
    skyfs_u32_t op_off;
    skyfs_u32_t obj_start;
    skyfs_u32_t obj_stop;
    skyfs_u64_t obj_id;
    skyfs_u64_t last_obj_id;
    skyfs_u64_t loop_count = 0;
    skyfs_u32_t buf_off;
    skyfs_s32_t adapt_op;
    skyfs_s32_t algorithm = 0;
    skyfs_s64_t total_changed_space  = 0;
    int do_async_io = 0;

    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;

    skyfs_u32_t skyfs_profile_flag = 1;
    uint32_t print_compress = 0;
    uint64_t write_server_time = 0;
    struct timeval tv_write;

    int zstd_comp = 0;
    write_obj_cnt ++;

    if((write_obj_cnt %2000 == 0) && (write_obj_cnt >2000)){
	    SKYFS_ERROR("SKYFFS write request count %llu, total access time %llu us, compressing time %llu us\n", 
			    write_obj_cnt, write_obj_time, write_obj_compress_time );
    }

      if(isComp_zstd() && compress_type){
	      zstd_comp = 1;
      }

    SKYFS_ERROR("op write offset %llu , size %d \n", offset, size);

    replica_cnt = get_replica();
    __skyfs_get_starttime(&start_time, skyfs_profile_flag);

	obj_start = offset / SKYFS_OBJECT_SIZE;
	obj_stop = (offset + size - 1) / SKYFS_OBJECT_SIZE;
	buf_off = 0;

	op_off=(offset+buf_off) % SKYFS_OBJECT_SIZE;
	SKYFS_ERROR("__skyfs_C2O_write:enter:ino:%llu,offset:%u,count:%d\n \
        obj_start:%d,obj_stop:%d, comp %d, comp_type %d , iscomp %d base %d\n, ",
       	ino, offset, size, obj_start, obj_stop,zstd_comp, compress_type, isComp_zstd(), base_file);

	if(zstd_comp ){
		op_size = SKYFS_OBJECT_NODE_SIZE - (op_off % SKYFS_OBJECT_NODE_SIZE);
	}else{
		op_size = SKYFS_OBJECT_SIZE - (op_off % SKYFS_OBJECT_SIZE);

	}

		op_size = SKYFS_OBJECT_NODE_SIZE - (op_off % SKYFS_OBJECT_NODE_SIZE);
	if (op_size+buf_off > size){
		op_size = size - buf_off;
	}

	obj_id = obj_start;
	last_obj_id = obj_start;

	gettimeofday(&writeobj_start_tv, NULL);
	while(obj_id <= obj_stop) 
	//for(obj_id = obj_start; obj_id <= obj_stop; obj_id++) 
	{
		if(loop_count > 0){
    		//op_off= offset % SKYFS_OBJECT_SIZE;
		    //curr_offset += op_size;
		    buf_off += op_size;
		    if (buf_off >= size)
			    break;
		     op_off= (offset+buf_off) % SKYFS_OBJECT_SIZE;
		     
		     if(zstd_comp){
		     	op_size = SKYFS_OBJECT_NODE_SIZE - ((buf_off+offset) % SKYFS_OBJECT_NODE_SIZE);
		     }else{
			op_size = SKYFS_OBJECT_SIZE - ((buf_off+offset) % SKYFS_OBJECT_SIZE);
		     }
		     	op_size = SKYFS_OBJECT_NODE_SIZE - ((buf_off+offset) % SKYFS_OBJECT_NODE_SIZE);

        	     if (op_size + buf_off  > size){
                	op_size = size - buf_off;
		     }
		     obj_id = (buf_off+offset) / SKYFS_OBJECT_SIZE;
		     
		     if(last_obj_id < obj_id){
			     op_off = 0;
		     }
		     last_obj_id = obj_id;
		     
		     //op_off = curr_offset;
			     

        	}

#if 0
		if(obj_id == obj_start){
    		op_off= offset % SKYFS_OBJECT_SIZE;
			if((obj_id + 1) * SKYFS_OBJECT_SIZE >= (offset +size)){
				op_size = size;
			}else{
                op_size = SKYFS_OBJECT_SIZE - op_off; 
			}
		}else{
			op_off = 0;
            if((obj_id + 1) * SKYFS_OBJECT_SIZE >= (offset +size)){
				op_size = size - buf_off;
			}else{
                op_size = SKYFS_OBJECT_SIZE;
			}
		}
		if(op_size + op_off > SKYFS_OBJECT_NODE_SIZE ){
			op_size = SKYFS_OBJECT_NODE_SIZE - op_off;
		}
#endif

		//adapt_op = __skyfs_C_lookup_dlentry(ino, obj_id, 
		//			&osd_id, &subset_id, &chunk_id);
		//if(adapt_op> 0){
		//	goto begin_process;
		//}
normal_locate:
       	//subset_id = __skyfs_C_get_dl_subsetid(ino, obj_id);
       	//osd_id = __skyfs_C_judge_osdid(subset_id);
	// TODO : changed by mayl
	
	SKYFS_ERROR("C2O_write, obj_id %lu, op_off %lu, op_size %lu \n ", obj_id, op_off, op_size);
	osd_gid =  find_osdgid(osd_num, ino, obj_id);
	osd_id = find_replica_osd(osd_num, osd_gid, 1, replica_cnt);

	
begin_process:	

	algorithm = base_file >> 16;
	int64_t tmp_changed_space;
	if((algorithm != 0) && op_size <  SKYFS_OBJECT_NODE_SIZE
			||algorithm >= 0x100 && op_size < SKYFS_GPU_COMPRESS_BUF_SIZE ){
		// try to write to buf and merge
	   int cache_rc = 0;
	   // TODO : do normal write
	   //cache_rc = -ENOMEM;
	   //goto do_normal_write;
	   if(algorithm < 0x100){
	   	cache_rc = __skyfs_C_place_submit_writebuf(ino, (uint64_t)op_off+ obj_id*SKYFS_OBJECT_SIZE,
                	op_size, (char *)(buf+buf_off), algorithm, &tmp_changed_space);
	   }else{// gpu merge buffer
	   	cache_rc = __skyfs_C_place_submit_gpu_writebuf(ino, (uint64_t)op_off+ obj_id*SKYFS_OBJECT_SIZE,
                	op_size, (char *)(buf+buf_off), algorithm, &tmp_changed_space);

	   }

do_normal_write:
	   if(cache_rc >= 0){


		   total_write += op_size;
		   if(cache_rc >0){
			   total_changed_space += tmp_changed_space;
		   }
		   * write_to_buf = 1;
		   // TODO
		   adapt_op = 0;
		   loop_count++;
		   continue;

	   }else if(cache_rc == -ENOMEM){
		   SKYFS_ERROR("Skyfs cacheed rc error, do normal write, write obj count %lu\n", write_obj_cnt);
		   // do normal_write
		   * write_to_buf = 0;

	   }else if(cache_rc == -EIO){
		   SKYFS_ERROR_1("Skyfs cacheed rc IO FAILED, return EIO, write obj count %lu\n", write_obj_cnt);
		   * write_to_buf = -EIO;
		   rc = -EIO;
		   goto err_msg;
	   
	   }else if(cache_rc <0 ){
		   SKYFS_ERROR_1("Skyfs cacheed rc FAILED %d, return EIO, write obj count %lu\n", cache_rc, write_obj_cnt);
		   * write_to_buf = -EIO;
		   rc = -EIO;
		   goto err_msg;

	   }
	}


		rc = __amp_alloc_request(&req);
    	if(rc < 0){
        	SKYFS_ERROR_1("__skyfs_C2O_write:alloc request failed\n");
        	goto err_none;
    	}

    	rc = -ENOMEM;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_t);    
    	req->req_msg = (amp_message_t *)malloc(req_size);
    	if(!req->req_msg){
        	SKYFS_ERROR("__skyfs_C2O_write:alloc req_msg failed\n");
        	goto err_req;
    	}


	
    	bzero(req->req_msg, req_size);

    	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_WRITE_OBJ,
        	client_this_id, SKYFS_CLIENT, req_size);
    	msgp->u.writeObjReq.vec.ino = ino;
    	msgp->u.writeObjReq.vec.obj_id = obj_id;
    	msgp->u.writeObjReq.vec.partition_id = obj_id/SKYFS_MAX_OBJ_PER_PART;
    	msgp->u.writeObjReq.vec.obj_size = SKYFS_OBJECT_SIZE;
    	msgp->u.writeObjReq.vec.offset = op_off;
    	msgp->u.writeObjReq.vec.count = op_size;
    	msgp->u.writeObjReq.vec.replica_num = replica_cnt;
    	msgp->u.writeObjReq.vec.replica_id = SKYFS_DEFAULT_REPLICA_NUM;
    	msgp->u.writeObjReq.vec.page_idx = 0;

        SKYFS_ERROR("__skyfs_C2O_write: process, ino %llu obj_id %llu, replica_num %lu, replica_id %d, osd_id %d\n",  msgp->u.writeObjReq.vec.ino,
			 msgp->u.writeObjReq.vec.obj_id,  msgp->u.writeObjReq.vec.replica_num,  msgp->u.writeObjReq.vec.replica_id, osd_id);
		
	//if(base_file)
    	//	msgp->u.writeObjReq.vec.page_idx = 0xfbfb;


		if(adapt_op){
			msgp->u.writeObjReq.dest = osd_id;
			msgp->u.writeObjReq.subset = subset_id;
			msgp->u.writeObjReq.chunk = chunk_id;
		}

	// changed by mayl for async write test
	/*
	if(!is_async_write || algorithm != 0){
    		SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, req_size);
	}else{
    		SKYFS_FILL_REQ(req, SKYFS_NEEDNOT_ACK, AMP_REQUEST|AMP_DATA, req_size);
	}*/

    	SKYFS_MSG("__skyfs_C2O_write-ino:%llu,op_off:%u,op_size:%d\n \
        	obj_id:%llu,sid:%d,osd_id:%d\n",
        	ino, op_off, op_size, 
			obj_id, subset_id, osd_id);

	
	write_buf = (skyfs_s8_t *)(buf + buf_off);
	if(isComp_zstd() && compress_type){

		// do_compressing
		compressed_size = 0;
		 char * compress_buf = NULL;
		int real_compress_type = base_file >> 16;
		if(!print_compress){
			//print_compress = 1;
			//SKYFS_ERROR_1("write compress use altorithm %d \n", real_compress_type);
		}
		compressed_size = op_size;
		if(real_compress_type){

			uint64_t comp_used_time = 0;
			gettimeofday(&writecomp_start_tv, NULL);
			compress_buf = compress_func(write_buf, op_size, &compressed_size,real_compress_type); 
			gettimeofday(&writecomp_end_tv, NULL);

			comp_used_time += ((writecomp_end_tv.tv_sec * 1000000)+ writecomp_end_tv.tv_usec);
			comp_used_time -= ((writecomp_start_tv.tv_sec * 1000000)+ writecomp_start_tv.tv_usec);


			write_obj_compress_time += comp_used_time;
			//SKYFS_ERROR_1("++ compess time %llu, total comp time %llu\n", comp_used_time , write_obj_compress_time);
			if(!print_compress){
				SKYFS_ERROR("compress write buf size %d, compress size %d, type %d  \n ", op_size, compressed_size, real_compress_type);
				print_compress =1;
			}

			if(compress_buf == NULL){
			goto err_msg;
			}
		}
		algorithm = base_file >> 16;
		if(compressed_size > op_size){
			SKYFS_ERROR("compressed data is bigger, use original data\n");
			//memcpy(compress_buf, write_buf, op_size);
			algorithm = 0;
			compressed_size = op_size;
			free(compress_buf);

		}

		if(algorithm != 0)
			write_buf = compress_buf;
		SKYFS_ERROR("compress write op_size %lu, comp_size %lu, off %lu\n", op_size, compressed_size, op_off);
		// changed by mayl for
		if(write_obj_cnt <6){
			SKYFS_ERROR_1("write async set %d \n", is_async_write);
		}
		if(!is_async_write || algorithm != 0){
    			SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_DATA, req_size);
		}else{
			do_async_io = 1;
    			SKYFS_FILL_REQ(req, SKYFS_NEEDNOT_ACK, AMP_REQUEST|AMP_DATA, req_size);
		}

		msgp->u.writeObjReq.vec.algorithm = algorithm;
		if((base_file >> 16) != 0){
			// mayl tell osd this file with compressing , although this block maybe not compressed.
			 msgp->u.writeObjReq.vec.algorithm |= 0x80000000;
		}
		msgp->u.writeObjReq.vec.foffset = op_off;
		msgp->u.writeObjReq.vec.fcount = compressed_size;

	}

    	kiovp = (amp_kiov_t *)malloc(sizeof(amp_kiov_t));
    	kiovp->ak_addr = write_buf;
    	kiovp->ak_len = op_size;

	if(isComp_zstd() && compress_type){
		 kiovp->ak_len = msgp->u.writeObjReq.vec.fcount;
	}
    	kiovp->ak_offset = 0;
    	kiovp->ak_flag = 0;
    	req->req_iov = kiovp;
    	req->req_niov = 1;

	
	if(op_size >= 1024){
		uint64_t * pdata = (uint64_t*)write_buf;
		//msgp->u.writeObjReq.check_data1 = pdata[126];
		//msgp->u.writeObjReq.check_data2 = pdata[127];
	}


#if 0
	memset(pattern ,0, 128);
	if(base_file){
		if(!memcmp(pattern, write_buf,128) && size == 32768 ){
			//SKYFS_ERROR_1("SKYFS_WRITE_TO OBJ zero , ino %llu, offset %llu , size %lu, buf %p \n ", ino, offset,op_size, write_buf  );

		}
	}
#endif

    	SKYFS_ERROR("__skyfs_C2O_write: before send. req_size %lu, req head len %lu, osd_id %d, req %p , req_type %x\n", req_size,  
			AMP_SKYFS_MSGHEAD_SIZE, osd_id, req, req->req_type); 
	if(1){
		
		struct timeval tv1;
		gettimeofday(&tv1, NULL);
		uint64_t amp_start_time = tv1.tv_sec * 1000000 + tv1.tv_usec; 
    		rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
		gettimeofday(&tv1, NULL);
		write_server_time = tv1.tv_sec * 1000000 + tv1.tv_usec;
                write_server_time -= amp_start_time;
	}else{
		rc = op_size;
	}
    	if(rc < 0){
       		SKYFS_ERROR("__skyfs_C2O_write:send request failed.rc:%d\n", rc);
        	goto err_msg;
    	}

    	SKYFS_MSG("__skyfs_C2O_write: after send :%d\n", rc); 
	
   		msgp = __skyfs_get_msg(req->req_reply);

    	SKYFS_ERROR("__skyfs_C2O_write:rc:%d,fromid:%d,fromType:%d, ori offset:%llu,size:%u, changed_space %ld\n, algo %d, compress_type %d\n", 
       		msgp->error, msgp->fromid, msgp->fromType,offset,size, msgp->u.writeObjAck.space_changed, algorithm, compress_type) ;

	// changed by mayl for async write
	if(! do_async_io){
    		rc = msgp->error;
	}else{
		rc = op_size;
	}

	if(do_async_io){
		new_changed_space = op_size;
		//msgp->u.writeObjAck.space_changed = op_size;
	}else{
		new_changed_space = msgp->u.writeObjAck.space_changed;
	}
	// changed by mayl for async write end
	if(write_server_time != 0){
		total_write_server_time += write_server_time;
		if(rc >=0){
			//uint64_t storage_time =  msgp->u.writeObjAck.exe_time;
			//total_rdma_net_time += (write_server_time - storage_time);
		}
	}
	if(algorithm == 0 &&  new_changed_space == 0){
		// chagned by mayl for nocomp directory files
		if(rc >=0){
			if(!do_async_io)
			//total_changed_space+=rc;
				total_changed_space += msgp->u.writeObjAck.space_changed;
			else
				total_changed_space += rc;
		}
	}else{
		//total_changed_space += msgp->u.writeObjAck.space_changed;
		total_changed_space += new_changed_space;
	}
	if(rc >= 0){

			 if(isComp_zstd() && compress_type){
				 if(rc == compressed_size)
            				total_write = total_write + op_size;
				 else   total_write += 0;
			 }else{
				 total_write = total_write + rc;
			 }

			if(!do_async_io){
				subset_id = msgp->u.writeObjAck.subset;
				chunk_id = msgp->u.writeObjAck.chunk;
			}
			// added by mayl
			if(adapt_op == 0){
				if(! do_async_io)
					__skyfs_C_add_dlentry(ino, obj_id, msgp->fromid, subset_id, chunk_id);
				else
					__skyfs_C_add_dlentry(ino, obj_id, osd_id, subset_id, chunk_id);
			}
		}else if(rc == -2 && adapt_op == 2){
			adapt_op = 0;
			goto normal_locate;
	}

	if(! do_async_io){
    	if(msgp->fromid != osd_id){
        	SKYFS_ERROR_1("__skyfs_C2O_write:msg forward,newosd:%d,oldosd:%d, ino %llu , obj_id %lu\n",
            	msgp->fromid, osd_id, ino, obj_id);
        	__skyfs_C_clear_dl_depth();
			if(adapt_op == 0){
				__skyfs_C_add_dlentry(ino, obj_id, msgp->fromid, subset_id, chunk_id);
			}else{
				__skyfs_C_release_dlentry(ino, obj_id);
			}
    	}
	}

		SKYFS_MSG("__skyfs_C2O_write:free req_reply\n");

        if(kiovp){
            free(kiovp);
	    kiovp = NULL;
 	    if(isComp_zstd() && write_buf != NULL && algorithm != 0 && compress_type){
			free(write_buf);
			write_buf = NULL;

	    }	
	}

    	if(req->req_reply){
        	free(req->req_reply);
			req->req_reply = NULL;
    	}

		SKYFS_MSG("__skyfs_C2O_write:free req_msg\n");
    	if(req->req_msg){
        	free(req->req_msg);
			req->req_msg = NULL;
    	}
    	
		SKYFS_MSG("__skyfs_C2O_write:free req\n");
    	if(req){
       		__amp_free_request(req);
			req = NULL;
    	}

		adapt_op = 0;

		SKYFS_ERROR("C2O_write, obj_id %lu, op_off %lu, op_size %lu loop %lu finished\n ", obj_id, op_off, op_size, loop_count);
		loop_count ++;

		//buf_off = buf_off + op_size;
	}  // end while, end for

     *changed_space = total_changed_space;

     gettimeofday(&writeobj_end_tv, NULL);
     {
	     uint64_t write_used_time = 0;
			
			write_used_time += ((writeobj_end_tv.tv_sec * 1000000)+ writeobj_end_tv.tv_usec);
			write_used_time -= ((writeobj_start_tv.tv_sec * 1000000)+ writeobj_start_tv.tv_usec);
			write_obj_time += write_used_time;

     }
err_msg:
	SKYFS_MSG("__skyfs_C2O_write:final free req_msg\n");
    if(req){
		if(req->req_msg){
        	free(req->req_msg);
		}
    }
err_req:
	SKYFS_MSG("__skyfs_C2O_write:final free req\n");
    if(req){
        __amp_free_request(req);
    }
err_none:

    __skyfs_get_endtime(&start_time, &end_time, skyfs_profile_flag, 
		"writelookup");

    SKYFS_LEAVE("__skyfs_C2O_write:exit,Total_write:%d,rc:%d\n", 
		total_write, rc);

    if((size+offset) >= (skyfs_u64_t)(1<<24)){
	    SKYFS_ERROR("__skyfs_C2O_write: big_pos_size exit,Total_write:%d,rc:%d, ino %llu offset 0x%llx, size 0x%x, osdid %d\n", 
			    total_write, rc, ino , offset, size, osd_id);
    }
#if 0
    if((write_obj_cnt %1000 == 0) && (write_obj_cnt >1000)){
	    SKYFS_ERROR_1("SKYFFS write request count %llu, total access time %llu us, compressing time %llu us\n", 
			    write_obj_cnt, write_obj_time, write_obj_compress_time );
    }
#endif
	    
    return total_write;
}

skyfs_s32_t
__skyfs_C2O_get_dl_head(skyfs_u32_t osd_id,
                skyfs_u32_t pad_id,
                skyfs_DL_head_t *dl_head)
{
    amp_request_t *req = NULL;
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t size = 0;
    skyfs_s32_t rc = 0;

    SKYFS_ERROR("__skyfs_C2O_get_dl_head:enter.osd_id:%d,dl_head:%p\n",
        osd_id, dl_head);

    rc = __amp_alloc_request(&req);
    if(rc != 0){
        SKYFS_ERROR("__skyfs_C2O_get_dl_head:alloc_request failed\n");
        goto err_none;
    }

    rc = -ENOMEM;

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_getdlhead_args_t);
    req->req_msg = (amp_message_t *)malloc(size);
    if(!req->req_msg){
        SKYFS_ERROR("__skyfs_C2O_get_dir_head:alloc req_msg failed\n");
        rc = -errno;
        goto err_req;
    }
    

    bzero(req->req_msg, size);
    SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_O_GET_DLHEAD,
        client_this_id, SKYFS_CLIENT, size);
    msgp->u.getdlheadReq.pad_id = pad_id;
    
    SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

    SKYFS_MSG("__skyfs_C2O_get_dl_head:before send:req %p\n", req);

    rc = amp_send_sync(client_comp_context, req, SKYFS_OSD, osd_id, 1);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_C2O_get_dl_head:send request failed.rc:%d\n", rc);
        goto err_msg;
    }

    msgp = __skyfs_get_msg(req->req_reply);
    rc = msgp->error;
    if(rc >= 0){
        memcpy(dl_head, msgp->u.mtext, sizeof(skyfs_DL_head_t));
    }

    SKYFS_MSG("__skyfs_C2O_get_dl_head:msgp:%p\n", msgp);

    if(req->req_reply){
        free(req->req_reply);
    }
err_msg:
    if(req->req_msg){
        free(req->req_msg);
    }
err_req:
    if(req){
        __amp_free_request(req);
    }
err_none:

    SKYFS_LEAVE("__skyfs_C2O_get_dl_head:exit\n");

    return rc;
}
/*This is end of client_ito.c*/
