#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


extern char * adm_compress_data(char * src, size_t src_len , size_t* pdest_len);
char * adm_decompress_data(char * src, size_t ori_len , size_t* pdest_len);

int main(int argc, char* argv[])
{
	int fd = 0;
	int fdw = 0;
	int dest_fd = 0;
	size_t dest_len = 0;
	int comp_length = 0;
	int buf_length  = 0;
        char * dest_buf = NULL;	

	char * src_buf = NULL;
	int curr_src_len = comp_length;
	int src_len = 0;
	size_t off = 0;
	size_t total_compressed_len = 0;

	if(argc <4 ){
		printf("call_mapping_u16 src_filename record_length dest_filename\n");
		return 0;
	}
	comp_length = atoi(argv[2]);
	

	fd = open(argv[1], O_RDONLY);
	if(fd <= 0){
		printf("can not open file %s\n", argv[1]);
		return -ENOENT;
	}
	
	fdw = open(argv[3], O_RDWR|O_CREAT, 0666);
	if(fdw <= 0){
		printf("can not open dest file %s\n", argv[3]);
		return -ENOENT;
	}

	 src_buf = malloc(comp_length);

	if(src_buf == NULL){
		printf("can not alloc src buf \n");
		return -ENOMEM;
	}

	
	 size_t*  dmata;
	 size_t  offset = 0;
	 size_t  curr_data_len = 0; 
	 size_t  total_ori_data = 0;
	 size_t  decomp_cnt = 0;
	 struct timeval tv, tv1;
	 size_t decomp_time  = 0;

	 memset(src_buf, 0, comp_length);

	 src_len = read(fd, src_buf+offset, 5*sizeof(size_t));
	 dmata = ((size_t *)src_buf);
	 curr_data_len = dmata[4];
	 offset += 5*sizeof(size_t);
	 src_len = read(fd, src_buf+offset, curr_data_len);


	 while(src_len > 0){
		 if(src_len > 0){
			gettimeofday(&tv, NULL);
			dest_buf = adm_decompress_data(src_buf,comp_length, &dest_len);
			gettimeofday(&tv1, NULL);
			decomp_time += (tv1.tv_sec *1000000 + tv1.tv_usec);
			decomp_time -= (tv.tv_sec *1000000 + tv.tv_usec);

			

			if(dest_buf == NULL){
				printf("get compressed data failed\n");
				free(src_buf);
				break;
			}
			int write_len = write(fdw, dest_buf, dest_len);
			total_ori_data += write_len;
			if(write_len <=0 || write_len < dest_len ){
				printf("write compressed data failed %ld: %ld\n", dest_len, write_len);
				free(src_buf);
				free(dest_buf);
				break;
			}
			off += 5*sizeof(size_t);
			decomp_cnt ++;
			
			//offset += src_len;
			total_compressed_len += dest_len;
			off += curr_data_len;
			//memset(src_buf, 0, comp_length);
			if(dest_buf)
				free(dest_buf);
			dest_len = 0;


			// next_loop
			offset = 0;


			src_len = read(fd, src_buf+offset, 5*sizeof(size_t));
         		dmata = ((size_t *)src_buf);
         		curr_data_len = dmata[4];
         		offset += 5*sizeof(size_t);
         		src_len = read(fd, src_buf+offset, curr_data_len);
	 		//src_len = read(fd, src_buf, comp_length);

		 }
	 }


	 printf("compress data from offset %lu , length %lu, decompressed data len %lu , total_ori_data %lu , decomp_cnt %lu\n", off,src_len, dest_len, total_ori_data, decomp_cnt );
	 printf("original data len %lu, compressed data len %lu  time %llu us\n", off, total_compressed_len, decomp_time);
	 free(src_buf);

	 close(fd);
	 close(fdw);
	return 0;


}


