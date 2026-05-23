#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <omp.h>
#include "sz3c.h"

struct compress_meta{
	size_t ori_offset;
	size_t compressed_size;
};

int check_data_size(int dataType, size_t  dataCount, size_t recSize )
{
	size_t calcsize = 0;
	switch (dataType){
		case SZ_FLOAT:
			calcsize = dataCount * sizeof(float); break;
		case SZ_DOUBLE:
			calcsize = dataCount * sizeof(double); break;
		case SZ_UINT8:
			calcsize = dataCount * sizeof(uint8_t); break;
		case SZ_UINT16:
			calcsize = dataCount * sizeof(uint16_t); break;
		case SZ_UINT32:
			calcsize = dataCount * sizeof(uint32_t); break;
		case SZ_UINT64:
			calcsize = dataCount * sizeof(uint64_t); break;
		default:
			break;

	}
	return (calcsize == recSize);
}

int main(int argc, char * argv[])
{
	int errBoundMode;
	int dataType;
	struct compress_meta cmeta;
	double errBoundRatio; // for ABS : errbound, for rel and  pwr: BoundRatio 
	size_t r1, r2, r3, r4, r5;
	size_t rec_size;
	int loop = 0;
	int fd_src = 0;
	int fd_dest = 0;
	char * src_data = NULL;
	char * dest_data = NULL;
	int ret = 0;
	size_t check_size = 0, ori_size = 0, total_size = 0;
	size_t compressed_size = 0;

	double absErrBound = 0;
	double relBoundRatio = 0;
	double pwrBoundRatio = 0;
	size_t ret_w = 0;

	cmeta.ori_offset = 0;
	cmeta.compressed_size = 0;
	r1 = 0;
	r2 = r3 = r4 = r5 = 0;
	if(argc < 8){
		printf("test_sz3c_compress srcfile destfile recsize errBoundMode dataType errBoundRatio r1[r2 r3 r4 r5] \n   ");
		return 0;
	}
	omp_set_num_threads(1);
	// step 1 : get src_fd and dest_fd
	fd_src = open(argv[1],O_RDONLY);
	if(fd_src <= 0){
		printf("can not open src file %s\n", argv[1]);
		return -ENOENT;
	}
	fd_dest = open(argv[2],O_RDWR|O_CREAT,0666);
	if(fd_dest <= 0){
		printf("can not open dest file %s\n", argv[2]);
		close(fd_src);
		return -ENOENT;
	}
	rec_size = atoi(argv[3]);
	errBoundMode = atoi(argv[4]);
	dataType = atoi(argv[5]);
	errBoundRatio = (double)atof(argv[6]);
	r1 = atoi(argv[7]);
	if(argc > 8)
		r2 = atoi(argv[8]);
	if(argc > 9)
		r3 = atoi(argv[9]);
	if(argc > 10)
		r4 = atoi(argv[10]);
	if(argc > 11)
		r5 = atoi(argv[11]);

	check_size = r1;
	if(r2 != 0)
		check_size *=r2;
	if(r3 != 0)
		check_size *=r3;
	if(r4 != 0)
		check_size *=r4;
	if(r5 != 0)
		check_size *=r5;
	
	// check_size 
	if(! check_data_size(dataType, check_size, rec_size )){
		printf("data size not match %lu: %lu, type %d\n",check_size, rec_size, dataType );
		close(fd_src);
		close(fd_dest);
		return -EINVAL;
	}
	src_data = (char *)malloc(rec_size);
	if(src_data == NULL){
		printf("can not alloc memory for read file , size %d\n", rec_size);
		close(fd_src);
		close(fd_dest);
		return -ENOMEM;
	}


	ret = read(fd_src, src_data, rec_size);
	cmeta.ori_offset = 0;
	//total_size += sizeof(struct compress_meta);

	
	while(ret == rec_size){
	
		loop++;

		if (errBoundMode == ABS)
			absErrBound = errBoundRatio;
		if (errBoundMode == REL)
			relBoundRatio = errBoundRatio;
		if (errBoundMode == PSNR)
			pwrBoundRatio = errBoundRatio;

		dest_data = SZ_compress_args(dataType, src_data, &compressed_size,
                                errBoundMode, absErrBound, relBoundRatio, pwrBoundRatio,
                                r5, r4, r3, r2, r1);
		if(dest_data == NULL || compressed_size <= 0){
			printf("sz3 compressed failed \n");
			free(src_data);
			close(fd_src);
			close(fd_dest);
			return -EIO;
		}
		//if(loop < 10)
		//printf("loop %d, compressed_size %lu\n", loop, compressed_size );
		cmeta.compressed_size = compressed_size;
		ret_w = write(fd_dest, &cmeta, sizeof(cmeta));
		total_size += ret_w;
		ret_w = write(fd_dest, dest_data, compressed_size);
		if(ret_w <= 0)
			break;
		total_size += ret_w;
		ori_size += ret;
		cmeta.ori_offset = ori_size;
		free(dest_data);
		dest_data = NULL;
		ret = read(fd_src, src_data, rec_size);

	}
	
	printf("ori_size %lu, compresse_size %lu, compress Ratio %f \n", ori_size, total_size, ((float)ori_size)/((float)total_size));
	if(src_data)
		free(src_data);
	close(fd_src);
	close(fd_dest);
}

