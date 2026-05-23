#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>


long long str_to_val(const char * str)
{
    char unit_flag = str[strlen(str) - 3];
    long long val = atoll(str);

    if ((unit_flag == 'k') || (unit_flag == 'K'))
        val *= (long long)(1024);

    if ((unit_flag == 'm') || (unit_flag == 'M'))
        val *= (long long)(1024*1024);
    
    return val;
}

long long get_proc_used_mem()
{
    pid_t proc_pid;
    char  status_fpath[1024] = {0};
    char  line_buf[1024] = {0};
    FILE * status_f;
    long long mem_used = 0;
    const char mem_flag_str[] = "VmRSS:";

    proc_pid = getpid();
    sprintf(status_fpath, "/proc/%lld/status", (long long)proc_pid);
    status_f = fopen(status_fpath, "r");
    if (status_f == NULL) {
        fprintf(stderr, "Open %s error. %s\n", status_fpath, strerror(errno));
        return (long long)(0);
    }

    while (fgets(line_buf, 1024, status_f) != NULL) {
        if (strncmp(line_buf, mem_flag_str, strlen(mem_flag_str)) == 0) {
            mem_used = str_to_val(line_buf + strlen(mem_flag_str));
            break;
        }
    }

    fclose(status_f);

    return mem_used;
}

long long get_sys_free_mem()
{
    const char meminfo_fpath[] = "/proc/meminfo";
    FILE  * meminfo_f;
    long long mem_free = 0;
    char line_buf[1024] = {0};
    const char mem_free_str[] = "MemFree:";
    const char buffer_str[] = "Buffers:";
    const char cached_str[] = "Cached:";

    meminfo_f = fopen(meminfo_fpath, "r");
    if (meminfo_f == NULL) {
        fprintf(stderr, "Open %s error. %s\n", meminfo_fpath, strerror(errno));
        return (long long)(0);
    }

    while (fgets(line_buf, 1024, meminfo_f)) {
        if (strncmp(line_buf, mem_free_str, strlen(mem_free_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(mem_free_str));

        if (strncmp(line_buf, buffer_str, strlen(buffer_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(buffer_str));

        if (strncmp(line_buf, cached_str, strlen(cached_str)) == 0)
            mem_free += str_to_val(line_buf + strlen(cached_str));
    }

    fclose(meminfo_f);

    return mem_free;
}

int main()
{
    char * buf;

    buf = malloc(1024*7680);
    memset(buf, 1, 1024*7680);

    printf("proc mem used: %lld\n", get_proc_used_mem());
    printf("proc mem free: %lld\n", get_sys_free_mem());

    return 0;
}
