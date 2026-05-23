#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#if 1
#include "../include/skyfs_sys.h"
#include "../include/skyfs_list.h"
#include "../include/skyfs_const.h"
#include "../include/skyfs_types.h"
#include "../include/skyfs_fs.h"

#include "amp.h"

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

#include "../mds/mds_fs.h"

#include "osd_ito.h"
#endif


extern skyfs_u32_t      skyfs_recover_data_size;
extern int __skyfs_send_replica_recover_msg(skyfs_o_replica_recover_t * request_head, char * part_buf, uint64_t xid, int dest_osd_id);
static int parse_file_name = 1;
static int ttmp_pos = 0;

void get_all_replica_partition_state(char * dir_path, uint64_t * p_partitions, int64_t * p_fault_partitions){


    struct dirent *entry = NULL;
    skyfs_DL_part_t tmp_part;
    uint64_t partitions = 0;
    uint64_t fault_partitions = 0;
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        SKYFS_ERROR_1("opendir() failed \n");
        return;
    }

    partitions = 0;
    fault_partitions = 0;
    SKYFS_ERROR_1("scan and stat dir %s  \n", dir_path);
    while ((entry = readdir(dir)) != NULL) {


            SKYFS_ERROR("check file %s  \n", entry->d_name);
	    if(!strcmp(entry->d_name, "."))
		    continue;

	    if(!strcmp(entry->d_name, ".."))
		    continue;
	    //printf("get_file_name %s\n",  entry->d_name);
            if (entry->d_type == DT_REG) { 
            	char file_path[1024];
		int fd = 0;
		partitions++;
		memset(&tmp_part, 0 , sizeof(skyfs_DL_part_t));
		memset(file_path, 0, 1024);
            	snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
		fd = open(file_path, O_RDWR);
		
        	SKYFS_ERROR("open file %s get fd %d \n", file_path, fd);
            	if (fd <= 0) {
                	
    	    		SKYFS_ERROR_1(stderr, "open part file %s FAILED\n", file_path);
			fault_partitions++;
                	continue;
            	}
		int read_cnt = pread(fd, &tmp_part, sizeof(tmp_part) , 0);
		if(read_cnt <= 0){
			SKYFS_ERROR_1("read replica part file %s failed %d, offset %llu length %llu \n", file_path, read_cnt, 0, sizeof(tmp_part));
			close(fd);
			fault_partitions++;
			continue;
		}
		close(fd);
		if((int64_t)tmp_part.replica_write_version < 0){
			SKYFS_ERROR_1("read replica part file %s version invalid %lld,  \n", file_path, tmp_part.replica_write_version );
			close(fd);
			fault_partitions++;
			continue;
		}

	    }// endif

	}// end while

    	
    	SKYFS_ERROR_1("%llu partition files in dir %s, %llu record fault, record size %d\n", partitions, dir_path, fault_partitions, sizeof(tmp_part));
	*p_partitions = partitions;
	*p_fault_partitions = fault_partitions;



   }

void flush_directory(const char *dir_path, char * part_buf, 
		size_t total_buf_len,int * buf_pos, skyfs_o_replica_recover_t *request_head, int dest_osd_id) {

    int cur_pos ;
    char * tmp_buf = NULL;
    int rc = 0;
    
    struct dirent *entry = NULL;
    skyfs_replica_recover_head_t * recover_head = NULL;
    struct stat statbuf;
    ssize_t  read_cnt = 0;
    int tmp_offset;
    uint64_t ino = 0;
    int obj_num = 0;

    int head_size = sizeof(skyfs_replica_recover_head_t);
    //int head_size = 32;
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
		    sscanf(entry->d_name,"%llu-%d", &ino, &obj_num);
		    fprintf(stderr,"parse this file: skyfs_ino %llu, obj_num %d\n", ino, obj_num);
	    }
            int fd = open(file_path, O_RDWR);
            if (fd == -1) {
                perror("open()");
    	    fprintf(stderr, "open fsync file %s FAILED\n", file_path);
                continue;
            }
	    fstat(fd, &statbuf);
    	    printf("start fsync regular file %s , ino %lu , size %lu, tmp_pos %d\n", file_path, statbuf.st_ino, statbuf.st_size, cur_pos);
            // 刷新文件
            //fsync(fd);
            //close(fd);
	    tmp_offset = 0;

	    if(cur_pos >= total_buf_len-head_size){
	        fprintf(stderr, "buf full pre  at line %d, try to send buf\n", __LINE__);
		request_head->total_data_size = cur_pos;
	        rc = __skyfs_send_replica_recover_msg(request_head, part_buf, request_head->xid, dest_osd_id);
		fprintf(stderr, "buffer %p is full , pos %lu, should send and clear BUF, rc %d, first char %x\n", tmp_buf, cur_pos, rc, tmp_buf[0]);
		memset(part_buf, 0 , total_buf_len);
		cur_pos = 0;
		request_head->replica_obj_cnt = 0;
		request_head->total_data_size = 0;

	    }
	    while(cur_pos < total_buf_len-head_size){
		    recover_head = (skyfs_replica_recover_head_t *)(&(part_buf[cur_pos]));
		    cur_pos += head_size;
		    tmp_buf = &(part_buf[cur_pos]);
		    recover_head->ino = ino;
		    recover_head->obj_id = obj_num;


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
				//if(read_cnt == 176){
				//	fprintf(stderr, " tmp_buf[0x70] = %x, tmp_buf %p , part_buf %p \n", tmp_buf[0x70], tmp_buf, part_buf);
				//}
			        close(fd);
				recover_head->start_offset = tmp_offset;
				recover_head->size = read_cnt;
				request_head->replica_obj_cnt ++;
				request_head->total_data_size += read_cnt;
				fd = 0;
				tmp_offset = 0;
				cur_pos += read_cnt;
				fprintf(stderr, "tmp_pos set to %d, recover_head %p , part_buf %p\n", cur_pos, recover_head, part_buf);
				break;

		    
		    }else{
			    // buf full and file NOT end
			    recover_head->start_offset = tmp_offset;
			    recover_head->size = read_cnt;
			    request_head->replica_obj_cnt ++;
			    request_head->total_data_size += read_cnt;
			    cur_pos += read_cnt;

			    fprintf(stderr, "buf full at line %d, try to send buf xid %llu, offset %llu\n", __LINE__,  request_head->xid, recover_head->start_offset);
			    request_head->total_data_size = cur_pos;
			    rc = __skyfs_send_replica_recover_msg(request_head, part_buf, request_head->xid, dest_osd_id);
			    fprintf(stderr, "buf %p ,pread %s, from %llu, len %d , buf FULL, return %d, SEND buf and continue in same file, rc %d, first char %x\n",
					    tmp_buf, file_path,tmp_offset,  total_buf_len - cur_pos,  read_cnt , rc, tmp_buf[0]);

	    		     cur_pos = 0;
			     			    //memset(old_buf, 0, total_buf_len);
			     tmp_offset += read_cnt;
			     memset(part_buf, 0 , total_buf_len);
			     request_head->replica_obj_cnt = 0;
			     request_head->total_data_size = 0;
			     fprintf(stderr, "cur_pos set to %d, recover_head %p , part_buf %p\n", cur_pos, recover_head, part_buf);


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
	    flush_directory(subdir_path, part_buf, skyfs_recover_data_size, buf_pos, request_head, dest_osd_id);
	    continue;
	}
    }

    cur_pos = *buf_pos;
    if(cur_pos > 0){
	    fprintf(stderr, "need to SEND remain msg , size %lu , from buf %p\n ", cur_pos,part_buf);
	    request_head->total_data_size = cur_pos;
    }
 
    closedir(dir);
}


#if 0
int main(int argc, char * argv[]) {
    //const char *dir_path = "/path/to/your/directory"; // 替换为你的目录路径
    const char *dir_path = argv[1]; // 替换为你的目录路径
    if(argc > 2){
	    parse_file_name = atoi(argv[2]);
    }

    int buf_pos = 0;
    return 0;
}
#endif
