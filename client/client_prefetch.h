#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <stddef.h> // 引入 size_t 类型

// 缓存上下文结构体（隐藏内部实现细节）
typedef struct CacheContext CacheContext;

// 初始化缓存上下文
// file_count: 文件总数, offset: 读取偏移量, read_len: 每次读取长度
CacheContext* cache_init(int file_count, size_t offset, size_t read_len);

// 执行多线程并发读取并聚合到缓存
// ctx: 缓存上下文, thread_count: 线程数量, base_path: 文件路径前缀(如 "test_files/file_")
int cache_read_files(CacheContext *ctx, int thread_count, const char *base_path);

// 获取聚合后的缓存数据指针
unsigned char* cache_get_data(CacheContext *ctx);

// 获取实际读取到的总字节数
size_t cache_get_size(CacheContext *ctx);

// 释放缓存上下文及内存
void cache_destroy(CacheContext *ctx);

#endif
