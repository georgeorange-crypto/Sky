#include <stdio.h>
#include "file_cache.h"

#define FILE_COUNT 512
#define OFFSET (256 * 1024)    // 256KB
#define READ_LEN (128 * 1024)  // 128KB
#define THREAD_COUNT 4

int main() {
    printf("正在初始化动态库接口...\n");
    // 1. 调用库接口初始化
    CacheContext *ctx = cache_init(FILE_COUNT, OFFSET, READ_LEN);
    if (!ctx) {
        printf("初始化缓存上下文失败！\n");
        return -1;
    }

    // 2. 调用库接口执行并发读取 (假设测试文件在当前目录下的 test_files/ 文件夹中)
    printf("开始使用 %d 个线程并发读取文件...\n", THREAD_COUNT);
    if (cache_read_files(ctx, THREAD_COUNT, "test_files/file_") != 0) {
        printf("读取文件过程中发生错误！\n");
        cache_destroy(ctx);
        return -1;
    }

    // 3. 获取结果并校验
    unsigned char *data = cache_get_data(ctx);
    size_t size = cache_get_size(ctx);

    printf("\n--- 动态库调用测试完成 ---\n");
    printf("总共读取并合并的数据量: %zu 字节 (%.2f MB)\n", size, (double)size / (1024 * 1024));
    
    printf("缓存前 16 个字节校验: ");
    for (int i = 0; i < 16 && i < (int)size; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    // 4. 释放资源
    cache_destroy(ctx);
    return 0;
}
