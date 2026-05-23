

#ifndef __GPU_COMPRESS_H
#define __GPU_COMPRESS_H

/*for ZSTD*/
extern int cuda_device_count;
void create_cuda_streams(void ** streams, int count);

void select_cuda_device(int devid);

void destroy_cuda_streams(void * streams, int count);

int launchKernel(void * *temp_dev_buffer, size_t buf_size, size_t count, size_t * psize) ;

void alloc_cuda_hostdata(char* host_data, size_t length);

void free_cuda_host_data(char* host_data, size_t length);

void alloc_cuda_memory(char* host_data, size_t length);

void free_cuda_memory(char* host_data);

int register_host_data(void* host_data,size_t length);

 int  unregister_host_data(void* host_data);


void call_gpu_zstd_compress(char* data, char * ret_data, size_t size, size_t blk_size, 
		void ** d_temp,size_t * d_temp_size,
                void * * streams,
                void * *  input_uncomp_data, void **  input_uncomp_ptrs , void ** input_uncomp_sizes,
                void * *  input_comp_data, void * * input_comp_ptrs , void ** input_comp_sizes, pthread_mutex_t * dev_mutex
		);
#endif // GPU_COMPRESS_h
/*for other such as lz4 ...*/
