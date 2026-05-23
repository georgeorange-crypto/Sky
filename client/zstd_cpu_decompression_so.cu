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


#include <stdio.h>
#include <sys/time.h>
#include "zstd.h"
#include "nvcomp/zstd.h"
#include "BatchData.h"


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

BatchDataCPU * GetBatchDataCPUptr(const BatchData& batch_data, bool copy_data)
{
  BatchDataCPU *  pbatch_data_cpu = new BatchDataCPU(
      batch_data.ptrs(),
      batch_data.sizes(),
      batch_data.data(),
      batch_data.size(),
      copy_data);
  return pbatch_data_cpu;
}


static void run_example(const std::vector<std::vector<char>>& data,
                        size_t warmup_iteration_count, size_t total_iteration_count, 
			size_t * comp_buf_count, size_t * comp_buf_sizes,  char * * comp_bufs, void * * pbatch  )
{
  struct timeval  t3, t4, t5;
  //gettimeofday(&t1,NULL);

  //void* d_comp_temp1;
  //CUDA_CHECK(cudaMalloc(&d_comp_temp1, 2097152));

  assert(!data.empty());
  if(warmup_iteration_count >= total_iteration_count) {
    throw std::runtime_error("ERROR: the total iteration count must be greater than the warmup iteration count");
  }

  size_t total_bytes =
    std::accumulate(data.begin(), data.end(), size_t(0), [](const size_t& a, const std::vector<char>& part) {
        return a + part.size();
  });

  //std::cout << "----------" << std::endl;
  //std::cout << "files: " << data.size() << std::endl;
  //std::cout << "uncompressed (B): " << total_bytes << std::endl;

  constexpr size_t chunk_size = 1 << 17;
  static_assert(chunk_size <= ZSTD_BLOCKSIZE_MAX, "Chunk size must be less than the constant specified in the Zstandard library");
  static_assert(chunk_size <= nvcompZstdCompressionMaxAllowedChunkSize, "Chunk size must be less than the constant specified in the nvCOMP library");

  //auto nvcompBatchedZstdOpts = nvcompBatchedZstdCompressDefaultOpts;
  // changed by mayl for cuda_12 not cuda_13
  auto nvcompBatchedZstdOpts = nvcompBatchedZstdDefaultOpts;


  //gettimeofday(&t2,NULL);

  // Query compression alignment requirements
  nvcompAlignmentRequirements_t compression_alignment_reqs;
  nvcompStatus_t status = nvcompBatchedZstdCompressGetRequiredAlignments(
    nvcompBatchedZstdOpts,
    &compression_alignment_reqs);

  //gettimeofday(&t2,NULL);
  if (status != nvcompSuccess) {
    throw std::runtime_error("ERROR: nvcompBatchedZstdCompressGetRequiredAlignments() not successful");
  }

  // Build up GPU data
  BatchData input_data(data, chunk_size, compression_alignment_reqs.input);

  //gettimeofday(&t2,NULL);
  const size_t chunk_count = input_data.size();

  // Compress on the GPU using batched API
  size_t comp_temp_bytes;
  // changed by mayl

  //gettimeofday(&t2,NULL);
//comp_temp_bytes = chunk_count*chunk_count *25;
#if 1
status = nvcompBatchedZstdCompressGetTempSize(
      chunk_count,
      chunk_size,
      nvcompBatchedZstdOpts,
      &comp_temp_bytes);
      //chunk_count * chunk_size);
  //printf("temp_bytes %lu\n", comp_temp_bytes);
  //gettimeofday(&t2,NULL);
/*
  status = nvcompBatchedZstdCompressGetTempSizeAsync(
      chunk_count,
      chunk_size,
      nvcompBatchedZstdOpts,
      &comp_temp_bytes,
      chunk_count * chunk_size);
     */
  if (status != nvcompSuccess) {
    throw std::runtime_error("ERROR: nvcompBatchedZstdCompressGetTempSizeAsync() not successful");
  }
#endif

  //gettimeofday(&t2,NULL);
  void* d_comp_temp;
  CUDA_CHECK(cudaMalloc(&d_comp_temp, comp_temp_bytes));

  size_t max_out_bytes;
  status = nvcompBatchedZstdCompressGetMaxOutputChunkSize(
      chunk_size, nvcompBatchedZstdOpts, &max_out_bytes);
  if (status != nvcompSuccess) {
    throw std::runtime_error("ERROR: nvcompBatchedZstdCompressGetMaxOutputChunkSize() not successful");
  }

  BatchData  compressed_data (max_out_bytes, chunk_count, compression_alignment_reqs.output);
  //BatchData compressed_data(max_out_bytes, chunk_count, compression_alignment_reqs.output);

  cudaStream_t stream;
  struct timeval tv_c1, tv_c2, tv_c3, tv_c4;
  //gettimeofday(&tv_c1, NULL);

  CUDA_CHECK(cudaStreamCreate(&stream));

  //gettimeofday(&tv_c2, NULL);

  cudaEvent_t start, end;
  CUDA_CHECK(cudaEventCreate(&start));
  CUDA_CHECK(cudaEventCreate(&end));

  /*
    nvcompStatus_t nvcompBatchedZstdCompressAsync(
    const void* const* device_uncompressed_chunk_ptrs,
    const size_t* device_uncompressed_chunk_bytes,
    size_t max_uncompressed_chunk_bytes,
    size_t num_chunks,
    void* device_temp_ptr,
    size_t temp_bytes,
    void* const* device_compressed_chunk_ptrs,
    size_t* device_compressed_chunk_bytes,
    nvcompBatchedZstdOpts_t format_opts,
    cudaStream_t stream);
   
   */


  auto perform_compression = [&]() {
    if (nvcompBatchedZstdCompressAsync(
          input_data.ptrs(),
          input_data.sizes(),
          chunk_size,
          chunk_count,
          d_comp_temp,
          comp_temp_bytes,
          compressed_data.ptrs(),
          compressed_data.sizes(),
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
  CUDA_CHECK(cudaEventRecord(start, stream));
  for (size_t iter = warmup_iteration_count; iter < total_iteration_count; ++iter) {
    perform_compression();
  }

  gettimeofday(&t3,NULL);
  CUDA_CHECK(cudaEventRecord(end, stream));
  CUDA_CHECK(cudaStreamSynchronize(stream));

  gettimeofday(&t4,NULL);
  float ms;
  CUDA_CHECK(cudaEventElapsedTime(&ms, start, end));
  ms /= total_iteration_count - warmup_iteration_count;

  // compute compression ratio
  std::vector<size_t> compressed_sizes_host(chunk_count);
  CUDA_CHECK(cudaMemcpy(
      compressed_sizes_host.data(),
      compressed_data.sizes(),
      chunk_count * sizeof(*compressed_data.sizes()),
      cudaMemcpyDeviceToHost));

  size_t comp_bytes = std::accumulate(compressed_sizes_host.begin(), compressed_sizes_host.end(), size_t(0));

#if 0
  std::cout << "comp_size: " << comp_bytes
            << ", compressed ratio: " << std::fixed << std::setprecision(2)
            << (double)total_bytes / comp_bytes << std::endl;
  std::cout << "compression throughput (GB/s): "
            << (double)total_bytes / (1.0e6 * ms) << std::endl;
  std::cout << "dur_time: " << ms << std::endl;
#endif

  //printf("start copy compressed data cpu, chunk_count %lu\n", chunk_count);
  // Allocate and prepare output/compressed batch
  BatchDataCPU *  pcompressed_data_cpu =  GetBatchDataCPUptr(compressed_data, true);
  * comp_buf_count = chunk_count;
  for(int i = 0; i< chunk_count; i++){
	  comp_bufs[i] = (char *) pcompressed_data_cpu->ptrs()[i];
	  comp_buf_sizes[i] =  pcompressed_data_cpu->sizes()[i];
	  
  }
  *pbatch = pcompressed_data_cpu;
  //printf("end copy compressed data cpu\n");
  //BatchDataCPU decompressed_data_cpu = GetBatchDataCPU(input_data, false);
#if 0
  // loop over chunks on the CPU, decompressing each one
  for (size_t i = 0; i < chunk_count; ++i) {
    size_t size = ZSTD_decompress(decompressed_data_cpu.ptrs()[i],
                                  decompressed_data_cpu.sizes()[i],
                                  compressed_data_cpu.ptrs()[i],
                                  compressed_data_cpu.sizes()[i]);
    if (ZSTD_isError(size)) {
      throw std::runtime_error(
          "Zstandard CPU failed to decompress chunk " + std::to_string(i) + ". Error code: " + std::to_string(size) + ", Message: " + ZSTD_getErrorName(size));
    }
    decompressed_data_cpu.sizes()[i] = size;
  }
  // Validate decompressed data against input
  if (!(decompressed_data_cpu == input_data)) {
    throw std::runtime_error("Failed to validate CPU decompressed data");
  } else {
    std::cout << "CPU decompression validated :)" << std::endl;
  }
#endif
  CUDA_CHECK(cudaFree(d_comp_temp));
  //CUDA_CHECK(cudaFree(d_comp_temp1));

  CUDA_CHECK(cudaEventDestroy(start));
  CUDA_CHECK(cudaEventDestroy(end));

  gettimeofday(&tv_c3, NULL);
  CUDA_CHECK(cudaStreamDestroy(stream));
  gettimeofday(&tv_c4, NULL);

  gettimeofday(&t5,NULL);
#if 0
  printf("t1 %lu.%06lu , t2 %lu.%06lu, t3 %lu.%06lu , t4 %lu.%06lu, t5 %lu.%06lu \n",
		  t1.tv_sec, t1.tv_usec,
		   t2.tv_sec, t2.tv_usec,
		    t3.tv_sec, t3.tv_usec,
		     t4.tv_sec, t4.tv_usec,
		      t5.tv_sec, t5.tv_usec);

printf("tc1 %lu.%06lu , tc2 %lu.%06lu, tc3 %lu.%06lu , tc4 %lu.%06lu \n",
		  tv_c1.tv_sec, tv_c1.tv_usec,
		   tv_c2.tv_sec, tv_c2.tv_usec,
		    tv_c3.tv_sec, tv_c3.tv_usec,
		     tv_c4.tv_sec, tv_c4.tv_usec
		      );
#endif

}

extern "C" 
void call_gpu_zstd_compress(char* data, int size, size_t warmup_iteration_count, size_t total_iteration_count, size_t * comp_bufs_count, size_t * comp_buf_sizes, 
		char ** comp_bufs, void ** pbatch)
{
	timeval t1, t2;
	
	//std::vector<char> data_vec(data, data+size);
	std::vector<std::vector<char>> data_vec(1);
	gettimeofday(&t1, NULL);
	data_vec[0].assign(data, data + size);
	run_example(data_vec, warmup_iteration_count, total_iteration_count, comp_bufs_count,
			comp_buf_sizes, comp_bufs, pbatch);
	gettimeofday(&t2, NULL);
	//printf("\n compress start at %lu.%06lu, end at %lu.%06lu \n",
	//		t1.tv_sec, t1.tv_usec, t2.tv_sec, t2.tv_usec);

        //cudaDeviceSynchronize();


}


extern "C" void launchKernel(void * *temp_dev_buffer) {

    //kernelFunction<<<1, 1>>>();
    //cudaDeviceSynchronize();

  CUDA_CHECK(cudaMalloc(temp_dev_buffer, 65536));
}

extern "C" void free_cpu_batch(void * cpu_batch) {

    //kernelFunction<<<1, 1>>>();
    //cudaDeviceSynchronize();

  //CUDA_CHECK(cudaMalloc(temp_dev_buffer, 2097152));

	BatchDataCPU * batch_data = static_cast < BatchDataCPU *>(cpu_batch);

	//printf("try delete pbatch %p \n", batch_data);
	delete batch_data;
}



#if 0
int main(int argc, char* argv[])
{
  std::vector<std::string> file_names;

  size_t warmup_iteration_count = 2;
  size_t total_iteration_count = 5;

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

  auto data = multi_file(file_names);

  run_example(data, warmup_iteration_count, total_iteration_count);

  return 0;
}

#endif
