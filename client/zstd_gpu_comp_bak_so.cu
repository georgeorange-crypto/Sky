/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <sys/time.h>

#include "util.h"
#include "zstd.h"
#include "nvcomp/zstd.h"
//#include "BatchData.h"

static int cycles = 1;

/*
BatchDataCPU GetBatchDataCPU(const BatchData& batch_data, bool copy_data)
{
  BatchDataCPU batch_data_cpu(
      batch_data.ptrs(),
      batch_data.sizes(),
      batch_data.data(),
      batch_data.size(),
      copy_data);
  return batch_data_cpu;
}
*/
static void run_compress(void * ori_data, // include all data 
			 void * ret_data,
			void * * data_ptrs,
			size_t * sizes,
			int blk_size, // 128KB means 17
			size_t data_len,
			size_t warmup_iteration_count, size_t total_iteration_count,
			void * * pd_temp, size_t * p_temp_size,
                        cudaStream_t * streams,
			void * *  input_uncomp_data, void * * input_uncomp_ptrs , void ** input_uncomp_sizes,
			void * *  input_comp_data, void * * input_comp_ptrs , void ** input_comp_sizes
	       	)
{
	/*1 alloc uncompress data , ptrs , and sizes */
	/*1.1  alloc d_temp */ 
	
  	nvcompStatus_t status = nvcompSuccess;
	size_t comp_temp_bytes;
  	uint64_t dur1 = 0;
  	uint64_t dur2 = 0;
  	uint64_t dur3 = 0;
  	size_t chunk_size = blk_size;
	struct timeval tv1, tv2;

  	
	
  	size_t chunk_count = (data_len + chunk_size -1)/chunk_size;

  	//static_assert(chunk_size <= ZSTD_BLOCKSIZE_MAX, "Chunk size must be less than the constant specified in the Zstandard library");
  	//static_assert(chunk_size <= nvcompZstdCompressionMaxAllowedChunkSize, "Chunk size must be less than the constant specified in the nvCOMP library");
	
	gettimeofday(&tv1, NULL);
	auto nvcompBatchedZstdOpts = nvcompBatchedZstdDefaultOpts;
  // changed by mayl
/*
	nvcompAlignmentRequirements_t compression_alignment_reqs;
  	status = nvcompBatchedZstdCompressGetRequiredAlignments(
    		nvcompBatchedZstdOpts,
    		&compression_alignment_reqs);
  	if (status != nvcompSuccess) {
    		throw std::runtime_error("ERROR: nvcompBatchedZstdCompressGetRequiredAlignments() not successful");
  	}

*/
	comp_temp_bytes = (*p_temp_size);
	status = nvcompSuccess;
	if(comp_temp_bytes == 0){
		
      		status = nvcompBatchedZstdCompressGetTempSize(
      		chunk_count,
      		chunk_size,
      		nvcompBatchedZstdOpts,
      		&comp_temp_bytes);
      		*p_temp_size = comp_temp_bytes;
		fprintf(stderr, "alloc and set d_temp_bytes %lu, chunk count %lu , chunksize %lu \n",  
			comp_temp_bytes, chunk_count, chunk_size);
	}

	void* d_comp_temp;
  	d_comp_temp = (*pd_temp);
  	if(d_comp_temp == NULL){
  		CUDA_CHECK(cudaMalloc(&d_comp_temp, comp_temp_bytes));

		//printf("alloc and set d_temp %p\n",  d_comp_temp);
		*pd_temp = d_comp_temp;
  	}

  	size_t max_out_bytes;
  	status = nvcompBatchedZstdCompressGetMaxOutputChunkSize(
      	chunk_size, nvcompBatchedZstdOpts, &max_out_bytes);
  	if (status != nvcompSuccess) {
    		throw std::runtime_error("ERROR: nvcompBatchedZstdCompressGetMaxOutputChunkSize() not successful");
  	}

  	gettimeofday(&tv2,NULL);

	/*2.1 cuda alloc enough buf to contain data and meta for uncompress and compress data */

	size_t uncompress_total_bytes = data_len+4096;  // 16 bytes align for input_data

	//uncompress_total_bytes += (chunk_count * sizeof(size_t *)); // store input ptrs
	//uncompress_total_bytes += (chunk_count * sizeof(size_t))+4096; // store input sizes

	int uncomp_exist = 0;
	int comp_exist = 0;

	void * uncompressed_data = NULL;
	void * uncomp_data_ptrs = NULL;
	void * uncomp_sizes = NULL;
	if(*input_uncomp_data == NULL){
		CUDA_CHECK(cudaMalloc(&uncompressed_data, uncompress_total_bytes));
		CUDA_CHECK(cudaMalloc(&uncomp_data_ptrs, (chunk_count * sizeof(size_t *))));
		CUDA_CHECK(cudaMalloc(&uncomp_sizes, (chunk_count * sizeof(size_t *))));

		
		*input_uncomp_data =  uncompressed_data;
		*input_uncomp_ptrs =  uncomp_data_ptrs;
		*input_uncomp_sizes =  uncomp_sizes;


	}else{
		uncomp_exist = 1;
		uncompressed_data =  *input_uncomp_data;
		uncomp_data_ptrs = *input_uncomp_ptrs;
		uncomp_sizes =  *input_uncomp_sizes;


	}

	//printf("cuda_malloc uncomp %lu bytes\n", uncompress_total_bytes);

	/*2.1.1 copy uncompressed data and */

	char * tmp_ptr = (char*)uncompressed_data;
	for(int n = 0; n<chunk_count; n++){
		data_ptrs[n] = (void*)(tmp_ptr+chunk_size*n);
		sizes[n] = chunk_size;
	}
	if(data_len % chunk_size != 0){
		sizes[chunk_count-1] = (data_len % chunk_size);
	}


	
	/*2.1.2 : set compressed size and ptrs to device memory */

	void * compressed_data = NULL;
	void * compressed_ptrs = NULL;
	void * compressed_sizes = NULL;
	size_t total_bytes = max_out_bytes * chunk_count;
	total_bytes += max_out_bytes * chunk_count;
	//total_bytes += (chunk_count * sizeof(size_t *)); // store compressed ptrs
	//total_bytes += (chunk_count * sizeof(size_t))+4096; // store compressed  sizes
	if(*input_comp_data == NULL){
		CUDA_CHECK(cudaMalloc(&compressed_data, total_bytes));
		CUDA_CHECK(cudaMalloc(&compressed_ptrs, (chunk_count * sizeof(size_t *))));
		CUDA_CHECK(cudaMalloc(&compressed_sizes, (chunk_count * sizeof(size_t ))));


		*input_comp_data =  compressed_data;
		*input_comp_ptrs =  compressed_ptrs;
		*input_comp_sizes = compressed_sizes;
	}else{

		
		comp_exist = 1;
		compressed_data =  *input_comp_data;
                compressed_ptrs = *input_comp_ptrs;
                compressed_sizes =  *input_comp_sizes;

	}


	//printf("cuda_malloc comp %lu bytes\n", total_bytes);

	//char * compressed_ptrs = ((char * )(compressed_data));
	
	

	//total_bvtes -= (max_out_bytes * chunk_count);
	//CUDA_CHECK(cudaMemcpyAsync((void*)compressed_ptrs, ori_data, total_bytes,cudaMemcpyHostToDevice , streams[1] ));
	/*2.1.3 : set uncompressed data and ptrs to device memory */

  

	//cudaEvent_t end;

	
	//printf("uncomp data %p , uncomp_ptrs %p ,uncomp_sizes %p \n", 
//		uncompressed_data, uncomp_data_ptrs, uncomp_sizes);

	//printf("ori data %p , data_ptrs %p ,data_sizes %p \n", 
	//	ori_data, data_ptrs, sizes);
	
	//printf("comp data %p , comp_ptrs %p ,comp_sizes %p, blksize %lu, blkcount %lu \n", 
	//	compressed_data, compressed_ptrs, compressed_sizes, chunk_size, chunk_count);

	
	
	//CUDA_CHECK(cudaEventCreate(&end));
	CUDA_CHECK(cudaMemcpyAsync(uncompressed_data, ori_data, data_len, cudaMemcpyHostToDevice , streams[1] ));
	if(uncomp_exist == 0 )
		CUDA_CHECK(cudaMemcpyAsync(uncomp_data_ptrs, data_ptrs, (chunk_count * sizeof(size_t *)), cudaMemcpyHostToDevice , streams[1] ));
	CUDA_CHECK(cudaMemcpyAsync(uncomp_sizes, sizes, (chunk_count * sizeof(size_t )), cudaMemcpyHostToDevice , streams[1] ));
	
	


	//CUDA_CHECK(cudaMemcpyAsync(compressed_ptrs, data_ptrs, uncompress_total_bytes, cudaMemcpyHostToDevice , streams[1] ));
	//CUDA_CHECK(cudaMemcpyAsync(compressed_sizes, sizes, uncompress_total_bytes, cudaMemcpyHostToDevice , streams[1] ));

	//char copy_ptrs[chunk_count * sizeof(size_t *)];

	//CUDA_CHECK(cudaMemcpy(copy_ptrs, uncomp_data_ptrs, chunk_count * sizeof(size_t *),cudaMemcpyDeviceToHost ));

	
	/*2.1.4 : wait for cudaMemcpyAsync and call cudaZstd compress  */

  	CUDA_CHECK(cudaStreamSynchronize(streams[1]));
	 //CUDA_CHECK(cudaEventRecord(end, streams[1]));



	tmp_ptr = (char*)compressed_data; 
	if(comp_exist == 0){
		for (int xx = 0 ; xx < chunk_count; xx++){
			data_ptrs[xx] = (void*)(tmp_ptr+max_out_bytes*xx);
		}

		CUDA_CHECK(cudaMemcpyAsync(compressed_ptrs, data_ptrs, (chunk_count * sizeof(size_t *)), cudaMemcpyHostToDevice , streams[1] ));

	//CUDA_CHECK(cudaEventRecord(end, streams[2]));
	//void ** ptrss = (void * *)copy_ptrs;
	
  		CUDA_CHECK(cudaStreamSynchronize(streams[1]));
	}


	

  	dur1 = tv2.tv_sec;
  	dur1 *=1000000;
  	dur1 +=(tv2.tv_usec);
	dur1 -=(tv1.tv_sec *1000000 + tv1.tv_usec); // DUR1 pre-comp_duration
  	//*mtime += dur2-dur1;


  	 cudaStream_t stream = streams[0];
	auto perform_compression = [&]() {
    	if (nvcompBatchedZstdCompressAsync(
          (void * *)uncomp_data_ptrs,//input_data.ptrs(),
          (size_t *)uncomp_sizes,
          chunk_size,
          chunk_count,
          d_comp_temp, 
          comp_temp_bytes,
          (void * *)compressed_ptrs,
          (size_t *)compressed_sizes,
          nvcompBatchedZstdOpts,
          //nullptr,  // changed by mayl
          stream) != nvcompSuccess) {
      throw std::runtime_error("nvcompBatchedZstdCompressAsync() failed.");
    }
  };

  // Run warm-up compression
  for (size_t iter = 0; iter < warmup_iteration_count; ++iter) {
    perform_compression();
  }

  // Re-run compression to get throughput
  //CUDA_CHECK(cudaEventRecord(start, stream));

  gettimeofday(&tv1,NULL);
  for (size_t iter = warmup_iteration_count; iter < total_iteration_count; ++iter) {
    perform_compression();
  }

  CUDA_CHECK(cudaStreamSynchronize(stream));
  
  gettimeofday(&tv2, NULL);
  dur2 = tv2.tv_sec;
  dur2 *=1000000;
  dur2 +=(tv2.tv_usec);

  dur2 -=(tv1.tv_sec *1000000 + tv1.tv_usec); // DUR2 pure comp_duration


  gettimeofday(&tv1,NULL);
  //CUDA_CHECK(cudaEventRecord(end, stream));


  /* cp back the compreseed data*/
  CUDA_CHECK(cudaMemcpyAsync(data_ptrs, compressed_ptrs, (chunk_count * sizeof(size_t *)), cudaMemcpyDeviceToHost , streams[1] ));
  
  CUDA_CHECK(cudaMemcpyAsync(sizes, compressed_sizes, (chunk_count * sizeof(size_t )), cudaMemcpyDeviceToHost , streams[1] ));
  CUDA_CHECK(cudaStreamSynchronize(streams[1]));



  #if 1
  //char * ret_dest_ptr = (char *)malloc(data_len);
  char * ret_dest_ptr = (char*)ret_data;
  //char * comp_src_ptr = NULL;
  //char * tmp_dest_ptr = (char*)uncompressed_data;
  size_t total_ret_size = 0;
  
  
  // TODO : cudaMemcpyAsync DeveceToDevice
#if 0

  for (int y=0; y<chunk_count; y++){
	size_t cur_size = sizes[y];
	comp_src_ptr = (char *)(data_ptrs[y]);
	CUDA_CHECK(cudaMemcpyAsync((void*)tmp_dest_ptr, comp_src_ptr, 
				cur_size, cudaMemcpyDeviceToDevice , streams[1] ));
	tmp_dest_ptr += sizes[y]; 
	total_ret_size += sizes[y];
		  
  }
  //CUDA_CHECK(cudaStreamSynchronize(streams[0]));
  CUDA_CHECK(cudaStreamSynchronize(streams[1]));
#endif
  //CUDA_CHECK(cudaStreamSynchronize(streams[2]));
  //CUDA_CHECK(cudaStreamSynchronize(streams[3]));
  //CUDA_CHECK(cudaStreamSynchronize(streams[4]));

  total_ret_size = max_out_bytes * chunk_count;
   CUDA_CHECK(cudaMemcpyAsync((void*)ret_dest_ptr, compressed_data,
                                total_ret_size, cudaMemcpyDeviceToHost , streams[1] ));

  //printf("compressed size %lu \n", total_ret_size);
  CUDA_CHECK(cudaStreamSynchronize(streams[1]));
  //free(ret_dest_ptr);
#endif
gettimeofday(&tv2,NULL);
  dur3 = tv2.tv_sec;
  dur3 *=1000000;
  dur3 +=(tv2.tv_usec);
  

  dur3 -=(tv1.tv_sec *1000000 + tv1.tv_usec); // DUR3 post comp_duration
  
  sizes[chunk_count +2] = dur1; // pre_comp_time
  sizes[chunk_count +3] = dur2; // comp_time
  sizes[chunk_count +4] = dur3; // post_comp_time



  	  	//*etime += dur2-dur1;

  //gettimeofday(&tv2, NULL);

   //CUDA_CHECK(cudaFree(uncompressed_data));

  //CUDA_CHECK(cudaFree(uncomp_data_ptrs));
  //CUDA_CHECK(cudaFree(uncomp_sizes));
  
  //CUDA_CHECK(cudaFree(compressed_data));
  //CUDA_CHECK(cudaFree(compressed_ptrs));
  //CUDA_CHECK(cudaFree(compressed_sizes));

  










}

extern "C"
void create_cuda_streams(cudaStream_t * streams, int count)
{
	for(int i = 0; i< count ; i++){
		CUDA_CHECK(cudaStreamCreate(&streams[i]));


	}
}

extern "C"
void destroy_cuda_streams(cudaStream_t * streams, int count)
{
		
	for(int i = 0; i< count ; i++){
		CUDA_CHECK(cudaStreamDestroy(streams[i]));


	}
}

extern "C"
void launchKernel(void * *temp_dev_buffer, size_t chunk_size, size_t chunk_count, size_t * ptemp_size) {

    //kernelFunction<<<1, 1>>>();
    //cudaDeviceSynchronize();

  nvcompStatus_t status = nvcompSuccess;
  int device_count = 0;

  auto nvcompBatchedZstdOpts = nvcompBatchedZstdDefaultOpts;
  cudaGetDeviceCount(&device_count);
  fprintf(stderr, "get GPU device count %d\n", device_count);

  *ptemp_size = 0;
  status = nvcompBatchedZstdCompressGetTempSize(
      		chunk_count,
      		chunk_size,
      		nvcompBatchedZstdOpts,
      		ptemp_size);
  if (status != nvcompSuccess){
	  fprintf(stderr, "nvcompBatchedZstdCompressGetTempSize failed\n");
	  *ptemp_size = 0;

  }else{
	  fprintf(stderr, "nvcompBatchedZstdCompress, GetTempSize %lu \n", *ptemp_size);

  }

  if(device_count <=0)
	  return;
  for(int i = 0; i<device_count; i++){
	cudaSetDevice(i);
  	CUDA_CHECK(cudaMalloc(&temp_dev_buffer[i], 65536));

  }
  cudaSetDevice(0);


}

extern "C"
void select_cuda_device(int devid)
{
	if(devid >=8)
		cudaSetDevice(0);
	else
		cudaSetDevice(devid);
}

extern "C"
int register_host_data(void* host_mem, size_t mem_len)
{
	cudaError_t status;
	status = cudaHostRegister(host_mem, mem_len, cudaHostRegisterPortable);
	if(status != cudaSuccess){
		fprintf(stderr, "register_host_data failed , %s \n", cudaGetErrorString(status));
		return -1;
	}
	return 0;
}

extern "C"
int unregister_host_data(void* host_mem)
{
	cudaError_t status;
	status = cudaHostUnregister(host_mem);
	if(status != cudaSuccess){
		fprintf(stderr,"unregister_host_data failed , %s \n", cudaGetErrorString(status));
		return -1;
	}
	return 0;
}


extern "C"
 void alloc_cuda_hostdata(char* host_data, size_t length)
{

	CUDA_CHECK(cudaMallocHost((void **)&host_data,length));
	return ;

}

extern "C"
 void free_cuda_host_data(char* host_data, size_t length)
{

	CUDA_CHECK(cudaFreeHost((void *)host_data));
	return ;

}


extern "C"
 void alloc_cuda_memory(char* host_data, size_t length)
{

	CUDA_CHECK(cudaMalloc((void **)&host_data, length));
	return ;

}


extern "C"
 void free_cuda_memory(char* host_data)
{

	CUDA_CHECK(cudaFree((void *)host_data));
	return ;

}




extern "C"
void call_gpu_zstd_compress(char* data, char * ret_data, size_t size, size_t blk_size, 
		void ** d_temp,size_t * d_temp_size,
                void * * streams,
                void * *  input_uncomp_data, void **  input_uncomp_ptrs , void ** input_uncomp_sizes,
                void * *  input_comp_data, void * * input_comp_ptrs , void ** input_comp_sizes
		)
{
        //timeval t1, t2;
	//int ret = 0;
	size_t pos = 0;
	char * data_ptrs = ret_data ;
	char * data_sizes ;

	data_ptrs +=(size*10/9);
	pos = (size_t )data_ptrs;
	pos = ((pos+(1<<12)-1) >>12)<<12;
	data_ptrs = (char *)pos;
	data_sizes = (char *)pos;
	pos += (sizeof(char*)*(size/blk_size+1));
	pos = ((pos+(1<<12)-1) >>12)<<12; // 4KB align
	data_sizes = (char *)pos;


	
	
	run_compress((void*)data,
                     (void*)ret_data, 
                     (void * *)data_ptrs, // in_out : contain ret_data_ptrs when return 
                     (size_t *)data_sizes,// in out: contain ret_data_size when return
                      blk_size,
		      size,      
                      0, 1,
                      d_temp, d_temp_size,
                      (cudaStream_t *)streams,
                      input_uncomp_data, input_uncomp_ptrs, input_uncomp_sizes,
                      input_comp_data, input_comp_ptrs, input_comp_sizes);

        //std::vector<char> data_vec(data, data+size);
              //printf("\n compress start at %lu.%06lu, end at %lu.%06lu \n",
        //              t1.tv_sec, t1.tv_usec, t2.tv_sec, t2.tv_usec);

        //cudaDeviceSynchronize();


}



void thread_cb(char * data, size_t size, int thread_id, int blk_bits) {
	//cout << str << endl;
	timeval t1, t2;
	char * host_data ; 
	char * host_ret_data ; 
	char *  data_ptrs;
	char *  data_sizes;
	
	void * input_uncomp_data = NULL;
	void * input_uncomp_ptrs = NULL;
	void * input_uncomp_sizes = NULL;


	void * input_comp_data = NULL;
	void * input_comp_ptrs = NULL;
	void * input_comp_sizes = NULL;

	size_t pos = 0;
	size_t blk_size = 1<<blk_bits; 
	size_t blk_count = (size+blk_size -1)/blk_size;
	cudaStream_t  streams[5];
	CUDA_CHECK(cudaMallocHost((void **)&host_data, 
			size + blk_count * (sizeof(size_t *)+sizeof(size_t))+4096)); // 锁定主机内存

	CUDA_CHECK(cudaMallocHost((void **)&host_ret_data, 
			size + blk_count * (sizeof(size_t *)+sizeof(size_t))+40960)); // 锁定主机内存
	memcpy(host_data, data, size);
	pos += size;
	pos = ((pos +1023)/1024)*1024;
	if(pos <1024+size)
		pos = ((size +1023)/1024)*1024 +1024;
	data_ptrs = (char *)host_data;
	data_ptrs += pos;
	data_sizes = data_ptrs;
	pos = blk_count * (sizeof(size_t *));

	pos = ((pos +1023)/1024)*1024;
	if(pos <  blk_count * (sizeof(size_t *))+1024)
		 pos = ((blk_count * sizeof(size_t *) +1023)/1024)*1024 +1024;

	data_sizes += pos;
        gettimeofday(&t1, NULL);
	printf("data %p , ptrs %p , size %p thread %d at %lu.%06lu\n", host_data, data_ptrs, data_sizes, thread_id, t1.tv_sec, t1.tv_usec);
	if(thread_id % 2 == 0){
		 cudaSetDevice(0);
	}else{
		 cudaSetDevice(1);
	}
        



        //std::vector<char> data_vec(data, data+size);
        //std::vector<std::vector<char>> data_vec(1);
        gettimeofday(&t1, NULL);
	//printf("start thread %d\n", thread_id);
        //data_vec[0].assign(host_data, host_data + size);
	uint64_t dur_time = 0;
	uint64_t dur_time1 = 0;
	uint64_t dur_time2 = 0;
	void * d_temp = NULL;
	size_t d_temp_size = 0;

	for(int xx = 0 ; xx<5; xx++){
		streams[xx] = NULL;
	}
	create_cuda_streams((cudaStream_t *)streams, 5);
	
	for(int n = 0; n<cycles; n++){
        	    run_compress(host_data,
				 host_ret_data, 
				(void * *)data_ptrs, 
				(size_t *)data_sizes,
			        blk_bits,
				size,	
				0, 1,  
				&d_temp, &d_temp_size, 
				(cudaStream_t *)streams,
				&input_uncomp_data, &input_uncomp_ptrs, &input_uncomp_sizes,
				&input_comp_data, &input_comp_ptrs, &input_comp_sizes);
	}
	if(d_temp != NULL){
		CUDA_CHECK(cudaFree((void*)d_temp));
	}
        gettimeofday(&t2, NULL);
	printf("thread %d run zstd_compress data_size %lu, cycle %d, start_time %lu.%06lu, end_time %lu.%06lu, input memcpy dur %lu, compress time %lu, extra time %lu\n",
			thread_id, size, cycles, t1.tv_sec, t1.tv_usec, t2.tv_sec, t2.tv_usec, dur_time1, dur_time, dur_time2);
	
	CUDA_CHECK(cudaFreeHost((void*)host_data));
	CUDA_CHECK(cudaFreeHost((void*)host_ret_data));

        gettimeofday(&t1, NULL);
	destroy_cuda_streams((cudaStream_t *)streams, 5);

        gettimeofday(&t2, NULL);
	//printf("thread %d run zstd_compress data_size %lu, cycle %d, free start_time %lu.%06lu, free end_time %lu.%06lu \n",
          //              thread_id, size, cycles, t1.tv_sec, t1.tv_usec, t2.tv_sec, t2.tv_usec);


	return;
}

int  call_multi_zstd_gpu(char * data, size_t read_size,int  thread_count, int blk_bits)
{
	std::vector<std::thread> threads;
	for (int i = 0; i < thread_count; ++i) {
		auto task = std::bind(thread_cb, std::placeholders::_1, std::placeholders::_2, 
				std::placeholders::_3, std::placeholders::_4); 
        	threads.emplace_back(task, data, read_size, i, blk_bits);
	}


    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    return 0;

}

#if 0
int main(int argc, char* argv[])
{
  std::vector<std::string> file_names;
  // TODO chanbged by mayl
  void * temp_dev_buffer;
  int blk_size = 1<<17;
  int blk_bits = 17;
  int data_size = atol(argv[2]);
  int blk_count = (data_size+blk_size-1)/blk_size  ;
  int thread_count = atoi(argv[3]);
  char * data = (char *)malloc(data_size+ (blk_count*(sizeof(size_t)+sizeof(size_t *)))+4096);
  int fd = open(argv[1], O_RDONLY);
  size_t read_size = read(fd, data,data_size );

  
  cycles = atoi(argv[4]);


  launchKernel(&temp_dev_buffer);
  call_multi_zstd_gpu(data, read_size, thread_count, blk_bits);
  //free (data);
  return 0;

#if 0
  size_t warmup_iteration_count = 0;
  size_t total_iteration_count = 1;
  void * temp_dev_buffer = NULL;

  do {
    if (argc < 3) {
      break;
    }

    int i = 1;
    while (i < argc) {
      const char* current_argv = argv[i++];
      if (strcmp(current_argv, "-f") == 0) {
        // parse until next `-` argument
        while (i < argc && argv[i][0] != '-') {
          file_names.emplace_back(argv[i++]);
        }
      } else {
        std::cerr << "Unknown argument: " << current_argv << std::endl;
        return 1;
      }
    }
  } while (0);

  if (file_names.empty()) {
    std::cerr << "Must specify at least one file via '-f <file>'." << std::endl;
    return 1;
  }

  //auto data = multi_file(file_names);
  

  //run_example(data, warmup_iteration_count, total_iteration_count);



  return 0;
#endif
}
#endif



