 #include <unistd.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <errno.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <sys/time.h>
 #include <fcntl.h>

 void launchKernel(void * temp_dev_buffer);
 void call_gpu_zstd_compress(char* data, int size,
                        size_t warmup_iteration_count, size_t total_iteration_count,
			size_t * comp_bufs_count, size_t * comp_buf_sizes, char ** comp_bufs, 
			void ** pbatch);
 void free_cpu_batch(void * cpu_batch);

 int main(int argc, char* argv[])
{
	int fd = 0;
	char * data = NULL;
	size_t data_size = 0;
	size_t  read_size = 0;
	fd = open(argv[1], O_RDONLY);
	data_size = atol(argv[2]);
	if(fd <= 0){
		printf("can not open file %s for compressing \n", argv[1]);
		return -ENOENT;
	}
	data = (char *)malloc(data_size);
	if(data == NULL){
		
		printf("can not alloc memory for  file %s for compressing \n", argv[1]);
		return -ENOMEM;
	}
	printf("start read file \n");
	read_size = read(fd, data, data_size);
	if(read_size <=0){
		printf("read file failed \n");
		return -EIO;
	}
	if(read_size < data_size){
		printf("Warning! , to read %lu, got %lu\n ", data_size, read_size);
	}
	// now call gpu compress
	
	printf("start compress_data file \n");
	void * temp_dev_buffer = NULL;

	char * comp_bufs[260];
	size_t comp_bufs_sizes[260];
	size_t comp_buf_count = 0;
	void * cpu_batch = NULL;
	int loop_count = 0;

        launchKernel(&temp_dev_buffer); 

	struct timeval tv1, tv2;
	gettimeofday(&tv1, NULL);

	for(loop_count = 0 ; loop_count<10; loop_count++ ){
		cpu_batch = NULL;
		call_gpu_zstd_compress(data,read_size,0,1,&comp_buf_count, comp_bufs_sizes, comp_bufs, &cpu_batch);
		if(loop_count == 9 || loop_count == 8){
			for(int xx = 0 ; xx <16 ; xx++ ){
				printf("comp bufs[%d]: %p, comp size 0x%lx -- %lu\n", xx, comp_bufs[xx], comp_bufs_sizes[xx], comp_bufs_sizes[xx]);
			}
		}
		free_cpu_batch(cpu_batch);
	}

	gettimeofday(&tv2, NULL);

     	printf("compress start %lu.%06lu , end at %lu.%06lu\n", tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec);	
	//printf("buf 5 size %lu, buf 5 data[0] %x \n", comp_bufs_sizes[5], comp_bufs[5][0]);
	 //comp_bufs[5][1000] = 99;
	
	printf("\n end compress_data file \n");
	//
	return 0;


}

       //ssize_t read(int fd, void *buf, size_t count);

