#include "file_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// 内部结构体定义
struct CacheContext {
    unsigned char *global_cache;
    size_t total_cache_size;
    size_t offset;
    size_t read_len;
    int file_count;
    size_t cache_write_pos;
    pthread_mutex_t cache_mutex;
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
        if (fd == -1) continue;

        if (lseek(fd, ctx->offset, SEEK_SET) == -1) {
            close(fd);
            continue;
        }

        ssize_t bytes_read = read(fd, buffer, ctx->read_len);
        close(fd);
        if (bytes_read != (ssize_t)ctx->read_len) continue;

        // 线程安全写入全局缓存
        pthread_mutex_lock(&ctx->cache_mutex);
        if (ctx->cache_write_pos + ctx->read_len <= ctx->total_cache_size) {
            memcpy(ctx->global_cache + ctx->cache_write_pos, buffer, ctx->read_len);
            ctx->cache_write_pos += ctx->read_len;
        }
        pthread_mutex_unlock(&ctx->cache_mutex);
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
    ctx->total_cache_size = (size_t)file_count * read_len; // 预计算所需总缓存大小
    ctx->cache_write_pos = 0;
    
    ctx->global_cache = (unsigned char *)malloc(ctx->total_cache_size);
    if (!ctx->global_cache) {
        free(ctx);
        return NULL;
    }
    memset(ctx->global_cache, 0, ctx->total_cache_size);
    pthread_mutex_init(&ctx->cache_mutex, NULL);
    
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
    return ctx ? ctx->cache_write_pos : 0;
}

// 接口实现：销毁资源
void cache_destroy(CacheContext *ctx) {
    if (ctx) {
        pthread_mutex_destroy(&ctx->cache_mutex);
        free(ctx->global_cache);
        free(ctx);
    }
}
