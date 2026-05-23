/* 
 *  Copyright (c) 2013 by XING JING 
 *  All rights reserved.
 *  *  Written by Xing Jing */ /*
 * $Id: osd_op.c $
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


#include "osd_fs.h"
#include "osd_op.h"
#include "osd_thread.h"
#include "osd_init.h"
#include "osd_thread.h"
#include "osd_profile.h"
#include "osd_layout.h"
#include "osd_help.h"

#include "mds_fs.h"

#include "osd_ito.h"
#include "osd_dl.h"
#include "osd_loadb.h"
#include "osd_cache.h"
#include "interval_tree.h"

#include <zstd.h>
#include <zlib.h>

static no_comp_over_write = 0;
static uint64_t tree_lookup_cnt = 0;
static uint64_t tree_lookup_time = 0;

extern char *  compress_func(char * src, size_t  src_len, size_t * dest_len, int algorithm);
extern char *  decompress_func(char * csrc, size_t csrc_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf);
//extern char *  decompress_nonstd_func(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char* pre_dest_buf);

//extern char *  compress_nonstd_func(char * src, size_t src_len, size_t * dest_len, int algorithm);
static  char * skyfs_xattr_names[] = {
	"user.compression_type",
	
	"user.compression_vector",
	
	"user.encription_type",
	
	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	
	"xattr_end"
	
};

static  char * skyfs_xattr_values[] = {
	"ZSTD",
	//"NZSTD",
	
	"0,1024->0,701",
	
	"SHA-256",

	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	"null",
	
        "xattr_end" //end

	
};

#if 0

char *  decompress_output(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm)
{

	char * dest = NULL;
	size_t ori_len = 0;
	dest = malloc(dest_buf_len);
	
	if(algorithm == COMPRESS_NONE_ALGORITHM){
	}
	if(dest == NULL){
                SKYFS_ERROR_1("failed to get decompressing for memory size %d \n", src_len);
                return NULL;
        }
	ori_len =  ZSTD_decompress(dest, dest_buf_len, src, src_len);
	if(ZSTD_isError(ori_len)){
                SKYFS_ERROR_1("Compression error: %s\n", ZSTD_getErrorName(ori_len));
                free(dest);
                return NULL;
        }

	*dest_len = ori_len;
        SKYFS_ERROR_1("decompress src data len %lu, dest data len %lu\n", src_len, ori_len);

	return dest;
}

char *  compress_output(char * src, size_t src_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	size_t compress_len = 0;
	size_t compress_bound = ZSTD_compressBound((size_t) src_len);

	// get enough buffer for compressing
	compress_bound = src_len * 2;
	dest = (char *)malloc(compress_bound);
	if(dest == NULL){
		SKYFS_ERROR_1("failed to get compressing for memory size %d \n", src_len);
		return NULL;
	}
	compress_len = ZSTD_compress(dest, compress_bound, src, src_len, 16);
	if(ZSTD_isError(compress_len)){
        	SKYFS_ERROR_1("Compression error: %s\n", ZSTD_getErrorName(compress_len));
		free(dest);
		return NULL;
	}
	*dest_len = compress_len;
	SKYFS_ERROR_1("compress src data len %lu, dest data len %lu\n", src_len, compress_len);
	return dest;



}

#endif


char *  decompress_nonstd_output(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	dest = malloc(dest_buf_len);
	if(dest == NULL){
                SKYFS_ERROR_1("failed to get decompressing for memory size %d \n", src_len);
                return NULL;
        }

	if(algorithm == COMPRESS_NONE_ALGORITHM){
		*dest_len  =  src_len;
                memcpy(dest, src, src_len);
		return dest;

	}else if(algorithm != COMPRESS_ZLIB_ALGORITHM){

                SKYFS_ERROR_1("failed to support decompress algothrim %d  \n", algorithm);
		free(dest);
                return NULL;

	}else{

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

    	}

	

	return dest;
}


char *  compress_nonstd_output(char * src, size_t src_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	int ret = 0;
	dest = malloc(src_len * 2);
	if(dest == NULL){
                SKYFS_ERROR_1("failed to get compressing for memory size %d \n", src_len);
                return NULL;
        }

	if(algorithm == COMPRESS_NONE_ALGORITHM){
		*dest_len  =  src_len;
                memcpy(dest, src, src_len);
		return dest;

	}else if(algorithm != COMPRESS_ZLIB_ALGORITHM){

                SKYFS_ERROR_1("failed to support compress algothrim %d  \n", algorithm);
		free(dest);
                return NULL;

	}else{

    		z_stream stream;

    		stream.zalloc = Z_NULL;
    		stream.zfree = Z_NULL;
    		stream.opaque = Z_NULL;
    		stream.avail_in = (uInt)src_len; // 设置输入数据的长度
    		stream.next_in = (Bytef *)src;   // 设置输入数据的指针
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

    	}

		

	return dest;
}



char *  decompress_output(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm)
{

	char * dest = NULL;
	size_t ori_len = 0;


	if(algorithm != COMPRESS_ZSTD_ALGORITHM){
		//SKYFS_ERROR("\n");
		return decompress_nonstd_output(src, src_len, dest_buf_len, dest_len, algorithm);
	}

	dest = malloc(dest_buf_len);
	if(dest == NULL){
                SKYFS_ERROR_1("failed to get decompressing for memory size %d \n", src_len);
                return NULL;
        }

	
	ori_len =  ZSTD_decompress(dest, dest_buf_len, src, src_len);
	if(ZSTD_isError(ori_len)){
                SKYFS_ERROR_1("Compression error: %s\n", ZSTD_getErrorName(ori_len));
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




static struct IntervalTree * test_intervalTree = NULL;
static int max_errors = 50;
static int test_write_errors = 0;
static int test_read_errors = 0;
static size_t read_cnt = 0, rwrite_cnt = 0;
static size_t local_read_cnt = 0;
static size_t write_cnt = 0;
static uint64_t read_time = 0;
static uint64_t local_read_time = 0;
static uint64_t read_amp_time = 0;
extern skyfs_u32_t             skyfs_replica;


extern int get_error_test_state();

extern void get_all_replica_partition_state(char * dir_path, uint64_t * p_partitions, int64_t * p_fault_partitions);
extern skyfs_u32_t  skyfs_data_stripe_cnt;
extern skyfs_u32_t  skyfs_recover_data_size;
static int recover_state = 0; //0: normal 1: running 
static int partition_recover_state[8] = {0};
static uint64_t  data_recover_state[8] = {0};


//extern skyfs_htb_t skyfs_special_replica_htbbase[];
extern  skyfs_htb_t * get_special_replica_htbbase();

static virtue_osd_cnt = SKYFS_OSD_CONSIST_HASH_SEGSIZE;

static find_gid_cnt = 0;


#if 0
int get_other_replica_osdid(int local_osd_id, int local_replica, int prefer_replica, int osd_cnt)
{
	int rc = (local_osd_id + prefer_replica - local_replica);
	if (rc <= 0){
		rc += osd_cnt;
	}else if(rc > osd_cnt){
		rc -= osd_cnt;
	}
	return rc;

}
#endif

uint32_t find_osdgid(uint32_t osd_cnt, skyfs_ino_t ino, size_t obj_id)
{
	uint32_t osdgid = 1;
	uint64_t scope;
	uint32_t region_hash = (ino + obj_id/SKYFS_MAX_OBJ_PER_PART)& 0x3fffffff;
	uint64_t cur_pos;

	if(osd_cnt == 1 || region_hash == 0){
		osdgid = 0;
		goto ret_exit;
	}
	//scope = (((uint64_t)(1<<32))+osd_cnt-1)/osd_cnt;  // 10.5,2
	//cur_pos = ((region_hash / scope))*scope ;
	 
	// TODO : so cur_pos beween 0 to 128k -1 distribute in all OSDs
	cur_pos = region_hash/virtue_osd_cnt;
        osdgid = cur_pos;	
#if 0
	while(region_hash >= cur_pos ){
		osdgid ++;
		cur_pos += scope;
	}
#endif

	
ret_exit:
	find_gid_cnt ++;
	if(find_gid_cnt <3){
		SKYFS_ERROR_1("ino %llu, obj_id %lu, region %lu, virtue_osd_cnt %d, gid %d, osd_cnt %d\n",
				ino, obj_id, region_hash, virtue_osd_cnt, osdgid, osd_cnt);
	}

	return osdgid;
	//return (osdgid % osd_cnt)+1;
}

/* TODO : the two function below should  change when we support OSD HA or load balance  */
int find_specical_replica_osd(int osd_gid, int replica_id)
{
      struct list_head * index, * head;
      skyfs_htb_t * htbp,  * base_htbp;
      skyfs_replica_osd_t * tmp;
      skyfs_u32_t osd_id = 0;
       
      uint64_t hashval = ((uint64_t)osd_gid + replica_id) % SKYFS_OSD_CONSIST_HASH_LEN;
      base_htbp =  get_special_replica_htbbase();
      htbp = &(base_htbp[hashval]);
      head = &htbp->head;
      SKYFS_ERROR("\n find_spec hash osd %lu : %p ,%p \n", hashval, base_htbp, htbp  );
      pthread_mutex_lock(&htbp->lock);
      list_for_each(index, head){
      	tmp = list_entry(index, skyfs_replica_osd_t, replica_hash);
	if(tmp->osd_gid == osd_gid && tmp->replica_id == replica_id){
		osd_id = tmp->osd_id;
		break;

	}
      }

      pthread_mutex_unlock(&htbp->lock);

      if(osd_gid != 0)
      	SKYFS_ERROR("find_spec osd_hash   %lu : %p complete osd_id %d, od_gid %u\n", hashval, htbp, osd_id , osd_gid);
      return osd_id;

}



// TODO: how to suppert load balance 
int find_normal_replica_osd(int osd_cnt, int osd_gid, int replica_id)
{
     
     int ret = 0;
     int this_replica_osd = 0;
     size_t virtue_groups_per_osd = (((1ull <<30) / virtue_osd_cnt)+osd_cnt)/osd_cnt;

     // osd for the replica 1;
     this_replica_osd = ((int)osd_gid)/virtue_groups_per_osd;
     this_replica_osd = (this_replica_osd+replica_id-1)%osd_cnt;
     ret = this_replica_osd+1;

     if(osd_cnt == 1 && ret !=1){
	     SKYFS_ERROR_1("bug!! osd_cnt %d, this_osd %d, osd_gid %d ,replica_id %d ,ret %d\n",
			     osd_cnt, this_replica_osd, osd_gid, replica_id,ret);
     }

     if(find_gid_cnt<3){
	     SKYFS_ERROR_1("osd_cnt %d, this_osd %d, osd_gid %d ,replica_id %d ,ret %d, virtg_per_osd %lu\n",
			     osd_cnt, this_replica_osd, osd_gid, replica_id, ret, virtue_groups_per_osd);
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


skyfs_s32_t 
SS_pending_io_check(skyfs_io_file_t * file,skyfs_pending_io_t * pending_io )
{
	struct list_head * index, * head;
	skyfs_pending_io_t * tmp;
	int conflict = 0;
	
	head = &file->pending_io_list;

	list_for_each(index, head){
        	tmp = list_entry(index, skyfs_pending_io_t, pending_io_entry);
        	if(tmp->type == 0 && pending_io->type == 0) // all reading , no conflict
			continue; 
            	if(pending_io->offset+pending_io->length <= tmp->offset)
			continue;
            	if(tmp->offset+tmp->length <= pending_io->offset)
			continue;

		SKYFS_ERROR_1("find running io %d %lu %lu conflict with new_io %d %lu %lu , ino is %lu\n ",
				tmp->type, tmp->offset, tmp->length, 
				pending_io->type, pending_io->offset, pending_io->length, file->ino);
		conflict = 1;
		break;
            	            
        }
	return conflict;
 }


skyfs_io_file_t * 
SS_get_pending_file_locked (skyfs_ino_t ino, int create)
{
	skyfs_io_file_t * pending_file = NULL;
	struct list_head * index, * head;
	skyfs_io_file_t * tmp;
	int pos = (int)(ino % SKYFS_DOP_HASH_LEN);
	skyfs_htb_t * htbp = &skyfs_dop_htbbase[pos];

        head = &htbp->head;
	index = NULL;
	pthread_mutex_lock(&htbp->lock);
	list_for_each(index,head){
		tmp = list_entry(index, skyfs_io_file_t, htb_entry);
		if(tmp->ino == ino){
			pending_file = tmp;
			pthread_spin_lock(&pending_file->lock); //  get it locked
			break;
		}
	}
	if(pending_file == NULL){
		if(!create){
			goto ERR;
		}
		pending_file = (skyfs_io_file_t * )calloc(1, sizeof(skyfs_io_file_t));
		if(pending_file == NULL){
			SKYFS_ERROR_1("can not alloc new skyfs_io_file for ino %llu\n", ino);
		}else{
			pending_file->ino = ino;
			INIT_LIST_HEAD(&pending_file->htb_entry);
			INIT_LIST_HEAD(&pending_file->pending_io_list);
			pthread_spin_init(&pending_file->lock, PTHREAD_PROCESS_PRIVATE);
			list_add(&pending_file->htb_entry, &htbp->head);
			pthread_spin_lock(&pending_file->lock);


		}
	}

ERR:

        pthread_mutex_unlock(&htbp->lock);

	return pending_file;
		

}

skyfs_s32_t
skyfs_release_pending_io (skyfs_pending_io_t * pending_io)
{

	int ret = 0;
	/* disable this test function  */
#ifdef OSD_TEST_CONFLICT
	skyfs_io_file_t * io_file = NULL;
	io_file = SS_get_pending_file_locked(pending_io->ino, 0);

	if(io_file == NULL){
		ret = -ENOENT;
		SKYFS_ERROR_1("can not find io_file for delete, ino %llu\n", pending_io->ino);
		goto ERR;

	}
	list_del_init(&pending_io->pending_io_entry);
	pthread_spin_unlock(&io_file->lock);
#endif
	

ERR:
	return ret;

}

skyfs_s32_t
skyfs_insert_pending_io (skyfs_pending_io_t * pending_io)
{
	int ret = 0;

#ifdef OSD_TEST_CONFLICT
	skyfs_io_file_t * io_file = NULL;
	io_file = SS_get_pending_file_locked(pending_io->ino, 1);

	if(io_file == NULL){
		ret = -ENOENT;
		SKYFS_ERROR_1("can not find io_file for insert,  ino %llu\n", pending_io->ino);
		goto ERR;

	}

	if(SS_pending_io_check(io_file, pending_io) != 0){
		SKYFS_ERROR_1("warning ! pending_io conflict in ino %llu\n ", pending_io->ino);
	}
	list_add_tail(&pending_io->pending_io_entry, &io_file->pending_io_list);
	
	pthread_spin_unlock(&io_file->lock);
#endif	

ERR:
	return ret;

}



skyfs_s32_t
__skyfs_SS_do_read(skyfs_ino_t ino,
        skyfs_u32_t obj_size,
        skyfs_s8_t *buf, 
        skyfs_u64_t offset, 
        skyfs_u32_t count, 
        skyfs_u32_t partition_id, 
        skyfs_u64_t obj_id,
        skyfs_u32_t replica_id)
{
    skyfs_s32_t rc = 0;
    skyfs_s32_t show_msg = 0;
    skyfs_s32_t fd = 0;
    skyfs_u64_t obj_offset = 0;
    struct timeval tv;
    //skyfs_u32_t stripe_num = ((ino+obj_id)% skyfs_data_stripe_cnt);

    if(offset ==0 && count == 32768){
    	SKYFS_ERROR("%s:enter ino :%llx, obj_id:%llu,count;%d,obj_offset:%llu\n", 
        	__FUNCTION__, ino, obj_id, count, offset);
	//show_msg = 1;
    }

    fd = __skyfs_SS_open_obj_file(ino, partition_id, obj_id, replica_id, 2);
    if(fd <= 0){
        SKYFS_ERROR_1("%s:open obj file errno:%d\n", __FUNCTION__, fd);
	if(show_msg){
		 SKYFS_ERROR_1("%s:open obj file errno:%d, ino %llx\n", __FUNCTION__, fd, ino);
	}
        rc = fd;
        goto ERR;
    }

    obj_offset = offset % obj_size;
    fdatasync(fd);

    //rc = lseek(fd,obj_offset,SEEK_SET);
    //if(rc < 0){
      //     SKYFS_ERROR_1("%s:lseek fd:%d errno:%d, ino %llx\n", __FUNCTION__, fd, rc, ino);
        //goto ERR;
    //}
    //rc = read(fd, buf, count);

	//read_cnt++;
    if(get_error_test_state() &&  test_read_errors <= max_errors){
	if((read_cnt % 20000 == 357)){
		rc = -EIO;
		test_read_errors ++;
	}
    }
    if(rc == 0){
    	rc = pread(fd, buf, count, obj_offset);
    }
    if(rc <=  0){
           SKYFS_ERROR_1("%s:read fd:%d errno:%d, ino %llu, offset %llu count %llu, op_cnt %d, obj_offset %llu\n", __FUNCTION__, fd, rc, ino, offset , count,read_cnt, obj_offset);
        goto ERR;
    }
ERR:
    if(fd){
        close(fd);
    }


    //if(count >0 && rc <=  0)
    if(count == 4096)
    	SKYFS_ERROR("%s:exit failed .ino:%llu, rc:%d, offset:%llu, count %llu,obj_offset %lu, first char %d\n", __FUNCTION__, ino, 
			rc, offset, count, obj_offset, buf[0]);

    return rc;
}


void __skyfs_SS_listxattr(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  kiov;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;
    skyfs_u32_t buf_size;
    skyfs_s8_t  *buf = NULL;
    skyfs_u32_t i = 0;
    skyfs_u32_t total_data_size = 0;
    char * pos = NULL;

    skyfs_ino_t       ino;
    int rc = 0;
    //skyfs_u32_t osd_id;

    skyfs_io_vector_t vec;
    msgp = __skyfs_get_msg(req->req_msg);

    req_type = msgp->fromType;
    req_id = msgp->fromid;
    memcpy(&vec, &(msgp->u.listxattrReq.vec), sizeof(skyfs_io_vector_t));
    ino = vec.ino;
    buf_size = vec.count;
    //skyfs_u32_t       subset_id;
    //skyfs_u32_t       chunk_id;

    // TODO : need to find the ino->xattr_list and conclude how to reply
    SKYFS_ERROR("SS_LISTXATTR enter, buf_size %d\n", buf_size);
    if(ino == 0){
    	SKYFS_ERROR_1("SS_LISTXATTR ino is zero , return\n");
	    rc = -ENOENT;
	    goto ERR;
    }
   
    if(buf_size == 0){
	rc = -ENOBUFS;
    	buf = (skyfs_s8_t *)malloc(16);
    }else{
    	buf = (skyfs_s8_t *)malloc(buf_size);
    }
    pos = buf;
    int index = 0;
    int old_index = 0;

    while(strcmp(skyfs_xattr_names[i],"xattr_end")){

         if(!strcmp(skyfs_xattr_names[i],"null")){
		 i++;
		 continue;
	 }
	 total_data_size += (strlen(skyfs_xattr_names[i])+1);
    	 //SKYFS_ERROR_1("SS_LISTXATTR , add name str , total data_size %d\n", total_data_size);
/*
 	 if(buf_size <= total_data_size){
	    rc = -ENOBUFS;
	    goto ERR; 
-
    	 } */

	 if(buf_size < total_data_size){
		 rc = -ENOBUFS;

    	 	SKYFS_ERROR_1("SS_LISTXATTR , no bufs , bufsize %d , total data_size %d, index %d\n", buf_size,total_data_size, i);
		 i++;
		 continue;
		 //goto ERR;
	 }
	 strcpy(pos,skyfs_xattr_names[i]);
	 old_index = index;
    	 //SKYFS_ERROR_1("SS_LISTXATTR , total data size %d ,add name str %s, str index %d, pos_index %d\n", total_data_size, skyfs_xattr_names[i], i, old_index );
	 pos += (strlen(skyfs_xattr_names[i]));
	 index += (strlen(skyfs_xattr_names[i]));

	 pos[0] = 0;
	 pos ++;
	 index++;
	 i++;
	 //rc = total_data_size;

    } 

    if(rc >= 0)
	    rc = index;

    //if(rc > 1)
//	    rc = rc - 1;
    SKYFS_ERROR("all list end index at %d\n", index);
    pos = buf;
#if 0
    for (index = 0; index < total_data_size; index++){
	
    	SKYFS_ERROR_1("%c:", pos[index]);
    }
#endif


ERR:

    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_listxattr_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_DATA, 0, NULL, req_size);

    kiov.ak_addr = buf;
    if(rc > 0)
    	kiov.ak_len = rc;
    else
    	kiov.ak_len = 16;
    kiov.ak_offset = 0;
    kiov.ak_flag = 0;

    req->req_niov = 1;
    req->req_iov = &kiov;
    msgp->error = rc;
    //if(rc <= 0)
//	    msgp->error = 16;
    msgp->type = SKYFS_MSG_O_LISTXATTR;
    msgp->fromType= SKYFS_OSD;
    msgp->fromid = osd_this_id;
    msgp->u.listxattrAck.xattr_cnt = total_data_size;

    SKYFS_ERROR("SS_LISTXATTR , rc %d , ret total data_size %d\n",  msgp->error, msgp->u.listxattrAck.xattr_cnt);
    rc = amp_send_sync(osd_comp_context, req,
                req_type,
                req_id,
                0);
    SKYFS_ERROR("SS_LISTXATTR , amp_send rc %d , ret total data_size %d\n", rc, msgp->u.listxattrAck.xattr_cnt);
	if(kiov.ak_addr){
        	free(kiov.ak_addr);
    	}

ERR_NONE:

    	if(req->req_msg){
        	free(req->req_msg);
    	}

    	if(req->req_reply){
        	free(req->req_reply);
    	}

    	__amp_free_request(req);

	SKYFS_ERROR("SS_LISTXATTR return \n");


    

}

void __skyfs_SS_read(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  kiov;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;
    skyfs_s8_t  *buf = NULL;

    skyfs_u32_t osd_id;

    skyfs_io_vector_t vec;
    skyfs_u32_t       subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    skyfs_u32_t       count;
    skyfs_u32_t       read_count;
    skyfs_u32_t       offset;
    skyfs_u64_t       file_offset;
    skyfs_u32_t       obj_size;

    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file = NULL;
    skyfs_DL_file_t   tmp_dl_file;
    skyfs_DL_part_t *   tmp_dl_part = NULL;

    skyfs_dl_dest_t   des;
    skyfs_u32_t       replica_id;
    skyfs_s32_t       partition_id;
    skyfs_u32_t       osd2read = 0;
    skyfs_u32_t       replica_num;
    skyfs_s32_t       fd = 0;
    skyfs_s32_t       rc = 0;
    skyfs_s32_t       rc0 = 0;
    skyfs_u32_t       direct_op = 0;
    skyfs_s32_t       show_msg = 0;
    int 	      algorithm = 0;
    size_t 	      foff = 0;
    size_t 	      flen = 0;
    skyfs_pending_io_t pending_read;
    skyfs_O_objbuf_t  *objbuf = NULL;
    int rc1 = 0;
    int do_read = 0;
    int need_unlock = 0;
    struct timeval tv_read_start, tv_read_end, tv_local_read_start, tv_local_read_end;

    SKYFS_ENTER("%s:enter:ino:\n", __FUNCTION__);
    //skyfs_u32_t         time_total = 0;
    //skyfs_f64_t         read_throughput = 0;
    //skyfs_timespec_t    start_time;
    //skyfs_timespec_t    end_time;

    gettimeofday(&tv_read_start, NULL);

#if 0
    if(0){
	    uint64_t recv_time = req->req_recv_time;
	    SKYFS_ERROR_1("osd read req %p , req_recv_time %llu, start process time %llu\n ",
			    req, recv_time, tv_read_start.tv_sec*1000000+tv_read_start.tv_usec);
    }
#endif
    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.readObjReq.vec), sizeof(skyfs_io_vector_t));
    SKYFS_ENTER("%s:after memcpy.\n", __FUNCTION__);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = vec.ino;
    obj_id = vec.obj_id;
    file_offset = vec.offset + obj_id * SKYFS_OBJECT_SIZE;
    offset = vec.offset;
    count = vec.count;
    foff = vec.offset;
    flen = vec.count;
    replica_num = vec.replica_num;

    direct_op = vec.direct_op;
    if(offset == 0 && count == 32768){
    	SKYFS_ERROR("%s:enter start :ino:%llx, obj_id:%llu, offset:%u, count:%d,req_remote_type:%u,directop:%u\n",
        	__FUNCTION__, ino, obj_id, offset, count, req_type, vec.direct_op);
	show_msg = 1;
    }

    //offset = vec.offset;
    obj_id = vec.obj_id;
    obj_size = vec.obj_size;
    partition_id = vec.partition_id;
    replica_id = vec.replica_id;
    replica_num = vec.replica_num;

    if(local_read_cnt %100000 == 0)
    	SKYFS_ERROR_1("SS_read start : ino %llu , offset %lu, count %lu, obj_id , %lu\n", ino, offset, vec.count , obj_id );

    if(req->req_remote_type == SKYFS_OSD && (vec.direct_op & 0x01) ){
        offset = vec.offset;
        obj_id = vec.obj_id;
        obj_size = vec.obj_size;
        partition_id = vec.partition_id;
        replica_id = vec.replica_id;
	if(vec.forward_count >= replica_num){
		    SKYFS_ERROR_1("all replica failed in ino %lu ,partition %lu, forward cnt %d\n", ino, partition_id, vec.forward_count);
		    need_unlock = 0;
		    rc = -EIO;
		    goto ERR;
	    }

	 SKYFS_ERROR(" got req  in ino %lu ,partition %lu, replica_id %d, forward from other osd\n", ino, partition_id,  vec.replica_id);
	
        //goto ReadyToRead;
    }

    /* new step 1: TODO get partition info */
    partition_id = obj_id /(SKYFS_MAX_OBJ_PER_PART);
    /*TODO init tmp_dl_file */
    tmp_dl_file.ino = ino;
    tmp_dl_file.partition_num = partition_id;
    tmp_dl_file.replica_num = replica_num;
    dl_file = &tmp_dl_file;
    read_cnt ++;

    	SKYFS_ERROR("%s:enter start :ino:%llx, obj_id:%llu, offset:%u, count:%d,req_remote_type:%u,directop:%u\n",
        	__FUNCTION__, ino, obj_id, offset, count, req_type, vec.direct_op);
    rc = __skyfs_SS_read_fill_des(dl_file, obj_id, partition_id, &des, replica_id, &tmp_dl_part);
    if(des.replica_location[replica_id] != osd_this_id || 
		     des.write_version[replica_id] != des.max_write_version){
	    SKYFS_ERROR_1("SSSSS_READ parttion %lu ,local_replica %d in osd %d fault, location %d, write_version %d max_write_version %d ,forward to next ,dl_part %p, offset %lu\n", 
			    partition_id,replica_id, osd_this_id,
			des.replica_location[replica_id], 
		        des.write_version[replica_id],  des.max_write_version, tmp_dl_part,file_offset);
	    //if(tmp_dl_part == NULL)
		goto after_read;
	    	    
	    // TODO forward request 
	    uint32_t osd_gid = find_osdgid(osd_num, ino, obj_id);
	    uint32_t next_replica = replica_id + 1;
	    if(next_replica > replica_num)
		    next_replica = 1; 
	    uint32_t next_osd =  find_replica_osd(osd_num, osd_gid, next_replica , replica_num);
		msgp->u.readObjReq.vec.offset = offset;
        	msgp->u.readObjReq.vec.obj_id = obj_id;
        	msgp->u.readObjReq.vec.partition_id = partition_id;
        	msgp->u.readObjReq.vec.obj_size = obj_size;
        	msgp->u.readObjReq.vec.replica_id = next_replica;
        	msgp->u.readObjReq.vec.direct_op |= 1;
		msgp->u.readObjReq.vec.forward_count++;
		if(1)
        		SKYFS_ERROR_1("forward readobj request to osd %d \n", next_osd);
        	__skyfs_SS_forward_request(req, SKYFS_OSD, next_osd);
        	goto ERR_NONE;


    }else{
	    // TODO do_read assume no readahead buffer
	    read_count = count;
	    

	    buf = (skyfs_s8_t *)malloc(read_count);
    	    if(buf == NULL){
        	SKYFS_ERROR_1("%s:malloc buf errno:%d\n", __FUNCTION__, errno);
        	goto ERR;
    	    }
	    obj_size = vec.obj_size;
	    offset = file_offset % obj_size;
	    
	    if(tmp_dl_part != NULL){
		    pthread_rwlock_rdlock(&tmp_dl_part->part_lock);
	    }
    	    
    	    gettimeofday(&tv_local_read_start, NULL);

	    if(!strcmp(skyfs_xattr_values[0],"ZSTD")){

	     uint32_t part_obj_id = obj_id % SKYFS_MAX_OBJ_PER_PART;
		    // get the exact offset and count
	     struct IntervalTree *  this_intervalTree = tmp_dl_part->interval_tree_handles[part_obj_id];
                        if(this_intervalTree != NULL){
                              struct interval inte;
                              inte.low = ((obj_id*SKYFS_OBJECT_SIZE + offset)/SKYFS_OBJECT_NODE_SIZE)*SKYFS_OBJECT_NODE_SIZE;
                              inte.high =  inte.low + SKYFS_OBJECT_NODE_SIZE -1;
                              //inte.data_low =  offset % SKYFS_OBJECT_SIZE; // tobe change to dense mode
                              //inte.data_high = inte.data_low + count -1;
                              //inte.comp_low =  offset % SKYFS_OBJECT_NODE_SIZE;
                              //inte.comp_high = inte.data_low + vec.fcount-1;
                              //nte.obj_id = obj_id;
                              struct IntervalTNode * this_interval =  do_search_exact_interval(this_intervalTree,inte);
			      if( this_interval == NULL){
				      SKYFS_ERROR_1("SS_READ: can not find interval node at obj:offset %lu:%lu\n",obj_id, offset );
				      rc = 0;
				      goto after_read;
			      }else{
				      uint32_t read_len = this_interval->inte.comp_high -  this_interval->inte.comp_low + 1;
				      size_t   read_offset =  this_interval->inte.comp_low; 
				      algorithm = this_interval->inte.flag;
				      if(algorithm == 0){
					      //no compress
					      rc = __skyfs_SS_do_read(ino, obj_size, buf, offset, read_count, partition_id, obj_id, replica_id);
					      flen = rc;
					      goto after_read;
				      }
				      // TODO: if buf is not enough ?
				      if(read_len > read_count && algorithm != 0){
					      SKYFS_ERROR_1("the compressed data to read is bigger than original , free and remalloc buffer \n");
					      free(buf);
					      buf = malloc(read_len);
				      }
				      rc =  __skyfs_SS_do_read(ino, obj_size, buf, 
				      	read_offset, read_len, partition_id, obj_id, replica_id);
				      foff =  this_interval->inte.data_low;
				      flen =  this_interval->inte.data_high - this_interval->inte.data_low +1;
				      //algorithm = this_interval->inte.flag;
				      SKYFS_ERROR("SS_READ: find interval node at obj:offset %lu:%lu, algorithm %d\n",obj_id, offset , algorithm);
				      if(rc <= 0){

				      	SKYFS_ERROR_1("SS_READ: can not read compress data at  %lu:%lu\n",obj_id, offset );
				      }else if(rc != read_len){
				      	SKYFS_ERROR_1("SS_READ: Warning !read compress data at  %lu:%lu, length %lu , expect %lu\n",
							obj_id, offset, rc, read_len );
				      }

				      goto after_read;

			      }

		         }else{
				 SKYFS_ERROR_1("SS_READ: can not open interval tree at obj:node %lu:%u, ino %llu\n",obj_id, part_obj_id , ino);
				 rc = -ENOENT;
				 goto after_read;
			 }

	    }

	    rc = __skyfs_SS_do_read(ino, obj_size, buf, offset, read_count, partition_id, obj_id, replica_id);
	    if(local_read_cnt %100000 == 0)
	    	SKYFS_ERROR_1("do_normal do_read, ino %lu, offset %llu, read_count %lu,  objid %d, buf[0] %x \n", ino, offset, read_count, obj_id, buf[0]);
after_read:
	    if(rc <=0){
		    SKYFS_ERROR_1("After read file failed . file offset %llu, count %llu\n", file_offset, count);
	    }
    	    gettimeofday(&tv_local_read_end, NULL);
	    local_read_cnt ++;
	    
	    if(tmp_dl_part != NULL){
		    pthread_rwlock_unlock(&tmp_dl_part->part_lock);
	    }
	    if(rc < 0 ){

		    if(tmp_dl_part != NULL){
		    	pthread_rwlock_wrlock(&tmp_dl_part->part_lock);
	    	     }

		    // TODO : update local partition file info 
		    int tmp_rc =  __skyfs_DL_invalid_partition(ino, partition_id, replica_id, (uint64_t)-2, 0);
		    
		    if(tmp_dl_part != NULL){
		    	pthread_rwlock_unlock(&tmp_dl_part->part_lock);
	    	     }


		    // try to forward retuest
		uint32_t osd_gid = find_osdgid(osd_num, ino, obj_id);
	    	uint32_t next_replica = replica_id + 1;
	    	if(next_replica > replica_num)
		    next_replica = 1; 
	    	uint32_t next_osd =  find_replica_osd(osd_num, osd_gid, next_replica , replica_num);
		msgp->u.readObjReq.vec.offset = offset;
        	msgp->u.readObjReq.vec.obj_id = obj_id;
        	msgp->u.readObjReq.vec.partition_id = partition_id;
        	msgp->u.readObjReq.vec.obj_size = obj_size;
        	msgp->u.readObjReq.vec.replica_id = next_replica;
        	msgp->u.readObjReq.vec.direct_op |= 1;
		msgp->u.readObjReq.vec.forward_count++;
		if(1)
        		SKYFS_ERROR_1("local read failed_obj_id %lu,  forward readobj request to osd %d , replica %d\n", 
					obj_id, next_osd,  msgp->u.readObjReq.vec.replica_id);
        		__skyfs_SS_forward_request(req, SKYFS_OSD, next_osd);
        	goto ERR_NONE;

	    }else{
		    need_unlock = 0;
		    goto FinishRead;
	    }
	




    }
    // check if this replica can use








    SKYFS_ERROR("%s:enter:ino:%llu, obj_id:%llu, offset:%u, count:%d,req_remote_type:%u,directop:%u\n",
        __FUNCTION__, ino, obj_id, offset, count, req_type, vec.direct_op);

    htbp = __skyfs_SS_locate_dl_subset(ino, 0, &subset_id, &osd_id);
    if(htbp == NULL){
        rc = -ENOENT;
        SKYFS_ERROR_1("%s:ino:%llu,read sub not here,offset %llu , count %llu forward\n", __FUNCTION__, ino, file_offset, count);
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    /*Load balance support*/
    osd_id = __skyfs_SS_check_dl_htbcache(htbp);
    if(osd_id != 0){
        rc = -ENOENT;
	if(show_msg){
        	SKYFS_ERROR_1("%s:ino:%llx,obj_id:%llu,htb not here,forward\n",
            		__FUNCTION__, ino, obj_id);
	}
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }

    dl_subset = __skyfs_SS_get_dl_subset(htbp, subset_id);
    if(dl_subset == NULL){
        rc = -ENOENT;
	if(1){
        	SKYFS_ERROR_1("%s:ino:%llx,obj_id:%llu,dl_subset not here,Failed\n",
            		__FUNCTION__, ino, obj_id);
	}

        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_rdlock(&(dl_subset->rwlock));

    dl_chunk = __skyfs_SS_locate_dl_chunk(dl_subset, ino, 0, &chunk_id);
    if(dl_chunk == NULL){
        dl_chunk = __skyfs_SS_get_dl_chunk(dl_subset, chunk_id);
        if(dl_chunk == NULL){
            rc = -ENOENT;
	    	
	if(1){
        	SKYFS_ERROR_1("%s:ino:%llx,obj_id:%llu,dl_chunk not here,Failed\n",
            		__FUNCTION__, ino, obj_id);
	}
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&dl_chunk->rwlock);
    pthread_mutex_unlock(&dl_chunk->lock);

    SKYFS_MSG("%s:ino:%llu,subset_id:%u,chunk_id:%u\n", 
        __FUNCTION__, ino, subset_id, chunk_id);    
    dl_file = __skyfs_SS_locate_dl_file(dl_chunk, ino);
    if(dl_file == NULL){
        SKYFS_ERROR_1("%s:dl_file not exist:subset_id:%u,chunk_id:%u,ino:%llx, ReadNothing\n", 
            __FUNCTION__, subset_id, chunk_id, ino);    
        rc = 0;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ReadNothing;
    }

    /*2 Locate content, concurrency*/
    obj_id = __skyfs_SS_get_objid(dl_file, file_offset);
    SKYFS_MSG("%s:get objid,obj_id:%llu\n", __FUNCTION__, obj_id);    

    /*find obj, and make sure the partition in the cache, consider the size of obj*/
    partition_id = __skyfs_SS_locate_obj(dl_file, obj_id);
    if(partition_id == -2){
        SKYFS_ERROR_1("%s:get partition err ,rc:%d\n", __FUNCTION__, rc);    
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }else if(partition_id == -1){
        SKYFS_ERROR_1("%s:can not get partition,obj_id:%llu\n", __FUNCTION__, obj_id);    
        rc = 0;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ReadNothing;
    }

    if(skyfs_replica == 1){
	    /*TODO temp changed by mayl , just for performance */
	    replica_id = 1;
	    osd2read = osd_this_id;
	    goto Read_in_local;
    }
    rc = __skyfs_SS_fill_des(dl_file, obj_id, partition_id, &des);
    if(rc < 0){
        SKYFS_ERROR_1("%s:fill des error, rc:%d, ino %llx, offset %llx, count %x, dl_file %p , partition_id %d, obj_id %d \n", 
			__FUNCTION__, rc, ino, offset, count, dl_file, partition_id, obj_id);    
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    replica_id = __skyfs_SS_choose2read(dl_file, &des);
    osd2read = des.replica_location[replica_id];
 
Read_in_local:

    obj_size = dl_file->obj_size;
    offset = file_offset % obj_size;
    SKYFS_MSG("%s:begin to do_read, osd2read:%d\n", __FUNCTION__, osd2read);    
    if(osd2read != osd_this_id){
        /*Modify the content of request*/
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));

        msgp->u.readObjReq.vec.offset = offset;
        msgp->u.readObjReq.vec.obj_id = obj_id;
        msgp->u.readObjReq.vec.partition_id = partition_id;
        msgp->u.readObjReq.vec.obj_size = obj_size;
        msgp->u.readObjReq.vec.replica_id = replica_id;
        msgp->u.readObjReq.vec.direct_op |= 1;
	if(show_msg)
        	SKYFS_ERROR_1("forward readobj request to osd %d \n", osd2read);
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd2read);
        goto ERR_NONE;
    }

ReadyToRead:
 /* mayl : we can not use readbuf for direct io */
    if((direct_op & 0x02) == 0){  /* client buffer read */
	if(1){
	      SKYFS_ERROR_1("READ DATA from buffer ino %llx\n", ino);
        }	      
    	rc = __skyfs_SS_read_objbuf(ino, obj_id, file_offset, count, &objbuf, &buf);
    	if(rc > 0){
        	SKYFS_ERROR("%s:read cache.rc:%d\n", __FUNCTION__, rc);    
        	goto FinishRead;
    	}
    }else{
	    /* changed by mayl for direct io , NO  readahead*/
	    osd_readahead_size = 0;
    }


    pthread_mutex_lock(&osd_block_insert_lock);
    if(osd_readahead_size && osd_block_insert_objbuf){
        if((offset % obj_size + osd_readahead_size < obj_size) && (osd_readahead_size > count)){
            read_count = osd_readahead_size;
        }else{
            read_count = count;
        }
    }else{
        read_count = count;
    }
    pthread_mutex_unlock(&osd_block_insert_lock);

    buf = (skyfs_s8_t *)malloc(read_count);
    if(buf == NULL){
        SKYFS_ERROR_1("%s:malloc buf errno:%d\n", __FUNCTION__, errno);
        goto ERR;
    }
    /* mayl add pending read*/
    pending_read.ino = ino;
    pending_read.offset = file_offset;
    pending_read.length = read_count;
    pending_read.type = 0; //READ
    INIT_LIST_HEAD(&pending_read.pending_io_entry);
    rc1 = skyfs_insert_pending_io(&pending_read);

    rc = __skyfs_SS_do_read(ino, obj_size, buf, offset, read_count, partition_id, obj_id, replica_id);

    rc1 = skyfs_release_pending_io(&pending_read);
    do_read = 1;
    
    if(rc <= 0){
        if(osd2read == osd_this_id){
            SKYFS_ERROR_1("%s:doread nothing or error, rc:%d\n", __FUNCTION__, rc);    
            pthread_rwlock_unlock(&dl_chunk->rwlock);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
        }
        goto ERR;
    }

    if((direct_op & 0x02)== 0){ /*client buffer read */  
    	rc0 = __skyfs_SS_insert_objbuf(ino, obj_id, file_offset, rc, buf);
    	if(rc0 < 0){
        	SKYFS_ERROR("%s:insert objbuf err, rc:%d\n", __FUNCTION__, rc0);    
    	}
    }

    if(rc > count){
        rc = count;
    }

FinishRead:
ReadNothing:
ERR:

    
    /*4.Send reply*/
    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_readobj_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_DATA, 0, NULL, req_size);

    kiov.ak_addr = buf;
    kiov.ak_len = rc;
    kiov.ak_offset = 0;
    kiov.ak_flag = 0;
    //kiov.ak_foffset = foff;
    //kiov.ak_flen = flen;
    kiov.ak_flen = rc;

    msgp->error = rc;
    msgp->type = SKYFS_MSG_O_READ_OBJ;
    msgp->fromType= SKYFS_OSD;
    msgp->fromid = osd_this_id;
    if(rc > 0){
        req->req_niov = 1;
        req->req_iov = &kiov;
        msgp->u.readObjAck.dest = osd_this_id;
        msgp->u.readObjAck.subset = subset_id;
        msgp->u.readObjAck.chunk = chunk_id;
        msgp->u.readObjAck.foffset = foff;
        msgp->u.readObjAck.flen = flen;

        msgp->u.readObjAck.algorithm = algorithm;
        if(osd2read == osd_this_id && need_unlock){
            pthread_rwlock_unlock(&dl_chunk->rwlock);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
        }
	
	if((count >= 1024) && 0){
		uint64_t * pdata = (uint64_t*)buf;
		msgp->u.readObjAck.check_data1 = pdata[126];
		msgp->u.readObjAck.check_data2 = pdata[127];
				


	}
    }else{
        SKYFS_ERROR_1("%s:read Error or Nothing:%d., ino %llu, offset %llu , size %llu from %d , do_read %d\n", 
			__FUNCTION__, rc, ino, file_offset, read_count, req_id, do_read  );
        req->req_type = AMP_REPLY|AMP_MSG;
    }
 
    SKYFS_MSG("%s:rc:%d,client_id:%d\n", __FUNCTION__, msgp->error, req_id);

    if(fd){
        close(fd);
    }


    struct timeval tv_net_start, tv_net_end;
    gettimeofday(&tv_read_end, NULL);
    read_time += (tv_read_end.tv_sec *1000000 + tv_read_end.tv_usec);
    read_time -= (tv_read_start.tv_sec *1000000 + tv_read_start.tv_usec);
    local_read_time += (tv_local_read_end.tv_sec *1000000 + tv_local_read_end.tv_usec);
    local_read_time -= (tv_local_read_start.tv_sec *1000000 + tv_local_read_start.tv_usec);
    
    gettimeofday(&tv_net_start,NULL);
    rc = amp_send_sync(osd_comp_context, req,
                req_type,
                req_id,
                0);
    gettimeofday(&tv_net_end,NULL);

    read_amp_time += (tv_net_end.tv_sec *1000000 + tv_net_end.tv_usec);
    read_amp_time -= (tv_net_start.tv_sec *1000000 + tv_net_start.tv_usec);
    
    if(read_cnt %1200 == 0){
#if 0	    
	 uint64_t recv_time = req->req_recv_time;
	SKYFS_ERROR_1("SS_READ_cnt %lu, local_read_cnt %lu, read_time %lu , local_read_time %lu , net trans time %llu\n", 
			read_cnt, local_read_cnt,read_time, local_read_time, read_amp_time);
        SKYFS_ERROR_1("osd read req %p , req_recv_time %llu, start process time %llu , net_start_time %llu , net_end_time %llu \n",
                            req, recv_time, tv_read_start.tv_sec*1000000+tv_read_start.tv_usec,
			     tv_net_start.tv_sec*1000000+tv_net_start.tv_usec,
			     tv_net_end.tv_sec*1000000+tv_net_end.tv_usec);
#endif
    }


    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(kiov.ak_addr){
        free(kiov.ak_addr);
    }

ERR_NONE:

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("%s:leave:rc:%d,ino:%llu,obj_id:%llu,offset:%d,count:%d,sid:%d,chunkid:%d\n",
        __FUNCTION__, rc, ino, obj_id, offset, count, subset_id, chunk_id);

    return;

}
#if 0
S
	amp_kiov_t *kiov, 
	skyfs_dl_dest_t *des,
	skyfs_io_vector_t *vec,
	skyfs_u32_t partition_id)
{
    skyfs_s32_t client_id;
    skyfs_u32_t count;
    skyfs_u64_t offset;
    skyfs_ino_t ino;
    skyfs_s32_t rc = 0;

    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       hashvalue;


    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.prepareWriteReq.vec), sizeof(skyfs_io_vector_t));
    client_id = msgp->u.prepareWriteReq.client_id;
    partition_id = msgp->u.prepareWriteReq.partition_id;
    kiov = req->req_iov;
    ino = msgp->u.prepareWriteReq.ino;
    offset = vec.offset;
    count = vec.count;

    SKYFS_MSG("%s:enter.ino %llu from %u\n\n", __FUNCTION__, ino, client_id);

    /*1.cache the iov in the writebuf hash*/
    hashvalue = __skyfs_get_obj_hashvalue(ino, client_id);
    hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
    htbp = &skyfs_writecache_htbbase[hashvalue];
    pthread_mutex_lock(&htbp->lock);
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("%s:hash table NULL,%u\n", __FUNCTION__, hashvalue);
        goto OUT;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_databuf_t, databuf_hash);
        if(tmp->ino == ino 
            && tmp->client_id == client_id
            && tmp->offset == offset
            && tmp->count == count){
            databuf = tmp;
            SKYFS_LEAVE("%s:same write exist,ino:%llu,client:%d\n", __FUNCTION__, ino, client_id);
            goto OUT;
        }
    }

OUT:
    if(databuf == NULL){
        databuf = (skyfs_O_databuf_t *)malloc(sizeof(skyfs_O_databuf_t));
        if(databuf == NULL){
            SKYFS_ERROR("%s:malloc databuf error\n", __FUNCTION__);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }
        databuf->ino = ino;
        databuf->client_id = client_id;
        databuf->offset = offset;
        databuf->count = count;
        databuf->partition_id = partition_id;
        databuf->ref = 1;
        databuf->buf = kiov->ak_addr;
        list_add(&(databuf->databuf_hash), &(skyfs_writecache_htbbase[hashvalue].head));
        SKYFS_ERROR("%s:insert write buf at %d,%p,%p\n", 
            __FUNCTION__, hashvalue,head,&(skyfs_writecache_htbbase[hashvalue].head));
        pthread_mutex_unlock(&htbp->lock);
    }else{
        /*Deal with replica data store in the same node*/
        databuf->ref ++;
        SKYFS_ERROR("%s:same write exist,ref:%d,%p\n", __FUNCTION__, databuf->ref, databuf);
        if(kiov->ak_addr){
            free(kiov->ak_addr);
        }
        pthread_mutex_unlock(&htbp->lock);
        goto ERR_NONE;
    }

ERR_NONE:
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_MSG("%s:leave.buffed %llu from %d\n\n", __FUNCTION__, ino, client_id);

}
#endif

#if 0
static void recover_partition_files(amp_request_t * req, int cur_replica_id, int replace_old)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;
    char pname[256];
    int part_cnt  = 0;
    int i = 0;
    int rc = 0, fd = 0;
    skyfs_DL_part_t * part_ptr = NULL;

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    part_cnt = msgp->u.replicaRecoverReq.replica_obj_cnt;
    part_ptr = (skyfs_DL_part_t *)kiov->ak_addr;
    if(msgp->u.replicaRecoverReq.flag == 3){
	//TODO: record this data and copy form other healthy osd
	SKYFS_ERROR_1("%s: get invalid recover data\n");
	return ;
    }
    memset(pname, 0 , 256);

    while(i < part_cnt){
	    
	   // reqad part and wreite to files

	    part_ptr->replica_id = cur_replica_id;

	    rc = __skyfs_SS_compose_partname(part_ptr->ino, part_ptr->partition_id, part_ptr->replica_id, pname);
	    if(!replace_old)	    
		fd = open(pname, O_RDWR|O_CREAT, 0666);
            else	    
		fd = open(pname, O_RDWR|O_CREAT|O_TRUNC);
	    if(fd > 0){
			//dl_part = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			//bzero(dl_part, sizeof(skyfs_DL_part_t));
		rc = write(fd, part_ptr, sizeof(skyfs_DL_part_t));
		close(fd);
		}else{
			SKYFS_ERROR_1("%s:open partition file %s err:errno:%d, ino %llx, partition_id,%d,replica_id %d\n", 
					__FUNCTION__, pname, errno, part_ptr->ino , part_ptr->partition_id, cur_replica_id);
			//goto ERR;
		}

	    part_ptr++;
	    i++;

    }
    if(msgp->u.replicaRecoverReq.flag == 1 ){
	    partition_recover_state[msgp->u.replicaRecoverReq.src_replica_id] = 0;

    }


   //skyfs_io_vector_t vec;

}
#endif 

static void recover_partition_files(amp_request_t * req, int cur_replica_id, int replace_old)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;
    char dname[256];
    int obj_part_cnt  = 0;
    int i = 0;
    int rc = 0, fd = 0;
    char * obj_data_ptr = NULL;
    size_t obj_addr, write_length;
    skyfs_replica_recover_head_t * recover_data_head;
    skyfs_u64_t partition_id;
    size_t start_offset;
    

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    obj_part_cnt = msgp->u.replicaRecoverReq.replica_obj_cnt;
    obj_data_ptr = (char *)kiov->ak_addr;
    memset(dname, 0 , 256);
    SKYFS_ERROR_1("%s , try to recover %d  partition parts \n", __FUNCTION__, obj_part_cnt);
    while(i < obj_part_cnt){
	    
	   // reqad part and wreite to files

	    //part_ptr->replica_id = cur_replica_id;
	    //rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, 1, path);
	    recover_data_head = (skyfs_replica_recover_head_t *)obj_data_ptr;
	    SKYFS_ERROR_1("%s --%d : try to recover partition  desc file %d ,  %llu-%d, from offset %llu , length %llu \n",__FUNCTION__, __LINE__,i, 
			    recover_data_head->ino, recover_data_head->obj_id, recover_data_head->start_offset, recover_data_head->size);

	    //obj_data_ptr += sizeof(skyfs_replica_recover_head_t); 
#if 1
	    //partition_id = recover_data_head->obj_id/SKYFS_MAX_OBJ_PER_PART;
	    partition_id = recover_data_head->obj_id;
	    rc = __skyfs_SS_compose_partname(recover_data_head->ino, partition_id, cur_replica_id, dname);
    	    if(rc < 0){
            	SKYFS_ERROR_1("%s:compose partition  file %s err:%d\n", __FUNCTION__, dname ,rc);
	    //rc = -1;
	    	obj_data_ptr += sizeof(skyfs_replica_recover_head_t);
	        obj_data_ptr += recover_data_head->size;
	        
	    	continue;
            //goto ERR_WRITE;
            }else{
            	SKYFS_ERROR_1("%s:compose partition  file %s err:%d\n", __FUNCTION__, dname ,rc);

	    }


	    //rc = __skyfs_SS_compose_partname(part_ptr->ino, part_ptr->partition_id, part_ptr->replica_id, pname);
	    if(replace_old && (recover_data_head->start_offset == 0))	    
		fd = open(dname, O_RDWR|O_CREAT|O_TRUNC, 0666);
            else	    
		fd = open(dname, O_RDWR|O_CREAT, 0666);

	    obj_data_ptr += sizeof(skyfs_replica_recover_head_t);
	    if(fd > 0){
			//dl_part = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			//bzero(dl_part, sizeof(skyfs_DL_part_t));
		rc = pwrite(fd, obj_data_ptr, recover_data_head->size, recover_data_head->start_offset);
		// TODO : mayl channge obj_localtionj
		int total_file_size = (int)(recover_data_head->size + recover_data_head->start_offset);
		if(total_file_size == sizeof(skyfs_DL_part_t)){
			SKYFS_ERROR_1("receieve full part file, check it and remove cache ... , offset %llu, size %lu\n ",
					recover_data_head->start_offset, (int)(recover_data_head->size));
			skyfs_DL_part_t tmp_part_buf ;
			memset(&tmp_part_buf, 0,  sizeof(skyfs_DL_part_t));
			pread(fd, &tmp_part_buf, sizeof(skyfs_DL_part_t), 0);
			for(int n = 0; n<SKYFS_MAX_OBJ_PER_PART; n++)
				tmp_part_buf.obj_location[n] = osd_this_id;
			pwrite(fd, &tmp_part_buf, sizeof(skyfs_DL_part_t), 0);
			SKYFS_ERROR_1("recover part, replica_write_version %lld\n", tmp_part_buf.replica_write_version);
			rc = __skyfs_DL_remove_partition_cache(recover_data_head->ino, partition_id, cur_replica_id);
			// TODO : free in-memory part buf 
		}
		close(fd);
		}else{
			SKYFS_ERROR_1("%s:open partition file %s err:errno:%d, ino %llx, partition_id,%d, obj_id %d, replica_id %d\n", 
					__FUNCTION__, dname, errno, recover_data_head->ino , partition_id, recover_data_head->obj_id, cur_replica_id);
			//goto ERR;
	    	}
	    SKYFS_ERROR("pwrite return  %d\n, fd %d, size %lu, offset %llu , check buf[0x70] %x]\n", rc, fd, recover_data_head->size, 
			    recover_data_head->start_offset, obj_data_ptr[0x70] );
#endif
	    
	    obj_data_ptr += recover_data_head->size;
	    i++;

    }
    // last or error
    if(msgp->u.replicaRecoverReq.flag & 0x01){
	    data_recover_state[msgp->u.replicaRecoverReq.dest_replica_id] = 0;

    }else{
	    data_recover_state[msgp->u.replicaRecoverReq.dest_replica_id] = msgp->u.replicaRecoverReq.xid;
    }

   //skyfs_io_vector_t vec;

}

static void recover_obj_files(amp_request_t * req, int cur_replica_id, int replace_old)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;
    char dname[256];
    int obj_part_cnt  = 0;
    int i = 0;
    int rc = 0, fd = 0;
    char * obj_data_ptr = NULL;
    size_t obj_addr, write_length;
    skyfs_replica_recover_head_t * recover_data_head;
    skyfs_u64_t partition_id;
    size_t start_offset;
    

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    obj_part_cnt = msgp->u.replicaRecoverReq.replica_obj_cnt;
    obj_data_ptr = (char *)kiov->ak_addr;
    memset(dname, 0 , 256);
    SKYFS_ERROR_1("%s , try to recover %d obj parts , xid  %llu \n", __FUNCTION__, obj_part_cnt, msgp->u.replicaRecoverReq.xid);
    while(i < obj_part_cnt){
	    
	   // reqad part and wreite to files

	    //part_ptr->replica_id = cur_replica_id;
	    //rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, 1, path);
	    recover_data_head = (skyfs_replica_recover_head_t *)obj_data_ptr;
	    SKYFS_ERROR_1("%s --%d : try to recover obj %d ,data file %llu-%d, from offset %llu , length %llu \n",__FUNCTION__, __LINE__,i, 
			    recover_data_head->ino, recover_data_head->obj_id, recover_data_head->start_offset, recover_data_head->size);

	    //obj_data_ptr += sizeof(skyfs_replica_recover_head_t); 
#if 1
	    partition_id = recover_data_head->obj_id/SKYFS_MAX_OBJ_PER_PART;
	    rc = __skyfs_SS_prepareopen_objfile(recover_data_head->ino, partition_id, recover_data_head->obj_id, cur_replica_id, dname);
    	    if(rc < 0){
            	SKYFS_ERROR_1("%s:open obj  file %s err:%d\n", __FUNCTION__, dname ,rc);
	    //rc = -1;
	    	obj_data_ptr += sizeof(skyfs_replica_recover_head_t);
	        obj_data_ptr += recover_data_head->size;
		i++;
	    	continue;
            //goto ERR_WRITE;
            }
               SKYFS_ERROR_1("%s:open obj  file %s ret:%d\n", __FUNCTION__, dname ,rc);


	    //rc = __skyfs_SS_compose_partname(part_ptr->ino, part_ptr->partition_id, part_ptr->replica_id, pname);
	    if(replace_old)	    
		fd = open(dname, O_RDWR|O_CREAT|O_TRUNC, 0666);
            else	    
		fd = open(dname, O_RDWR|O_CREAT, 0666);

	    obj_data_ptr += sizeof(skyfs_replica_recover_head_t);
	    if(fd > 0){
			//dl_part = (skyfs_DL_part_t *)malloc(sizeof(skyfs_DL_part_t));
			//bzero(dl_part, sizeof(skyfs_DL_part_t));
		rc = pwrite(fd, obj_data_ptr, recover_data_head->size, recover_data_head->start_offset);


		if(i == 0)
			SKYFS_ERROR("%s:open obj file %s first char:%x, ino %llx, partition_id,%d, obj_id %d, replica_id %d, obj_buf %p , part_buf %p\n", 
				__FUNCTION__, dname, obj_data_ptr[0], recover_data_head->ino , 
				partition_id, recover_data_head->obj_id, cur_replica_id, obj_data_ptr,kiov->ak_addr);
		close(fd);
	     }else{
		     
			SKYFS_ERROR_1("%s:open obj file %s err:errno:%d, ino %llx, partition_id,%d, obj_id %d, replica_id %d\n", 
					__FUNCTION__, dname, errno, recover_data_head->ino , partition_id, recover_data_head->obj_id, cur_replica_id);
			//goto ERR;
	    }
	    if(rc <0 ){
			SKYFS_ERROR_1("%s:pwrite obj file %s err:errno:%d, ino %llx, partition_id,%d, obj_id %d, replica_id %d\n", 
					__FUNCTION__, dname, errno, recover_data_head->ino , partition_id, recover_data_head->obj_id, cur_replica_id);
	    }
#endif
	    
	    obj_data_ptr += recover_data_head->size;
	    i++;

    }
    if(msgp->u.replicaRecoverReq.flag  == 2){
	    //data_recover_state[msgp->u.replicaRecoverReq.dest_replica_id] = 0;

	    data_recover_state[msgp->u.replicaRecoverReq.dest_replica_id] = msgp->u.replicaRecoverReq.xid;
    }

   //skyfs_io_vector_t vec;

}


void __skyfs_SS_recover_replica_write(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;

    skyfs_io_vector_t vec;
    uint32_t recover_data_type = 1; // 1 : replica patition files 2 replica data obj files
    //skyfs_s32_t client_id;
    skyfs_u32_t count;
    skyfs_u32_t replica_id;
    skyfs_u32_t src_replica_id;
    skyfs_u64_t offset;
    skyfs_ino_t ino;
    skyfs_s32_t rc = 0;
    skyfs_u32_t obj_size;
    skyfs_u64_t obj_id;
    //struct timeval tv;

    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       partition_id;
    skyfs_u32_t       hashvalue;
    skyfs_s8_t path[SKYFS_MAX_NAME_LEN];


    //skyfs_u32_t recover_data_size = skyfs_recover_data_size;
    msgp = __skyfs_get_msg(req->req_msg);

    SKYFS_ERROR_1("start %s - %d\n", __FUNCTION__, __LINE__);
    replica_id = msgp->u.replicaRecoverReq.dest_replica_id;
    recover_data_type =  msgp->u.replicaRecoverReq.data_type;
    if(recover_data_type == 1){
    	SKYFS_ERROR_1("start %s - %d, recover_partition_files, replica_id %d\n", __FUNCTION__, __LINE__, replica_id);
	    recover_partition_files(req,  replica_id, 1);
	    goto ERR_NONE;
	    return;
    } 
    if(recover_data_type == 2){

    	SKYFS_ERROR_1("start %s - %d, recover_obj_files, replica_id %d\n", __FUNCTION__, __LINE__, replica_id);
	// mayl for debug : 
	recover_obj_files(req,  replica_id, 0);
	goto ERR_NONE;
	    return;
    } 

ERR_NONE:
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type,
                    req->req_remote_id,
                    0);
    if(rc < 0){
        SKYFS_ERROR_1("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

     if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);



    return;


}

void __skyfs_SS_remote_replica_write(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;

    skyfs_io_vector_t vec;
    skyfs_s32_t client_id;
    skyfs_u32_t count;
    skyfs_u32_t replica_id;
    skyfs_u64_t offset;
    skyfs_ino_t ino;
    skyfs_s32_t rc = 0;
    skyfs_u32_t obj_size;
    skyfs_u64_t obj_id;
    struct timeval tv;


    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       partition_id;
    skyfs_u32_t       hashvalue;
    skyfs_s8_t path[SKYFS_MAX_NAME_LEN];


    //skyfs_u32_t recover_data_size = skyfs_recover_data_size;
    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.prepareWriteReq.vec), sizeof(skyfs_io_vector_t));
    client_id = msgp->u.prepareWriteReq.client_id;
    partition_id = msgp->u.prepareWriteReq.partition_id;
    kiov = req->req_iov;
    ino = msgp->u.prepareWriteReq.ino;
    offset = vec.offset;
    count = vec.count;
    replica_id = vec.replica_id;
    obj_id = vec.obj_id;
    obj_size = vec.obj_size;
    int partition_locked = 0;
    int fd = 0;
    size_t obj_offset;
    skyfs_DL_part_t * tmp_dl_part ;
    skyfs_DL_part_t  got_dl_part ;

    SKYFS_ERROR("SS_write_remote replica start , ino %lu ,offset %lu, part id %lu, replica %u\n ", ino, offset, partition_id, replica_id);
    SKYFS_MSG("%s:enter.ino %llu from %u\n\n", __FUNCTION__, ino, client_id);
    goto DO_WRITE;

    /*1.cache the iov in the writebuf hash*/
    hashvalue = __skyfs_get_obj_hashvalue(ino, client_id);
    hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
    htbp = &skyfs_writecache_htbbase[hashvalue];
    pthread_mutex_lock(&htbp->lock);
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("%s:hash table NULL,%u\n", __FUNCTION__, hashvalue);
        goto OUT;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_databuf_t, databuf_hash);
        if(tmp->ino == ino 
            && tmp->client_id == client_id
            && tmp->offset == offset
            && tmp->count == count){
            databuf = tmp;
            SKYFS_LEAVE("%s:same write exist,ino:%llu,client:%d\n", __FUNCTION__, ino, client_id);
            goto OUT;
        }
    }

OUT:
    if(databuf == NULL){
        databuf = (skyfs_O_databuf_t *)malloc(sizeof(skyfs_O_databuf_t));
        if(databuf == NULL){
            SKYFS_ERROR("%s:malloc databuf error\n", __FUNCTION__);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }
        databuf->ino = ino;
        databuf->client_id = client_id;
        databuf->offset = offset;
        databuf->count = count;
        databuf->partition_id = partition_id;
        databuf->ref = 1;
        databuf->buf = kiov->ak_addr;
        list_add(&(databuf->databuf_hash), &(skyfs_writecache_htbbase[hashvalue].head));
        SKYFS_ERROR("%s:insert write buf at %d,%p,%p\n", 
            __FUNCTION__, hashvalue,head,&(skyfs_writecache_htbbase[hashvalue].head));
        pthread_mutex_unlock(&htbp->lock);
    }else{
        /*Deal with replica data store in the same node*/
        databuf->ref ++;
        SKYFS_ERROR("%s:same write exist,ref:%d,%p\n", __FUNCTION__, databuf->ref, databuf);
        if(kiov->ak_addr){
            free(kiov->ak_addr);
        }
        pthread_mutex_unlock(&htbp->lock);
        goto ERR_NONE;
    }

DO_WRITE:
    
    SKYFS_ERROR("SS_write_remote replica start do_write start , ino %lu ,offset %lu, part id %lu, replica %u\n ", ino, offset, partition_id, replica_id);
    tmp_dl_part = __skyfs_SS_check_alloc_partition(ino, partition_id, replica_id, osd_this_id, &got_dl_part);
    if(tmp_dl_part == NULL){
	    goto ERR_WRITE;
    }else{
		     pthread_rwlock_wrlock(&tmp_dl_part->part_lock);
		     partition_locked = 1;
    }

     rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, replica_id, path);
    if(rc < 0){
            SKYFS_ERROR_1("%s:open obj dir err:%d\n", __FUNCTION__, rc);
	    rc = -1;
            goto ERR_WRITE;
        }

        //fd = open(path, O_RDWR|O_CREAT,0666);

	write_cnt ++;
	if(get_error_test_state() &&  test_write_errors <= max_errors){
		//gettimeofday(&tv, NULL);
			if(write_cnt % 20000 == 555 ){
				fd = -1;
				test_write_errors ++;
			}
	}
	if(fd == 0){
        	fd = open(path, O_RDWR|O_CREAT,0666);
	}

        if(fd < 0){
            SKYFS_ERROR_1("%s:remote open obj err:%d\n", __FUNCTION__, errno);
	    rc = -2;
            goto ERR_WRITE;
        }
	
	obj_offset = offset % obj_size;
	
        //rc = pwrite(fd, kiov->ak_addr, count, obj_offset);
	
	if(get_error_test_state() &&  test_write_errors <= max_errors){
			if(write_cnt %20000 == 245 ){
				rc = -EIO;
				SKYFS_ERROR_1("write remote rplica make error cnt %d\n", write_cnt);
				test_write_errors ++;
		}
	}
	if(rc == 0){
        	rc = pwrite(fd, kiov->ak_addr, count, obj_offset);
	}



        if(rc < 0){
            SKYFS_ERROR_1("%s:write fd:%d errno:%d, rc:%d, obj_id:%llu, replica_id:%u, write_cnt %d\n",
                __FUNCTION__, fd, errno, rc, obj_id, 1, write_cnt);
            SKYFS_ERROR_1("%s:buf:%p,count:%u,obj_offset:%u\n",
                __FUNCTION__, kiov->ak_addr, count, obj_offset);
	    rc = -3;
            goto ERR_WRITE;
        }
	// temp del the line below for for performance by mayl TODO : need optimize with other direct IO method 
    	//fdatasync(fd);

        //close(fd);

ERR_WRITE:
   
	
   if(rc < 0){

	int tmp_rc = __skyfs_DL_invalid_partition(ino, partition_id, replica_id, (uint64_t)-2, 0);

    }
   // TODO: maylshould free the data_buf in kiov->ak_addr, otherwise will cause memory leak ! 
    if(kiov->ak_addr != NULL){
	    free(kiov->ak_addr);
    }
   
    SKYFS_ERROR("SS_write_remote replica start end do_write , ino %lu ,offset %lu, part id %lu, replica %u\n ", ino, offset, partition_id, replica_id);
	

ERR_NONE:
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    msgp->opType = SKYFS_MSG_O_PREPARE_WRITE;

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    /* TODO prepare_write do syncdata after reply , for spped up the replying */
     if(fd > 0 && rc >= 0){
	        int rc1 = 0;
		int rc2 = 0;
		// TODO skip doing sync write when replica_num enough
		if(vec.replica_num < 3)
			rc1 = fdatasync(fd);
		rc2 = close(fd);
		if(rc1 != 0 || rc2 != 0){
			SKYFS_ERROR_1("do forward replica %d write data failed %d,%d  ino %llu, offset %llu, ", 
					rc1, rc1, rc2, ino, offset);
			int tmp_rc = __skyfs_DL_invalid_partition(ino, partition_id, replica_id, (uint64_t)-2, 0);
		}
		
     }

    if(tmp_dl_part != NULL && partition_locked !=0){
	pthread_rwlock_unlock(&tmp_dl_part->part_lock);
    }


    if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("SS_write_remote replica start return , ino %lu ,offset %lu, part id %lu, replica %u\n ", ino, offset, partition_id, replica_id);
    SKYFS_MSG("%s:leave.buffed %llu from %d\n\n", __FUNCTION__, ino, client_id);

}


void __skyfs_SS_prepare_write_old(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov;
    skyfs_u32_t req_size;

    skyfs_io_vector_t vec;
    skyfs_s32_t client_id;
    skyfs_u32_t count;
    skyfs_u64_t offset;
    skyfs_ino_t ino;
    skyfs_s32_t rc = 0;

    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       partition_id;
    skyfs_u32_t       hashvalue;


    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.prepareWriteReq.vec), sizeof(skyfs_io_vector_t));
    client_id = msgp->u.prepareWriteReq.client_id;
    partition_id = msgp->u.prepareWriteReq.partition_id;
    kiov = req->req_iov;
    ino = msgp->u.prepareWriteReq.ino;
    offset = vec.offset;
    count = vec.count;

    SKYFS_MSG("%s:enter.ino %llu from %u\n\n", __FUNCTION__, ino, client_id);

    /*1.cache the iov in the writebuf hash*/
    hashvalue = __skyfs_get_obj_hashvalue(ino, client_id);
    hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
    htbp = &skyfs_writecache_htbbase[hashvalue];
    pthread_mutex_lock(&htbp->lock);
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_MSG("%s:hash table NULL,%u\n", __FUNCTION__, hashvalue);
        goto OUT;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_databuf_t, databuf_hash);
        if(tmp->ino == ino 
            && tmp->client_id == client_id
            && tmp->offset == offset
            && tmp->count == count){
            databuf = tmp;
            SKYFS_LEAVE("%s:same write exist,ino:%llu,client:%d\n", __FUNCTION__, ino, client_id);
            goto OUT;
        }
    }

OUT:
    if(databuf == NULL){
        databuf = (skyfs_O_databuf_t *)malloc(sizeof(skyfs_O_databuf_t));
        if(databuf == NULL){
            SKYFS_ERROR("%s:malloc databuf error\n", __FUNCTION__);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }
        databuf->ino = ino;
        databuf->client_id = client_id;
        databuf->offset = offset;
        databuf->count = count;
        databuf->partition_id = partition_id;
        databuf->ref = 1;
        databuf->buf = kiov->ak_addr;
        list_add(&(databuf->databuf_hash), &(skyfs_writecache_htbbase[hashvalue].head));
        SKYFS_ERROR("%s:insert write buf at %d,%p,%p\n", 
            __FUNCTION__, hashvalue,head,&(skyfs_writecache_htbbase[hashvalue].head));
        pthread_mutex_unlock(&htbp->lock);
    }else{
        /*Deal with replica data store in the same node*/
        databuf->ref ++;
        SKYFS_ERROR("%s:same write exist,ref:%d,%p\n", __FUNCTION__, databuf->ref, databuf);
        if(kiov->ak_addr){
            free(kiov->ak_addr);
        }
        pthread_mutex_unlock(&htbp->lock);
        goto ERR_NONE;
    }

ERR_NONE:
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_MSG("%s:leave.buffed %llu from %d\n\n", __FUNCTION__, ino, client_id);

}

#if 0
void __skyfs_SS_do_commit_write()
{
    skyfs_io_vector_t vec;
    skyfs_u32_t client_id;
    skyfs_u32_t partition_id;
    skyfs_u32_t count;
    skyfs_u64_t offset;
    skyfs_u32_t obj_offset;
    skyfs_u32_t obj_size;
    skyfs_u64_t obj_id;
    skyfs_ino_t ino;
    skyfs_s32_t fd;
    skyfs_s32_t rc = 0;
    skyfs_u32_t release_databuf = 0;
    skyfs_u32_t replica_id;
    skyfs_s8_t path[SKYFS_MAX_NAME_LEN];

    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       hashvalue;

    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.commitWriteReq.vec), sizeof(skyfs_io_vector_t));
    client_id = msgp->u.commitWriteReq.client_id;
    partition_id = msgp->u.commitWriteReq.partition_id;
    replica_id = msgp->u.commitWriteReq.replica_id;
    ino = vec.ino;
    offset = vec.offset;
    count = vec.count;
    obj_id = vec.obj_id;
    obj_size = vec.obj_size;

    SKYFS_MSG("%s:enter,client_id:%u,obj_id:%u,offset:%u\n",
        __FUNCTION__, client_id, obj_id, offset);
    
    SKYFS_MSG("%s:count:%u,replica_id:%u\n",
        __FUNCTION__, count, replica_id);


    /*1.find the iov in the writebuf hash*/
    hashvalue = __skyfs_get_obj_hashvalue(ino, client_id);
    hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
    htbp = &skyfs_writecache_htbbase[hashvalue];
    pthread_mutex_lock(&htbp->lock);
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_ERROR("%s:hash table NULL,hashvalue%d\n", __FUNCTION__,hashvalue);
        pthread_mutex_unlock(&htbp->lock);
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_databuf_t, databuf_hash);
        if(tmp->ino == ino 
            && tmp->client_id == client_id
            && tmp->offset == offset
            && tmp->count == count){
            databuf = tmp;
            SKYFS_LEAVE("%s:writebuf exist,ino:%llu,ref:%d,%p\n", 
                __FUNCTION__, ino, databuf->ref, databuf);
            goto OUT;
        }
    }

    /*2.write the iov into the required file*/
OUT:
    if(databuf != NULL){
        //pthread_mutex_lock(&osd_commit_write_lock);
        databuf->ref --;
        if(databuf->ref == 0){
            list_del_init(&(databuf->databuf_hash));
            release_databuf = 1;
        }
        
        rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, replica_id, path);
        if(rc < 0){
            SKYFS_ERROR("%s:open obj dir err:%d\n", __FUNCTION__, rc);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }

        fd = open(path, O_RDWR|O_CREAT,0666);
        if(fd < 0){
            SKYFS_ERROR("%s:open obj err:%d\n", __FUNCTION__, errno);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }

        obj_offset = offset % obj_size;
		#if 0
        rc = pwrite(fd, databuf->buf, count, obj_offset);
        if(rc < 0){
               SKYFS_ERROR("%s:write fd:%d errno:%d, rc:%d, obj_id:%llu, replica_id:%u",
                __FUNCTION__, fd, errno, rc, obj_id, replica_id);
            SKYFS_ERROR("%s:buf:%p,count:%u,obj_offset:%u\n",
                __FUNCTION__, databuf->buf, count, obj_offset);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }
		#endif
		rc = count;

        close(fd);
        //pthread_mutex_unlock(&osd_commit_write_lock);
        pthread_mutex_unlock(&htbp->lock);

           SKYFS_ERROR("%s:write fd:%d rc:%d\n", __FUNCTION__, fd, rc);
    }else{
        SKYFS_ERROR("%s:writebuf not exist\n", __FUNCTION__);
        pthread_mutex_unlock(&htbp->lock);
        goto ERR;
    }

ERR:
    if(release_databuf){
        free(databuf->buf);
        free(databuf);
    }

    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(fd){
        SKYFS_MSG("%s:close:%d\n", __FUNCTION__, fd);
    //    close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_MSG("%s:leave.write %llu %d\n", __FUNCTION__, ino, rc);

}
#endif

void __skyfs_SS_commit_write(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    //skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    //skyfs_u32_t req_id;

    skyfs_io_vector_t vec;
     skyfs_dl_dest_t  des;
    skyfs_u32_t client_id;
    skyfs_u32_t partition_id;
    skyfs_u32_t count;
    skyfs_u64_t offset;
    skyfs_u32_t obj_offset;
    skyfs_u32_t obj_size;
    skyfs_u64_t obj_id;
    skyfs_ino_t ino;
    skyfs_s32_t fd = 0;
    skyfs_s32_t rc = 0;
    skyfs_u32_t release_databuf = 0;
    skyfs_u32_t replica_id;
    skyfs_u32_t replica_cnt;
    skyfs_u32_t is_new_partition = 0;;
    skyfs_s8_t path[SKYFS_MAX_NAME_LEN];

    skyfs_htb_t       *htbp = NULL;
    struct list_head  *head = NULL, *index = NULL;
    skyfs_O_databuf_t *databuf = NULL, *tmp = NULL;
    skyfs_u32_t       hashvalue;

    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.commitWriteReq.vec), sizeof(skyfs_io_vector_t));
    memcpy(&des, &(msgp->u.commitWriteReq.des),sizeof(skyfs_dl_dest_t));
    client_id = msgp->u.commitWriteReq.client_id;
    partition_id = msgp->u.commitWriteReq.partition_id;
    replica_id = msgp->u.commitWriteReq.replica_id;
    if(replica_id & (1ul<<31)){
	    // need to create a new partition files. and insert it into hash value;
	    replica_id &= 0xffffffff;
	    is_new_partition = 1;
    }
    replica_cnt = msgp->u.commitWriteReq.replica_cnt;
    ino = vec.ino;
    offset = vec.offset;
    count = vec.count;
    obj_id = vec.obj_id;
    obj_size = vec.obj_size;

    if(offset == 0){
    	SKYFS_ERROR_1("%s:enter,client_id:%u,obj_id:%llu,offset:%llu, ino %llx\n",
        	__FUNCTION__, client_id, obj_id, offset, ino);
    }
    
    SKYFS_MSG("%s:count:%u,replica_id:%u\n",
        __FUNCTION__, count, replica_id);


    /*1.find the iov in the writebuf hash*/
    hashvalue = __skyfs_get_obj_hashvalue(ino, client_id);
    hashvalue = hashvalue % SKYFS_DLSUBSET_HASH_LEN;
    htbp = &skyfs_writecache_htbbase[hashvalue];
    pthread_mutex_lock(&htbp->lock);
    head = &(htbp->head);
    if(list_empty(head)){
        SKYFS_ERROR("%s:hash table NULL,hashvalue%d\n", __FUNCTION__,hashvalue);
        pthread_mutex_unlock(&htbp->lock);
        goto ERR;
    }

    list_for_each(index, head){
        tmp = list_entry(index, skyfs_O_databuf_t, databuf_hash);
        if(tmp->ino == ino 
            && tmp->client_id == client_id
            && tmp->offset == offset
            && tmp->count == count){
            databuf = tmp;
            SKYFS_LEAVE("%s:writebuf exist,ino:%llu,ref:%d,%p\n", 
                __FUNCTION__, ino, databuf->ref, databuf);
            goto OUT;
        }
    }

    /*2.write the iov into the required file*/
OUT:
    if(databuf != NULL){
        //pthread_mutex_lock(&osd_commit_write_lock);
        databuf->ref --;
        if(databuf->ref == 0){
            list_del_init(&(databuf->databuf_hash));
            release_databuf = 1;
        }
        
        rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, replica_id, path);
        if(rc < 0){
            SKYFS_ERROR("%s:open obj dir err:%d\n", __FUNCTION__, rc);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }

        fd = open(path, O_RDWR|O_CREAT,0666);
        if(fd < 0){
            SKYFS_ERROR("%s:open obj err:%d\n", __FUNCTION__, errno);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }

        obj_offset = offset % obj_size;
        rc = pwrite(fd, databuf->buf, count, obj_offset);
        if(rc < 0){
               SKYFS_ERROR("%s:write fd:%d errno:%d, rc:%d, obj_id:%llu, replica_id:%u",
                __FUNCTION__, fd, errno, rc, obj_id, replica_id);
            SKYFS_ERROR("%s:buf:%p,count:%u,obj_offset:%u\n",
                __FUNCTION__, databuf->buf, count, obj_offset);
            pthread_mutex_unlock(&htbp->lock);
            goto ERR;
        }

        close(fd);
		/* TODO : mayl need add dl_file for replica to the dl hash here 
		*  add write the dl subset at same time */
		
        //pthread_mutex_unlock(&osd_commit_write_lock);
        pthread_mutex_unlock(&htbp->lock);
	if(1){
		//TODO : create or update new partiton files in this node
		SKYFS_ERROR("create new partition files in this replica %d\n", replica_id);

		rc = skyfs_commit_update_patition(ino, partition_id, replica_id, replica_cnt);
		if(rc <0){
			SKYFS_ERROR_1("create new partition files in this replica %d failed\n", replica_id);
			goto ERR;
		}	
	}

           SKYFS_ERROR("%s:write fd:%d rc:%d\n", __FUNCTION__, fd, rc);
    }else{
        SKYFS_ERROR("%s:writebuf not exist\n", __FUNCTION__);
        pthread_mutex_unlock(&htbp->lock);
	rc = -ENOMEM;
        goto ERR;
    }

ERR:
    if(release_databuf){
        free(databuf->buf);
        free(databuf);
    }

    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(fd){
        SKYFS_MSG("%s:close:%d\n", __FUNCTION__, fd);
    //    close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                       req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR_1("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(req->req_iov){
        free(req->req_iov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_MSG("%s:leave.write %llu %d\n", __FUNCTION__, ino, rc);

}

skyfs_s32_t 
__skyfs_SS_do_local_write(skyfs_ino_t ino, 
    amp_kiov_t *kiov, 
    //skyfs_dl_dest_t *des,
    skyfs_u32_t replica_id,
    skyfs_io_vector_t *vec,
    skyfs_u32_t partition_id,
    skyfs_u32_t is_new_partition,
    skyfs_u32_t * pfd)
{
	skyfs_u64_t obj_id = 0;
	//skyfs_ino_t ino;
    	skyfs_s32_t rc = 0;
    	//skyfs_s32_t base_file = 0;
	skyfs_s32_t fd = 0;
	skyfs_u32_t obj_size;

	skyfs_u32_t alignment = 0x200;
	skyfs_u32_t obj_offset;
	skyfs_u32_t count;
	skyfs_u64_t offset;
	skyfs_s8_t  path[SKYFS_MAX_NAME_LEN];
	int do_replica = 0;
	int do_commit = 0;
	struct timeval tv;
	
	obj_id = vec->obj_id;
	offset = vec->offset;
	count = vec->count;
	obj_size = vec->obj_size;
	rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, 1, path);
	
	if(rc < 0){
            SKYFS_ERROR_1("%s:open obj dir err:%d\n", __FUNCTION__, rc);
	    rc = -1;
            goto ERR;
        }

        //fd = open(path, O_RDWR|O_CREAT,0666);

	write_cnt++;
	if(get_error_test_state() &&  test_write_errors <= max_errors){
		if(write_cnt %20000 == 234 ){
			fd = -1;
			SKYFS_ERROR_1("make local write open failed, cnt %d\n ", write_cnt);
			test_write_errors ++;
		}

	}
	if (fd == 0){
	    int mask = alignment-1;
	    size_t test_count = count;
	    size_t test_offset = offset;
	    uint64_t test_addr = (uint64_t)kiov->ak_addr;
	    if(vec->fcount >0)
		    test_count = vec->fcount;
	    if((test_count & mask) ||(test_offset & mask) || (test_addr & mask) ){
            	fd = open(path, O_RDWR|O_CREAT,0666);
	    }else{
            	fd = open(path, O_RDWR|O_CREAT|O_DIRECT,0666);

	    }

	}
        if(fd < 0){
            SKYFS_ERROR_1("%s:open obj err:%d\n", __FUNCTION__, errno);
	    rc = -2;
            goto ERR;
        }
	
	obj_offset = offset % obj_size;
	
        //rc = pwrite(fd, kiov->ak_addr, count, obj_offset);
	if(get_error_test_state() &&  test_write_errors <= max_errors){
		if(write_cnt %20000 == 225 ){
			rc = -EIO;
			test_write_errors ++;
		}
	}
	
	if(rc == 0){

		if(vec->fcount >0){
			size_t vcount = vec->fcount;
			//SKYFS_ERROR_1("TRY TO DO local pwrite , fcount %lu, size %lu\n", vec->fcount, obj_offset );
        		rc = pwrite(fd, kiov->ak_addr, vcount, obj_offset);
			//SKYFS_ERROR_1(" DO local pwrite , fcount %lu, size %lu, ret %d, fd %d\n", vec->fcount, obj_offset, rc, fd );
		}
		else{
        		rc = pwrite(fd, kiov->ak_addr, count, obj_offset);
		}
	}

        if(rc <=  0){
            SKYFS_ERROR_1("%s:write fd:%d errno:%d, rc:%d, obj_id:%llu, replica_id:%u, path %s\n",
                __FUNCTION__, fd, errno, rc, obj_id, 1, path);
            SKYFS_ERROR_1("%s:buf:%p,count:%u,obj_offset:%u\n",
                __FUNCTION__, kiov->ak_addr, count, obj_offset);
	    rc = -3;
            goto ERR;
        }
	// temp del the line below for for performance by mayl TODO : need optimize with other direct IO method 
    	//fdatasync(fd);

        //close(fd);

ERR:    if(fd > 0 && rc >0 ){
		//int rc1 = fdatasync(fd);
		//if(rc1 != 0)
		//	rc = -4;
		//close(fd);
		// TODO record this fd, file will be flush and close after all replica written
		*pfd = fd; 
	}

	SKYFS_ERROR("ss_do_local_write return %d, fcount %d, count %d\n", rc, vec->fcount, count);

	return rc;


}


/* if is_new_partition we need to send requests to all replica osd nodes for this partition
 * so we can keep partition replica */
skyfs_s32_t 
__skyfs_SS_do_write(skyfs_DL_file_t *dl_file, 
    amp_kiov_t *kiov, 
    skyfs_dl_dest_t *des,
    skyfs_io_vector_t *vec,
    skyfs_u32_t partition_id,
    skyfs_u32_t is_new_partition
    )
{
	skyfs_u64_t obj_id = 0;
	skyfs_ino_t ino;
    	skyfs_s32_t rc = 0;
    	skyfs_s32_t base_file = 0;
	skyfs_s32_t fd = 0;
	skyfs_u32_t obj_size;
	skyfs_u32_t obj_offset;
	skyfs_u32_t count;
	skyfs_u64_t offset;
	skyfs_s8_t  path[SKYFS_MAX_NAME_LEN];
	int do_replica = 0;
	int do_commit = 0;
	char test_str[128];

    SKYFS_ERROR("%s:enter. rc:%d\n", __FUNCTION__, rc);    

	ino = dl_file->ino;
	obj_id = vec->obj_id;
	offset = vec->offset;
	count = vec->count;
	obj_size = dl_file->obj_size;
	/* TODO temp added by mayl , just for performance , do not support dl split!  */

	if(skyfs_replica == 1){
		//des->replica_location[1] = osd_this_id;
	}

	if((vec->page_idx &0xfbfb) == 0xfbfb)
		base_file = 1;
	des->max_write_version ++;
	if((skyfs_replica == 1) && (des->replica_location[1] == osd_this_id)){
		rc = __skyfs_SS_prepareopen_objfile(ino, partition_id, obj_id, 1, path);
        if(rc < 0){
            SKYFS_ERROR_1("%s:open obj dir err:%d\n", __FUNCTION__, rc);
            goto ERR;
        }

        fd = open(path, O_RDWR|O_CREAT,0666);
        if(fd < 0){
            SKYFS_ERROR_1("%s:open obj err:%d\n", __FUNCTION__, errno);
            goto ERR;
        }

        obj_offset = offset % obj_size;
	
        rc = pwrite(fd, kiov->ak_addr, count, obj_offset);
        if(rc < 0){
            SKYFS_ERROR_1("%s:write fd:%d errno:%d, rc:%d, obj_id:%llu, replica_id:%u",
                __FUNCTION__, fd, errno, rc, obj_id, 1);
            SKYFS_ERROR_1("%s:buf:%p,count:%u,obj_offset:%u\n",
                __FUNCTION__, kiov->ak_addr, count, obj_offset);
            goto ERR;
        }
	// temp del the line below for for performance by mayl TODO : need optimize with other direct IO method 
    	//fdatasync(fd);

        close(fd);
	des->write_version[1]++;
	//dl_file->write_version++;
	// mayl TODO: need wrire dl_file back to disk
	
#if 0
	memset(test_str, 0, 128);
	if(base_file != 0 && !memcmp(kiov->ak_addr, test_str, 128) && count == 32768){
#endif
	if(0){
		SKYFS_ERROR_1("find ZERO buf, ino %llu, offset %lu \n", ino, offset, count);
	}
	}else if(skyfs_replica >= 1){
    	/*1. transfer data to all replica servers, including this node self!
     	*2. commit write */
    	rc = __skyfs_O2O_prepare_write(dl_file, kiov, des, vec, partition_id);
	do_replica = 1;
    	if(rc < 0 ){
       		SKYFS_ERROR_1("%s:prepare write err.rc:%d\n", __FUNCTION__, rc);
        	goto EXIT;
    	}
    	rc = __skyfs_O2O_commit_write(dl_file, des, vec, partition_id, is_new_partition);
	do_commit = 1;
    	if(rc < 0){
        	SKYFS_ERROR_1("%s:commit write err.rc:%d\n", __FUNCTION__, rc);
        	goto EXIT;
    	}
	}
ERR:
EXIT:
    SKYFS_MSG("%s:exit. rc:%d\n", __FUNCTION__, rc);
    if(count !=0 && rc <= 0 || offset == 0 || do_replica != 0 || base_file != 0){
	    SKYFS_ERROR_1("SS_do_write ret %d, count %lu, offset %lu, ino %llu , this_osd_id %d  , replica_1_osdid %d , obj_offset %lu, do_replica %d \n", 
			    rc, count, offset, ino, osd_this_id, des->replica_location[1], obj_offset, do_replica);
    }

    return rc;
}


// TODO 2026  by mayl
void __skyfs_SS_write_multi_objs(amp_request_t *req)
{

    amp_kiov_t  kiov ;
    amp_kiov_t * ori_kiov ;
    skyfs_msg_t *ori_msgp = NULL;
    skyfs_msg_t *msgp = NULL;
    amp_request_t * tmp_req = NULL;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;
    skyfs_ino_t ino;
    int rc = 0;
    int total_rc = 0;
    int error = 0;
    int call_error = 0; 
    int calling_cnt = 0;
    int vec_idx = 0;
    //uint64_t obj_id;
    //uint64_t partition_id;
    
    skyfs_u32_t       subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_multi_io_vector_t * multi_vec ;
    skyfs_io_vector_t *  tmp_io_vec ;
    int64_t space_changed = 0;
    SKYFS_ERROR("enter SS_write_multi\n");

    ori_kiov = req->req_iov;

	// 1 parse req and prepare a new amp_request_t for calling skyfs_SS_write
		
    	ori_msgp = __skyfs_get_msg(req->req_msg);
	multi_vec = &ori_msgp->u.Multi_writeObjReq.multi_vec;
    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_t);    
	
    	req_type = ori_msgp->fromType;
    	req_id = ori_msgp->fromid;
	rc = __amp_alloc_request(&tmp_req);

    	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_t);    
    	tmp_req->req_msg = (amp_message_t *)malloc(req_size);
	if(tmp_req->req_msg == NULL){
		SKYFS_ERROR_1("can not alloc req_msg for multiple write obj\n");
		goto RETURN;
	}


	
    SKYFS_ERROR("build req  SS_write_single\n");
	// 2 check the ori request, get the calling count;
	//msgp = tmp_req->req_msg; 
	bzero(tmp_req->req_msg, req_size);
         
    	SKYFS_INIT_MSG(msgp, tmp_req, SKYFS_FSID, SKYFS_MSG_O_WRITE_OBJ,
        	osd_this_id, SKYFS_CLIENT, req_size);

	tmp_io_vec = &msgp->u.writeObjReq.vec;
	msgp->type = SKYFS_MSG_O_WRITE_MULTI_OBJ;
	msgp->fromid = ori_msgp->fromid;
	msgp->u.writeObjReq.dest = ori_msgp->u.Multi_writeObjReq.dest;
	tmp_io_vec->offset = multi_vec->offset[0];
	tmp_io_vec->count = multi_vec->count[0];
	tmp_io_vec->foffset = multi_vec->foffset[0];
	tmp_io_vec->fcount = multi_vec->fcount[0];
	// init tmp_io_vec
	calling_cnt = multi_vec->vector_cnt;
	tmp_io_vec->page_idx = multi_vec->page_idx;
	tmp_io_vec->ino = multi_vec->ino;
	tmp_io_vec->obj_id = multi_vec->obj_id;
	tmp_io_vec->algorithm = multi_vec->algorithm;
	tmp_io_vec->replica_num = multi_vec->replica_num;
	tmp_io_vec->replica_id = multi_vec->replica_id;
	tmp_io_vec->partition_id = multi_vec->partition_id;
	tmp_io_vec->obj_size = multi_vec->obj_size;
	tmp_io_vec->direct_op = multi_vec->direct_op;
	tmp_io_vec->forward_count = multi_vec->forward_count;
	// init kiov
	tmp_req->req_iov = &kiov;
	tmp_req->req_niov = 1;
	kiov.ak_addr = req->req_iov->ak_addr; 
	kiov.ak_len = multi_vec->fcount[0];
	kiov.ak_offset = 0;
	kiov.ak_flag = 0;
    	tmp_req->req_iov = &kiov;
	// first calling
         __skyfs_SS_write(tmp_req);
	 rc = msgp->error;
	 if(rc > 0){
		 ori_msgp->u.Multi_writeObjAck.ret_vals[0] = rc;
		 total_rc += rc;
		 space_changed += msgp->u.writeObjAck.space_changed;
		 //ori_msgp->u.Multi_writeObjAck.space_changed  += msgp->u.writeObjAck.space_changed;

    		//SKYFS_ERROR_1("build req  SS_write_single, after first calling, rc = %d, type = %d, \n", rc, msgp->type);
	 }else{
		 SKYFS_ERROR_1("multiple writeobj faile at call %d, ret %d\n", 0, rc);
		 error = rc;
	 }

	 char * tmp_ak_addr;
	// 3 call __skyfs_SS_write for next other vectors
	 for(int n = 1 ; n< calling_cnt; n++){

	    	 
	    msgp->type = SKYFS_MSG_O_WRITE_MULTI_OBJ;
	    msgp->fromid = ori_msgp->fromid;
	    msgp->u.writeObjReq.dest = ori_msgp->u.Multi_writeObjReq.dest;
	    tmp_io_vec->offset = multi_vec->offset[n];
	    tmp_io_vec->count = multi_vec->count[n];
	    tmp_io_vec->foffset = multi_vec->foffset[n];
	    tmp_io_vec->fcount = multi_vec->fcount[n];
	    tmp_req->req_iov = &kiov;
	    tmp_req->req_niov = 1;
	    tmp_ak_addr  = req->req_iov->ak_addr;
	    tmp_ak_addr = &tmp_ak_addr[multi_vec->fcount[n-1]];
	    kiov.ak_addr = tmp_ak_addr;
	     
	    kiov.ak_len = multi_vec->fcount[n];
	    kiov.ak_offset = 0;
	    kiov.ak_flag = 0;
    	    tmp_req->req_iov = &kiov;

	   __skyfs_SS_write(tmp_req);
	   rc = msgp->error;
	   if(rc > 0){
		 ori_msgp->u.Multi_writeObjAck.ret_vals[n] = rc;
		 total_rc += rc;
		 space_changed += msgp->u.writeObjAck.space_changed;

		 //ori_msgp->u.Multi_writeObjAck.space_changed  += msgp->u.writeObjAck.space_changed;

	   } else{
		 SKYFS_ERROR_1("multiple writeobj faile at call %d, ret %d\n", n, rc);
		 if(rc <0)
		 	error = rc;
		 else
			 error = -ENOENT;
	    }


	 }

    SKYFS_ERROR("build req  SS_write callings end, total_rc %lu, space_changed %lu,  run count %d\n", total_rc, space_changed, calling_cnt);
	 // return amp multiple writeobj reply 
		 
RETURN:
	req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_multi_writeobj_ack_t);
        __skyfs_SS_init_reply(&req, &ori_msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    if(error  <0){
    	ori_msgp->error = error;
    }else{
    	ori_msgp->error = total_rc;
    }
    ori_msgp->fromid = osd_this_id;
    ori_msgp->fromType= SKYFS_OSD;
    if(rc > 0){
        msgp->u.writeObjAck.dest = osd_this_id;
        msgp->u.writeObjAck.subset = subset_id;
        msgp->u.writeObjAck.chunk = chunk_id;
    }
    
    SKYFS_ERROR("ORI error %d\n", ori_msgp->error);   
    ori_msgp->u.Multi_writeObjAck.space_changed = space_changed;
    rc = amp_send_sync(osd_comp_context, req,
        req_type,
        req_id,
        0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_multi_write:send failed.rc:%d\n", rc);
    }
    

    // free req && tmp_req
    if(tmp_req->req_msg)
	    free(tmp_req->req_msg);

    
    __amp_free_request(tmp_req);

    if(ori_kiov->ak_addr)
	    free(ori_kiov->ak_addr);
    if(ori_kiov)
	    free(ori_kiov);
    if(req->req_reply)
	    free(req->req_reply);
    if(req->req_msg)
	    free(req->req_msg);

    __amp_free_request(req);



	return;
}

void __skyfs_SS_write(amp_request_t *req) {
    //amp_request_t *req_replica2 = NULL;
    //amp_request_t *req_replica3 = NULL;
    //skyfs_msg_t *msgp_replica2 = NULL;
    //skyfs_msg_t *msgp_replica3 = NULL;
    int need_writeback_dl = 0;
    int skip_amp_reply = 0;

    int32_t max_length = 0;
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  *kiov = NULL;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;
    skyfs_u32_t is_new_partition = 0;

    skyfs_u32_t osd_id;
    skyfs_u32_t base_file = 0;
    skyfs_u32_t do_write = 0;
    skyfs_u32_t       obj_size;
    skyfs_u32_t       replica_id;

    skyfs_io_vector_t vec;
    skyfs_u32_t       subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    skyfs_u64_t       offset;
    skyfs_u64_t       file_offset;
    skyfs_s64_t       used_size_changed = 0;
    skyfs_u32_t       count;

    skyfs_s32_t       partition_id;
    skyfs_pending_io_t this_pending_io;

    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file = NULL;

    //skyfs_s8_t        chunkfile[SKYFS_MAX_NAME_LEN];
    //skyfs_s32_t       fd = 0;

    skyfs_u32_t       ino_hash;
    skyfs_u32_t       tmp_subset_id;
    skyfs_dl_dest_t   des;
    skyfs_u32_t       replica_num;
    skyfs_u32_t       osd_gid;
    skyfs_u32_t       cur_osd_id;

    skyfs_u32_t       create_dlfile_flag = 0;
    skyfs_s32_t       rc = 0;
    skyfs_s32_t       local_write_fd = 0;
    skyfs_s32_t       rc1 = 0;
    skyfs_s32_t       rc2 = 0;
    skyfs_s32_t       need_unlock = 0;
    skyfs_u64_t       check_data1 = 0;
    skyfs_u64_t       check_data2 = 0;
    skyfs_u32_t       part_obj_id;
    struct timeval    tv_write_start;
    uint64_t write_start_time;
   
    //skyfs_s32_t       rc0 = 0;

    //skyfs_u32_t       time_total = 0;
    //skyfs_f64_t       write_throughput = 0;
    skyfs_timespec_t  start_time;
    skyfs_timespec_t  end_time;
    skyfs_u64_t       timeusec;

    int input_algorithm = 0;
    int test_net_bw = 0;


    //skyfs_u32_t       tmp;
    
     gettimeofday(&tv_write_start, NULL);
     write_start_time = tv_write_start.tv_sec * 1000000 + tv_write_start.tv_usec;
    if(rwrite_cnt < 100){
#if 0
            uint64_t recv_time = req->req_recv_time;
            SKYFS_ERROR_1("osd write req %p , req_recv_time %llu, start process time %llu\n ",
                            req, recv_time, tv_write_start.tv_sec*1000000+tv_write_start.tv_usec);
#endif
    }
    rwrite_cnt++;


    msgp = __skyfs_get_msg(req->req_msg);
    if(msgp->type == SKYFS_MSG_O_WRITE_MULTI_OBJ){
	    msgp->type = SKYFS_MSG_O_WRITE_OBJ;
	    skip_amp_reply = 1;

    }
    memcpy(&vec, &(msgp->u.writeObjReq.vec), sizeof(skyfs_io_vector_t));
    check_data1 = msgp->u.writeObjReq.check_data1;
    check_data2 = msgp->u.writeObjReq.check_data2;
    kiov = req->req_iov;
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = vec.ino;
    obj_id = vec.obj_id;
    offset = vec.offset;
    count = vec.count;
    input_algorithm = vec.algorithm & 0x0ffffff;
    file_offset = offset + obj_id * SKYFS_OBJECT_SIZE;
    part_obj_id = obj_id % SKYFS_MAX_OBJ_PER_PART;

    replica_num = vec.replica_num;
    if(vec.page_idx & 0xfbfb == 0xfbfb){
	    base_file = 1;
    }

    obj_size = vec.obj_size;
    partition_id = vec.partition_id;
    replica_id = vec.replica_id;

    SKYFS_ERROR("SS_WRITE in ino %lu ,partition %d, replica_id %u\n", ino, partition_id, replica_id);
    if(req->req_remote_type == SKYFS_OSD && (vec.direct_op & 0x01) ){
        offset = vec.offset;
        obj_id = vec.obj_id;
        obj_size = vec.obj_size;
        partition_id = vec.partition_id;
        replica_id = vec.replica_id;
	if(vec.forward_count >= replica_num){
		    SKYFS_ERROR_1("all-replica write failed in ino %lu ,partition %lu", ino, partition_id);
		    need_unlock = 0;
		    rc = -EIO;
		    goto ERR;
	 }
	 
	
        //goto ReadyToRead;
    }

    if( input_algorithm == 0 && test_net_bw == 1){
	    rc = count;
	    used_size_changed = count;
	    goto SEND_REPLY;
    }

    //if(test_intervalTree == NULL){
//	    test_intervalTree = get_IntervalTree_handle();
  //  }


    /* new steps start*/
    
    /* 1 try to do_write in all replica , including local */
    /* 1.1 try verify replica_id ,  */
    osd_gid = find_osdgid(osd_num, ino, obj_id);
    cur_osd_id =  find_replica_osd(osd_num, osd_gid, replica_id , replica_num);

    SKYFS_ERROR("SS_WRITE in ino %lu ,partition %lu, replica_id %lu, osd_gid %lu, osd_id %u, this_id %d, obj_id %d , offset %lu, count %u  \n", 
		    ino, partition_id, replica_id, osd_gid, cur_osd_id, osd_this_id,
	            obj_id, offset, count ) ;
    if(cur_osd_id != osd_this_id){
	if(vec.forward_count >= replica_num){
		    SKYFS_ERROR_1("all-replica normal write failed in ino %lu ,partition %lu", ino, partition_id);
		    need_unlock = 0;
		    rc = -EIO;
		    goto ERR;
	 }else{
		 SKYFS_ERROR_1("forward write request to osd %d, ino:%llu,prefered replica  %d not here %d \n", cur_osd_id, ino,replica_id, osd_this_id, osd_this_id );
        	 msgp->u.writeObjReq.vec.forward_count++;
		  __skyfs_SS_forward_request(req, SKYFS_OSD, cur_osd_id);
		 need_unlock = 0;
        	 goto ERR_NONE;
	 }

    }else{
	    int invalid_version = 0;
	    //uint32_t local_write_length = 0;
	     skyfs_DL_part_t* tmp_dl_part = NULL;
	     skyfs_DL_part_t got_dl_part ;
	    /* 1.2 check partition for this reeplica */

    	     //SKYFS_ERROR_1("SS_WRITE check alloc_partition in ino %lu ,partition %lu, obj_id %lu , offset %lu ,replica_id %lu\n", ino, partition_id, 
	//		     obj_id, offset,replica_id);
	     tmp_dl_part = __skyfs_SS_check_alloc_partition(ino, partition_id, replica_id, cur_osd_id, &got_dl_part);
    	     //SKYFS_ERROR("SS_WRITE check post alloc_partition in ino %lu ,partition %lu, replica_id %lu, dl_part %p \n", 
	//		     ino, partition_id, replica_id, tmp_dl_part);
	     if(tmp_dl_part != NULL){
		     pthread_rwlock_wrlock(&tmp_dl_part->part_lock);
	     }

	    /* 1.3 fill des for do_write() */
	     for(int replica_idx = 1 ; replica_idx <= replica_num; replica_idx++){
		     
		     //SKYFS_ERROR_1("SS_write replica_idx %d, replica_id %d", replica_idx, replica_id);
		     if(tmp_dl_part == NULL && replica_idx == replica_id){
			     rc = -ENOENT;
			     // try other replica
			     SKYFS_ERROR_1("can not alloc/get partition for replica %u, ino %lu\n", replica_idx , ino);
			     continue;
		     }else if (replica_idx == replica_id){
			     /*1.3.1 write in local */
			     if(tmp_dl_part->replica_write_version != tmp_dl_part->max_write_version){
			     	//SKYFS_ERROR_1(" partition for replica %u, ino %lu, invalid write version %ld\n", 
				//		replica_idx , ino, (tmp_dl_part->replica_write_version);


			     	     SKYFS_ERROR_1(" partition for replica %u, ino %lu, invalid write version %d\n", replica_idx ,ino,
						     tmp_dl_part->replica_write_version );
				     continue;
			     }
			     local_write_fd = -1;
			     SKYFS_ERROR("replica_id equal , try to do local_write, compress_type %x \n", vec.algorithm);

			     // need not try interval tree if compress algorithm is COMPRESS_NONE
			     if(!strcmp(skyfs_xattr_values[0],"ZSTD")){
			     //if(!strcmp(skyfs_xattr_values[0],"ZSTD") && (vec.algorithm != 0)){
				     // find the handle and set entry to it   -- by mayl
				   struct IntervalTree *  this_intervalTree = tmp_dl_part->interval_tree_handles[part_obj_id];
				     if(this_intervalTree != NULL){
					     struct interval inte;
					     // low, high range in total file, align node_size
					     inte.low = ((obj_id*SKYFS_OBJECT_SIZE + offset)/SKYFS_OBJECT_NODE_SIZE)*SKYFS_OBJECT_NODE_SIZE;
					     inte.high =  inte.low + SKYFS_OBJECT_NODE_SIZE -1;
					     // data low, high in this object
					     inte.data_low =  offset % SKYFS_OBJECT_SIZE; // tobe change to dense mode 
					     inte.data_high = inte.data_low + count -1;
					     // compress low, high in this object
				             inte.comp_low =  offset % SKYFS_OBJECT_SIZE;
					     inte.comp_high = inte.comp_low + vec.fcount-1;
					     inte.obj_id = obj_id;



			     		     SKYFS_ERROR(" try to do local_write compressed , ino %lu, obj_id %lu, offset %lu, size %lu, comp_size %lu, tree %p, comp_low %lu, comp_high , %lu, algorithm %x\n",
							     ino, obj_id, offset, count, vec.fcount,  this_intervalTree, inte.comp_low, inte.comp_high, vec.algorithm );
				             struct timeval tv_lk ;
					     uint64_t look_time = 0;
					     //gettimeofday(&tv_lk, NULL);
					     look_time = tv_lk.tv_sec * 1000000+ tv_lk.tv_usec;
					
					     struct IntervalTNode * this_interval =  do_search_exact_interval(this_intervalTree,inte);
					     //gettimeofday(&tv_lk, NULL);
					     look_time = (tv_lk.tv_sec * 1000000+ tv_lk.tv_usec) - look_time;
                                              tree_lookup_cnt ++;
					      tree_lookup_time += look_time;
                                              if(tree_lookup_cnt %200 == 0){
						  //SKYFS_ERROR_1("inter tree lookup cnt %llu, time %llu us\n", tree_lookup_cnt, tree_lookup_time);
					      }
					     
					     if( this_interval == NULL){
						  //  insert new node
						  //  set algorithm

						  //SKYFS_ERROR_1("WRITE new interval \n");
						  inte.flag = vec.algorithm & 0x0ffffff;
						  do_IntervalT_Insert( this_intervalTree, inte);

						  // write the compress data .....
						   // vec.count = ......
						   rc = __skyfs_SS_do_local_write( ino,kiov,replica_id, &vec, partition_id, 0, &local_write_fd);
						   if(rc <= 0){
							   // remove this interval node
						    	SKYFS_ERROR_1("write Error, remove the intervalTNode at offset %lu\n ", inte.low);
							do_IntervalT_Delete( this_intervalTree, this_interval);
						   }
						   if(rc>0){
							   used_size_changed += rc;
						   }
						   goto after_write;

					     }else if(this_interval->inte.data_low == inte.data_low 
						&& this_interval->inte.data_high == inte.data_high ){
						     SKYFS_ERROR("WRITE replace interval\n");
						  long long ori_comp_len = this_interval->inte.comp_high - this_interval->inte.comp_low+1;
					
						 // original interval  matches to new interval, repleace it ..... 

						  //  set algorithm
						  inte.flag = vec.algorithm & 0x0ffffff;
						  do_replace_exact_interval(this_intervalTree, inte);
						  
						rc = __skyfs_SS_do_local_write( ino,kiov,replica_id, &vec, partition_id, 0, &local_write_fd);
                                                   if(rc <= 0){
                                                           // remove this interval no9e
                                                        SKYFS_ERROR_1("write Error after replace inte, remove the intervalTNode at offset %lu\n ", inte.low);
                                                        do_IntervalT_Delete( this_intervalTree, this_interval);
							used_size_changed -= ori_comp_len;

                                                   }else{
							used_size_changed += rc;
							used_size_changed -= ori_comp_len;
						   }
                                                  goto after_write;


					        // offset and length match , just replace it.

					     }else if(((vec.algorithm & 0x0ffffff) == 0) && this_interval->inte.flag == 0
							     && inte.obj_id == this_interval->inte.obj_id ){
						     // both no compress, just do overwrite
						     // TODO: how to replace?
						     uint64_t new_data_low = inte.data_low;
						     uint64_t new_data_high = inte.data_high;
						     uint64_t new_comp_low = inte.comp_low;
						     uint64_t new_comp_high = inte.comp_high;
						     no_comp_over_write++;
						     if(no_comp_over_write <3){
							     SKYFS_ERROR_1("NOcomp overwrite\n");
						     }

						     long long ori_comp_len = this_interval->inte.comp_high - this_interval->inte.comp_low+1;

						     if( this_interval->inte.data_low > new_data_low){
							      this_interval->inte.data_low = new_data_low;
						     }

						     if( this_interval->inte.data_high < new_data_high){
							      this_interval->inte.data_high = new_data_high;
						     }

						    if( this_interval->inte.comp_low > new_comp_low){
							      this_interval->inte.comp_low = new_comp_low;
						     }

						     if( this_interval->inte.comp_high < new_comp_high){
							      this_interval->inte.comp_high = new_comp_high;
						     }

						     // set compress data range
						   rc = __skyfs_SS_do_local_write( ino,kiov,replica_id, &vec, partition_id, 0, &local_write_fd);
                                                   if(rc <= 0){
                                                           // remove this interval no9e
                                                        SKYFS_ERROR_1("write Error after replace inte, remove the intervalTNode at offset %lu\n ", inte.low);
                                                        do_IntervalT_Delete( this_intervalTree, this_interval);
							used_size_changed -= ori_comp_len;

                                                   }else{
							used_size_changed += rc;
							used_size_changed -= ori_comp_len;
						   }
                                                  goto after_write;

					     
					     }else{
						     //long int xx = 1024*1024*1024;
						     //xx *= 16;

						     inte.flag = vec.algorithm & 0x0ffffff;
						     //SKYFS_ERROR("WRITE not match interval, comp_high %lu, comp_low %lu, longint size %d, value %lld\n",
						//		     this_interval->inte.comp_high, this_interval->inte.comp_low, sizeof(xx), xx);
						     // TODO: important, original interval not match ,  de_compress two data phase and merge them
						     char * tmp_buf = NULL, *read_buf = NULL;
						     int algorithm = 1;
						     size_t src_data_len = 0;
						     size_t dest_data_len = 0;
						     size_t new_data_len = 0, new_data_offset = 0;
						     char * node_data_buf = malloc(SKYFS_OBJECT_NODE_SIZE);

						     long long ori_comp_len = this_interval->inte.comp_high - this_interval->inte.comp_low+1;
						     if(node_data_buf == NULL){
							     rc = -ENOMEM;
							     SKYFS_ERROR_1("Error: can not alloc  buffer for for re-merge data at offset %lu\n  ", offset);
							     goto after_write;
						     }
						     memset(node_data_buf, 0, SKYFS_OBJECT_NODE_SIZE);
						     //src_data_len = this_interval->inte.data_high - this_interval->inte.data_low +1; 
						     //TODO : read the old data
						     dest_data_len =  this_interval->inte.comp_high - this_interval->inte.comp_low +1;
						     read_buf = malloc(dest_data_len);
						     if(read_buf == NULL){
							     rc = -ENOMEM;
                                                             SKYFS_ERROR_1("Error: can not alloc  buffer for  re-merge ori data at offset %lu\n  ", offset);
							     free(node_data_buf);
                                                             goto after_write;
						     }
						     rc = __skyfs_SS_do_read(ino, obj_size, read_buf, this_interval->inte.comp_low, dest_data_len, partition_id, obj_id, replica_id);
						     
						     SKYFS_ERROR("0 read old compress data return %d , exp %d, buf %p \n", rc, dest_data_len, read_buf);
						     if (rc< dest_data_len){
							     // TODO: reply error;
							     SKYFS_ERROR_1("Error: can not read enough ori compress data at offset %lu, length %lu, %d\n", 
									     offset, rc, dest_data_len);
							     rc = -EIO;
							     free(read_buf);
							     free(node_data_buf);
							     goto after_write;
						     }

						     
						     dest_data_len = 0;
						     if(this_interval->inte.flag != 0){
						     	tmp_buf = decompress_func(read_buf, rc,SKYFS_OBJECT_NODE_SIZE ,
									&dest_data_len, this_interval->inte.flag, NULL);
						     	free(read_buf);
						     //tmp_buf = decompress_output();
						     	if(tmp_buf == NULL){
							     rc = -EIO;
                                                             SKYFS_ERROR_1("Error: can not decompress  original data offset %lu, obj:%lu ",
									     this_interval->inte.data_low, obj_id);
							     free(node_data_buf);
                                                             goto after_write;

						     	}
						     }else{
							     tmp_buf = read_buf;
							     dest_data_len = rc;
						     }
						     memcpy(&node_data_buf[(this_interval->inte.data_low) % SKYFS_OBJECT_NODE_SIZE], tmp_buf,dest_data_len );
						     SKYFS_ERROR("1 memcpy decompressed old data, at buf %p , offset %llu, length %lu\n",
								     node_data_buf, (this_interval->inte.data_low) % SKYFS_OBJECT_NODE_SIZE, dest_data_len);
						     free(tmp_buf);

						     // TODO: decompress current data and copy to node_data_buf
						     //src_data_len = inte.data_high - inte.data_low +1;
						     int this_count = vec.fcount;
						     if(inte.flag != 0){
						     	tmp_buf = decompress_func(kiov->ak_addr, vec.fcount,SKYFS_OBJECT_NODE_SIZE ,&dest_data_len, inte.flag,NULL);
						     	if(tmp_buf == NULL){
                                                             rc = -EIO;
                                                             SKYFS_ERROR_1("Error: can not decompress new data offset %lu, obj:%lu ",
                                                                             this_interval->inte.data_low, obj_id);

							     free(node_data_buf);
                                                             goto after_write;

                                                     	}
						     }else{
							     tmp_buf = kiov->ak_addr;
							     dest_data_len = vec.fcount;
						     }
						     memcpy(&node_data_buf[(inte.data_low) % SKYFS_OBJECT_NODE_SIZE], tmp_buf,dest_data_len);
						     SKYFS_ERROR("2 memcpy decompressed new data, at buf %p , offset %llu, length %lu\n",
								     node_data_buf, (inte.data_low) % SKYFS_OBJECT_NODE_SIZE, dest_data_len);

						     if(inte.flag != 0){
                                                     	free(tmp_buf);
						     }
						     new_data_len = inte.data_high > this_interval->inte.data_high? inte.data_high: this_interval->inte.data_high;
						     new_data_offset = inte.data_low < this_interval->inte.data_low? inte.data_low: this_interval->inte.data_low;
						     // compress to new buffer
						     
						     SKYFS_ERROR("try to recompress : buf %p , inte.low %lu, inte.data_low %lu, new_offset %lu, this_data_low %lu, new_data_len %d\n",
								     node_data_buf, inte.low, inte.data_low, new_data_offset, this_interval->inte.data_low, new_data_len);
						      if(inte.flag != 0){
						      tmp_buf = compress_func(&node_data_buf[new_data_offset % SKYFS_OBJECT_NODE_SIZE], 
								      new_data_len-new_data_offset+1, &dest_data_len, inte.flag);
						      
						      }else{
							      tmp_buf = &node_data_buf[new_data_offset % SKYFS_OBJECT_NODE_SIZE];
							      dest_data_len = new_data_len - new_data_offset +1;
							      
						      }

						      if(dest_data_len > (new_data_len - new_data_offset+1)){
							      SKYFS_ERROR_1("re-compressed data bigger than oringinal merged data\n");
							      tmp_buf = &node_data_buf[new_data_offset];
							      dest_data_len = (new_data_len - new_data_offset+1);
							      algorithm = 0;
						      }else{
							      algorithm = inte.flag;
						      }
						      if(tmp_buf == NULL){
                                                             rc = -EIO;
                                                             SKYFS_ERROR_1("Error: can not re-compress merged data offset %lu, obj:%lu ",
                                                                             this_interval->inte.data_low, obj_id);

                                                             free(node_data_buf);
                                                             goto after_write;

                                                     }
						      vec.fcount = dest_data_len;
						      vec.offset = new_data_offset;
						      // modiry this_interval
						      amp_kiov_t tmp_kiov;
						      tmp_kiov.ak_addr = tmp_buf;
						      rc = __skyfs_SS_do_local_write( ino,&tmp_kiov,replica_id, &vec, partition_id, 0, &local_write_fd);
						      SKYFS_ERROR("write not match interval ret %d, buf %p, node buf %p  \n", rc, tmp_buf, node_data_buf);
						      if(rc<0){
							      
                                                        SKYFS_ERROR_1("write Error after replace inte, remove the intervalTNode at offset %lu\n ", this_interval->inte.low);
                                                        do_IntervalT_Delete( this_intervalTree, this_interval);
							
							 used_size_changed -= ori_comp_len;
						      	 if(algorithm != 0)
						      		free(tmp_buf);
						      	  free(node_data_buf);
							  goto after_write;
						      }else{
							      if(rc == dest_data_len){
								      rc = this_count;
							      }else{
								      SKYFS_ERROR_1("write after compression failed %lu, %lu\n", rc, dest_data_len);
								      //rc = 0;
							      }

						      }

						      this_interval->inte.data_low = new_data_offset; // relative offset for object
						      this_interval->inte.data_high = new_data_len;
						      this_interval->inte.comp_low = new_data_offset; // relative  offset for object 
						      this_interval->inte.comp_high = new_data_offset+dest_data_len-1;
						      this_interval->inte.flag = algorithm;

						      SKYFS_ERROR("interval data_low %lu, data_high %lu, high %lu low %lu  comp_high %lu comp_low %lu \n",
								      this_interval->inte.data_low, this_interval->inte.data_high, this_interval->inte.low,
								      this_interval->inte.high, this_interval->inte.comp_low, this_interval->inte.comp_high);
						      used_size_changed += dest_data_len;
						      used_size_changed -= ori_comp_len;


						      if(algorithm != 0)
						      	free(tmp_buf);
						      free(node_data_buf);
						      goto after_write;


					     }





				     }else{
					     rc = -ENOENT;
					     SKYFS_ERROR_1("can not open a interval tree for ino %lu ,obj_id %lu\n", ino, obj_id);
					     goto after_write;
				     }



			     } // end if zstd

			     rc = __skyfs_SS_do_local_write( ino,kiov,replica_id, &vec, partition_id, 0, &local_write_fd);
after_write:

			     if(rc >= 0 ){
				//local_write_length = rc;
				if(rc > max_length)
					max_length = rc;
			     }else{
				     invalid_version = 1;
				     rc = -EIO;
				     SKYFS_ERROR_1("write_local_failed %d, local_write_fd %d, partition %lu, obj_id %lu\n", rc, local_write_fd,
						     partition_id, obj_id);
			     
			     }
			     

		     }else{ // write other replica
			      skyfs_DL_file_t tmp_dl_file ;
			      tmp_dl_file.ino = ino;
			      dl_file = &tmp_dl_file;
			      for(int j = 1; j<=replica_num; j++){
			      	des.replica_location[j] = 0;
			      }
			      uint32_t osd_gid = find_osdgid(osd_num, ino, obj_id);
			      uint32_t this_osd = find_replica_osd(osd_num, osd_gid, replica_idx , replica_num);
			      des.replica_location[replica_idx] = this_osd; 
			      des.replica_num = replica_num;
			      
    	     			SKYFS_ERROR("SS_WRITE  in ino %lu ,partition %lu, obj_id %lu , offset %lu ,replica_id %lu, write other \n", ino, partition_id, 
			     		obj_id, offset, replica_id);
			      rc  = __skyfs_O2O_prepare_write(dl_file, kiov, &des, &vec, partition_id);
				SKYFS_ERROR("SS_WRITE to replica  in ino %lu ,partition %lu, obj_id %lu , offset %lu ,replica_id %lu, write other end \n", ino, partition_id, 
			     		obj_id, offset, replica_id);

			      if(rc > max_length){
					max_length = rc;

			      }

			      if(rc <= 0)
			      SKYFS_ERROR_1("write replica %d,ret  %d\n", replica_idx, rc);

			     /*1.3.2 prepare and commit write TODO */

			     
		     }	     
	     }// end for each replica
	     if(local_write_fd > 0){
		     int rc1 = 0;
		     int rc2 = 0;
		     if(replica_num < 3 && replica_num >1){
			     rc1 = fdatasync(local_write_fd);
		     }
		     rc2 = close(local_write_fd);
		     if(rc1 <0 || rc2 <0){
			     invalid_version = 1;
			     SKYFS_ERROR_1("SS_write do local_write and sync in ino %llu ,failed %d, %d offset %llu\n", 
					     ino, rc1, rc2, file_offset);
		     }

	     }
	     
	     if(tmp_dl_part && invalid_version){
		/*invalid local partition ,the function below will add part_lock  */
		     int tmp_rc = __skyfs_DL_invalid_partition(ino, partition_id, replica_id, (uint64_t)-2, 0);
	     }
	     if(tmp_dl_part != NULL){
		     pthread_rwlock_unlock(&tmp_dl_part->part_lock);
	     }

    	     SKYFS_ERROR("SS_WRITE  in ino %lu ,partition %lu, obj_id %lu , offset %lu ,replica_id %lu, return \n", ino, partition_id, 
			     obj_id, offset, replica_id);
	     //rc = max_length;
	     // TODO : other replica is written by client
	     goto ERR;
	    
	     

    }


    
    this_pending_io.ino = ino;
    this_pending_io.offset = file_offset;
    this_pending_io.length = count;
    this_pending_io.type = 1; // 1 : write 0: read
    INIT_LIST_HEAD(&this_pending_io.pending_io_entry);

    ino_hash = __skyfs_get_obj_hashvalue(ino, 0);

    SKYFS_ENTER("__skyfs_SS_write:enter:ino:%llu, obj_id:%llu, offset:%u, count:%u, replica_num:%u\n",
        ino, obj_id, vec.offset, count, vec.replica_num);

    if(base_file){
    		SKYFS_ERROR_1("__skyfs_SS_write: base enter:ino:%llu, obj_id:%llu, offset:%u, count:%u, replica_num:%u\n",
        		ino, obj_id, vec.offset, count, vec.replica_num);
    }

    gettimeofday(&start_time, NULL);

    SKYFS_ENTER("__skyfs_SS_write:count:%u, replica_num:%u\n", count, replica_num);
    /*0 Judge if this server can process directly*/

    //dl_file = __skyfs_SS_locate_cache_file(ino, ino_hash);

    /*1 Locate file*/
    /*1.1 judge if request belong to this server*/
    /*1.2 judge if this server can alloc data to the ino, concurrency.
     * htbp,subset,chunk,file*/
	#if 1
relocate_dl_subset:
    htbp = __skyfs_SS_locate_dl_subset(ino, 0, &subset_id, &osd_id);
    if(htbp == NULL){
        rc = -ENOENT;
        SKYFS_ERROR_1("__skyfs_SS_write:ino:%llu,sub not here offset %llu , count %llu,forward\n",ino, offset , count);
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    /*Load balance support*/
    osd_id = __skyfs_SS_check_dl_htbcache(htbp);
    if(osd_id != 0){
        rc = -ENOENT;
        SKYFS_ERROR_1("__skyfs_SS_write:ino:%llu,obj_id:%llu, offset %llu , count %llu htb not here,forward\n",
            ino, obj_id, file_offset,count );
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }

    SKYFS_MSG("%s:prepare get_dl_subset\n", __FUNCTION__);
    dl_subset = __skyfs_SS_get_dl_subset(htbp, subset_id);
    if(dl_subset == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    SKYFS_MSG("%s:wrlock dl_subset:%u\n", __FUNCTION__, subset_id);
    pthread_rwlock_wrlock(&(dl_subset->rwlock));

    tmp_subset_id = __skyfs_get_subset_id(ino_hash, dl_subset->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("%s:subset splited waitting:old:%u,new:%u\n",
            __FUNCTION__, subset_id, tmp_subset_id);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_dl_subset;
    }

relocate_dl_chunk:
    SKYFS_MSG("%s:locate_dl_chunk\n", __FUNCTION__);
    dl_chunk = __skyfs_SS_locate_dl_chunk(dl_subset, ino, 0, &chunk_id);
    if(dl_chunk == NULL){
        dl_chunk = __skyfs_SS_get_dl_chunk(dl_subset, chunk_id);
        if(dl_chunk == NULL){
            rc = -ENOENT;
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&dl_chunk->rwlock);
    pthread_mutex_unlock(&dl_chunk->lock);


    SKYFS_MSG("%s:locate_dl_file\n", __FUNCTION__);
    if(create_dlfile_flag == 0){
        dl_file = __skyfs_SS_locate_dl_file(dl_chunk, ino);
        if(dl_file != NULL){
            goto begin_to_placement;
        }

        create_dlfile_flag = 1;
    }

//create_dlfile:
    if(dl_chunk->nfree > 0){
        rc = __skyfs_SS_alloc_dlfile(dl_chunk, &dl_file);
        rc = __skyfs_SS_init_dlfile(dl_file, ino, 0, replica_num, SKYFS_OBJECT_SIZE, osd_this_id, ino_hash);
        dl_subset->nlink_update ++;
		/* added by mayl, dl_subset and dl_trunk need to writeback here in some time  */
		//if((dl_subset->nlink_update % SKYFS_DL_WRITEBACK_COUNT) == 1 ) // the first change of this dl subset be sure to writeback !
		need_writeback_dl = 1; // in this version , always sync dl file --mayl 
		if(need_writeback_dl){
			__skyfs_DL_writeback_subset_without_release(dl_subset);
		}
    }else{
        pthread_rwlock_unlock(&dl_chunk->rwlock);

        if(dl_subset->split_depth < SKYFS_DL_FIRST_SPLIT_DEPTH){
            rc = __skyfs_SS_split_dlsubset(dl_subset);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            if(rc < 0){
                goto ERR;
            }
            goto relocate_dl_subset;
        }else if(dl_subset->subset_depth < SKYFS_DL_FIRST_SUBSET_DEPTH){
            rc = __skyfs_SS_enlarge_dlsubset(dl_subset);
            if(rc < 0){
                goto ERR;
            }
            goto relocate_dl_chunk;
        }else{
            rc = __skyfs_SS_split_dlsubset(dl_subset);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            if(rc < 0){
                goto ERR;
            }
            goto relocate_dl_subset;
        }
    }

    /*2 Locate/alloc content, concurrency*/
    /*2.1 judge if need to alloc data*/
begin_to_placement:
    obj_id = __skyfs_SS_get_objid(dl_file, file_offset);
    SKYFS_ERROR("%s:get objid,obj_id:%llu\n", __FUNCTION__, obj_id);    

    /*find obj, and make sure the partition in the cache, consider the size of obj*/
    partition_id = __skyfs_SS_locate_obj(dl_file, obj_id);
    if(partition_id == -1){
        /*2.2 alloc partition, alloc all the replica partition at the same time*/
        SKYFS_MSG("%s:before alloc part,p_id:%d\n", __FUNCTION__, partition_id); 
        is_new_partition = 1;	
        rc = __skyfs_SS_alloc_partition(dl_file, obj_id, &partition_id);
        if(rc < 0){ 
            SKYFS_ERROR("%s:alloc partition failed,rc:%d\n", __FUNCTION__, rc);    
            pthread_rwlock_unlock(&dl_chunk->rwlock);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
        /*2.3 alloc data and replica, adjust small file org!!!*/
        SKYFS_MSG("%s:before alloc obj ,partition_id:%d, obj_id:%llu\n", 
            __FUNCTION__, partition_id, obj_id);    
        rc = __skyfs_SS_alloc_objs(dl_file, partition_id);
        if(rc < 0){
            SKYFS_ERROR("%s:alloc obj failed,rc:%d\n", __FUNCTION__, rc);
            pthread_rwlock_unlock(&dl_chunk->rwlock);
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }else if(partition_id == -2){
        rc = -ENOENT;
        SKYFS_ERROR("%s:get partition_id error,rc:%d\n", __FUNCTION__, rc);
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    rc = __skyfs_SS_fill_des(dl_file, obj_id, partition_id, &des);
    if(count >= 32768 || rc <0){
	     SKYFS_ERROR("%s:fill des , rc:%d, ino %llx, offset %llx, count %x, dl_file %p , partition_id %d, obj_id %d \n",
                        __FUNCTION__, rc, ino, offset, count, dl_file, partition_id, obj_id);
    }
    if(rc < 0){
        SKYFS_ERROR("%s:error, rc:%d\n", __FUNCTION__, rc);    
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    /*3 Write data*/
    /*3.1 write to primary and replica, in pipeline way*/
    vec.obj_size = dl_file->obj_size;
	vec.obj_id = obj_id;
    
    // added by mayl for this pending io
    rc1 = skyfs_insert_pending_io(&this_pending_io);
    if(count >=1024 && 0){
		uint64_t * pdata = (uint64_t *) (kiov->ak_addr);
		if(pdata[126] != check_data1 || pdata[127] != check_data2){
			SKYFS_ERROR_1("SS Write obj Error, ino %llu, offset %llu, chk_dtata1 %llx:%llx, chk_data2 %llx, %llx\n",
					ino, offset, pdata[126], check_data1, pdata[127], check_data2);
		}

    }
    rc = __skyfs_SS_do_write(dl_file, kiov, &des, &vec, partition_id, is_new_partition);
    for(int replica_id = 1; replica_id <= dl_file->replica_num ; replica_id++){
	    //TODO upate patirion info and flush it if needed

    }
    

    rc1 = skyfs_release_pending_io(&this_pending_io);
    do_write = 1;
    rc2 = rc;

    if(rc < 0){
        SKYFS_ERROR_1("%s:do real write error, rc:%d\n", __FUNCTION__, rc);    
    }

    
    //rc0 = __skyfs_SS_release_objbuf(dl_file, partition_id, obj_id, file_offset, count);
    //if(rc0 < 0){
    //   SKYFS_ERROR("%s:release objbuf err:%d\n", __FUNCTION__, rc0);
    //}

    pthread_rwlock_unlock(&dl_chunk->rwlock);
    pthread_rwlock_unlock(&(dl_subset->rwlock));
    pthread_rwlock_unlock(&(htbp->rwlock));
#endif
ERR:
    // added by mayl , for async write test
    if(is_async_write && input_algorithm == 0){
	    goto ERR_NONE; 
    }

    /*4.Send reply*/
    if(max_length >0)
	    rc = max_length;
SEND_REPLY:
	if(is_async_write && input_algorithm == 0){
	    goto ERR_NONE; 
    }

    if(skip_amp_reply) {
	msgp->error = rc;
        msgp->fromid = osd_this_id;
        msgp->fromType= SKYFS_OSD;    
        msgp->u.writeObjAck.space_changed = used_size_changed;
	// mayl for replying multi_write_objs
	goto RETURN;
    }


    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_writeobj_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    rc2 = rc;
    msgp->fromid = osd_this_id;
    msgp->fromType= SKYFS_OSD;
    
    gettimeofday(&end_time, NULL);
    //msgp->u.writeObjAck.exe_time = end_time.tv_sec*1000000+end_time.tv_usec;
    //msgp->u.writeObjAck.exe_time =  msgp->u.writeObjAck.exe_time - write_start_time;
    if(rc > 0){
        msgp->u.writeObjAck.dest = osd_this_id;
        msgp->u.writeObjAck.subset = subset_id;
        msgp->u.writeObjAck.chunk = chunk_id;
        //msgp->u.writeObjAck.exe_time = end_time.tv_sec*1000000+end_time.tv_usec;
	//msgp->u.writeObjAck.exe_time =  msgp->u.writeObjAck.exe_time - write_start_time;
        
    }
    
            
    msgp->u.writeObjAck.space_changed = used_size_changed;


    //timeusec = (end_time.tv_sec - start_time.tv_sec) * 1000000 + 
				//(end_time.tv_usec - start_time.tv_usec);
    
    SKYFS_ERROR("%s:time_usage:%llu\n", __FUNCTION__, timeusec);

    rc = amp_send_sync(osd_comp_context, req,
        req_type,
        req_id,
        0);
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_write:send failed.rc:%d\n", rc);
    }else{
	    rc = rc2; 
    }

ERR_NONE:
    SKYFS_MSG("__skyfs_SS_write:begin free req.\n");

    if(kiov->ak_addr){
        free(kiov->ak_addr);
    }

    if(kiov){
        free(kiov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    if(0)
    	SKYFS_ERROR_1("%s:leave:ino:%llu,obj_id:%llu,offset:%llu,partition:%u,fileoff:%llu,do_write %d, rc %d\n",
        	__FUNCTION__,ino,obj_id,offset,partition_id,file_offset, do_write, rc2);

    if(rc <= 0){
	    SKYFS_ERROR_1("%s write Error or nothing  offset %llu, ino %llu , rc %d\n",
			    __FUNCTION__, offset, ino, rc);
    }

RETURN:
    SKYFS_ERROR("count:%d,sid:%d,chunkid:%d\n",count, subset_id,chunk_id);
	

    return;

}

void __skyfs_SS_create_obj(amp_request_t *req)
{

}

static int do_remove_interval_tree(skyfs_ino_t ino, skyfs_u32_t partition_id, skyfs_u64_t obj_id, size_t reserve_size)
{
	uint32_t interval_tree_idx = obj_id % partition_id;
	uint32_t total_intervals = SKYFS_OBJECT_NODE_SIZE;
	SKYFS_ERROR_1("start removing interval tree for ino %llu, partition_id %u, obj_id %llu, reserve_size %lu\n\n ",
			ino, partition_id, obj_id, reserve_size);



	
	return 0;
}

void __skyfs_SS_do_removeobj(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    //skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    //skyfs_u32_t req_id;

    skyfs_u32_t partition_id;
    skyfs_u64_t obj_id;
    skyfs_ino_t ino;
    skyfs_u32_t replica_id;
    skyfs_s8_t  objfile[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t rc = 0;
    skyfs_s64_t reserve_size;

    msgp = __skyfs_get_msg(req->req_msg);
    partition_id = msgp->u.doRemoveObjReq.partition_id;
    replica_id = msgp->u.doRemoveObjReq.replica_id;
    ino = msgp->u.doRemoveObjReq.ino;
    obj_id = msgp->u.doRemoveObjReq.obj_id;
    reserve_size =  msgp->u.doRemoveObjReq.reserve_size;

    skyfs_u32_t stripe_num = ((ino+obj_id)%skyfs_data_stripe_cnt);

    SKYFS_ERROR("%s:enter,ino:%llu,obj_id:%llu,replica_id:%u\n",
        __FUNCTION__, ino, obj_id, replica_id);
    
    sprintf(objfile, "%s/%d-%d/%d/%llu-%d/%llu-%llu", 
        SKYFS_OBJ_PATH, skyfs_lid, stripe_num, replica_id ,ino, partition_id, ino, obj_id);
    //sprintf(objfile, "%s/%d-%d/%d/%llu-%d/%llu-%llu",
      //  SKYFS_OBJ_PATH, skyfs_lid, stripe_num, replica_id, ino, partition_id, ino, obj_id);
    if(reserve_size < 0){
    	rc = unlink(objfile);
	if(rc >= 0){
		rc = do_remove_interval_tree(ino, partition_id, obj_id, 0); 
	}

	
    }else{
	    // do truncate by mayl
	SKYFS_ERROR_1("%s : truncate %s to %lld\n", __FUNCTION__, objfile, reserve_size);
	rc = truncate(objfile, reserve_size);
	if(rc >= 0 && reserve_size == 0){
		rc = do_remove_interval_tree(ino, partition_id, obj_id, 0); 
	}else if(rc >= 0){
		// TODO : remove part of interval_tree
	}

    }
    if(rc < 0){
        SKYFS_ERROR_1("__skyfs_SS_do_removeobj:unlink %s err:%d\n", 
            objfile, errno);
        goto ERR;
    }
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, req,
            req->req_remote_type, 
            req->req_remote_id, 
            0);
    if(rc < 0){
        SKYFS_ERROR("%s:send failed.rc:%d\n", __FUNCTION__, rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR_1("%s:leave.remove:ino:%llu,obj_id:%llu,replica_id:%u, rc:%d\n", 
        __FUNCTION__, ino, obj_id, replica_id,rc);

}

void __skyfs_SS_remove_obj(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    //amp_kiov_t  kiov;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;

    //skyfs_u32_t dest;
    skyfs_u32_t osd_id;

    skyfs_u32_t       subset_id;
    skyfs_u32_t       tmp_subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_u32_t       partition_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    //skyfs_u32_t       count;
    skyfs_u64_t       file_offset;
    skyfs_u32_t       obj_size;
    skyfs_u32_t       ino_hash;

    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file= NULL;
    skyfs_DL_file_t   tmp_dl_file;

    //skyfs_s8_t        chunkfile[SKYFS_MAX_NAME_LEN];
    //skyfs_s32_t       fd = 0;
    skyfs_s32_t       rc = 0;
    uint32_t 	      osd_gid;

    skyfs_dl_dest_t   des;

    //skyfs_timespec_t    start_time;
    //skyfs_timespec_t    end_time;

    //skyfs_u32_t tmp;

    msgp = __skyfs_get_msg(req->req_msg);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = msgp->u.removeObjReq.ino;
    obj_id = msgp->u.removeObjReq.obj_id;
    obj_size = msgp->u.removeObjReq.obj_size;
    file_offset = obj_size * obj_id;
    
    // changed by mayl
    partition_id = obj_id / SKYFS_MAX_OBJ_PER_PART;
    tmp_dl_file.ino = ino;
    osd_gid = find_osdgid(osd_num, ino, obj_id);
    osd_id =  find_replica_osd(osd_num, osd_gid, 1 , skyfs_replica);
    if(osd_id != osd_this_id){
	    SKYFS_ERROR("forware remove obj req from OSD-%d to OSD-%d\n", osd_this_id,osd_id);
	     __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
	     goto ERR_NONE;

    }else{
	    SKYFS_ERROR("send remove obj req from OSD-%d, replica_cnt %d \n", osd_this_id,skyfs_replica);
	     des.replica_num = skyfs_replica;
	     rc = __skyfs_O2O_remove_obj(&tmp_dl_file, &des, obj_id, partition_id);
	     goto ERR;


    }



    




    ino_hash = __skyfs_get_obj_hashvalue(ino, 0);

    SKYFS_ENTER("%s:enter:ino:%llu, obj_id:%llu, offset:%llu\n",
        __FUNCTION__, ino, obj_id, file_offset);

    /*0 Judge if this server can process directly*/

    //dl_file = __skyfs_SS_locate_cache_file(ino, ino_hash);

    /*1 Locate file*/
    /*1.1 judge if request belong to this server*/
    /*1.2 judge if this server can alloc data to the ino, concurrency.
     * htbp,subset,chunk,file*/
relocate_dl_subset:
    htbp = __skyfs_SS_locate_dl_subset(ino, 0, &subset_id, &osd_id);
    if(htbp == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_SS_remove_obj:ino:%llu,sub not here,forward\n",ino);
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    /*Load balance support*/
    osd_id = __skyfs_SS_check_dl_htbcache(htbp);
    if(osd_id != 0){
        rc = -ENOENT;
        SKYFS_ERROR("%s:ino:%llu,%llu htb not here,forward\n",
            __FUNCTION__, ino, obj_id);
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }

    dl_subset = __skyfs_SS_get_dl_subset(htbp, subset_id);
    if(dl_subset == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(dl_subset->rwlock));

    tmp_subset_id = __skyfs_get_subset_id(ino_hash, dl_subset->split_depth);
    if(tmp_subset_id != subset_id){
        SKYFS_ERROR("%s:subset splited waitting:old:%u,new:%u\n",
            __FUNCTION__, subset_id, tmp_subset_id);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto relocate_dl_subset;
    }

//relocate_dl_chunk:
    dl_chunk = __skyfs_SS_locate_dl_chunk(dl_subset, ino, 0, &chunk_id);
    if(dl_chunk == NULL){
        dl_chunk = __skyfs_SS_get_dl_chunk(dl_subset, chunk_id);
        if(dl_chunk == NULL){
            rc = -ENOENT;
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&dl_chunk->rwlock);
    pthread_mutex_unlock(&dl_chunk->lock);


    dl_file = __skyfs_SS_locate_dl_file(dl_chunk, ino);
    if(dl_file == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    obj_id = __skyfs_SS_get_objid(dl_file, file_offset);
    SKYFS_MSG("%s:get objid,obj_id:%llu\n", __FUNCTION__, obj_id);    

    /*find obj, and make sure the partition in the cache, consider the size of obj*/
    partition_id = __skyfs_SS_locate_obj(dl_file, obj_id);
    if(partition_id == -1){
        rc = -ENOENT;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    rc = __skyfs_SS_fill_des(dl_file, obj_id, partition_id, &des);
    if(rc < 0){
        SKYFS_ERROR("%s:fill des error, rc:%d\n", __FUNCTION__, rc);    
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    /*3 remove obj*/
    rc = __skyfs_O2O_remove_obj(dl_file, &des, obj_id, partition_id);
    if(rc < 0){
        SKYFS_ERROR("%s:remove obj error, rc:%d\n", __FUNCTION__, rc);    
    }

    pthread_rwlock_unlock(&dl_chunk->rwlock);
    pthread_rwlock_unlock(&(dl_subset->rwlock));
    pthread_rwlock_unlock(&(htbp->rwlock));

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    msgp->fromid = osd_this_id;
    msgp->fromType= SKYFS_OSD;

    SKYFS_ERROR("%s:ino:%llu,obj_id:%llu,rc:%d,req_type:%u,req_id:%u\n", 
        __FUNCTION__, ino, obj_id, rc, req_type, req_id);

    rc = amp_send_sync(osd_comp_context, req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_remove_obj:send failed.rc:%d\n", rc);
    }


ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("%s:leave:ino:%llu,obj_id:%llu,sid:%d,chunkid:%d\n",
        __FUNCTION__, ino, obj_id, subset_id, chunk_id);
}

#if 0
void __skyfs_SS_remove_obj(amp_request_t *req)
{
    skyfs_msg_t *msgp = NULL;
    amp_kiov_t  kiov;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;

    skyfs_u32_t dest;
    skyfs_u32_t osd_id;

    skyfs_u32_t       subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    skyfs_u32_t       count;
    skyfs_u32_t       offset;

    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file= NULL;

    skyfs_s8_t        chunkfile[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t       fd = 0;
    skyfs_s32_t       rc = 0;

    skyfs_timespec_t    start_time;
    skyfs_timespec_t    end_time;

    skyfs_u32_t tmp;

    msgp = __skyfs_get_msg(req->req_msg);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = msgp->u.removeObjReq.ino;
    obj_id = msgp->u.removeObjReq.obj_id;

    SKYFS_ENTER("__skyfs_SS_remove_obj:enter:ino:%llu,obj_id:%llu,offset:%d,count:%d\n",
        ino, obj_id, offset, count);

    dest = msgp->u.removeObjReq.dest;
    subset_id = msgp->u.removeObjReq.subset;
    chunk_id = msgp->u.removeObjReq.chunk;

    if(dest == osd_this_id){
        SKYFS_ERROR("__skyfs_SS_remove_obj:frd request,ino:%llu,obj_id:%llu\n",
            ino, obj_id);
        goto remove_data;
    }

    /*2,Judge if the layout entry belong to the server*/
    htbp = __skyfs_SS_locate_dl_subset(ino, obj_id, &subset_id, &osd_id);
    if(htbp == NULL){
        rc = -ENOENT;
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    osd_id = __skyfs_SS_check_dl_htbcache(htbp);
    if(osd_id != 0){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }

    /*3,Judge if the layout entry in the cache*/
    /*4,Locate the layout entry through subset->chunk->entry way*/
    dl_subset = __skyfs_SS_get_dl_subset(htbp, subset_id);
       if(dl_subset == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }
    pthread_rwlock_wrlock(&(dl_subset->rwlock));

    dl_chunk = __skyfs_SS_locate_dl_chunk(dl_subset, ino, obj_id, &chunk_id);
    if(dl_chunk == NULL){
        dl_chunk = __skyfs_SS_get_dl_chunk(dl_subset, chunk_id);
        if(dl_chunk == NULL){
            rc = -ENOENT;
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            goto ERR;
        }
    }
    pthread_rwlock_rdlock(&dl_chunk->rwlock);
    pthread_mutex_unlock(&dl_chunk->lock);

    dl_file = __skyfs_SS_locate_dl_file(dl_chunk, ino);
    if(dl_file == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        goto ERR;
    }

    osd_id = __skyfs_SS_check_osd_id(dl_file);
    if(osd_id != osd_this_id){
        rc = -ENOENT;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        msgp->u.removeObjReq.dest = osd_id;
        msgp->u.removeObjReq.subset = subset_id;
        msgp->u.removeObjReq.chunk = chunk_id;
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;
    }

    /*5, Begin to remove obj file*/
remove_data:
    rc = __skyfs_SS_compose_chunkfile_pathname(subset_id, 
                    chunk_id, 
                    ino, 
                    obj_id, 
                    chunkfile);
    if(rc < 0){
        goto ERR_REMOVE;
    }

    rc = unlink(chunkfile);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_remove_obj:unlink %s err:%d\n", 
            chunkfile, errno);
        goto ERR_REMOVE;
    }
ERR_REMOVE:
    if(dest != osd_this_id){
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    msgp->fromid = osd_this_id;
    msgp->fromType= SKYFS_OSD;

    SKYFS_ERROR("__skyfs_SS_remove_obj:ino:%llu,obj:%llu,rc:%d,req_type:%d,req_id:%d\n", 
        ino,obj_id,msgp->error,req_type,req_id);

    rc = amp_send_sync(osd_comp_context, req,
                req_type,
                req_id,
                0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_remove_obj:send failed.rc:%d\n", rc);
    }


ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_LEAVE("__skyfs_SS_remove_obj:leave:ino:%llu,obj_id:%llu,offset:%d,count:%d,sid:%d,chunkid:%d\n",
        ino, obj_id, offset, count, subset_id, chunk_id);
}

void __skyfs_SS_commit(amp_request_t *req)
{

}
#endif

void __skyfs_SS_truncate(amp_request_t *req)
{
	/* mayl: this function should implement */
    skyfs_msg_t *msgp = NULL;
    //amp_kiov_t  kiov;
    skyfs_u32_t req_type;
    skyfs_u32_t req_size;
    skyfs_u32_t req_id;

    //skyfs_u32_t dest;
    skyfs_u32_t osd_id;

    skyfs_u32_t       subset_id;
    skyfs_u32_t       tmp_subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_u32_t       partition_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    //skyfs_u32_t       count;
    skyfs_s64_t       file_offset;
    skyfs_u32_t       obj_size;
    skyfs_u32_t       ino_hash;

    skyfs_s64_t       reserve_size = 0;
    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file= NULL;
    skyfs_DL_file_t   tmp_dl_file;

    //skyfs_s8_t        chunkfile[SKYFS_MAX_NAME_LEN];
    //skyfs_s32_t       fd = 0;
    skyfs_s32_t       rc = 0;
    uint32_t 	      osd_gid;

    skyfs_dl_dest_t   des;

    //skyfs_timespec_t    start_time;
    //skyfs_timespec_t    end_time;

    //skyfs_u32_t tmp;

    msgp = __skyfs_get_msg(req->req_msg);
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = msgp->u.truncateReq.ino;
    obj_id = msgp->u.truncateReq.obj_id;
    //obj_size = msgp->u.removeObjReq.obj_size;
    reserve_size = (skyfs_s64_t) (msgp->u.truncateReq.size); 
    
    // changed by mayl
    partition_id = obj_id / SKYFS_MAX_OBJ_PER_PART;
    tmp_dl_file.ino = ino;
    osd_gid = find_osdgid(osd_num, ino, obj_id);
    osd_id =  find_replica_osd(osd_num, osd_gid, 1 , skyfs_replica);
    if(osd_id != osd_this_id){
	    SKYFS_ERROR("forware remove obj req from OSD-%d to OSD-%d\n", osd_this_id,osd_id);
	     __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
	     goto ERR_NONE;

    }else{
	     des.replica_num = skyfs_replica;
	     rc = __skyfs_O2O_truncate_obj(&tmp_dl_file, &des, obj_id, partition_id, reserve_size);

	    SKYFS_ERROR_1("send remove_truncate obj req from OSD-%d, replica_cnt %d \n", osd_this_id,skyfs_replica);
	     goto ERR;


    }



    

ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    msgp->fromid = osd_this_id;
    msgp->fromType= SKYFS_OSD;

    SKYFS_ERROR("%s:ino:%llu,obj_id:%llu,rc:%d,req_type:%u,req_id:%u\n", 
        __FUNCTION__, ino, obj_id, rc, req_type, req_id);

    rc = amp_send_sync(osd_comp_context, req,
            req_type,
            req_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_remove_obj:send failed.rc:%d\n", rc);
    }


ERR_NONE:
    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("%s:leave:ino:%llu,obj_id:%llu,sid:%d,chunkid:%d\n",
        __FUNCTION__, ino, obj_id, subset_id, chunk_id);
}




void __skyfs_SS_get_devinfo(amp_request_t *req)
{
    skyfs_msg_t    *msgp = NULL;
    skyfs_u32_t    req_size;
    struct statvfs64 stbuf, stbuf_tmp;
    skyfs_s8_t  obj_path[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t    rc = 0, i = 0;

    //sprintf(obj_path, "%s/%d/", SKYFS_OBJ_PATH, skyfs_lid);

    SKYFS_ERROR_1("__skyfs_SS_get_devinfo:enter:%s.\n",obj_path);

    //memset(&stbuf, 0 , sizeof(struct statvfs64));
    for(i = 0; i<skyfs_data_stripe_cnt; i++){
            memset(obj_path, 0, SKYFS_MAX_NAME_LEN);
            sprintf(obj_path, "%s/data_%d/", "/mnt/", i+3);
            // TODO !!! TODO
            rc = statvfs64(obj_path, &stbuf_tmp);
            if(rc<0){
                   SKYFS_ERROR_1("statvfs for dobj %d failed dev %s, error %d \n",i, obj_path, errno );
                    goto ERR;
            }
	    if(i == 0){
		    memcpy(&stbuf, &stbuf_tmp, sizeof(struct statvfs64));
	    }else{
            	stbuf.f_bsize = stbuf_tmp.f_bsize;
            	stbuf.f_blocks += stbuf_tmp.f_blocks;
            	stbuf.f_bfree += stbuf_tmp.f_bfree;
            	stbuf.f_bavail += stbuf_tmp.f_bavail;
            	stbuf.f_favail += stbuf_tmp.f_favail;
            	stbuf.f_ffree += stbuf_tmp.f_ffree;
            	stbuf.f_files += stbuf_tmp.f_files;
	     }
	





    }


    //rc = statvfs64(obj_path, &stbuf);
    SKYFS_ERROR("__skyfs_SS_get_devinfo:rc:%d.\n", rc);
    if(rc < 0){
        goto ERR;
    }
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_getdevinfo_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp = __skyfs_get_msg(req->req_reply);
    msgp->error = rc;
    if(rc >= 0){
        msgp->u.getDevinfoAck.cap.bsize = stbuf.f_bsize;
        msgp->u.getDevinfoAck.cap.blocks = stbuf.f_blocks;
        msgp->u.getDevinfoAck.cap.bfree = stbuf.f_bfree;
        msgp->u.getDevinfoAck.cap.bavail = stbuf.f_bavail;
        msgp->u.getDevinfoAck.cap.files = stbuf.f_files;
        msgp->u.getDevinfoAck.cap.ffree = stbuf.f_ffree;
        msgp->u.getDevinfoAck.cap.favail= stbuf.f_favail;
        SKYFS_MSG("skyfs_statfs:f_bsize:%lu\n", stbuf.f_bsize);
        SKYFS_MSG("skyfs_statfs:f_blocks:%lu\n", stbuf.f_blocks);
        SKYFS_MSG("skyfs_statfs:f_bfree:%lu\n", stbuf.f_bfree);
        SKYFS_MSG("skyfs_statfs:f_bavail:%lu\n", stbuf.f_bavail);
        SKYFS_MSG("skyfs_statfs:f_files:%lu\n", stbuf.f_files);
        SKYFS_MSG("skyfs_statfs:f_ffree:%lu\n", stbuf.f_ffree);
        SKYFS_MSG("skyfs_statfs:f_ffavail:%lu\n", stbuf.f_favail);
    }else{
        SKYFS_MSG("skyfs_SS_get_devinfo:errno:%d\n", errno);
    }

    rc = amp_send_sync(osd_comp_context,
            req,
            req->req_remote_type,
            req->req_remote_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_get_devinfo:send reply failed.rc:%d", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    } 
   
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_SS_get_devinfo:exit:\n\n");
}

/*Data layout processing below*/
void __skyfs_SS_create_dl_subset_index(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;

    skyfs_u32_t         subset_id;
    skyfs_u32_t         subset_depth;
    skyfs_u32_t         nlink;
    skyfs_u32_t         size;
    skyfs_s32_t         rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    subset_id = msgp->u.createdlsubiReq.subset_id;
    subset_depth = msgp->u.createdlsubiReq.subset_depth;
    nlink = msgp->u.createdlsubiReq.nlink;

    SKYFS_ENTER("__skyfs_SS_create_dl_subindex:enter:subset:%d,subdepth:%d,nlink:%d\n",
        subset_id, subset_depth, nlink);

    rc = __skyfs_DL_do_create_subset_index(subset_id, subset_depth, nlink);

    size = AMP_SKYFS_MSGHEAD_SIZE;

    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_create_dl_subindex:send reply failed.rc:%d\n", rc); 
    } 
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_SS_create_dl_subindex:exit,rc:%d\n", rc);

}

void __skyfs_SS_get_dl_head(amp_request_t *req)
{
    skyfs_DL_head_t   dl_head;
    skyfs_msg_t       *msgp = NULL;
    skyfs_u32_t       size;
    skyfs_s32_t       rc = 0;

    SKYFS_MSG("__skyfs_SS_get_dl_head:enter.\n");

    rc = __skyfs_DL_get_head(&dl_head, pad_id);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_get_dl_head:get head error\n");
    }

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_DL_head_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        memcpy(msgp->u.mtext, &dl_head, sizeof(skyfs_DL_head_t));
    }

    msgp->error = rc;

    SKYFS_MSG("__skyfs_SS_get_dl_head:before send\n");
    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_get_dl_head:send reply failed.rc:%d\n", rc); 
    } 
    
    SKYFS_MSG("__skyfs_SS_get_dl_head:after send\n");

    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_SS_get_dl_head:exit,rc:%d\n", rc);

}

void __skyfs_SS_handle_replica_recover(amp_request_t *req)
{

    skyfs_msg_t       *msgp = NULL;
    skyfs_u32_t       size;
    skyfs_s32_t       rc = 0;
    skyfs_u64_t       task_xid = 0;
    skyfs_o_ask_replica_recover_t request_info;
    
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_ask_replica_recover_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp = __skyfs_get_msg(req->req_msg);

    SKYFS_ERROR_1("%s:enter.\n", __FUNCTION__);
    memcpy(&request_info, &(msgp->u.replicaAskReq), sizeof(skyfs_o_ask_replica_recover_t));

    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    
    if(request_info.flag != 1){
	    rc = -EINVAL;

            SKYFS_ERROR_1("%s:handle request failed.rc:%d\n", __FUNCTION__,rc); 
	    goto ERR;
    }



    if(rc >= 0){
	  struct timeval tv;
	  gettimeofday(&tv, NULL);
	  task_xid = tv.tv_sec;
	  task_xid *= 1000;
	  task_xid +=(tv.tv_usec/1000);
	  msgp->u.replicaAskAck.task_xid = task_xid;
        //memcpy(&msgp->u.replicaQueryAck, &query_info, sizeof(skyfs_o_replica_query_ack_t));
    }

ERR:
EXIT:
    msgp->error = rc;

    SKYFS_MSG("__skyfs_SS_get_dl_head:before send\n");
    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    
    
    SKYFS_MSG("__skyfs_SS_get_dl_head:after send\n");

    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 

    // TODO: start 
    if(rc >= 0){

            SKYFS_ERROR_1("%s:handle request rc %d.try to start task , src replica_id  %d, dest_replica_id %d, xid %llu\n", 
			    __FUNCTION__,rc, request_info.src_replica_id, request_info.dest_replica_id, task_xid); 



	     rc = __skyfs_O2O_recover_data_objs(request_info.src_replica_id, request_info.dest_replica_id, 
			     request_info.dest_osd_id, task_xid, request_info.data_stripe_id);
	     SKYFS_ERROR_1("%s:handle request ., recover data objs , src replica_id  %d, dest_replica_id %d, xid %llu, return rc %d\n", 
			    __FUNCTION__, request_info.src_replica_id, request_info.dest_replica_id, task_xid,rc);
	     
	     rc = __skyfs_O2O_recover_partitions(request_info.src_replica_id, request_info.dest_replica_id, request_info.dest_osd_id, task_xid);
	        SKYFS_ERROR_1("%s:handle request .rc:recover partitions  , src replica_id  %d, dest_replica_id %d, xid %llu, return rc %d\n", 
			    __FUNCTION__,request_info.src_replica_id, request_info.dest_replica_id, task_xid, rc);
    }




}
//  mayl TODO reallize this function
void __skyfs_SS_query_replica_state(amp_request_t *req)
{
    //skyfs_DL_head_t   dl_head;
    int64_t used_partitions = 0;
    int64_t fault_partitions = 0;
    int i = 0;
    char dir_path[256];
    skyfs_msg_t       *msgp = NULL;
    skyfs_u32_t       size;
    skyfs_s32_t       rc = 0;
    skyfs_o_replica_query_ack_t query_info;

    msgp = __skyfs_get_msg(req->req_msg);
    SKYFS_ERROR_1("__skyfs_SS_query_replica_stqate:enter.\n");
    memset(&query_info, 0 ,sizeof(skyfs_o_replica_query_ack_t));
    for(i = 1; i <= skyfs_replica; i++){
	    memset(dir_path, 0, 256);
	    sprintf(dir_path, "%s/rep-%d-partdir", SKYFS_OBJ_PATH,i);
	    get_all_replica_partition_state(dir_path, &used_partitions, &fault_partitions);
	    query_info.replica_cnt = skyfs_replica;
	    query_info.used_partition_cnt[i] = used_partitions;
	    query_info.fault_partition_cnt[i] = fault_partitions;
	    query_info.running_recover_xid[i] = data_recover_state[i] ; // TODO ,need to fill this info
	    SKYFS_ERROR_1("get %lld parttitions, %lld fault\n", used_partitions, fault_partitions);

	    used_partitions = 0;
    	    fault_partitions = 0;


    }

    //rc = __skyfs_DL_get_head(&dl_head, pad_id);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_get_dl_head:get head error\n");
    }

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_o_replica_query_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);
    if(rc >= 0){
        memcpy(&msgp->u.replicaQueryAck, &query_info, sizeof(skyfs_o_replica_query_ack_t));
    }

    msgp->error = rc;

    SKYFS_MSG("__skyfs_SS_get_dl_head:before send\n");
    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR_1("%s:send reply failed.rc:%d\n", __FUNCTION__,rc); 
    } 
    
    SKYFS_MSG("__skyfs_SS_get_dl_head:after send\n");

    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_SS_get_dl_head:exit,rc:%d\n", rc);

}


void __skyfs_SS_create_dl_subset(amp_request_t *req)
{
    skyfs_msg_t            *msgp = NULL;
    skyfs_DL_subset_head_t dl_subset_head;
    skyfs_u32_t            req_size;
    skyfs_s32_t            rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    dl_subset_head.subset_id = msgp->u.createdlsubsetReq.subset_id;
    dl_subset_head.split_depth = msgp->u.createdlsubsetReq.split_depth;
    dl_subset_head.subset_depth = msgp->u.createdlsubsetReq.subset_depth;
    dl_subset_head.fir_osd = msgp->u.createdlsubsetReq.fir_osd;
    dl_subset_head.sec_osd = msgp->u.createdlsubsetReq.sec_osd;
    dl_subset_head.thi_osd = msgp->u.createdlsubsetReq.thi_osd;
    dl_subset_head.nlink= msgp->u.createdlsubsetReq.nlink;
    
    SKYFS_ERROR("__skyfs_SS_create_dl_subset:enter,subset_id:%d\n",
        dl_subset_head.subset_id);

    if(msgp->u.createdlsubsetReq.replica_id == 1){
        rc = __skyfs_DL_do_create_subset(&dl_subset_head);
    }else{
        rc = __skyfs_DL_create_local_subset(&dl_subset_head);    
    }

    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_create_dl_subset:create subset err:%d\n",rc);
    }

    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;
    
    SKYFS_MSG("__skyfs_SS_create_dl_subset:send reply :\n");

    rc = amp_send_sync(osd_comp_context,
            req,
            req->req_remote_type,
            req->req_remote_id,
            0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_create_dl_subset:send reply failed.rc:%d", rc);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    } 
   
    __amp_free_request(req); 
    
    SKYFS_LEAVE("__skyfs_SS_create_dl_subset:exit:\n\n");

}

void __skyfs_SS_write_dlchunk(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t          *kiov = NULL;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         chunk_id;
    skyfs_u32_t         req_size;
    skyfs_u32_t         size;
    skyfs_u32_t         offset;
    skyfs_s32_t         fd = 0;
    skyfs_s8_t          dl_subset_fname[SKYFS_MAX_NAME_LEN];
    skyfs_DL_chunk_t    *dl_chunk = NULL;
    skyfs_s32_t         rc = 0;

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    subset_id = msgp->u.writedlchunkReq.subset_id;
    chunk_id = msgp->u.writedlchunkReq.chunk_id;
    size = SKYFS_DLCHUNK_SIZE;

    SKYFS_ERROR("__skyfs_SS_write_dlchunk:enter.subset_id:%d,chunk_id:%d\n",
        subset_id, chunk_id);

    sprintf(dl_subset_fname, "%s%d", SKYFS_DL_PATH, subset_id);
    offset = chunk_id * size + sizeof(skyfs_DL_subset_head_t);

    fd = open(dl_subset_fname, O_WRONLY);
    if(fd < 0){
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:can not open subset file:%s\n", 
            dl_subset_fname);
        rc = -errno;
        goto ERR;
    }

    dl_chunk = kiov->ak_addr;
    SKYFS_MSG("__skyfs_SS_write_dlchunk:hashvalue:%lld,nfree:%d,firstfree:%d\n",
        dl_chunk->hashvalue, dl_chunk->nfree, dl_chunk->firstfree);
    SKYFS_ERROR("__skyfs_SS_write_dlchunk:cmeta.ino:%lld\n",
        dl_chunk->dlfile[0].ino);

    if(dl_chunk->firstfree < 0){
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:error:firstfree:%d\n", 
            dl_chunk->firstfree);
        exit(1);
    }

    if(dl_chunk->chunk_id != chunk_id){
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:error:chunk_id:%d,chunk_id:%d,fd:%d\n",
            dl_chunk->chunk_id, chunk_id, fd);    
        exit(1);
    }

    if(pwrite(fd, kiov->ak_addr, size, offset) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:write subset file err:%d\n", rc);
        goto ERR;
    }

    if(__skyfs_SS_create_chunkdir(subset_id, chunk_id) < 0){
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:error create chunk dir:%d,%d\n",
            subset_id, chunk_id);        
    }
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_write_dlchunk:send failed.rc:%d\n", rc);
    }

    if(kiov->ak_addr){
        free(kiov->ak_addr);
    }

    if(kiov){
        free(kiov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("__skyfs_SS_write_dlchunk:leave.subset:%d,chunk_id:%d,fd:%d\n\n", 
        subset_id, chunk_id, fd);

}

void __skyfs_SS_update_head_depth(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;

    skyfs_u32_t        subset_id;
    skyfs_u32_t        split_depth;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    msgp = __skyfs_get_msg(req->req_msg);
    subset_id = msgp->u.updatehdepthReq.subset_id;
    split_depth = msgp->u.updatehdepthReq.split_depth;

    SKYFS_ENTER("__skyfs_SS_update_head_depth:enter:subset_id:%d,split_depth:%d\n",
        subset_id, split_depth);
    rc = __skyfs_DL_do_update_hdepth(subset_id, split_depth);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_update_head_depth:update head failed\n");
    }

    size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_update_hdepth:send reply failed.rc:%d\n", rc); 
    } 
    if(req->req_msg){ 
         free(req->req_msg); 
    } 
    if(req->req_reply){ 
         free(req->req_reply); 
    } 
    __amp_free_request(req); 


    SKYFS_LEAVE("__skyfs_SS_update_hdepth:exit,rc:%d\n", rc);

}

void __skyfs_SS_copy_obj(amp_request_t *req)
{
    skyfs_msg_t         *msgp = NULL;
    amp_kiov_t          *kiov = NULL;
    skyfs_u32_t         subset_id;
    skyfs_u32_t         chunk_id;
    skyfs_ino_t         ino;
    skyfs_u64_t         obj_id;
    skyfs_u32_t         req_size;
    skyfs_u32_t         size;
    skyfs_s32_t         fd = 0;
    skyfs_s8_t          chunkfile[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t          subsetdir[SKYFS_MAX_NAME_LEN];
    skyfs_s32_t         rc = 0;
    struct stat buf;

    kiov = req->req_iov;
    msgp = __skyfs_get_msg(req->req_msg);
    subset_id = msgp->u.copyobjReq.subset_id;
    chunk_id = msgp->u.copyobjReq.chunk_id;
    ino = msgp->u.copyobjReq.ino;
    obj_id = msgp->u.copyobjReq.obj_id;
    size = SKYFS_OBJECT_SIZE;

    SKYFS_ERROR("__skyfs_SS_copy_obj:enter.subset_id:%d,chunk_id:%d\n",
        subset_id, chunk_id);

    sprintf(subsetdir, "%s/%d/%d", SKYFS_OBJ_PATH, skyfs_lid, subset_id);

    rc = __skyfs_SS_compose_chunkfile_pathname(subset_id, 
                    chunk_id, 
                    ino, 
                    obj_id, 
                    chunkfile);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_copy_obj:compose chunkfile err,rc:%d\n", 
            rc);
        goto ERR;
    }

    if((fd = open(chunkfile, O_WRONLY|O_CREAT, 0666)) < 0){
        SKYFS_ERROR("__skyfs_SS_copy_obj:can not open chunkfile:%s,%d\n", 
            chunkfile, errno);
        if((rc = errno) == ENOENT){
            if((rc = stat(subsetdir, &buf)) == -1){
                rc = mkdir(subsetdir, 0666);
                if(rc < 0){
                    SKYFS_ERROR("__skyfs_SS_copy_obj:create sub:%s,err:%d\n", 
                        subsetdir, errno);
                    goto ERR;
                }
            }
            if((rc = __skyfs_SS_create_chunkdir(subset_id, chunk_id)) == 0){
                fd = open(chunkfile, O_WRONLY|O_CREAT, 0666);
                if(fd > 0){
                    goto CONT;
                }
            }
        }
        goto ERR;
    }

CONT:
    if((rc = write(fd, kiov->ak_addr, size)) < 0){
        rc = -errno;
        SKYFS_ERROR("__skyfs_SS_copy_obj:write subset file err:%d\n", rc);
        goto ERR;
    }
ERR:
    req_size = AMP_SKYFS_MSGHEAD_SIZE;
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, req_size);

    msgp->error = rc;

    if(fd){
        close(fd);
    }

    rc = amp_send_sync(osd_comp_context, req,
                    req->req_remote_type, 
                    req->req_remote_id, 
                    0);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_copy_obj:send failed.rc:%d\n", rc);
    }

    if(kiov->ak_addr){
        free(kiov->ak_addr);
    }

    if(kiov){
        free(kiov);
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("__skyfs_SS_copy_obj:leave.subset:%d,chunk_id:%d,fd:%d\n\n", 
        subset_id, chunk_id, fd);

}

void __skyfs_SS_get_state(amp_request_t *req)
{
    skyfs_state_info_t state_info;
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_u32_t        layout_version;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_SS_get_state:enter,fromid:%d\n", req->req_remote_id);

    msgp = __skyfs_get_msg(req->req_msg);
    layout_version = msgp->ver;
/*
    if(ods_this_id != SKYFS_MASTER_MDS_ID){
        __skyfs_MS_check_layout_version(layout_version, req->req_remote_id);
    }
*/
    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_m_getstate_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    rc = __skyfs_SS_collect_state(&state_info);

    msgp->error = rc;
    if(rc >= 0){
        memcpy(&(msgp->u.getstateAck.state_info), &state_info, 
            sizeof(skyfs_state_info_t));
    }

    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_get_state:send reply failed.rc:%d\n", rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_SS_get_state:exit,rc:%d\n", rc);

}

void __skyfs_SS_update_state(amp_request_t *req)
{
    //skyfs_state_info_t state_info;
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_u32_t        layout_version;
    skyfs_u32_t        index;
    skyfs_s32_t        rc = 0;

    SKYFS_ERROR("__skyfs_SS_update_state:enter,fromid:%d\n", req->req_remote_id);

    msgp = __skyfs_get_msg(req->req_msg);
    layout_version = msgp->ver;

    memcpy(osd_status, 
            msgp->u.updatestateReq.osd_status,
            SKYFS_MAX_OSD_NUM * sizeof(skyfs_osd_status_t));    
   
    index = __skyfs_SS_judge_status(osd_this_id);
    if(index < 3 && osd_status[sort_osd_status[index]].state_info.request_num > osd_status[sort_osd_status[osd_num]].state_info.request_num * 1.5){
        SKYFS_ERROR("__skyfs_SS_update_state:this:%d,that:%d\n",
            osd_status[sort_osd_status[index]].state_info.request_num,
            osd_status[sort_osd_status[osd_num]].state_info.request_num);
        forced_to_split = 1; 
    }else{
        forced_to_split = 0;
    }

    size = AMP_SKYFS_MSGHEAD_SIZE;
    rc = __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;

    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_update_state:send reply failed.rc:%d\n", rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_SS_update_state:exit,rc:%d\n", rc);

}

void __skyfs_SS_init_config(amp_request_t *req)
{
    skyfs_msg_t        *msgp = NULL;
    skyfs_u32_t        size;
    skyfs_s32_t        rc = 0;

    SKYFS_ENTER("__skyfs_SS_init_config:enter,arch info size:%ld\n",
        sizeof(skyfs_arch_info_t));

    msgp = __skyfs_get_msg(req->req_msg);

    memcpy(&arch_info, 
        &(msgp->u.initconfigReq.arch_info), 
        sizeof(skyfs_arch_info_t));

    rc = __skyfs_init_nodes(&arch_info, &mds_info, &osd_info, &client_info);

    SKYFS_MSG("__skyfs_SS_init_config:osd_info.osd1:%d\n",
        osd_info.osd[1].id);
    sem_post(&osd_config_sem);

    size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_initconfig_ack_t);
    __skyfs_SS_init_reply(&req, &msgp, AMP_REPLY|AMP_MSG, 0, NULL, size);

    msgp->error = rc;
    rc = amp_send_sync(osd_comp_context, 
             req, 
             req->req_remote_type, 
             req->req_remote_id, 
             0); 
    if(rc < 0){ 
         SKYFS_ERROR("__skyfs_SS_init_config:send reply %d %d err:%d\n", 
            req->req_remote_type, req->req_remote_id, rc); 
    } 

    if(req->req_msg){ 
         free(req->req_msg); 
    }

    if(req->req_reply){ 
         free(req->req_reply); 
    }

    __amp_free_request(req); 

    SKYFS_LEAVE("__skyfs_SS_init_config:exit,rc:%d\n", rc);
}
skyfs_s32_t 
skyfs_SS_mv_obj(amp_request_t *req, skyfs_u32_t cold_osd_id)
{
    skyfs_msg_t *msgp = NULL;
    skyfs_u32_t req_type;
    //skyfs_u32_t req_size;
    skyfs_u32_t req_id;
    amp_kiov_t  *kiov;
    //skyfs_s8_t  *buf = NULL;

    skyfs_u32_t dest;
    skyfs_u32_t osd_id;
    skyfs_u32_t prev_osd = 0;

    skyfs_io_vector_t vec;
    skyfs_u32_t       subset_id;
    skyfs_u32_t       chunk_id;
    skyfs_ino_t       ino;
    skyfs_u64_t       obj_id;
    skyfs_u32_t       count;
    skyfs_u32_t       offset;
    skyfs_u32_t       hashkey;

    skyfs_htb_t       *htbp = NULL;
    skyfs_DL_subset_t *dl_subset = NULL;
    skyfs_DL_chunk_t  *dl_chunk = NULL;
    skyfs_DL_file_t   *dl_file= NULL;

    skyfs_s8_t        chunkfile[SKYFS_MAX_NAME_LEN];
    //skyfs_s32_t       fd = 0;
    skyfs_s32_t       rc = 0;

/*1. get the location info of the object*/
    msgp = __skyfs_get_msg(req->req_msg);
    memcpy(&vec, &(msgp->u.readObjReq.vec), sizeof(skyfs_io_vector_t));
    kiov = req->req_iov;
    req_type = msgp->fromType;
    req_id = msgp->fromid;
    ino = vec.ino;
    obj_id = vec.obj_id;
    offset = vec.offset;
    count = vec.count;
    hashkey = __skyfs_get_obj_hashvalue(ino, 0);


    SKYFS_ERROR_1("__skyfs_SS_mv:enter:ino:%llu,obj_id:%llu,offset:%d,count:%d,cold:%d.\n",
        ino, obj_id, offset, count, cold_osd_id);

    dest = msgp->u.readObjReq.dest;
    subset_id = msgp->u.readObjReq.subset;
    chunk_id = msgp->u.readObjReq.chunk;

    if(dest == osd_this_id){
        SKYFS_ERROR("__skyfs_SS_move:forwarded,ino:%llu,obj_id:%llu,from:%d,%d\n",
            ino, obj_id, req_type, req_id);
        goto forward_read_data;
    }

    htbp = __skyfs_SS_locate_dl_subset(ino, obj_id, &subset_id, &osd_id);
    if(htbp == NULL){
        rc = -ENOENT;
        SKYFS_ERROR("skyfs_SS_mv:ino:%llu,obj_id:%llu,sub:%u,from:%d,%d,forward:%d\n",
            ino, obj_id, subset_id, req_type, req_id, osd_id);
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
    pthread_rwlock_rdlock(&(htbp->rwlock));

    osd_id = __skyfs_SS_check_dl_htbcache(htbp);
    if(osd_id != 0){
        rc = -ENOENT;
        SKYFS_ERROR("__skyfs_SS_mv:ino:%llu,obj_id:%llu,htb not here,forward\n",
            ino, obj_id);
        pthread_rwlock_unlock(&(htbp->rwlock));
        __skyfs_SS_forward_request(req, SKYFS_OSD, osd_id);
        goto ERR_NONE;    
    }
#if 0
    dl_entry = __skyfs_SS_lookup_dl_entry(ino, obj_id, 
                    &subset_id, &chunk_id);
    if(dl_entry != NULL){
        goto read_data;
    }
#endif

    dl_subset = __skyfs_SS_get_dl_subset(htbp, subset_id);
    if(dl_subset == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&(htbp->rwlock));
        SKYFS_ERROR("__skyfs_SS_mv:ino:%llu,obj_id:%llu,sub error,subset:%d\n",
            ino, obj_id, subset_id);
        goto ERR;
    }
    pthread_rwlock_wrlock(&(dl_subset->rwlock));

    dl_chunk = __skyfs_SS_locate_dl_chunk(dl_subset, ino, obj_id, &chunk_id);
    if(dl_chunk == NULL){
        dl_chunk = __skyfs_SS_get_dl_chunk(dl_subset, chunk_id);
        if(dl_chunk == NULL){
            rc = -ENOENT;
            pthread_rwlock_unlock(&(dl_subset->rwlock));
            pthread_rwlock_unlock(&(htbp->rwlock));
            SKYFS_ERROR("__skyfs_SS_mv:ino:%llu,obj_id:%llu,chunk err,sub:%d,chunk:%d\n",
                ino, obj_id, subset_id, chunk_id);

            goto ERR;
        }
    }

    pthread_rwlock_unlock(&dl_chunk->rwlock);
    pthread_mutex_unlock(&dl_chunk->lock);

    dl_file = __skyfs_SS_locate_dl_file(dl_chunk, ino);
    if(dl_file == NULL){
        rc = -ENOENT;
        pthread_rwlock_unlock(&dl_chunk->rwlock);
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
        SKYFS_ERROR("__skyfs_SS_mv:ino:%llu,obj_id:%llu,entry err,sub:%d,chunk:%d\n",
            ino, obj_id, subset_id, chunk_id);

        goto ERR;
    }

//read_data:
/*2. set the location info of the object*/
    prev_osd = dl_file->real_location;
    dl_file->real_location= cold_osd_id;

forward_read_data:
/*3. move the object*/
       rc = __skyfs_SS_compose_chunkfile_pathname(subset_id, 
                    chunk_id,
                    ino,
                    obj_id,
                    chunkfile);
    if(rc < 0){
        SKYFS_ERROR("__skyfs_SS_mv:compose path err,ino:%llu,obj_id:%llu,subset:%d\n",
            ino,obj_id,subset_id);
        goto ERR;
    }

    rc = __skyfs_O2O_move_obj(cold_osd_id, subset_id, chunk_id, 
        ino, obj_id, chunkfile);
    if(rc < 0){
        if(dl_file){
            dl_file->real_location= prev_osd;
        }
        SKYFS_ERROR("__skyfs_SS_mv:mv obj err,ino:%llu,obj_id:%llu,subset:%d\n",
            ino,obj_id,subset_id);

        goto ERR;
    }

    if(dl_file){
        pthread_rwlock_unlock(&(dl_chunk->rwlock));
        pthread_rwlock_unlock(&(dl_subset->rwlock));
        pthread_rwlock_unlock(&(htbp->rwlock));
    }
       
    __skyfs_SS_forward_request(req, SKYFS_OSD, cold_osd_id);


ERR_NONE:

    if(msgp->type == SKYFS_MSG_O_WRITE_OBJ){
        if(kiov->ak_addr){
            free(kiov->ak_addr);
        }

        if(kiov){
               free(kiov);
        }
    }

    if(req->req_msg){
        free(req->req_msg);
    }

    if(req->req_reply){
        free(req->req_reply);
    }

    __amp_free_request(req);

    SKYFS_ERROR("__skyfs_SS_mv:leave:ino:%llu,obj_id:%llu,offset:%d,count:%d,sid:%d,chunkid:%d\n",
        ino, obj_id, offset, count, subset_id, chunk_id);


    pthread_mutex_lock(&osd_request_queue_lock);
    osd_nr_request --;
    pthread_mutex_unlock(&osd_request_queue_lock);
    return rc;

ERR:
    SKYFS_ERROR("__skyfs_SS_mv:error:ino:%llu,obj_id:%llu,offset:%d,count:%d\n",
        ino, obj_id, offset, count);

    pthread_mutex_lock(&osd_request_queue_lock);
    
    osd_nr_request ++;
    list_add_tail(&req->req_list, &osd_request_queue);

    pthread_mutex_unlock(&osd_request_queue_lock);

    rc = sem_post(&osd_request_queue_sem);

    return rc;
}
/*This is end of osd_dop.c*/
