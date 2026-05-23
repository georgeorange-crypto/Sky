#include "file_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

// 内部结构体定义
struct CacheContext {
    unsigned char *global_cache;
    size_t *file_offsets;   // 新增：记录每个文件在缓存中的起始偏移量
    size_t total_cache_size;
    size_t offset;          // 文件内读取偏移量
    size_t read_len;        // 每次读取的长度
    int file_count;
    pthread_mutex_t error_mutex; // 仅用于打印错误日志，不保护数据写入
};

// 内部线程任务参数
typedef struct {
    CacheContext *ctx;
    int start_file_idx;
    int end_file_idx;
    const char *base_path;
} ThreadArg;

// 内部线程处理函数
void *thread_task(void *arg) {
    ThreadArg *task_arg = (ThreadArg *)arg;
    CacheContext *ctx = task_arg->ctx;
    char filename[256];
    unsigned char *buffer = malloc(ctx->read_len);
    if (!buffer) return NULL;

    for (int i = task_arg->start_file_idx; i < task_arg->end_file_idx; i++) {
        snprintf(filename, sizeof(filename), "%s%d.bin", task_arg->base_path, i);

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

        // 读取数据
        ssize_t bytes_read = read(fd, buffer, ctx->read_len);
        close(fd);
        
        if (bytes_read != (ssize_t)ctx->read_len) {
            pthread_mutex_lock(&ctx->error_mutex);
            fprintf(stderr, "文件 %s 读取长度不足\n", filename);
            pthread_mutex_unlock(&ctx->error_mutex);
            continue;
        }

        // 【核心修改】根据文件索引 i 获取预先分配好的专属偏移量，直接写入对应位置
        // 因为每个文件对应的缓存区间是独立且互不重叠的，所以这里不需要加锁
        size_t target_offset = ctx->file_offsets[i];
        memcpy(ctx->global_cache + target_offset, buffer, ctx->read_len);
    }
    free(buffer);
    return NULL;
}

// 接口实现：初始化
CacheContext* cache_init(int file_count, size_t offset, size_t read_len) {
    CacheContext *ctx = (CacheContext *)malloc(sizeof(CacheContext));
    if (!ctx) return NULL;

    ctx->file_count = file_count;
    ctx->offset = offset;
    ctx->read_len = read_len;
    ctx->total_cache_size = (size_t)file_count * read_len;
    
    // 分配全局缓存
    ctx->global_cache = (unsigned char *)malloc(ctx->total_cache_size);
    if (!ctx->global_cache) {
        free(ctx);
        return NULL;
    }
    memset(ctx->global_cache, 0, ctx->total_cache_size);

    // 【核心修改】预计算每个文件在缓存中的专属偏移量（保证顺序对应）
    ctx->file_offsets = (size_t *)malloc(sizeof(size_t) * file_count);
    for (int i = 0; i < file_count; i++) {
        ctx->file_offsets[i] = (size_t)i * read_len;
    }

    pthread_mutex_init(&ctx->error_mutex, NULL);
    return ctx;
}

// 接口实现：执行读取
int cache_read_files(CacheContext *ctx, int thread_count, const char *base_path) {
    if (!ctx || thread_count <= 0) return -1;

    pthread_t *threads = malloc(sizeof(pthread_t) * thread_count);
    ThreadArg *args = malloc(sizeof(ThreadArg) * thread_count);
    int files_per_thread = ctx->file_count / thread_count;

    for (int i = 0; i < thread_count; i++) {
        args[i].ctx = ctx;
        args[i].start_file_idx = i * files_per_thread;
        args[i].end_file_idx = (i == thread_count - 1) ? ctx->file_count : (i + 1) * files_per_thread;
        args[i].base_path = base_path;

        pthread_create(&threads[i], NULL, thread_task, &args[i]);
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);
    return 0;
}

// 接口实现：获取数据指针
unsigned char* cache_get_data(CacheContext *ctx) {
    return ctx ? ctx->global_cache : NULL;
}

// 接口实现：获取实际大小
size_t cache_get_size(CacheContext *ctx) {
    return ctx ? ctx->total_cache_size : 0;
}

// 接口实现：销毁资源
void cache_destroy(CacheContext *ctx) {
    if (ctx) {
        pthread_mutex_destroy(&ctx->error_mutex);
        free(ctx->global_cache);
        free(ctx->file_offsets); // 释放偏移量数组
        free(ctx);
    }
}
