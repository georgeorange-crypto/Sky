#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#if 0
#include "../include/skyfs_sys.h"
#include "../include/skyfs_list.h"
#include "../include/skyfs_const.h"
#include "../include/skyfs_types.h"
#include "../include/skyfs_fs.h"

//#include "amp.h"

#include "../include/skyfs_msg.h"
#include "../include/skyfs_debug.h"
#include "../include/skyfs_hash.h"


#include "osd_fs.h"
#include "osd_op.h"
#include "osd_thread.h"
#include "osd_init.h"
#include "osd_thread.h"
#include "osd_profile.h"
#include "osd_layout.h"
#include "osd_help.h"
#include "osd_loadb.h"

//#include "../mds/mds_fs.h"

#include "osd_ito.h"
#endif


static int parse_file_name = 0;
static int ttmp_pos = 0;
void flush_directory(const char *dir_path, char * part_buf, size_t total_buf_len,int * buf_pos) {

    int cur_pos ;
    char * tmp_buf = NULL;
    
     struct dirent *entry = NULL;
    struct stat statbuf;
    ssize_t  read_cnt = 0;
    int tmp_offset;

    //int head_size = sizeof(skyfs_replica_recover_head_t);
    int head_size = 32;
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir()");
        return;
    }
 
    //struct dirent *entry;
    //struct stat statbuf; 
    printf("start sync direcory %s \n", dir_path);
    cur_pos = * buf_pos;
    while ((entry = readdir(dir)) != NULL) {
	    cur_pos = * buf_pos;
	    if(!strcmp(entry->d_name, "."))
		    continue;

	    if(!strcmp(entry->d_name, ".."))
		    continue;
	    //printf("get_file_name %s\n",  entry->d_name);

        if (entry->d_type == DT_REG) { // 只刷新普通文件
            char file_path[1024];
            snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
	    if(parse_file_name){
		    u_int64_t ino = 0;
		    int obj_num = 0;
		    sscanf(entry->d_name,"%llu-%d", &ino, &obj_num);
		    printf("parse this file: skyfs_ino %llu, obj_num %d\n", ino, obj_num);
	    }
            int fd = open(file_path, O_RDWR);
            if (fd == -1) {
                perror("open()");
    	    printf("open fsync file %s FAILED\n", file_path);
                continue;
            }
	    fstat(fd, &statbuf);
    	    printf("start fsync regular file %s , ino %lu , size %lu, tmp_pos %d\n", file_path, statbuf.st_ino, statbuf.st_size, cur_pos);
            // 刷新文件
            fsync(fd);
            //close(fd);
	    tmp_offset = 0;

	    if(cur_pos >= total_buf_len-head_size){
		fprintf(stderr, "buffer %p is full , pos %lu, should send and clear BUF\n", tmp_buf, cur_pos);
		cur_pos = 0;

	    }
	    while(cur_pos < total_buf_len-head_size){
		    cur_pos += head_size;
		    tmp_buf = &(part_buf[cur_pos]);

		    read_cnt = pread(fd, tmp_buf, total_buf_len - cur_pos, tmp_offset);
		    if(read_cnt <= 0){

			    fprintf(stderr, "buf %p pread %s, from %llu, len %d at end of file Or error, return %d, roll back and try next file\n",tmp_buf,file_path,tmp_offset,  
					    total_buf_len - cur_pos,  read_cnt );
			    cur_pos -= head_size;
			    tmp_buf = &(part_buf[cur_pos]);
			    close(fd);
			    fd = 0;
			    tmp_offset = 0;
			    break;
		    }else if(read_cnt <  (total_buf_len - cur_pos)){
				fprintf(stderr, " buf %p ,pread %s, from %llu, len %d at end of file No error, return %d, try next file\n",tmp_buf, file_path,tmp_offset,  
					    total_buf_len - cur_pos, read_cnt);
			        close(fd);
				fd = 0;
				tmp_offset = 0;
				cur_pos += read_cnt;
				fprintf(stderr, "tmp_pos set to %d\n", cur_pos);
				break;

		    
		    }else{
			    // buf full and file NOT end
			    fprintf(stderr, "buf %p ,pread %s, from %llu, len %d , buf FULL, return %d, SEND buf and continue in same file\n",tmp_buf, file_path,tmp_offset,  
					    total_buf_len - cur_pos,  read_cnt );

	    		    cur_pos = 0;
			    //memset(old_buf, 0, total_buf_len);
			    tmp_offset += read_cnt;

			    continue;


		    }


	    }
	    //tmp_pos += sizeof(statbuf);
	    *buf_pos = cur_pos;
	    continue;
        }
	else{
            char subdir_path[1024];
            snprintf(subdir_path, sizeof(subdir_path), "%s/%s", dir_path, entry->d_name);
	    flush_directory(subdir_path, part_buf, 8*1024*1024, buf_pos);
	    continue;
	}
    }

    cur_pos = *buf_pos;
    if(cur_pos > 0){
	    fprintf(stderr, "need to SEND remain msg , size %lu , from buf %p\n ", cur_pos,part_buf);
    }
 
    closedir(dir);
}
 
int main(int argc, char * argv[]) {
    //const char *dir_path = "/path/to/your/directory"; // 替换为你的目录路径
    const char *dir_path = argv[1]; // 替换为你的目录路径
    if(argc > 2){
	    parse_file_name = atoi(argv[2]);
    }

    int buf_pos = 0;
    char * part_buf = (char *)malloc(8*1024*1024);
    flush_directory(dir_path, part_buf, 8*1024*1024,&buf_pos);
    return 0;
}
