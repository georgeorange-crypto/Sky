#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>


struct test_mp_arg{
	int thread_num;
	int op_type; // 0 read 1 write
	int dup_cnt;
	int thread_cnt;
	unsigned long  skip_len;
	unsigned char ori_pattern;
	char filename[128];
};


void *
mp_rw_thread(void *argv)
{
	struct test_mp_arg * arg = NULL;
	unsigned char * buf = NULL;
	unsigned char pattern = 0;
	int i = 0;
	int fd = 0;
	int ret = 0;
	arg = (struct test_mp_arg *) argv;
	printf("TThread %d start \n", arg->thread_num);
	buf = (unsigned char *) malloc(arg->skip_len);
	printf("Now thread %d start, name %s\n", arg->thread_num, arg->filename);
	if(buf == NULL){
		printf("can not alloc buff in thread %d , length %lu\n", arg->thread_num , arg->skip_len);
		goto err;
	}
	printf("buf allocate, thread %d\n", arg->thread_num);
	fd = open(arg->filename, O_CREAT|O_RDWR, 0666);
	if(fd <= 0){
		printf("can not open file %s in thread %d\n", arg->filename, arg->thread_num);
		goto err;
	}
	printf("file opened %s, thread %d\n", arg->filename, arg->thread_num);
	pattern = arg->ori_pattern+arg->thread_num;
	for(i = 0 ; i<arg->dup_cnt; i++){
		if(arg->op_type == 1){
			memset(buf, pattern, arg->skip_len);
			ret = pwrite(fd, buf, arg->skip_len, (size_t)(arg->skip_len * (arg->thread_cnt *i + arg->thread_num)));
			if(ret <= 0){
				printf("write thread %d failed , op_type %d, offset %llu , length %llu\n", arg->thread_num, arg->op_type,
					arg->skip_len * (arg->thread_cnt *i + arg->thread_num),  arg->skip_len);
					goto err;

			}else{
				 printf("write thread %d  , op_type %d, offset %llu , length %llu\n", arg->thread_num, arg->op_type,
                                        arg->skip_len * (arg->thread_cnt *i + arg->thread_num),  arg->skip_len);

			}

		}else{
			int j = 0;
			memset(buf, 0, arg->skip_len);
			ret = pread(fd, buf, arg->skip_len, (size_t)(arg->skip_len * (arg->thread_cnt * i + arg->thread_num)));
			if(ret <= 0){

				printf("read_check thread %d failed , op_type %d, offset %llu , leng %llu\n", arg->thread_num, arg->op_type,
					arg->skip_len * (arg->thread_cnt *i + arg->thread_num),  arg->skip_len);
					goto err;

			}
			for (j = 0 ; j< arg->skip_len; j++){
				if(buf[j] != pattern){
					printf("read_check mismath offset %llu , exp %02x , got %02x\n",  arg->skip_len * (arg->thread_cnt *i + arg->thread_num)+j, pattern, buf[j]);
					goto err;
				}
			}
			
		

		}
	}


	printf("rw_check thread %d success , op_type %d\n", arg->thread_num, arg->op_type);
	 
err:
	if(buf)
		free(buf);
	return NULL;
}

int main(int argc, char * argv[])
{
	struct test_mp_arg mp_args[100];
	pthread_t threads[100] = {0};
	char filename[128];
	int record_len = 0;
	int thread_count = 0;
	int dup_count = 0;
	int op_type = 0;
	int i = 0;

	if(argc <6){
		printf("error , need 5 parameters or more  filename op_type record_len dup_cnt thread_cnt \n");
		return -1;
	}
	memset(filename, 0 ,128);
	strcpy(filename, argv[1]);
	op_type = atoi(argv[2]);
	record_len = atoi(argv[3]);
	dup_count = atoi(argv[4]);
	thread_count = atoi(argv[5]);

	for (i = 0; i<thread_count; i++){
		strcpy(mp_args[i].filename, filename);
		printf("Thread %d , filename %s\n", i, mp_args[i].filename);
		mp_args[i].thread_num = i;
		mp_args[i].thread_cnt = thread_count;
	        mp_args[i].dup_cnt = dup_count;	
	        mp_args[i].op_type = op_type;
	        mp_args[i].skip_len = record_len;
		mp_args[i].ori_pattern = 0x35;

	}
	for(i = 0; i < thread_count; i++){
		printf("Start thread %d , filename %s\n", i, mp_args[i].filename);
		pthread_create(&threads[i], NULL, mp_rw_thread , (void *)&mp_args[i]);
	}
	sleep(100);
	return 0;

}

