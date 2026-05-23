#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <zstd.h>

#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"


#include "skyfs_fs.h"
#include "skyfs_debug.h"
#include "client_compress_thread.h"

#define COMP_NUM_THREADS (64)
#define COMP_NUM_TASKS (4)
#define GCOMP_NUM_TASKS (66)
//#define GCOMP_INDEX_RANGE (16)
#define GCOMP_GROUP_RANGE (4)

sem_t task_sem[COMP_NUM_THREADS];       // 任务同步信号量
sem_t done_sem[COMP_NUM_THREADS];       // 任务完成信号量

pthread_t compress_threads[COMP_NUM_THREADS];

pthread_mutex_t compress_task_mutex_locks[COMP_NUM_TASKS];// 最大并发任务锁数组

pthread_mutex_t gcompress_task_mutex_locks[GCOMP_NUM_TASKS];// 最大并发任务锁数组
compress_task_t  comp_tasks[COMP_NUM_THREADS];


extern void do_pre_split_p_v0(double * data, size_t num_doubles , uint8_t * lanes, size_t start_index, size_t len);
extern void do_post_merge_v0( uint8_t * lanes, size_t num_doubles, double * ori_data);

pthread_mutex_t * get_dev_mutex(int dev_idx)
{
	int delta = random();
	delta = delta % 5;
	return  &gcompress_task_mutex_locks[dev_idx * 8 +16 + delta];
}
int lock_gpu_comp_task(int gtask_num)
{
	int start_index = (gtask_num/GCOMP_GROUP_RANGE) * GCOMP_GROUP_RANGE;
	int this_index = start_index + (((int)random()) % GCOMP_GROUP_RANGE); 
	pthread_mutex_lock( &gcompress_task_mutex_locks[this_index]);
	return this_index;
}

void unlock_gpu_comp_task(int gtask_index)
{

	pthread_mutex_unlock( &gcompress_task_mutex_locks[gtask_index]);
	return ;
}

int lock_gpu_init_task(int dev_idx)
{
	int delta = random();
	int select_idx = 0;
	delta = 0;
	select_idx = dev_idx*8+32 +delta;
	//int start_index = (gtask_num/GCOMP_GROUP_RANGE) * GCOMP_GROUP_RANGE;
	//int this_index = start_index + (((int)random()) % GCOMP_GROUP_RANGE); 
	pthread_mutex_lock( &gcompress_task_mutex_locks[select_idx]);
	return select_idx;
}

void unlock_gpu_init_task(int gtask_index)
{

	pthread_mutex_unlock( &gcompress_task_mutex_locks[gtask_index]);
	return ;
}




void* compress_thread_function(void* arg) {
   compress_task_t * this_task = NULL;
   int task_idx = 0;
   int compressed_blk_cnt ;
   int op_type = 0;
   char * compress_ptr = NULL;
   char * uncompress_ptr = NULL;
   size_t start_offset = 0;
	


    while (1) {
	
        
        // 等待任务信号量
	this_task = (compress_task_t *)arg;
	task_idx = this_task->task_idx;
        sem_wait(&task_sem[task_idx]);
	op_type = this_task->op_type;
	//fprintf(stderr, "get  semphores to task_id   %d, op %d\n", task_idx, op_type);

        //printf("Thread %ld executing task\n", (long)arg);
        // 模拟任务执行时间
	if(op_type == 0 || (op_type != 0x01 && op_type != 0x10000001)){
        	usleep(50);
	}else{
		// do comprss for all blks in this task one by one
		
		
		int wait_prop = this_task->need_prop & 0x01;
		//int idx = 0;
		int do_prop = this_task->need_prop & 0x10;
		compress_ptr = this_task->compress_buf;
		uncompress_ptr = this_task->uncompress_buf;
		compressed_blk_cnt = 0;
		size_t compress_ret_len = 0;
		size_t compress_len = this_task->compress_blk_size;
		size_t uncompress_len = this_task->uncompress_blk_size;
		uint8_t * real_uncompress_ptr = NULL;
                SKYFS_ERROR_1("start_compress : start compress, optype %d, need_prop %x, buf_idx %d\n ", 
						op_type,  this_task->need_prop, this_task->buf_idx);
		start_offset = this_task->start_offset;
		if(wait_prop){
		   do_pre_split_p_v0((double *)this_task->uncompress_buf, (this_task->ori_size)/sizeof(double), 
			(uint8_t*)(this_task->prop_buf), start_offset/sizeof(double),
			uncompress_len * this_task->blk_cnt/sizeof(double) );
		   	this_task->need_prop =  0x10;
			SKYFS_ERROR("DO prop : data %p , num_doubles %lu, prop_buf %p , start_double_idx %lu, this_num_doubles %lu \n",
					 this_task->uncompress_buf, (this_task->ori_size)/sizeof(double),
					 this_task->prop_buf,  start_offset/sizeof(double), uncompress_len * this_task->blk_cnt/sizeof(double));
		   goto end_prop;
		}
		//fprintf(stderr, "start  to run task_id   %d, op %d\n", task_idx, op_type);
		while(compressed_blk_cnt < this_task->blk_cnt){
			real_uncompress_ptr = uncompress_ptr;
			
			if(do_prop){
				// TODO: mayl, uncompress_ptr is not right!!
				//do_pre_split_p_v0((double *)this_task->ori_uncompress_buf, this_task->ori_size/sizeof(double), 
				//	(uint8_t*)(this_task->prop_buf), start_offset,uncompress_len  );
				real_uncompress_ptr = &this_task->prop_buf[start_offset];
				//start_offset += uncompress_len;
			}
			this_task->compressed_chunk_sizes[compressed_blk_cnt] = 0;
			if(op_type & 0x10000000 && 0){
#if 1
				// now do decompress according to optype
				compress_len = this_task->compress_data_sizes[compressed_blk_cnt];
				if(compress_len >= SKYFS_OBJECT_NODE_SIZE){
					memcpy(real_uncompress_ptr, compress_ptr, SKYFS_OBJECT_NODE_SIZE );						
				}else{
					// this is the max compressed len
					//uncompress_len  = SKYFS_OBJECT_NODE_SIZE;
					//uncompress_len = this_task->compress_sizes[]; // TODO: set the compress_sizes
					//uncompres_len here is compress len !!, need to set by caller
					compress_ret_len = ZSTD_decompress(real_uncompress_ptr, uncompress_len,
                                        compress_ptr, compress_len); 
				}
#endif
				
			}else{
				// now do compress according to optype
				compress_ret_len = ZSTD_compress(compress_ptr, compress_len, 
					real_uncompress_ptr, uncompress_len, 2);
				if(this_task->buf_idx  == 0  && compressed_blk_cnt == 0){
					char * tmp1 = (char *)compress_ptr;
					fprintf(stderr,"First para compress len %d, src [%x:%x:%x:%x]\n ", compress_ret_len, tmp1[0], tmp1[1] , tmp1[2], tmp1[3]);
					tmp1 = (char *)real_uncompress_ptr;
					fprintf(stderr,"First para uncompress len %d, src [%x:%x:%x:%x]\n ", compress_ret_len, tmp1[0], tmp1[1] , tmp1[2], tmp1[3]);
				}
			}

			if(ZSTD_isError(compress_ret_len)){
        			fprintf(stderr, "Compression error: %s\n", ZSTD_getErrorName(compress_len));
				break;
			}
                        if(compress_ret_len >= uncompress_len && (op_type & 0x10000000 ==0)){
				SKYFS_ERROR_1("get CPU ccmpress data bigger than original data, %lu, %lu  \n", compress_ret_len , uncompress_len);
                                compress_ret_len = uncompress_len;
                                // copy original data 
                                memcpy(compress_ptr,  real_uncompress_ptr, compress_ret_len);  
			}
			SKYFS_ERROR("zstd_compress , uncomp buf %p , uncmp_len %lu, comp_len %lu, start_offset %lu \n", 
				real_uncompress_ptr,  uncompress_len,  compress_ret_len, start_offset);
			this_task->compressed_chunk_sizes[compressed_blk_cnt] = compress_ret_len;
			// for decompress
			//this_task->uncompressed_chunk_sizes[compressed_blk_cnt] = compress_ret_len;
			
			this_task->compress_buf_ptrs[compressed_blk_cnt] = compress_ptr;

			compressed_blk_cnt++;

			//if(op_type & 0x10000000 ==0){
			uncompress_ptr += uncompress_len;
			//}else{
				// TODO
			//}
			if(do_prop){
			      start_offset += uncompress_len;
			}
			//if(op_type & 0x10000000 ==0){
				
			//}else{
			     compress_ptr += compress_len;
			//}

		}
		this_task->complete_blks = compressed_blk_cnt;
		//fprintf(stderr, "end  to run task_id   %d, op %d\n", task_idx, op_type);



	}
        //printf("Thread %ld finished task\n", (long)arg);
end_prop:
        // 触发完成信号量
        sem_post(&done_sem[task_idx]);
    }
    return NULL;
}




void init_compress_worker_threads()
{
	// 初始化 信号量数组
	for(int i = 0; i<COMP_NUM_THREADS;i++){

    		sem_init(&task_sem[i], 0, 0);  // 初始值为0，表示没有任务可做
    		sem_init(&done_sem[i], 0, 0);  // 初始值为0，表示没有任务完成
	}
	// 初始化 压缩线程组锁
	for(int j = 0; j<COMP_NUM_TASKS;j++){
		pthread_mutex_init(&compress_task_mutex_locks[j],NULL);
	}

	for(int j = 0; j<GCOMP_NUM_TASKS;j++){
		pthread_mutex_init(&gcompress_task_mutex_locks[j],NULL);
	}

	
	// 初始化压缩任务参数
	
	// 创建 压缩线程池
	for(int n = 0; n<COMP_NUM_THREADS; n++){
		memset(&comp_tasks[n],0, sizeof(compress_task_t));
		comp_tasks[n].task_idx = n;
        	pthread_create(&compress_threads[n], NULL, compress_thread_function, (void*)(&comp_tasks[n]));

	}

	
}

int  run_multithead_compress(compress_task_t * comp_task)
{
	// 确定压缩工作组
	int work_group_id = random() % COMP_NUM_TASKS;
	int start_worker_id = work_group_id *(COMP_NUM_THREADS / COMP_NUM_TASKS);
	int tasks_per_thread = comp_task->blk_cnt/(COMP_NUM_THREADS / COMP_NUM_TASKS);
	int n = 0; // 本次任务启动的线程数量
	int total_tasks = comp_task->blk_cnt;
	char * compress_ptr;
	char * uncompress_ptr;
	//size_t * compress_sizes_ptr;
	int x = 0;
	int rc = 0;
	int complete_tasks = 0;
	size_t start_offset = 0;
	

	if(tasks_per_thread == 0){
		tasks_per_thread =1;
	}


	compress_task_t * curr_task = NULL;

	// 设置所有参数到各个压缩线程
	// 1 获取线程组锁
	pthread_mutex_lock(&(compress_task_mutex_locks[work_group_id]));
	// 2 设置参数
	compress_ptr = comp_task->compress_buf;
	uncompress_ptr = comp_task->uncompress_buf;
	//compress_sizes_ptr = comp_task->compressed_chunk_sizes;


	//int buf_idx = 0;
	while(total_tasks > 0){

		curr_task = &comp_tasks[start_worker_id+n];
		curr_task->task_idx = start_worker_id+n;
		curr_task->ino = comp_task->ino;
		curr_task->buf_idx = x;
		curr_task->compress_data_sizes = comp_task-> compress_data_sizes;

		if(total_tasks >=  tasks_per_thread)
			curr_task->blk_cnt =  tasks_per_thread;
		else
			curr_task->blk_cnt =  total_tasks;

		curr_task->complete_blks = 0;
		curr_task->compress_buf = compress_ptr;
		curr_task->uncompress_buf = uncompress_ptr;


		curr_task->op_type = comp_task->op_type;
		curr_task->compress_blk_size = comp_task->compress_blk_size;
		curr_task->uncompress_blk_size = comp_task->uncompress_blk_size;
		curr_task->compressed_chunk_sizes = &(comp_task->compressed_chunk_sizes[x]);
		curr_task->compress_buf_ptrs = &(comp_task->compress_buf_ptrs[x]);

                
		curr_task->uncompressed_chunk_sizes = &(comp_task->uncompressed_chunk_sizes[x]);


		curr_task->compress_buf = compress_ptr;
		curr_task->uncompress_buf = uncompress_ptr;
		curr_task->prop_buf = comp_task->prop_buf;
		curr_task->ori_size = comp_task->ori_size;
		if(comp_task->need_prop)
			curr_task->need_prop = comp_task->need_prop ;
		curr_task->ori_uncompress_buf = comp_task->uncompress_buf;
		curr_task->start_offset = start_offset;
		
		compress_ptr += (curr_task->compress_blk_size * curr_task->blk_cnt);
		uncompress_ptr += (curr_task->uncompress_blk_size * curr_task->blk_cnt);
		
		start_offset  += (curr_task->uncompress_blk_size * curr_task->blk_cnt);
		
		total_tasks -= curr_task->blk_cnt;
		x += curr_task->blk_cnt;

		n++;


	}
	// do_prop:
	
	//fprintf(stderr, "send semphores to group , start by %d\n", start_worker_id);
	// 3 触发启动信号量
	int wait_prop = comp_task->need_prop;
	if(wait_prop){
		SKYFS_ERROR("DO_prop doubles %lu\n", (comp_task->ori_size)/sizeof(double));
		 do_pre_split_p_v0((double *)comp_task->uncompress_buf, (comp_task->ori_size)/sizeof(double),
                        (uint8_t*)(comp_task->prop_buf), 0,
                        comp_task->ori_size/sizeof(double) );

	}
	for(int i = 0; i<n; i++){
        	sem_post(&task_sem[start_worker_id]+i);
	}
	// 4 等待结束信号量
	//fprintf(stderr, "wait semphores to group , start by %d\n", start_worker_id);
	for(int j = 0; j<n; j++){
        	sem_wait(&done_sem[start_worker_id]+j);
		curr_task = &comp_tasks[start_worker_id+j];
		//if(!wait_prop)
			rc += curr_task->complete_blks;
	}
	/*
	if(wait_prop){
	    wait_prop = 0;
	    for(int i = 0; i<n; i++){
		
                curr_task = &comp_tasks[start_worker_id+i];
		curr_task->need_prop = 0x10;
                sem_post(&task_sem[start_worker_id]+i);
            }
	    for(int j = 0; j<n; j++){
                sem_wait(&done_sem[start_worker_id]+j);
                curr_task = &comp_tasks[start_worker_id+j];
                //if(!wait_prop)
                rc += curr_task->complete_blks;
           }


	}*/
	

	//fprintf(stderr, "wait semphores to group , start by %d, Done\n", start_worker_id);
	// 释放工作组锁
	pthread_mutex_unlock(&(compress_task_mutex_locks[work_group_id]));
	if(rc < comp_task->blk_cnt){
		fprintf(stderr, "compress error ! try to compress %d blks , complete %d\n",
				comp_task->blk_cnt, rc);
		rc = -EIO;
	}
	return rc;

	
}
#if 0
int main() {
    pthread_t threads[NUM_THREADS];
    long i;

    // 初始化信号量
    sem_init(&task_sem, 0, 0);  // 初始值为0，表示没有任务可做
    sem_init(&done_sem, 0, 0);  // 初始值为0，表示没有任务完成

    // 创建线程池
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_function, (void*)i);
    }

    // 分配任务并等待完成（模拟）
    for (i = 0; i < NUM_TASKS; i++) {
        // 触发任务信号量以分配任务
        sem_post(&task_sem);
        printf("Main: Task %ld posted\n", i);
        // 等待一个任务完成
        sem_wait(&done_sem);
        printf("Main: Task %ld completed\n", i);
    }

    // 清理并退出
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    sem_destroy(&task_sem);
    sem_destroy(&done_sem);
    return 0;
}
#endif
