#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
     #include <sys/stat.h>
       #include <fcntl.h>
#include <unistd.h>


uint8_t *  pans_compress_data(uint8_t * in_buf,uint32_t filesize, uint32_t * compress_size);
uint8_t *  pans_decompress_data(uint8_t * in_buf,uint32_t filesize, uint32_t * compress_size);
int main(int argc, char* argv[])
{
	int op = atoi(argv[3]);
	uint8_t * in_buf = (uint8_t *)malloc(1024*1024*100);
	uint8_t * out_buf = NULL;
	int fd = open(argv[1], O_RDONLY);
	uint32_t  rc = read(fd, in_buf, 1024*1024*100); 
	uint32_t comp_size = 0;

	if(op == 1){
		out_buf = pans_compress_data(in_buf, rc, &comp_size);
		printf("output buf %p , comp_size %lu\n ", out_buf, comp_size);
	}else{
		out_buf = pans_decompress_data(in_buf, rc, &comp_size);
		printf("output buf %p , decomp_size %lu\n ", out_buf, comp_size);
	}
	close(fd);
	fd = open(argv[2], O_RDWR|O_CREAT, 0666);
	rc = write(fd, out_buf, comp_size);
	close(fd);


	return 0;
}


