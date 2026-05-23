/* *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_help.h 
 */
#ifndef __SKYFS_HELP_H
#define __SKYFS_HELP_H

#if 0
static inline skyfs_s32_t __skyfs_get_unit_info(
                skyfs_mds_info_t *mds_info,
                skyfs_osd_info_t *osd_info,
                skyfs_client_info_t *client_info)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t mds_num = 0;
    skyfs_u32_t osd_num = 0;
    skyfs_u32_t client_num = 0;
    skyfs_u32_t nic_num = 0;
    skyfs_u32_t i, j;
    skyfs_s8_t  *tmp_str = NULL;
    skyfs_entry_info_t tmp;
    FILE *fp = NULL;

    skyfs_s8_t str[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t pathname[SKYFS_MAX_NAME_LEN];

    SKYFS_ENTER("__skyfs_get_unit_info:enter\n");
    sprintf(pathname, "%s%s", SKYFS_ARCCFG_FILE_PATH, SKYFS_GEN_CONFIG);
    fp = fopen(pathname, "r");
    if(!fp){
        rc = -1;
        SKYFS_ERROR("__skyfs_get_unit_info:failed to open %s\n", pathname);
        goto err_out;
    }
    
    /*1. pass useless sentense*/
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        if(str[0] == '#') continue;
        if(strncmp(str, "[MDS]", 5) == 0){
            SKYFS_MSG("__skyfs_get_unit_info:begin to parse mds\n");
            break;
        }
    }

    /*2. parse mds info*/
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        if(str[0] == '#') continue;
        if(strncmp(str, "[OSD]", 5) == 0){
            SKYFS_MSG("__skyfs_get_unit_info:begin to parse osd\n");
            break;        
        }

        if(strncmp(str, "ID:", 3) == 0){
            tmp_str = str + 3;
            tmp.id = atoi(tmp_str);
            nic_num = 0;
            mds_info->mds[mds_num].id = tmp.id;
        }else{
            goto err_out;
        }

get_another_addr:
        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);
        if(strncmp(str, "TCP:", 4) == 0){
            SKYFS_MSG("__skyfs_get_unit_info:%s\n", str);
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[nic_num], tmp_str);
            strcpy(mds_info->mds[mds_num].TCP.addr[nic_num], tmp_str);
            nic_num ++;
            goto get_another_addr;
        }
        mds_info->mds[mds_num].TCP.nic_num = nic_num;
        SKYFS_MSG("__skyfs_get_unit_info:%d,%s,%d.\n", 
            tmp.id, tmp.TCP.addr[nic_num - 1], mds_num);
        bzero(&tmp, sizeof(skyfs_entry_info_t));
        bzero(str, SKYFS_MAX_NAME_LEN);
        mds_num ++;    
    }

    mds_info->mds_num = mds_num;

    /*3. parse osd info*/
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        if(str[0] == '#') continue;
        if(strncmp(str, "[CLIENT]", 7) == 0){
            SKYFS_MSG("__skyfs_get_unit_info:begin to parse client\n");
            break;    
        }

        if(strncmp(str, "ID:", 3) == 0){
            tmp_str = str + 3;
            tmp.id = atoi(tmp_str);
        }else{
            goto err_out;
        }

        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);

        if(strncmp(str, "TCP:", 4) == 0){
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[0], tmp_str);
            tmp.TCP.nic_num = 1;
        }else{
            goto err_out;
        }

        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);

        if(strncmp(str, "TCP:", 4) == 0){
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[1], tmp_str);
            tmp.TCP.nic_num = 2;
        }else{
            goto get_pid;
        }

        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);

        if(strncmp(str, "TCP:", 4) == 0){
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[2], tmp_str);
            tmp.TCP.nic_num = 3;
        }else{
            goto get_pid;
        }

        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);

        if(strncmp(str, "TCP:", 4) == 0){
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[3], tmp_str);
            tmp.TCP.nic_num = 4;
        }else{
            goto get_pid;
        }

        /*
        */
get_pid:
        if(strncmp(str, "PID:", 4) == 0){
            tmp_str = str + 4;
            tmp.pid = atoi(tmp_str);
        }else{
        //    bzero(str, SKYFS_MAX_NAME_LEN);
        //    fgets(str, SKYFS_MAX_NAME_LEN, fp);
            goto get_pid;
        }

        SKYFS_MSG("__skyfs_get_unit_info:%d,%s,%d\n", 
            tmp.id, tmp.TCP.addr[0], tmp.pid);
        memcpy(&(osd_info->osd[osd_num]), &tmp, sizeof(skyfs_entry_info_t));
        bzero(&tmp, sizeof(skyfs_entry_info_t));
        bzero(str, SKYFS_MAX_NAME_LEN);
        osd_num ++;
    }

    osd_info->osd_num = osd_num;

    /*4. parse client info*/
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        if(str[0] == '#') continue;

        if(strncmp(str, "ID:", 3) == 0){
            tmp_str = str + 3;
            tmp.id = atoi(tmp_str);
        }else{
            goto err_out;
        }

        bzero(str, SKYFS_MAX_NAME_LEN);
        fgets(str, SKYFS_MAX_NAME_LEN, fp);
        if(strncmp(str, "TCP:", 4) == 0){
            tmp_str = str + 4;
            strcpy(tmp.TCP.addr[0], tmp_str);
        }else{
            goto err_out;
        }
        tmp.TCP.nic_num = 1;
        SKYFS_MSG("__skyfs_get_unit_info:%d,%s\n", tmp.id, tmp.TCP.addr[0]);
        memcpy(&(client_info->client[client_num]), &tmp, sizeof(skyfs_entry_info_t));
        bzero(&tmp, sizeof(skyfs_entry_info_t));
        bzero(str, SKYFS_MAX_NAME_LEN);
        client_num ++;
    }

    client_info->client_num = client_num;

    /*5 init osd group*/
    
    j = 0;
    for(i = 0; i < osd_info->osd_num; i = i + SKYFS_OSD_GROUP_NUM){
        osd_info->osd_group[j] = osd_info->osd[i].id;
        j ++;
    }

    osd_info->group_num = j;


err_out:

    SKYFS_ENTER("__skyfs_get_unit_info:exit\n");
    return rc;
}
#endif

static inline skyfs_s32_t __skyfs_get_arch_info(skyfs_arch_info_t *arch_info)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t ip_num = 0;
    FILE *fp = NULL;

    skyfs_s8_t str[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t pathname[SKYFS_MAX_NAME_LEN];
    skyfs_s8_t ip[SKYFS_MAX_ADDR_LEN];
    skyfs_s8_t mds[16];
    skyfs_s8_t mds_str[16];
    skyfs_s8_t osd[16];
    skyfs_s8_t osd_str[16];
    skyfs_s8_t client[16];
    skyfs_s8_t client_str[16];
    skyfs_s8_t l1[16];
    skyfs_s8_t l1_str[16];
    skyfs_s8_t l2[16];
    skyfs_s8_t l2_str[16];
    skyfs_s8_t l3[16];
    skyfs_s8_t l3_str[16];
    skyfs_s8_t lid[16];
    skyfs_s8_t lid_str[16];

    SKYFS_ENTER("__skyfs_get_arch_info:enter\n");
    sprintf(pathname, "%s%s", SKYFS_ARCCFG_FILE_PATH, SKYFS_GEN_CONFIG);
    fp = fopen(pathname, "r");
    if(!fp){
        rc = -1;
        SKYFS_ERROR("__skyfs_get_arch_info:failed to open %s\n", pathname);
        goto err_out;
    }
    
    while(fgets(str, SKYFS_MAX_NAME_LEN, fp)){
        if(strlen(str) <= 1) continue;
        if(str[0] == '#') continue;
        sscanf(str, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
            ip, mds, mds_str, osd, osd_str, client, client_str,
            l1, l1_str, l2, l2_str, l3, l3_str, lid_str, lid);

        SKYFS_MSG("%s %s %s %s %s %s %s %s\n", 
            ip, mds_str, osd_str, client_str, l1_str, l2_str, l3_str, lid);

        arch_info->ip[ip_num].L1 = atoi(l1_str);
        arch_info->ip[ip_num].L2 = atoi(l2_str);
        arch_info->ip[ip_num].L3 = atoi(l3_str);
        arch_info->ip[ip_num].mds = atoi(mds_str);
        arch_info->ip[ip_num].osd = atoi(osd_str);
        arch_info->ip[ip_num].client = atoi(client_str);
        arch_info->ip[ip_num].lid= atoi(lid);
        strcpy(arch_info->ip[ip_num].addr, ip);

        ip_num ++;
    }
    arch_info->ip_num = ip_num;

err_out:

    SKYFS_LEAVE("__skyfs_get_arch_info:exit,ip_num:%d\n", ip_num);
    return rc;
}
static inline 
skyfs_s32_t __skyfs_init_nodes(skyfs_arch_info_t *arch_info, 
        skyfs_mds_info_t *mds_info,
        skyfs_osd_info_t *osd_info,
        skyfs_client_info_t *client_info)
{
    skyfs_u32_t mds_num = 0;
    skyfs_u32_t osd_num = 0;
    skyfs_u32_t client_num = 0;
    skyfs_u32_t ip_num = 0;
    skyfs_u32_t match_flag = 0;
    skyfs_u32_t L1, L2, L3;
    skyfs_u32_t i;
    skyfs_u32_t mds_id, osd_id, client_id;
    struct list_head *head, *index, *L2_head, *L2_index, *L3_head, *L3_index;
    skyfs_L1_info_t *L1_node = NULL;
    skyfs_L2_info_t *L2_node = NULL;
    skyfs_L3_info_t *L3_node = NULL;
    skyfs_s32_t     rc = 0;

    head = index = L2_head = L2_index = L3_head = L3_index = NULL;
    mds_id = osd_id = client_id = 0;
    INIT_LIST_HEAD(&(arch_info->topo_head));

    SKYFS_MSG("skyfs_init_nodes:ip_num:%d\n", arch_info->ip_num);
    for(i = 0; i < arch_info->ip_num; i++){
        /*Get MDS info*/
        SKYFS_MSG("skyfs_init_nodes:i:%d\n", i);
        mds_id = arch_info->ip[i].mds;
        if(mds_id > 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,mds_id:%d\n", i, mds_id);
            if(mds_info->mds[mds_id].id == 0){
                mds_info->mds[mds_id].id = mds_id;
                mds_info->mds[mds_id].ip_num = 1;
                mds_info->mds[mds_id].ip[0] = &arch_info->ip[i];
                mds_num ++;
                mds_info->mds_num ++;
            }else{
                ip_num = mds_info->mds[mds_id].ip_num;
                mds_info->mds[mds_id].ip[ip_num] = &arch_info->ip[i];
                mds_info->mds[mds_id].ip_num ++;
            }
        }

        /*Get OSD info*/
        osd_id = arch_info->ip[i].osd;
        if(osd_id > 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,init osd,osd_id:%d\n", i, osd_id);
            if(osd_info->osd[osd_id].id == 0 ){
                osd_info->osd[osd_id].id = osd_id;
                osd_info->osd[osd_id].ip_num = 1;
                osd_info->osd[osd_id].ip[0] = &arch_info->ip[i];
                osd_num ++;
                osd_info->osd_num ++;
            }else{
                ip_num = osd_info->osd[osd_id].ip_num;
                osd_info->osd[osd_id].ip[ip_num] = &arch_info->ip[i];
                osd_info->osd[osd_id].ip_num ++;
            }
        }

        /*Get CLIENT info*/
        client_id = arch_info->ip[i].client;
        if(client_id > 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,init client,client_id:%d\n", i, client_id);
            if(client_info->client[client_id].id == 0){
                client_info->client[client_id].id = client_id;
                client_info->client[client_id].ip_num = 1;
                client_info->client[client_id].ip[0] = &arch_info->ip[i];
                client_num ++;
                client_info->client_num ++;
            }else{
                ip_num = client_info->client[client_id].ip_num;
                client_info->client[client_id].ip[ip_num] = &arch_info->ip[i];
                client_info->client[client_id].ip_num ++;
            }
        }

        /*Construct network topology*/
        SKYFS_MSG("skyfs_init_nodes:i:%d,init arch\n", i);
        L1 = arch_info->ip[i].L1;
        L2 = arch_info->ip[i].L2;
        L3 = arch_info->ip[i].L3;
        /*Search the top level*/
        SKYFS_MSG("skyfs_init_nodes:i:%d,check L3\n", i);
        match_flag = 0;    
        head = &arch_info->topo_head; 
        if(!list_empty(head)){
            list_for_each(index, head){
                L3_node = list_entry(index, skyfs_L3_info_t, arch_list);
                if(L3_node->L3 == L3){
                       match_flag = 1;
                }
            }
        }

        if(match_flag == 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,alloc L3\n", i);
            L3_node = (skyfs_L3_info_t *)malloc(sizeof(skyfs_L3_info_t));
            L3_node->L3 = L3;
            L3_node->L2_num = 0;
            INIT_LIST_HEAD(&(L3_node->L3_head));
            list_add_tail(&(L3_node->arch_list), &arch_info->topo_head);
        }
            
        /*Search the second level*/
        SKYFS_MSG("skyfs_init_nodes:i:%d,check L2\n", i);
        match_flag = 0;    
        L3_head = &L3_node->L3_head;
        if(!list_empty(L3_head)){
            list_for_each(L3_index, L3_head){
                L2_node = list_entry(L3_index, skyfs_L2_info_t, L3_list);
                if(L2_node->L2 == L2){
                       match_flag = 1;
                }
            }
        }
    
        if(match_flag == 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,alloc L2\n", i);
            L2_node = (skyfs_L2_info_t *)malloc(sizeof(skyfs_L2_info_t));
            L2_node->L2 = L2;
            L2_node->L3 = L3;
            L2_node->L1_num = 0;
            INIT_LIST_HEAD(&(L2_node->L2_head));
            list_add_tail(&(L2_node->L3_list), &(L3_node->L3_head));
            L3_node->L2_num ++;
        }
    
        /*Search the second level*/
        SKYFS_MSG("skyfs_init_nodes:i:%d,check L1\n", i);
        match_flag = 0;    
        L2_head = &L2_node->L2_head;
        if(!list_empty(L2_head)){
            list_for_each(L2_index, L2_head){
                L1_node = list_entry(L2_index, skyfs_L1_info_t, L2_list);
                if(L1_node->L1 == L1){
                       match_flag = 1;
                }
            }
        }

        if(match_flag == 0){
        SKYFS_MSG("skyfs_init_nodes:i:%d,alloc L1\n", i);
            L1_node = (skyfs_L1_info_t *)malloc(sizeof(skyfs_L1_info_t));
            L1_node->L1 = L1;
            L1_node->L2 = L2;
            L1_node->L3 = L3;
            L1_node->ip_num = 0;
            INIT_LIST_HEAD(&(L1_node->L1_head));
            list_add_tail(&(L1_node->L2_list), &(L2_node->L2_head));
            L2_node->L1_num ++;
        }

        list_add_tail(&arch_info->ip[i].L1_list, &L1_node->L1_head);
        L1_node->ip_num ++;

    }

    return rc;
}

static inline 
skyfs_s32_t __skyfs_init_osd_group(skyfs_osd_info_t *osd_info)
{
    skyfs_s32_t rc = 0;
    skyfs_u32_t i, j;

    j = 0;

    for(i = 0; i < osd_info->osd_num; i = i + SKYFS_OSD_GROUP_NUM){
        j ++;
    }

    return rc;
}

static skyfs_s32_t tmp_print_tty()
{
	int fd = 0;
	if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
		ioctl(fd, TIOCSCTTY, (char *) NULL);
		fprintf(fd, "Try to recover tty success \n");
		sleep(1);
		ioctl(fd, TIOCNOTTY, (char *) NULL);
		close(fd);
	}else{
		fprintf(stderr, "Try to recover tty failed \n");

	}



}
static inline skyfs_s32_t __skyfs_daemonize(int id)
{
    int i, fd;

    if (fork() != 0)
        exit(0);

    if ((fd = open("/dev/tty", O_RDWR)) >= 0) {    /* disassociate
                 
        				    * contolling tty */

	//printf("Found tty, distatch it \n");
        ioctl(fd, TIOCNOTTY, (char *) NULL);
        close(fd);
    }

    setpgrp ();

    for (fd = 0; fd < 3; fd++)
        close(fd);

    i = open("/dev/null", O_RDONLY, 0);
    if (i != 0) {
        abort();
    }
    i = open("/dev/null", O_WRONLY, 0);
    if (i != 1) {
        abort();
    }
    
    {
        char *p;
        char prg_name [256];
        char log_name [1024];
        char hostname [256];

        strcpy (prg_name, getenv("_"));
        i = strlen (prg_name);
        while (*(prg_name + i) != '/') i --;
        i ++;
        p = &(prg_name [i]);

        gethostname(hostname, 256);
    
        sprintf (log_name, "%s%s-%d.%s.log", 
                SKYFS_LOG_FILE_PATH, p, id, hostname);
        unlink (log_name);

        i = open(log_name, (O_WRONLY | O_CREAT), 0666);
        if (i != 2) {
            abort();
        }
    }
    
    setpriority (PRIO_PGRP, getpgrp(), -20);
    
    return 0;
}

static inline skyfs_s32_t
__skyfs_five_cpu_numbers(skyfs_u64_t *uret, 
                skyfs_u64_t *nret, 
                skyfs_u64_t *sret, 
                skyfs_u64_t *iret, 
                skyfs_u64_t *iowait)
{
    FILE *fp;
    skyfs_u32_t byte_read;
    skyfs_s8_t buffer[100];
    skyfs_s32_t rc = 0;
        
    fp = fopen("/proc/stat", "r");
    byte_read = fread(buffer, 1, sizeof(buffer)-1, fp);
    fclose(fp);
        
    if (byte_read==0 || byte_read==sizeof(buffer)){
        rc = EIO;    
    }

    buffer[byte_read] = '\0';
        
    sscanf(buffer, "cpu %Lu %Lu %Lu %Lu %Lu", uret, nret, sret, iret, iowait);

    SKYFS_MSG("five_cpu_numbers:%lld,%lld,%lld,%lld,%lld\n",
        *uret,*nret,*sret,*iret,*iowait);


    return rc;

}

static inline void __skyfs_get_hostname(skyfs_s8_t *hostname, 
                skyfs_s8_t *str, 
                skyfs_u32_t flag)
{
    FILE *fp_hostname = NULL;
    sprintf(str, "%s%s", "hostname >", SKYFS_HOSTNAME_CONFIG);
    system(str);
    bzero(str, SKYFS_MAX_NAME_LEN);

    fp_hostname = fopen(SKYFS_HOSTNAME_CONFIG, "r");
    fgets(str, SKYFS_MAX_NAME_LEN, fp_hostname);
    sscanf(str, "%s", str);
    SKYFS_MSG("__skyfs_get_hostname:%d\n", flag);
    if(flag){
        sprintf(hostname, "%s%s", "g", str);
    }else{
        sprintf(hostname, "%s", str);
    }

    bzero(str, SKYFS_MAX_NAME_LEN);
    SKYFS_ERROR("__skyfs_get_hostname:%s\n", hostname);
}

static inline void 
__skyfs_get_starttime(skyfs_timespec_t *starttime, skyfs_u32_t flag)
{
    if(flag == 0){
        SKYFS_MSG("__skyfs_get_starttime\n");
    }else if (flag == 1){
        gettimeofday(starttime, NULL);    
    }
}

static inline skyfs_u32_t
__skyfs_get_endtime(skyfs_timespec_t *starttime, 
                skyfs_timespec_t *endtime, 
                skyfs_u32_t flag,
                const skyfs_s8_t *type)
{
    skyfs_u32_t num_usec = 0;
	
	type = NULL;

    if (flag == 1){
        gettimeofday(endtime, NULL);    
        num_usec = (endtime->tv_sec - starttime->tv_sec) * 1000000 
            + (endtime->tv_usec - starttime->tv_usec);
        SKYFS_MSG("%s:%d\n", type, num_usec);
    }

    return num_usec;
}

static inline skyfs_s32_t __skyfs_set_bit(skyfs_u8_t *addr,
    skyfs_u32_t local, 
    skyfs_u32_t value)
{
    skyfs_u8_t *p;
    skyfs_u32_t off;
    skyfs_u32_t bits;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_set_bit:enter.addr:%p,local:%d,val:%d\n",addr,local,value);

    off = local / 8;
    bits = local % 8;

    p = addr + off;

    if(value == 0){
        value = 1 << bits;
        value = ~value;
        *p = *p & value;
    }
    else if(value == 1){
        value = 1 << bits;
        *p = *p | value;
    }else{
        SKYFS_ERROR("__skyfs_set_bit:error value\n");
        rc = -1;
    }
    SKYFS_LEAVE("__skyfs_set_bit:leave.rc:%d\n",rc);

    return rc;

}

static inline skyfs_s32_t __skyfs_is_set(skyfs_u8_t *addr, skyfs_u32_t size)
{
    skyfs_u8_t  *p = NULL;
    skyfs_u32_t i;
    skyfs_s32_t rc = 0;

    SKYFS_ENTER("__skyfs_is_set:addr:%p,size:%d.\n",addr,size);

    p = addr;

    for(i = 0; i < size; i++){
        if(*p != 0){
            rc = 1;
            break;
        }
        p ++;
    }

    SKYFS_LEAVE("__skyfs_is_set:leave.rc=%d\n",rc);
    return rc;
}


static inline skyfs_s32_t
__skyfs_test_bit(skyfs_u8_t *addr, skyfs_u32_t local)
{
    skyfs_u8_t      *p;
    skyfs_u32_t     off;
    skyfs_u32_t     bits;
    skyfs_u8_t      value;
    skyfs_s32_t     rc = 0;

    SKYFS_ENTER("__skyfs_test_bit:enter.addr:%p,local:%d\n", addr, local);

    off = local / 8;
    bits = local % 8;

    p = addr + off;
    
    value = 1 << bits;

    rc = *p & value;

    return rc;
}

static inline skyfs_s32_t
__skyfs_init_req(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t msg_type,
                skyfs_u32_t ack_flag,
                skyfs_u32_t req_type,
                skyfs_u32_t    msgsize,
                skyfs_u32_t fromtype,
                skyfs_u32_t fromid)
{
    skyfs_s32_t rc = 0;
    amp_request_t *reqp = NULL;
    skyfs_msg_t      *msg = NULL;

    rc = __amp_alloc_request(&reqp);

    if(rc != 0){
        SKYFS_ERROR("__skyfs_init_req:alloc request failed\n"); 
        goto err_out;
    }

    reqp->req_msg = (amp_message_t *)malloc(msgsize);
    if(!reqp->req_msg){
        SKYFS_ERROR("__skyfs_init_req:alloc req msg failed\n");
        rc = -ENOMEM;
        goto err_out;
    }

    bzero(reqp->req_msg, msgsize);

    reqp->req_msglen = msgsize;
    reqp->req_need_ack = ack_flag;
    reqp->req_resent = 1;
    reqp->req_type = req_type;
    reqp->req_niov = 0;
    reqp->req_iov = NULL;

    msg = (skyfs_msg_t *)((skyfs_s8_t *)(reqp->req_msg) + AMP_MESSAGE_HEADER_LEN);
    msg->magic = SKYFS_MSG_MAGIC;
    msg->fs_id = 0;
    msg->type = msg_type;
    msg->error = 0;
    msg->fromid = fromid;
    msg->fromType = fromtype;
    *msgp = msg;
    *req = reqp;

    return rc;

err_out:
        
    if(reqp->req_msg){
        free(reqp->req_msg);
    }

    if(reqp != NULL){
        __amp_free_request(reqp);
    }

    return rc;
}

static inline skyfs_s32_t    
__skyfs_init_reply(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t req_type,
                skyfs_u32_t req_niov,
                amp_kiov_t 	*req_iov,
				skyfs_u32_t size)
{
    amp_message_t     *replymsgp = NULL;
    skyfs_s32_t        rc = 0;

    replymsgp = (amp_message_t *)malloc(size);
    if(replymsgp == NULL){
        rc = -1;
        SKYFS_ERROR("__skyfs_init_reply:alloc reply failed\n");
        goto ERR;
    }

    bzero(replymsgp, size);
    memcpy(replymsgp, (*req)->req_msg, AMP_MESSAGE_HEADER_LEN);

    (*req)->req_reply = replymsgp;

    *msgp = __skyfs_get_msg((*req)->req_reply);

    (*req)->req_replylen = size;
    (*req)->req_need_ack = 0;
    (*req)->req_resent    = 1;
    (*req)->req_type = req_type;
    (*req)->req_niov = req_niov;
    (*req)->req_iov = req_iov;

ERR:
    return rc;
}

static inline int __skyfs_get_config(skyfs_arch_info_t *config_info,
					skyfs_u32_t type,
					skyfs_u32_t id,
				    amp_comp_context_t *context)
{
	amp_request_t *req = NULL;
	skyfs_msg_t *msgp = NULL;
	skyfs_u32_t size = 0;
	skyfs_s32_t rc = 0;

	SKYFS_ENTER("__skyfs_get_config:enter.\n");

	rc = __amp_alloc_request(&req);
	if(rc != 0){
		SKYFS_ERROR("__skyfs_get_config:alloc_request failed\n");
		goto err_none;
	}

	rc = -ENOMEM;

	size = AMP_SKYFS_MSGHEAD_SIZE + sizeof(skyfs_getconfig_args_t);
	req->req_msg = (amp_message_t *)malloc(size);
	if(!req->req_msg){
		SKYFS_ERROR("__skyfs_get_config:alloc req_msg failed\n");
		rc = -errno;
		goto err_req;
	}
	

	bzero(req->req_msg, size);
	// changed by mayl for RDMA
	SKYFS_INIT_MSG(msgp, req, SKYFS_FSID, SKYFS_MSG_GET_CONFIG,
		id, type, size);
	msgp->u.getconfigReq.type = type;
	msgp->u.getconfigReq.id = id;
	
	SKYFS_FILL_REQ(req, SKYFS_NEED_ACK, AMP_REQUEST|AMP_MSG, size);

	context->conn_type = 1;
	SKYFS_MSG("__skyfs_get_config:before send:req %p\n", req);

	rc = amp_send_sync(context, req, SKYFS_ADM, 1, 1);
	if(rc < 0){
		SKYFS_ERROR("__skyfs_get_config:send request failed.rc:%d\n", rc);
		goto err_msg;
	}

	msgp = __skyfs_get_msg(req->req_reply);
	rc = msgp->error;
	if(rc >= 0){
		SKYFS_MSG("__skyfs_get_config:copy arch_info\n");
		memcpy(config_info, &(msgp->u.getconfigAck.arch_info), 
			sizeof(skyfs_arch_info_t));
	}


	if(req->req_reply){
		free(req->req_reply);
	}
err_msg:
	if(req->req_msg){
		free(req->req_msg);
	}
err_req:
	if(req){
		__amp_free_request(req);
	}
err_none:

	SKYFS_LEAVE("__skyfs_get_config:exit\n");

	return rc;


}

static inline void __skyfs_init_mdsmapping(skyfs_u32_t consistent_ok,
    skyfs_mds_info_t *mds_info,
    skyfs_layout_L1_t *mds_mapping_l1)
{
    skyfs_u32_t i,j;
    skyfs_u32_t mds_id;
    skyfs_u32_t mds_last_index = 0;
    skyfs_u64_t hashvalue, old_hash;

    for(j = 0; j < SKYFS_MAX_MDS_NUM; j ++){
next_mds:
        if(consistent_ok){
            if(mds_info->mds[j].id > 0){
            	SKYFS_MSG("init_mdsmapping:con,id:%d\n", mds_info->mds[j].id);
                mds_id = mds_info->mds[j].id;
                hashvalue = __skyfs_num2hashvalue(mds_id);
                hashvalue = hashvalue % SKYFS_MDS_L1MAPPING_LEN;
                old_hash = hashvalue;
                while(mds_mapping_l1[hashvalue].id){
                    hashvalue = (hashvalue + 1) % SKYFS_MDS_L1MAPPING_LEN;
                    if(hashvalue == old_hash){
                        goto next_mds;
                    }
                }
                mds_mapping_l1[hashvalue].id =  mds_id; 
            }
        }else{
            if(mds_info->mds[j].id > 0){
                mds_id = mds_info->mds[j].id;
                hashvalue = (j - 1) * (SKYFS_MDS_L1MAPPING_LEN / mds_info->mds_num);
                mds_mapping_l1[hashvalue].id = mds_id;
                SKYFS_MSG("init_mdsmapping:id:%d,hash:%lld\n", 
					mds_info->mds[j].id, hashvalue);
            }
        }
    }

    if(consistent_ok){
        for(i = 0; i < SKYFS_MDS_L1MAPPING_LEN; i++){
            if(mds_mapping_l1[i].id != 0){
                mds_last_index = i;
            }
        }
    }else{
        mds_last_index = 0;
    }

    mds_id = mds_mapping_l1[mds_last_index].id;
    SKYFS_MSG("init_mdsmapping:last_index:%d,id:%d\n", 
		mds_last_index,mds_id);
    for(i = 0; i < SKYFS_MDS_L1MAPPING_LEN; i ++){
        if(mds_mapping_l1[i].id != 0){
            mds_id = mds_mapping_l1[i].id;
        }else{
            mds_mapping_l1[i].id = mds_id;
        }
    }
}

static inline void __skyfs_init_osdmapping(skyfs_u32_t consistent_ok,
    skyfs_osd_info_t *osd_info,
    skyfs_layout_L1_t *osd_mapping_l1)
{
    skyfs_u32_t i,j;
    skyfs_u32_t osd_id;
    skyfs_u32_t osd_last_index = 0;
    skyfs_u64_t hashvalue, old_hash;
	skyfs_u32_t counter = 0;

    SKYFS_MSG("init_osdmapping:id:%d,osd_num:%d\n", 
		osd_info->osd[1].id, osd_info->osd_num);
    for(j = 0; j < SKYFS_MAX_OSD_NUM; j ++){
next_osd:
        if(consistent_ok){
            if(osd_info->osd[j].id > 0){
                SKYFS_MSG("init_osdmapping:id:%d\n", osd_info->osd[j].id);
                osd_id = osd_info->osd[j].id;
                hashvalue = __skyfs_num2hashvalue(osd_id);
                hashvalue = hashvalue % SKYFS_OSD_L1MAPPING_LEN;
                old_hash = hashvalue;
                while(osd_mapping_l1[hashvalue].id){
                    hashvalue = (hashvalue + 1) % SKYFS_OSD_L1MAPPING_LEN;
                    if(hashvalue == old_hash){
                        goto next_osd;
                    }
                }
                osd_mapping_l1[hashvalue].id =  osd_id; 
				counter ++;
            }
        }else{
            if(osd_info->osd[j].id > 0){
                osd_id = osd_info->osd[j].id;
                hashvalue = (j - 1) * (SKYFS_OSD_L1MAPPING_LEN / osd_info->osd_num);
                osd_mapping_l1[hashvalue].id = osd_id;
				SKYFS_MSG("init_osdmapping:id:%d,hash:%lld\n", 
					osd_info->osd[j].id, hashvalue);
            }
        }
    }

    if(consistent_ok){
        for(i = 0; i < SKYFS_OSD_L1MAPPING_LEN; i++){
            if(osd_mapping_l1[i].id != 0){
                osd_last_index = i;
            }
        }
    }else{
        osd_last_index = 0;
    }

    osd_id = osd_mapping_l1[osd_last_index].id;
    for(i = 0; i < SKYFS_OSD_L1MAPPING_LEN; i ++){
        if(osd_mapping_l1[i].id != 0){
            osd_id = osd_mapping_l1[i].id;
        }else{
            osd_mapping_l1[i].id = osd_id;
        }
    }

	SKYFS_LEAVE("init_osdmapping:counter:%d\n", counter);
}

#if 0
static inline unsigned char random(void)
{
	/* See "Numerical Recipes in C", second edition, p. 284 */
    random_num = random_num * 1664525L + 1013904223L;
    return (unsigned char) (random_num >> 24);
}
#endif
#endif
/* end of skyfs_help.h*/
