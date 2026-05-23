
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>
#include <sys/unistd.h>


extern char * compress_pans_buf(char * inp_buf, uint32_t in_size, uint32_t * out_size);
int main(int argc, char* argv[]) {
    if (argc < 3) {
        //std::cerr << "Usage: " << argv[0] << " <input.file> <output.file>" << std::endl;
        return 1;
    }
    uint32_t batchSize;
    uint32_t compressedSize;
    size_t filesize = 10*1024*1024;
    int precision = 10; 
    char * in_buf = (char*)calloc(10*1024*1024,1);
    //char * out_buf = (char*)calloc(10*1024*1024,1);
    uint8_t  * out_buf1 = NULL;
    int fd = open(argv[1],O_RDONLY);

    filesize = read(fd, in_buf, 10*1024*1024);
    printf("ori_file length %ld\n", filesize);
    //for(int i = 0; i< 9*1024*1024 ; i++)
//	    in_buf[i] = (unsigned char)(i & 0xff);

    /*
    compressFileWithANS(
        //argv[2],
	filesize,
	in_buf,
	out_buf,
        batchSize,
        &compressedSize,
        precision);
*/
    //uint8_t * inp_buf = (uint8_t *)in_buf;
    compress_pans_buf(in_buf, filesize, &compressedSize);
    printf("campressed size %lu\n", compressedSize);
        // printf("compressedSize: %d\n", compressedSize);
    printf("compress ratio: %f\n", 1.0 * batchSize / compressedSize);
    //std::cout << "Compression completed successfully." << std::endl;
    return 0;
}



