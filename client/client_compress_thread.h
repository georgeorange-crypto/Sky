#ifndef CLIENT_COMPRESS_TASK_H
#define CLIENT_COMPRESS_TASK_H


typedef struct compress_task{
	uint64_t ino;
	uint64_t buf_idx;
	uint64_t ori_size;
	char * * compress_buf_ptrs;
	//size_t uncompress_buf_size;
	size_t compress_blk_size;
	size_t uncompress_blk_size;
	size_t * compressed_chunk_sizes;
	size_t * uncompressed_chunk_sizes;
	int    blk_cnt;
	int    complete_blks;
	int    task_idx;
	int    op_type; // the highest bit is 1 means decompress
	char * compress_buf;
	char * uncompress_buf;
	char * ori_uncompress_buf;
	size_t * compress_data_sizes; //待解压的分段压缩数据长度 
	char * prop_buf;
	int    need_prop;
	size_t start_offset;


} compress_task_t;


extern void init_compress_worker_threads();

extern int  run_multithead_compress(compress_task_t * comp_task);


#endif
