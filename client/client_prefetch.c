#include "client_prefetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

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
//#include "client_op.h"
#include "client_cache.h"
#include "client_ito.h"
#include "sz3c.h"
#include "gpu_compress.h"


extern skyfs_s32_t __skyfs_C2O_submit_gpu_prefetch(skyfs_ino_t ino,
                const skyfs_s8_t *buf, // 数据缓存
                skyfs_u64_t offset,  // 文件预读偏移
                skyfs_u32_t size,   // 文件预读尺寸
                size_t *gpu_comp_size, // 返回的压缩数据尺寸
                int comp_type, // 压缩算子
                skyfs_u32_t * preal_comp_type, // 返回的压缩算子
                skyfs_s32_t * preal_fsize, // 实际返回的预读尺寸 
                size_t  * preal_foff  //  实际读起始位置
                );


// 内部结构体定义
struct CacheContext {
    skyfs_ino_t ino;
    unsigned char *global_cache;
    size_t *buf_offsets;   // 新增：记录每个预读项在缓存中的起始偏移量
    size_t total_cache_size;
    //size_t offset;          // 文件内读取偏移量
    //size_t read_len;        // 读取的长度, 在线程内部参数记录
    size_t * comp_sizes;      // 返回的压缩尺寸数组 
    //int file_count;
    pthread_mutex_t error_mutex; // 仅用于打印错误日志，不保护数据写入
};

// 内部线程任务参数
typedef struct {
    CacheContext *ctx;
    size_t start_file_offset; // 预读开始文件地址
    size_t block_size; // 单次预读尺寸（128KB）
    int start_file_idx; // 本次任务开始块号
    int end_file_idx;   // 本次任务结束块号
    //int * preal_fsizes;
    //int * pcomp_sizes;
    size_t * preal_foff;  // 返回的文件偏移数组
    size_t * preal_fsizes; // 返回的文件原始数据尺寸
    int * ret_comp_type;  // 返回的压缩算子数组
    int comp_type; 
    //const char *base_path;
} ThreadArg;

// 内部线程处理函数
void *thread_task(void *arg) {
    ThreadArg *task_arg = (ThreadArg *)arg;
    CacheContext *ctx = task_arg->ctx;
    //char filename[256];
    int rc = 0;
    unsigned char *buffer = malloc(task_arg->block_size);
    if (!buffer) { fprintf(stderr, "can not alloc 128KB buf for readahead \n");return NULL;}

    for (int i = task_arg->start_file_idx; i < task_arg->end_file_idx; i++) {
        //snprintf(filename, sizeof(filename), "%s%d.bin", task_arg->base_path, i);
#if 0
        int fd = open(filename, O_RDONLY);
        if (fd == -1) {
            pthread_mutex_lock(&ctx->error_mutex);
            fprintf(stderr, "无法打开文件: %s\n", filename);
            pthread_mutex_unlock(&ctx->error_mutex);
            continue;
        }


        // 将文件指针偏移到指定位置
        if (lseek(fd, ctx->offset, SEEK_SET) == -1) {
            close(fd);
            continue;
        }
#endif 

    	//unsigned char *buffer = NULL;
        // 读取数据
        ssize_t comp_size = 0;
        uint32_t  real_fsize = 0, ret_comp_type = 0;
        size_t real_foff;
        
	rc = __skyfs_C2O_submit_gpu_prefetch(ctx->ino,
                (char*)buffer,
                task_arg->start_file_offset + task_arg->block_size*i,
                task_arg->block_size,
                &comp_size,
                task_arg->comp_type,
                &ret_comp_type,  
                &real_fsize,
                &real_foff
                );
	
	if(rc >=0){
		//bytes_read = rc;
		if(comp_size == task_arg->block_size){
			// 不压缩
			ret_comp_type = 0;
		}
		task_arg->preal_foff[i] = real_foff;
		task_arg->preal_fsizes[i] = real_fsize;
		task_arg->ret_comp_type[i] = ret_comp_type;
                ctx->comp_sizes[i] = comp_size;
			 
		

	}else{
		ctx->comp_sizes[i] = rc ;
	}
        //close(fd);
        #if 0
        if (bytes_read != (ssize_t)ctx->read_len) {
            pthread_mutex_lock(&ctx->error_mutex);
            fprintf(stderr, "文件 %s 读取长度不足\n", filename);
            pthread_mutex_unlock(&ctx->error_mutex);
            continue;
        }

        // 【核心修改】根据文件索引 i 获取预先分配好的专属偏移量，直接写入对应位置
        // 因为每个文件对应的缓存区间是独立且互不重叠的，所以这里不需要加锁
        size_t target_offset = ctx->file_offsets[i];
       #endif
        // 每次得到压缩数据放置在block 边界， 每块待解压的压缩数据不相邻！！
        size_t target_offset =  task_arg->block_size*i;
        ctx->buf_offsets[i] = target_offset;
	if(comp_size > 0 && rc > 0 )
        	memcpy(ctx->global_cache + target_offset, buffer, comp_size);
    }
    free(buffer);
    return NULL;
}
#if 0
// 接口实现：初始化
CacheContext* cache_init(int block_count, size_t offset, size_t read_len) {
    CacheContext *ctx = (CacheContext *)malloc(sizeof(CacheContext));
    if (!ctx) return NULL;

    //ctx->file_count = file_count;
    //ctx->offset = offset;
    //ctx->read_len = read_len;
    ctx->total_cache_size = (size_t)block_count * read_len;
    
    // 分配全局缓存
    ctx->global_cache = (unsigned char *)malloc(ctx->total_cache_size);
    if (!ctx->global_cache) {
        free(ctx);
        return NULL;
    }
    //memset(ctx->global_cache, 0, ctx->total_cache_size);

    // 【核心修改】预计算每个文件在缓存中的专属偏移量（保证顺序对应）
    //ctx->file_offsets = (size_t *)malloc(sizeof(size_t) * file_count);
    //for (int i = 0; i < file_count; i++) {
      //  ctx->file_offsets[i] = (size_t)i * read_len;
    //}

    pthread_mutex_init(&ctx->error_mutex, NULL);
    return ctx;
}
#endif
// 接口实现  初始化CTX

// 接口实现：执行读取

unsigned char *  prefetch_init_ctx(void){
	CacheContext *ctx = ( CacheContext *)malloc(sizeof(CacheContext));
	return (unsigned char *) ctx; 	
}

int prefetch_cache(CacheContext *ctx, int thread_count, size_t start_foffset, 
	int block_size, int block_count, int comp_type, uint32_t * ret_comp_type , uint32_t * comp_sizes,
	size_t * preal_foff, size_t * preal_fsizes) 
{
    if (!ctx || thread_count <= 0) return -1;

    
    // 分配全局缓存
    ctx->global_cache = (unsigned char *)malloc(block_size*block_count);
    ctx->comp_sizes = comp_sizes;
    pthread_t *threads = malloc(sizeof(pthread_t) * thread_count);
    ThreadArg *args = malloc(sizeof(ThreadArg) * thread_count);
    int files_per_thread = block_count / thread_count;

    for (int i = 0; i < thread_count; i++) {
        args[i].ctx = ctx;
        args[i].start_file_idx = i * files_per_thread;
	args[i].block_size = block_size;
        args[i].comp_type = comp_type;
        args[i].end_file_idx = (i == thread_count - 1) ? block_count : (i + 1) * files_per_thread;
        //args[i].base_path = base_path;
	args[i].start_file_offset = start_foffset;
	args[i].ret_comp_type = ret_comp_type;
	args[i].preal_foff = preal_foff;
	args[i].preal_fsizes = preal_fsizes;
        

        pthread_create(&threads[i], NULL, thread_task, &args[i]);
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);
    return 0;
}
#if 0
// 接口实现：获取数据指针
unsigned char* cache_get_data(CacheContext *ctx) {
    return ctx ? ctx->global_cache : NULL;
}

// 接口实现：获取实际大小
size_t cache_get_size(CacheContext *ctx) {
    return ctx ? ctx->total_cache_size : 0;
}
#endif
// 接口实现：销毁资源
void prefetch_cache_destroy(CacheContext *ctx) {
    if (ctx) {
        pthread_mutex_destroy(&ctx->error_mutex);
        free(ctx->global_cache);
        //free(ctx->file_offsets); // 释放偏移量数组
        free(ctx);
    }
}
