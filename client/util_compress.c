
#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif


#include <stdio.h>
#include <stdlib.h>
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

//#include "client_help.h"
//#include "client_init.h"
//#include "client_op.h"
//#include "client_cache.h"
//#include "client_ito.h"
#include "sz3c.h"

static uint64_t zstd_comp_time = 0;
static uint64_t zstd_comp_cnt = 0;

extern int isComp_zstd();


extern char * adm_compress_data(char * src, size_t src_len , size_t* pdest_len);
extern char * adm_decompress_data(char * src, size_t src_len , size_t* pdest_len);


extern uint8_t *  pans_compress_data(uint8_t * src, uint32_t src_len, uint32_t * pdest_len);
extern uint8_t *  pans_decompress_data(uint8_t * src, uint32_t src_len, uint32_t * pdest_len);


char *  compress_output(char * src, size_t  src_len, size_t * dest_len, int algorithm);
char *  decompress_output(char * csrc, size_t csrc_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf);



char *  decompress_nonstd_func(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char* pre_dest_buf)
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


char *  compress_nonstd_func(char * src, size_t src_len, size_t * dest_len, int algorithm)
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



char *  decompress_func(char * src, size_t src_len, size_t dest_buf_len, size_t * dest_len, int algorithm, char * pre_dest_buf)
{

	char * dest = pre_dest_buf;
	size_t ori_len = 0;

	// TODO: mayl  COMPRESS_GZSTD_ALGORITHM compressed data temperaryly decompressed with  COMPRESS_ZSTD_ALGORITHM 
	if(algorithm != COMPRESS_ZSTD_ALGORITHM && (algorithm != COMPRESS_GZSTD_ALGORITHM)){
		//SKYFS_ERROR("\n");
		return decompress_nonstd_func(src, src_len, dest_buf_len, dest_len, algorithm, pre_dest_buf);
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
                SKYFS_ERROR_1("Compression error: %s, algorithm %d, srclen %d, src[%x:%x:%x:%x] \n", ZSTD_getErrorName(ori_len), algorithm, src_len,
					src[0],src[1], src[2],src[3] );
                free(dest);
                return NULL;
        }

	*dest_len = ori_len;
        SKYFS_ERROR("decompress src data len %lu, dest data len %lu, buf %p \n", src_len, ori_len, dest);

	return dest;
}

char *  compress_func(char * src, size_t src_len, size_t * dest_len, int algorithm)
{
	char * dest = NULL;
	size_t compress_len = 0;
	struct timeval tv1, tv2;
	uint64_t zstd_comp_dur = 0;

	
	if(algorithm != COMPRESS_ZSTD_ALGORITHM){
		//SKYFS_ERROR("\n");
		return compress_nonstd_func(src, src_len, dest_len, algorithm);
	}
	//size_t compress_bound = ZSTD_compressBound((size_t) src_len);
	size_t compress_bound = src_len *2;
	dest = (char *)malloc(compress_bound);
	if(dest == NULL){
		SKYFS_ERROR_1("failed to get compressing for memory size %d \n", src_len);
		return NULL;
	}

	zstd_comp_cnt ++;

	gettimeofday(&tv1, NULL);
	compress_len = ZSTD_compress(dest, compress_bound, src, src_len, 2);
	gettimeofday(&tv2, NULL);
	zstd_comp_dur = tv2.tv_sec*1000000 + tv2.tv_usec;
	zstd_comp_dur -= (tv1.tv_sec*1000000 + tv1.tv_usec);
	zstd_comp_time += zstd_comp_dur;
	

	if(ZSTD_isError(compress_len)){
        	SKYFS_ERROR_1("Compression error: %s\n", ZSTD_getErrorName(compress_len));
		free(dest);
		return NULL;
	}
	if(zstd_comp_cnt >1000 && zstd_comp_cnt % 1000 == 3 ){
		SKYFS_ERROR_1("skyfs zstd compress cnt %lu, time %lu us\n", zstd_comp_cnt, zstd_comp_time);
	}
	*dest_len = compress_len;
	SKYFS_ERROR("compress src data len %lu, dest data len %lu\n", src_len, compress_len);
	return dest;



}

