#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>

#define NUM_THREADS 200
#define BUFFER_SIZE (1024 * 1024) // 1MB buffer size

char * shared_buf;
char * shared_prefix;
size_t total_to_write;
int write_blk_size;

void *write_to_file(void *threadid) {
    struct timeval tv1, tv2;
    unsigned long long write_time = 0;
    long tid = (long)threadid;
    char filename[100];
    int this_write = 0;
    snprintf(filename, sizeof(filename), "%s_%ld.bin", shared_prefix, tid);
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC| O_DIRECT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("Error opening file");
        return NULL;
    }

    gettimeofday(&tv1, NULL);

    //char buffer[BUFFER_SIZE];
    size_t bytes_write = 0;
#if 0
    FILE *input_file = fopen("large_file", "r");
    if (!input_file) {
        //perror("Error opening input file");
        close(fd);
        return NULL;
    }

    fseek(input_file, BUFFER_SIZE * tid, SEEK_SET); // Seek to the position for this thread's
#endif
    while (bytes_write < total_to_write) {
        this_write = write(fd, &shared_buf[bytes_write],write_blk_size); // Write data to file descriptor directly for performance reasons
	if(this_write == write_blk_size){
		bytes_write += this_write;
		
	}else{
		printf("write faild at offset %lu, ret %d\n", bytes_write, this_write);
		break;
	}
    }
    //fclose(input_file);
    close(fd);
    gettimeofday(&tv2, NULL);
    write_time = (unsigned long long )tv2.tv_sec * 1000000 + tv2.tv_usec;
    write_time -= ((unsigned long long )tv1.tv_sec * 1000000 + tv1.tv_usec);

    printf("thread %d write data %lu bytes, time %lu us \n", tid, bytes_write, write_time);
    return NULL;
}

int main(int argc, char * argv[]) {
    pthread_t threads[NUM_THREADS];
    char * prefix;
    char * src_name;
    int thread_cnt;
    int buf_size;
    int blk_count;
    long t;
    char * src_buf;

    size_t total_size;
    FILE * src_fp = NULL;

    char * xxx = NULL;
    //* xxx = 256;


    if(argc <6){
	    printf("Usage : dist_data src_name target_prefix thread_cnt buf_size blk_count, cur argc = %d\n", argc);
	    
	    exit(0);
    }
    src_name = argv[1];
    prefix = argv[2];
    thread_cnt = atoi(argv[3]);
    buf_size = atoi(argv[4]);
    blk_count = atoi(argv[5]);

    total_size = (size_t)buf_size;
    total_size *= (blk_count+10);
    printf("try to alloc mem %lu bytes , thread_cnt %d\n",total_size, thread_cnt);
    src_buf = (char *)malloc(total_size);
    printf("alloc memory return %p \n", src_buf);
    if(src_buf == NULL){
	    printf("can not alloc %lu bytes buf for reading data\n", total_size);
	    return 0;
    }
    src_fp = fopen(src_name, "r");
    if(src_fp == NULL){
	    printf("can not open src file %s\n", src_name);
	    free(src_buf);
	    return -2;
    }
    printf("src_fp opend %p\n", src_fp);



    shared_buf = src_buf;
    shared_prefix = prefix;
    write_blk_size = buf_size;
    total_to_write = (size_t)buf_size;
    total_to_write *= blk_count;


    size_t  read_cnt = 0;
    int this_read = 0;
    int read_op_cnt = 0;
    while(read_cnt < total_to_write){
	    this_read = fread(&src_buf[read_cnt], buf_size, 1, src_fp);
	    if(this_read > 0){
		    read_cnt += buf_size;
		    fseek(src_fp, read_cnt, SEEK_SET);
		    read_op_cnt ++;
		    if(read_op_cnt <2)
			    printf("op read %d ret %d\n", read_op_cnt, this_read);
	    }else{
		    printf("read src file faled at offset %llu, ret %d\n",read_cnt, this_read );
		    return -1;
	    }

    }   

    printf("total read src file data %lu bytes \n", read_cnt);


    for(t = 0; t < thread_cnt; t++) {
        int rc = pthread_create(&threads[t], NULL, write_to_file, (void *)t);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    for(t = 0; t < thread_cnt; t++) {
        pthread_join(threads[t], NULL);
    }
    printf("All threads completed.\n");
    free(src_buf);
    return 0;
}

