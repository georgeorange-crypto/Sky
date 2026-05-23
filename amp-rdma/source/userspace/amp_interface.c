/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/
#include <amp.h>
/*
 * we build a new connection
 * 
 * param:  
 *            ctxt - context of this component;
 *            remote_type - the type of remote component
 *            remote_id - the id of remote component
 *            addr - the address of the remote peer, for listen , it's ANY;
 *            port - the port of the remote peer;
 *            conn_type - the type of connection;
 *            direction - the direction of connection.
 *            queue_req - a call back for queuing the request when received.
 *            allocpage - a call back for alloc page for the request.
 *            freepag -  a call back for free the page alloced by above callback.
 *
 * uplevel app should maintain a list which include conneciton,
 * and this list will check the conn per hour and reconn the disconnected conn
 * return:  0 - successfully, <0 - something wrong, return the wrong code.
 */
static long long int seq_num = 0;
// TODO mayl defined for LVSAN
static char* active_mlx_name = "mlx5_0";
char active_rdma_dev_name[256];

// added by mayl for ROCE GID INDEX, set to 5 for LVSUAN
int roce_v2_index = 0;
int opt_mtu = 1024;
amp_lock_t seq_lock;

struct __test_msg_ {
        int type;
        int seqno;
        int len;
        int page_num;
        char msg[512];
};
typedef struct __test_msg_  test_msg__t;


int get_gid_index()
{
	return roce_v2_index;
}

int get_opt_mtu()
{
	return opt_mtu;
}


amp_s32_t skyfs_net_get_var_conf(void)
{
    amp_s32_t rc = 0;
    
    FILE *fp;

    char str[256];
    char dir[256];
    char cmd[256];
    char type[256];

    roce_v2_index = -1;
    AMP_ERROR("__skyfs_net_get_var_conf:enter\n");

    sprintf(dir, "%s%s", "/cluster/skyfs/conf/", "skyfs_var.conf");
    fp = fopen(dir, "r");
    if(!fp){
        rc = -1;
        AMP_ERROR("__skyfs_C_get_var_conf:can't open %s file\n", dir);
        return rc;
    }

        bzero(active_rdma_dev_name, 256);
	strcpy(active_rdma_dev_name, "invalid_device");
    while(fgets(str, 256, fp)){
        if(strlen(str) <= 1) continue;
        bzero(cmd, 256);
        bzero(type, 256);
        sscanf(str, "%s %s", cmd, type); 
	AMP_ERROR("amp check var conf , cmd is %s\n");
#ifdef __AMP_ROCE__
        bzero(str, 256);
        if(strcmp(cmd, "SKYFS_RDMA_GID") == 0){
            roce_v2_index = atoi(type);
            AMP_ERROR("__skyfs_net_get_var_conf:rdma_gid_index:%d\n",roce_v2_index );
            continue;
        }
	
#endif
	if(strcmp(cmd, "SKYFS_RDMA_MTU") == 0){
            opt_mtu = atoi(type);
            AMP_ERROR("__skyfs_net_get_var_conf:rdma_mtu:%d\n",opt_mtu );
            continue;
        }
	if(strcmp(cmd, "SKYFS_RDMA_DEV") == 0){
        		bzero(active_rdma_dev_name, 256);
			strcpy(active_rdma_dev_name, type);
            		AMP_ERROR("__skyfs_get_var_conf:RDMA dev :%s\n", type);
			continue;
	}

    }

    fclose(fp);

    //SKYFS_LEAVE("__skyfs_C_get_var_conf:exit\n");
    if(roce_v2_index <0){
        AMP_ERROR("__skyfs_C_get_var_conf:can't get roce_index %d\n", roce_v2_index);
    }
    return rc;
}


int 
__amp_create_connection (amp_comp_context_t *ctxt,
                       amp_u32_t remote_type,
                       amp_u32_t remote_id,
                       amp_u32_t addr,
                       amp_u32_t port,
                       amp_u32_t conn_type,
                       amp_u32_t direction,
                       int (*queue_req) (amp_request_t *req),
                       int (*allocpages) (void *, amp_u32_t *, amp_kiov_t **),
                       void (*freepages)(amp_u32_t , amp_kiov_t **))
{
    amp_connection_t *conn = NULL;
    amp_s32_t  err = 0;
    struct sockaddr_in sin;
    amp_s32_t *fd = NULL;
#ifdef __AMP_CONNS_DUPLEX
    amp_connection_t *conn_r = NULL;
#endif
    AMP_ENTER("amp_create_connection: enter\n");

    if (direction == AMP_CONN_DIRECTION_ACCEPT) {
        AMP_ERROR("amp_create_connection: wrong direction: %d\n", direction);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("amp_create_connection: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_create_connection: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if ((conn_type != AMP_CONN_TYPE_TCP) && (conn_type != AMP_CONN_TYPE_UDP)) {
        AMP_ERROR("amp_create_connection: wrong type: %d\n", conn_type);
        err = -EINVAL;
        goto EXIT;
    }

    err = __amp_alloc_conn(&conn);
    if (err < 0) {
        AMP_ERROR("amp_create_connection: cannot alloc connection\n");
        goto EXIT;
    }

    conn->ac_ctxt = ctxt;   
    conn->ac_remote_ipaddr = addr;
    conn->ac_remote_port = port;
    conn->ac_queue_cb = queue_req;
    conn->ac_freepage_cb = freepages;
    conn->ac_allocpage_cb = allocpages;
    

    switch (direction)  {
        case AMP_CONN_DIRECTION_LISTEN:
            AMP_DMSG("amp_create_connection: create listen connection\n");
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = htonl(addr);
            sin.sin_port = htons(port);         
            err = AMP_OP(conn_type, proto_connect)(NULL, (void **)&fd, (void *)&sin, direction);
            
            if (err < 0)  {
                __amp_free_conn(conn);
                goto EXIT;
            }

            err = AMP_OP(conn_type, proto_init)((void *)fd, direction);
            if (err < 0) {
                AMP_ERROR("amp_create_connection: proto init error\n");
                __amp_free_conn(conn);
                goto EXIT;
            }
            conn->ac_sock = *fd;
            if(fd)
                free(fd);
            conn->ac_type = conn_type;
            __amp_add_to_listen_fdset(conn);
            pthread_mutex_lock(&ctxt->acc_lock);
            amp_listen_sockfd = conn->ac_sock;
            pthread_mutex_unlock(&ctxt->acc_lock);

            conn->ac_this_port = port;//modify by weizheng, 20170302
            ctxt->acc_listen_conn = conn;
            conn->ac_this_type = ctxt->acc_this_type;
            conn->ac_this_id = ctxt->acc_this_id;
            
            break;

        case AMP_CONN_DIRECTION_CONNECT:
            if (!ctxt->acc_conns[remote_type].acc_remote_conns) {
                AMP_ERROR("amp_create_connection: no acc_remote_conns for type:%d\n", remote_type);
                err = -EINVAL;
                goto EXIT;
            }
            
            if (remote_type > AMP_MAX_COMP_TYPE) {
                AMP_ERROR("amp_create_connection: wrong remote_type:%d\n", remote_type);
                err = -EINVAL;
                goto EXIT;
            }
            
            conn->ac_type = conn_type;  
            conn->ac_remote_comptype = remote_type;
            conn->ac_remote_id = remote_id;
            conn->ac_need_reconn = 1; //by weizheng 2013-11-19 reconn 
            conn->ac_remain_times = AMP_CONN_RECONN_MAXTIMES;
            conn->ac_this_type = ctxt->acc_this_type;
            conn->ac_this_id = ctxt->acc_this_id;

#ifdef __AMP_CONNS_DUPLEX
            conn->ac_duplex = 0;
#endif 
            err = __amp_connect_server (conn);
            if (err < 0) {
                __amp_free_conn(conn);
                goto EXIT;
            }
            AMP_DMSG("__amp_connect_server: remote %d-%d:%d, sock: %d\n", conn->ac_remote_comptype, conn->ac_remote_id, conn->ac_remote_port, conn->ac_sock);
            conn->ac_state = AMP_CONN_OK;
            __amp_enqueue_conn(conn, ctxt);
#ifndef __AMP_RDMA__
            __amp_add_to_listen_fdset(conn);
#endif

#ifdef __AMP_CONNS_DUPLEX
            err = __amp_alloc_conn(&conn_r);
            if (err < 0) {
                AMP_ERROR("amp_create_connection: cannot alloc connection\n");
                goto EXIT;
            }


            conn_r->ac_type = conn_type;  
            conn_r->ac_remote_comptype = remote_type;
            conn_r->ac_remote_id = remote_id;
            conn_r->ac_need_reconn = 1; //by weizheng 2013-11-19 reconn 
            conn_r->ac_remain_times = AMP_CONN_RECONN_MAXTIMES;
            conn_r->ac_this_type = ctxt->acc_this_type;
            conn_r->ac_this_id = ctxt->acc_this_id;
            conn_r->ac_ctxt = ctxt;   
            conn_r->ac_remote_ipaddr = addr;
            conn_r->ac_remote_port = port;
            conn_r->ac_queue_cb = queue_req;
            conn_r->ac_freepage_cb = freepages;
            conn_r->ac_allocpage_cb = allocpages;
    


            conn_r->ac_duplex = 1;
            err = __amp_connect_server(conn_r);
	        if(err < 0){
	            __amp_free_conn(conn_r);
               goto EXIT;
           }
           conn_r->ac_state = AMP_CONN_OK;
           __amp_enqueue_recv_conn(conn_r, ctxt);
#ifndef __AMP_RDMA__
           __amp_add_to_listen_fdset(conn_r);
#endif
#endif

           //__amp_conn_exchange(conn);

            break;
        
        default:
            AMP_ERROR("amp_create_connection: wrong connection type: %d\n", conn_type);
            __amp_free_conn(conn);
            err = -EINVAL;
            goto EXIT;
    }
    
    AMP_LEAVE("__amp_create_connection: created conn:%p\n", conn);  

    
EXIT:
    AMP_LEAVE("amp_create_connection: leave\n");
    return err; 

}
    
int 
amp_create_connection (amp_comp_context_t *ctxt,
                       amp_u32_t remote_type,
                       amp_u32_t remote_id,
                       amp_u32_t addr,
                       amp_u32_t port,
                       amp_u32_t conn_type,
                       amp_u32_t direction,
                       int (*queue_req) (amp_request_t *req),
                       int (*allocpages) (void *, amp_u32_t *, amp_kiov_t **),
                       void (*freepages)(amp_u32_t , amp_kiov_t **))
{
    int i = 0;

    for(i = 0; i < AMP_CONN_NUM; i++){
        __amp_create_connection (ctxt, remote_type, remote_id, addr, port, conn_type, direction, queue_req, allocpages, freepages);
	if(direction == AMP_CONN_DIRECTION_LISTEN){
		break;
	}
    }
    return 0;
}

int amp_send_sync (amp_comp_context_t *ctxt,
                   amp_request_t *req,
                   amp_u32_t  type,
                   amp_u32_t id,
                   amp_s32_t resent)
{
    amp_s32_t  err = 0;
    amp_u32_t   pid;
    amp_u32_t   req_type = 0;
    amp_u32_t   conn_type = 0;
    amp_u32_t   flags = 0;
    amp_u32_t   need_ack = 0;
    amp_message_t  *msgp = NULL;
    amp_u64_t   xid;
    amp_time_t  tm;
    struct timeval  tv, tv1, tv2, tv3, tv4, tv5;
    struct sockaddr_in  sout_addr;
    //amp_u32_t   sendsize = 0;
    amp_connection_t *conn = NULL;

    AMP_DMSG("amp_send_sync: enter\n");
    if (!req) {
        AMP_ERROR("amp_send_sync: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_send_sync: wrong type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("amp_send_sync: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_send_sync: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("amp_send_sync: no acc_remote_conns in this context\n");
        err = -EINVAL;
        goto EXIT;
    }

#if 0
#ifndef __AMP_SOCKET_POOL 
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    if (list_empty(&(ctxt->acc_conns[type].acc_remote_conns[id].queue))) {
        AMP_ERROR("amp_send_sync: conn[%d][%d] the remote_conns is empty\n", type, id);
        err = -ENOTCONN;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        goto EXIT;
    }
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_INIT;
    req->req_remote_type = type;
    req->req_remote_id = id;
    req->req_ctxt = ctxt;
    req->req_error = 0; 
    if (resent)
        req->req_resent = 1;
    else 
        req->req_resent = 0;
    amp_unlock(&req->req_lock);

    if (req->req_type & AMP_REQUEST) {
        AMP_DMSG("amp_send_sync: it's a request\n");
        if (!req->req_msg) {
            AMP_ERROR("amp_send_sync: no msg for a request\n");
            err = -EINVAL;
            goto EXIT;
        }

        //if we need to jugge the header of amp_message_t, in order to judge the request is resend or not
        //2014-5-14
        msgp = req->req_msg;
        xid = ctxt->acc_this_id;
	gettimeofday(&tv, NULL);
        tm.sec = tv.tv_sec;
        tm.usec = tv.tv_usec;
        pid = ctxt->acc_this_type;
        if(req->req_msglen >= 1024){
		AMP_ERROR("send sync too large msg , should use iov !!\n");
		exit(1);
	}

        AMP_FILL_REQ_HEADER(msgp, \
                                    req->req_msglen - AMP_MESSAGE_HEADER_LEN, \
                                    req->req_type, \
                                    pid, \
                                    req, \
                                    xid, \
                                    tm,  \
                                    NULL);
	AMP_DMSG("amp send request , req_msglen %d, AMP_MESSAGE_HEADER_LEN %d, amh_size %d\n", req->req_msglen , AMP_MESSAGE_HEADER_LEN, msgp->amh_size);
	msgp->amh_send_ts.sec = tv.tv_sec;
	msgp->amh_send_ts.usec = tv.tv_usec;
	AMP_DMSG("amp_send_sync: send time: %llu-%llu\n", msgp->amh_send_ts.sec, msgp->amh_send_ts.usec);
        amp_gettimeofday(&tv1);
	//TODO
	amp_lock(&seq_lock);
	seq_num ++;
	msgp->qp_num = seq_num;
	amp_unlock(&seq_lock);
    } else {
        if (!req->req_reply) {
            AMP_ERROR("amp_send_sync: no req_reply for a reply, req %p , type %x\n", req, req->req_type);
            err = -EINVAL;
            goto EXIT;
        }
	amp_gettimeofday(&tv1);
	req->req_reply->amh_send_ts.sec = tv1.tv_sec;
	req->req_reply->amh_send_ts.usec = tv1.tv_usec;
        req->req_reply->amh_size = req->req_replylen - AMP_MESSAGE_HEADER_LEN;
        msgp = req->req_reply;
        msgp->amh_type = req->req_type;
    }

    AMP_DMSG("amp_send_sync: req:%p, req->req_msg:%p, req->req_reply:%p\n", \
                 req, req->req_msg, req->req_reply);

    req_type = req->req_type;

    if ((req_type != (AMP_REQUEST | AMP_MSG))  &&
            (req_type != (AMP_REQUEST | AMP_DATA)) &&
            (req_type != (AMP_REPLY | AMP_MSG)) &&
            (req_type != (AMP_REPLY | AMP_DATA))) {
        AMP_ERROR("__amp_send_sync: wrong msg type: 0x%x\n", req_type);
        req->req_error = -EINVAL;
        err = -EINVAL;
        goto EXIT;
    }
    need_ack = req->req_need_ack;


    /*if (req_type & AMP_REQUEST) {
        sendsize = req->req_msglen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov); 
    } else {
        sendsize = req->req_replylen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov);
    }*/

int create_num = 0;
 amp_s8_t  *addrp = NULL;
 struct in_addr inaddr;
 amp_u32_t addr;
 amp_u32_t port;

SELECT_CONN:
    if((req_type & AMP_REPLY) && NULL != req->req_conn && req->req_conn->ac_state == AMP_CONN_OK && req->req_conn->ac_sock > 0){
        conn = req->req_conn;
        amp_lock(&conn->ac_lock);
        conn->ac_stage = AMP_CONN_SELECTED;
        //conn->ac_weight += sendsize;
        conn->ac_last_reconn = time(NULL);
    }else{
        err = __amp_select_conn(req->req_remote_type, req->req_remote_id, req->req_ctxt, &conn);
        if (err) {
            switch(err)  {
                case 2:
                    AMP_ERROR("amp_send_sync: no valid conn to peer (type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
                
                    if (req->req_resent && (req->req_type & AMP_REQUEST)) {
                        sleep(2);
                        __amp_add_resend_req(req);
                        if (need_ack)
                        {
                            amp_sem_down(&req->req_waitsem);
                            AMP_DMSG("amp_send_sync: after down waitsem\n");
                            err = req->req_error;
                        }else{
                            err = 0;
                            amp_sem_up(&req->req_waitsem);
                        }

                        goto EXIT;
                    }
                    if(req->req_type & AMP_REPLY){
                        err = -ENETUNREACH;
                        req->req_error = err;
                        amp_sem_up(&req->req_waitsem);
                        goto EXIT;
                    }
                case 1:
                default:
                    AMP_ERROR("amp_send_sync: no conn to peer(type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
		    
                    {
			    char * str1 = NULL;
			    AMP_ERROR("dump for debug\n");
		            *str1 = 0x01;
		    }

                    req->req_error = -ENOTCONN;
                    err = -ENOTCONN;
                    amp_sem_up(&req->req_waitsem);
                    goto EXIT;
            }
        }    
    }
    switch(conn->ac_state) {
        case AMP_CONN_BAD:
        case AMP_CONN_NOTINIT:
        case AMP_CONN_CLOSE:
        case AMP_CONN_RECOVER:
            conn->ac_refcont --;
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);
            AMP_WARNING("amp_send_sync: conn:%p is not valid currently\n", conn);
            goto SELECT_CONN;
        default:
            break;
    }
        amp_gettimeofday(&tv2);
#if 0
    if (req->req_type & AMP_REQUEST) {
        AMP_ERROR("amp_send_sync: it's a request, seqno: %d, conn:%p\n", req->req_msg->qp_num, conn);
    }else{
        AMP_ERROR("amp_send_sync: it's a reply, seqno: %d, conn:%p\n", req->req_reply->qp_num,conn);
    }
#endif
    conn_type = conn->ac_type;
    if ((!AMP_HAS_TYPE(conn_type)) || 
            (!AMP_OP(conn_type, proto_sendmsg)) || 
            (!AMP_OP(conn_type, proto_senddata)))  {
        AMP_WARNING("amp_send_sync: conn:%p, has no operations\n", conn);
        req->req_error = -ENOSYS;
        err = -ENOSYS;
        conn->ac_refcont --;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        goto EXIT;
    }   
    
    //conn->ac_weight += sendsize;
    //amp_unlock(&conn->ac_lock); //20160420

    amp_sem_down(&conn->ac_sendsem);

    if (conn->ac_state != AMP_CONN_OK) {
        AMP_WARNING("amp_send_sync: before send, state of conn:%p is invalid:%d\n", conn, conn->ac_state);
        //amp_lock(&conn->ac_lock);
        //conn->ac_weight -= sendsize;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        //amp_unlock(&conn->ac_lock);
        amp_sem_up(&conn->ac_sendsem);
        goto SEND_ERROR;
    }

    //amp_lock(&conn->ac_lock);/20160420
    /*lock request*/
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_START;
    req->req_conn = conn;
    req->req_stage = AMP_REQ_STAGE_MSG;
    if (req_type & AMP_DATA)
        flags = MSG_MORE;
    else
        flags = 0;

    sout_addr = conn->ac_remote_addr;
    /*
     * We must add the req to waiting reply list before 
     * really sending the msg, for in smp architecture,
     * it will cause the queuing operation postponed after
     * we have received the reply from peer.
     */

#ifdef __AMP_RDMA__
#if 0
        msgp = req->req_msg;
        msgp->rdma_addr = (uint64_t)conn->ac_rdma_recv_buf + 1024 * 1024; 
        msgp->rdma_rkey =  conn->ac_rdma_recv_mr->rkey;
        msgp->qp_num = conn->ac_rdma_local_config.qp_num;
        msgp->psn = conn->ac_rdma_local_config.psn;
        msgp->lid = conn->ac_rdma_local_config.lid;
        memcpy(msgp->gid, conn->ac_ctxt->acc_rdma_gid.raw, 16);
#endif
#endif
#if 0
        amp_gettimeofday(&tv2);
        while(1){
            err = AMP_OP(conn_type, proto_recvmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
            if(err == 1){
                break;
            }
        }
        amp_gettimeofday(&tv3);
        //AMP_ERROR("rdma_read used for confirm, used %llu us...\n", tv3.tv_usec - tv2.tv_usec + (tv3.tv_sec - tv2.tv_sec) * 1000000);
#endif

	if(conn->remote_msg_start_idx >= 512 && conn->next_bitmap_start > 0){
        	while(0 == (err = __amp_rdma_recvsb_half(conn, conn->remote_msg_end_idx / 8, (1016 - conn->remote_msg_end_idx) / 8)));
    	}

    	if(conn->remote_msg_start_idx >=  conn->remote_msg_end_idx){
        	while(0 == (err = __amp_rdma_recvsb(conn)));
    	}
	

	if (req_type & AMP_DATA)  {
		amp_kiov_t * iov = req->req_iov; 

        	/*stage2: send data*/
        	AMP_DMSG("amp_send_sync: conn:%p, send data\n", conn);
        	req->req_stage = AMP_REQ_STAGE_DATA;
        	//err = AMP_OP(conn_type, proto_senddata)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        	if(req_type & AMP_REQUEST){
		   
                    //AMP_ERROR("amp_send_sync data: conn:%p, send request, , xid %llx, \n", conn, req->req_msg->amh_xid);
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        	}else if(req_type & AMP_REPLY){
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        	}
        	if (err < 0) {
        	    AMP_ERROR("amp_send_sync: senddata error, conn:%p <%d, %d>, req:%p <%d, %d>, err:%d\n",conn, conn->ac_remote_comptype, conn->ac_remote_id, req, req->req_remote_type, req->req_remote_id, err);
        	    conn->ac_stage = AMP_CONN_NOT_SELECTED;
        	    amp_unlock(&req->req_lock);
        	    //amp_unlock(&conn->ac_lock);
        	    goto SEND_ERROR;
        	}
    	}
        amp_gettimeofday(&tv4);

    if (req_type & AMP_REQUEST) {
	if(req_type & AMP_DATA){
		req->req_msg->data_pos = conn->remote_data_start_pos;
		req->req_msg->data_len = err;
		conn->remote_data_start_pos += err;
		if(conn->remote_data_start_pos % 4096 > 0){
			conn->remote_data_start_pos= (conn->remote_data_start_pos + 4096)/4096*4096;
		}
	}
	 //mayl
       //AMP_ERROR("amp_send_sync: conn:%p, send request, , xid %llx, \n", conn, req->req_msg->amh_xid);
       //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
    } else {
        AMP_DMSG("amp_send_sync: conn:%p, send reply\n", conn);
        if (conn->ac_type == AMP_CONN_TYPE_UDP)
            sout_addr = req->req_reply->amh_addr;
	if(req_type & AMP_DATA){
		req->req_reply->data_pos = conn->remote_data_start_pos;
		req->req_reply->data_len = err;
		conn->remote_data_start_pos += err;
		if(conn->remote_data_start_pos % 4096 > 0){
			conn->remote_data_start_pos= (conn->remote_data_start_pos + 4096)/4096*4096;
		}

 	}
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        if (0 != err)
            AMP_ERROR("amp_send_sync: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
   
    }
     
    if (err < 0) {
        AMP_ERROR("amp_send_sync: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&req->req_lock);
        //amp_unlock(&conn->ac_lock);
        goto SEND_ERROR;
    }

    //AMP_ERROR("amp_send_sync: TIME statistic: %lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n", tv.tv_sec, tv.tv_usec, tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv3.tv_sec, tv3.tv_usec, tv4.tv_sec, tv4.tv_usec);


#if 0
    if (req_type & AMP_DATA)  {

       amp_kiov_t * iov = req->req_iov; 
        int i = 0;
        //AMP_ERROR("-------- %d %d %d %d %d %d %d %d %d %d %d\n", iov[i].ak_addr[0], iov[i].ak_addr[1], iov[i].ak_addr[2], iov[i].ak_addr[3],iov[i].ak_addr[4],iov[i].ak_addr[5],iov[i].ak_addr[6],iov[i].ak_addr[7], iov[i].ak_addr[8],iov[i].ak_addr[9], iov[i].ak_addr[10]);

        /*stage2: send data*/
        AMP_DMSG("amp_send_sync: conn:%p, send data\n", conn);
        req->req_stage = AMP_REQ_STAGE_DATA;
        //err = AMP_OP(conn_type, proto_senddata)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        if(req_type & AMP_REQUEST){
            err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }else if(req_type & AMP_REPLY){
            err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }
        if (err < 0) {
            AMP_ERROR("amp_send_sync: senddata error, conn:%p <%d, %d>, req:%p <%d, %d>, err:%d\n",conn, conn->ac_remote_comptype, conn->ac_remote_id, req, req->req_remote_type, req->req_remote_id, err);
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&req->req_lock);
            //amp_unlock(&conn->ac_lock);
            goto SEND_ERROR;
        }
    }
#endif
    amp_gettimeofday(&tv5);

    //AMP_ERROR("rdma_read:  %llu us, rdma_sendmsg: %llu us, rdma_senddata: %llu us...\n", tv3.tv_usec - tv2.tv_usec + (tv3.tv_sec - tv2.tv_sec) * 1000000, tv4.tv_usec - tv3.tv_usec + (tv4.tv_sec - tv3.tv_sec) * 1000000, tv5.tv_usec - tv4.tv_usec + (tv5.tv_sec - tv4.tv_sec) * 1000000);
    
    //req->req_send_state = AMP_REQ_SEND_END;
    amp_unlock(&req->req_lock);
    if(!need_ack)
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
    conn->ac_refcont --;
    amp_unlock(&conn->ac_lock);
#if 0
#ifdef __AMP_RECONFIRM_MSG
    if(need_ack){
        amp_htb_entry_t *htbentry = NULL;
        struct list_head *pos = NULL;
        struct list_head *nxt = NULL;
        amp_reconfirm_msg_t *reconf = NULL;
        amp_u32_t hashvalue = 0;
        amp_s32_t flag = 0;

        hashvalue = __amp_reconfirm_hash_c(req->req_reply->amh_sender_handle);
        htbentry = amp_reconfirm_hash_table_c + hashvalue;
        amp_lock(&htbentry->lock);
        list_for_each_safe(pos,nxt,&htbentry->queue){
            reconf = list_entry(pos, amp_reconfirm_msg_t, reconf_list);
            if(req->req_msg->amh_sender_handle == reconf->reconf_sender_handle &&
                    req->req_msg->amh_xid == reconf->reconf_xid &&
                    req->req_msg->amh_pid == reconf->reconf_pid){
                reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
                reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
                flag = 1;
                break;
            }
        }
        if(!flag){
            reconf = (amp_reconfirm_msg_t *)malloc(sizeof(amp_reconfirm_msg_t));
            if(NULL == reconf){
                AMP_ERROR("amp_send_sync malloc for amp_reconfig_msg_t failed\n");
                amp_unlock(&htbentry->lock);
                goto NEED_ACK;
            }
            memset(reconf, 0, sizeof(amp_reconfirm_msg_t));
            reconf->reconf_pid = req->req_msg->amh_pid;
            reconf->reconf_xid = req->req_msg->amh_xid;
            reconf->reconf_sender_handle = req->req_msg->amh_sender_handle;
            reconf->reconf_callback_handle = req->req_msg->amh_callback_handle;
            reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
            reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
            reconf->reconf_reply_msg = NULL;
            amp_lock_init(&reconf->reconf_lock);
            amp_lock(&reconf->reconf_lock);
            list_add(&reconf->reconf_list, &htbentry->queue);
            amp_unlock(&reconf->reconf_lock);
        }
        amp_unlock(&htbentry->lock);
    }
#endif
#endif

    AMP_DMSG("amp_send_sync complete ......\n");
    if (need_ack) { /*waiting for ack*/
        AMP_LEAVE("amp_send_sync: req:%p, need ack\n", req);
        if((amp_u64_t)req > amp_malloc_addr_max){
            amp_malloc_addr_max = (amp_u64_t)req;
        }
        if((amp_u64_t)req < amp_malloc_addr_min){
            amp_malloc_addr_min = (amp_u64_t)req;
        }
        
        if((amp_u64_t)req->req_msg > msg_malloc_addr_max){
            msg_malloc_addr_max = (amp_u64_t)req->req_msg;
        }
        if((amp_u64_t)req->req_msg < msg_malloc_addr_min){
            msg_malloc_addr_min = (amp_u64_t)req->req_msg;
        }


        amp_lock(&amp_waiting_reply_list_lock);
        //amp_lock(&req->req_lock);
        if(!__amp_within_waiting_reply_list(req))
            if (list_empty(&req->req_list)){
                list_add_tail(&req->req_list, &amp_waiting_reply_list);
            }
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
        //__amp_add_to_listen_fdset(conn);
    }else{
#if 0
 #ifdef __AMP_SOCKET_POOL
        AMP_ERROR("amp_send_sync: socket_pool reback conn: %p ......\n", conn);
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    }
    
    //__amp_add_to_listen_fdset(conn);
   
    if(req_type & AMP_REPLY)
    AMP_DMSG("amp_send_sync: req: %p, conn: %p, handle: %llu, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg->amh_sender_handle, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
else if(req_type & AMP_REQUEST)
    AMP_DMSG("amp_send_sync: req: %p, conn: %p, msg: %p, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
    
    //amp_lock(&conn->ac_lock);
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock);
    amp_sem_up(&conn->ac_sendsem);
    if (need_ack)
    {
        amp_sem_down(&req->req_waitsem);
        AMP_DMSG("amp_send_sync: after down waitsem\n");
        err = req->req_error;
    	//amp_sem_up(&conn->ac_sendsem);
    }else{
        err = 0;
    	//amp_sem_up(&conn->ac_sendsem);
        amp_sem_up(&req->req_waitsem);
    }

EXIT:   
    AMP_LEAVE("amp_send_sync: leave, err:%d\n", err);
    return err;

SEND_ERROR:
    AMP_ERROR("amp_send_sync: send error through conn:%p, sock: %d, err:%d\n", conn, conn->ac_sock, err);
    //amp_lock(&conn->ac_lock);    
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock); 
    amp_sem_up(&conn->ac_sendsem);

    /*if (conn->ac_type == AMP_CONN_TYPE_TCP)  {
        amp_lock(&conn->ac_lock);
        conn->ac_datard_count = 0;
        conn->ac_sched = 0;     
        amp_unlock(&conn->ac_lock);
    }*/
#if 0
#ifdef __AMP_SOCKET_POOL
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    if(!__amp_conn_test(conn)){
        goto SELECT_CONN;
    }

    conn->ac_refcont --;
    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
    conn->ac_state = AMP_CONN_RECOVER;
    amp_unlock(&conn->ac_lock);

    if (conn->ac_need_reconn) {
        /*
         * we must add the bad connection to the reconnect list.
         */
        if (conn->ac_state != AMP_CONN_RECOVER) {
            AMP_ERROR("amp_send_sync: set conn:%p to recover\n", conn);
            conn->ac_state = AMP_CONN_RECOVER;  
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_sync: someone else has set conn:%p to recover\n", conn);
            
    } else if (conn->ac_type == AMP_CONN_TYPE_TCP)   { 
        /*
         * Maybe it's in server side or it's realy need to be released, so we free it.
         */
        if (conn->ac_state != AMP_CONN_CLOSE) {
            AMP_ERROR("amp_send_sync: set conn:%p to close\n", conn);
            conn->ac_state = AMP_CONN_CLOSE;
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_sync: someone else has set conn:%p to close\n", conn);
        err = -ENETUNREACH;
        req->req_error = -ENETUNREACH;
        goto EXIT; 
    }
    goto SELECT_CONN;
}

int amp_send_async (amp_comp_context_t *ctxt,
                   amp_request_t *req,
                   amp_u32_t  type,
                   amp_u32_t id,
                   amp_s32_t resent)
{
    amp_s32_t  err = 0;
    amp_u32_t   pid;
    amp_u32_t   req_type = 0;
    amp_u32_t   conn_type = 0;
    amp_u32_t   flags = 0;
    amp_u32_t   need_ack = 0;
    amp_message_t  *msgp = NULL;
    amp_u64_t   xid;
    amp_time_t  tm;
    struct timeval  tv;
    struct sockaddr_in  sout_addr;
    //amp_u32_t   sendsize = 0;
    amp_connection_t *conn = NULL;

    AMP_DMSG("amp_send_async: enter\n");
    if (!req) {
        AMP_ERROR("amp_send_async: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_send_async: wrong type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("amp_send_async: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_send_async: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("amp_send_async: no acc_remote_conns in this context\n");
        err = -EINVAL;
        goto EXIT;
    }

#if 0
#ifndef __AMP_SOCKET_POOL 
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    if (list_empty(&(ctxt->acc_conns[type].acc_remote_conns[id].queue))) {
        AMP_ERROR("amp_send_sync: conn[%d][%d] the remote_conns is empty\n", type, id);
        err = -ENOTCONN;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        goto EXIT;
    }
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_INIT;
    req->req_remote_type = type;
    req->req_remote_id = id;
    req->req_ctxt = ctxt;
    req->req_error = 0; 
    if (resent)
        req->req_resent = 1;
    else 
        req->req_resent = 0;
    amp_unlock(&req->req_lock);

    if (req->req_type & AMP_REQUEST) {
        AMP_DMSG("amp_send_async: it's a request\n");
        if (!req->req_msg) {
            AMP_ERROR("amp_send_async: no msg for a request\n");
            err = -EINVAL;
            goto EXIT;
        }

        //if we need to jugge the header of amp_message_t, in order to judge the request is resend or not
        //2014-5-14
        msgp = req->req_msg;
        xid = ctxt->acc_this_id;
        amp_gettimeofday(&tv);
        tm.sec = tv.tv_sec;
        tm.usec = tv.tv_usec;
        pid = ctxt->acc_this_type;
	if(req->req_msglen >= 1024){
		AMP_ERROR("too large msg , should use iov !!\n");
		exit(1);
	}
        AMP_FILL_REQ_HEADER(msgp, \
                                    req->req_msglen - AMP_MESSAGE_HEADER_LEN, \
                                    req->req_type, \
                                    pid, \
                                    req, \
                                    xid, \
                                    tm,  \
                                    NULL);
    } else {
        AMP_DMSG("amp_send_async: it's a reply\n");
        if (!req->req_reply) {
            AMP_ERROR("amp_send_async: no req_reply for a reply\n");
            err = -EINVAL;
            goto EXIT;
        }
        req->req_reply->amh_size = req->req_replylen - AMP_MESSAGE_HEADER_LEN;
        msgp = req->req_reply;
        msgp->amh_type = req->req_type;
    }

    AMP_DMSG("amp_send_async: req:%p, req->req_msg:%p, req->req_reply:%p\n", \
                 req, req->req_msg, req->req_reply);

    req_type = req->req_type;

    if ((req_type != (AMP_REQUEST | AMP_MSG))  &&
            (req_type != (AMP_REQUEST | AMP_DATA)) &&
            (req_type != (AMP_REPLY | AMP_MSG)) &&
            (req_type != (AMP_REPLY | AMP_DATA))) {
        AMP_ERROR("__amp_send_async: wrong msg type: 0x%x\n", req_type);
        req->req_error = -EINVAL;
        err = -EINVAL;
        goto EXIT;
    }
    need_ack = req->req_need_ack;

#ifdef __TOKEN1__
while(1){
    amp_lock(&ctxt->acc_token_lock);
    if(ctxt->acc_token_num < ctxt->acc_token_total_num){
        ctxt->acc_token_num++;
        amp_unlock(&ctxt->acc_token_lock);
        break;
    }
    amp_unlock(&ctxt->acc_token_lock);
    usleep(500);
}
#endif


    /*if (req_type & AMP_REQUEST) {
        sendsize = req->req_msglen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov); 
    } else {
        sendsize = req->req_replylen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov);
    }*/
SELECT_CONN:
    if((req_type & AMP_REPLY) && NULL != req->req_conn && req->req_conn->ac_state == AMP_CONN_OK){
        conn = req->req_conn;
        amp_lock(&conn->ac_lock);
        conn->ac_stage = AMP_CONN_SELECTED;
        //conn->ac_weight += sendsize;
        conn->ac_last_reconn = time(NULL);
    }else{
        err = __amp_select_conn(req->req_remote_type, req->req_remote_id, req->req_ctxt, &conn);
        if (err) {
            switch(err)  {
                case 2:
                    AMP_ERROR("amp_send_async: no valid conn to peer (type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
                
                    if (req->req_resent && (req->req_type & AMP_REQUEST)) {
                        sleep(2);
                        __amp_add_resend_req(req);
                        if (need_ack)
                        {
                            amp_sem_down(&req->req_waitsem);
                            AMP_DMSG("amp_send_async: after down waitsem\n");
                            err = req->req_error;
                        }else{
                            err = 0;
                            amp_sem_up(&req->req_waitsem);
                        }

                        goto EXIT;
                    }
                    if(req->req_type & AMP_REPLY){
                        err = -ENETUNREACH;
                        req->req_error = err;
                        amp_sem_up(&req->req_waitsem);
                        goto EXIT;
                    }
                case 1:
                default:
                    AMP_ERROR("amp_send_async: no conn to peer(type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
                    req->req_error = -ENOTCONN;
                    err = -ENOTCONN;
                    amp_sem_up(&req->req_waitsem);
                    goto EXIT;
            }
        }    
    }
    switch(conn->ac_state) {
        case AMP_CONN_BAD:
        case AMP_CONN_NOTINIT:
        case AMP_CONN_CLOSE:
        case AMP_CONN_RECOVER:
            conn->ac_refcont --;
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);
            AMP_WARNING("amp_send_async: conn:%p is not valid currently\n", conn);
            goto SELECT_CONN;
        default:
            break;
    }

    conn_type = conn->ac_type;
    if ((!AMP_HAS_TYPE(conn_type)) || 
            (!AMP_OP(conn_type, proto_sendmsg)) || 
            (!AMP_OP(conn_type, proto_senddata)))  {
        AMP_WARNING("amp_send_async: conn:%p, has no operations\n", conn);
        req->req_error = -ENOSYS;
        err = -ENOSYS;
        conn->ac_refcont --;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        goto EXIT;
    }   
    
    //conn->ac_weight += sendsize;
    //amp_unlock(&conn->ac_lock); //20160420

    amp_sem_down(&conn->ac_sendsem);

    if (conn->ac_state != AMP_CONN_OK) {
        AMP_WARNING("amp_send_async: before send, state of conn:%p is invalid:%d\n", conn, conn->ac_state);
        //amp_lock(&conn->ac_lock);
        //conn->ac_weight -= sendsize;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_sem_up(&conn->ac_sendsem);
        goto SEND_ERROR;
    }

    //amp_lock(&conn->ac_lock);/20160420
    /*lock request*/
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_START;
    req->req_conn = conn;
    req->req_stage = AMP_REQ_STAGE_MSG;
    if (req_type & AMP_DATA)
        flags = MSG_MORE;
    else
        flags = 0;


    sout_addr = conn->ac_remote_addr;
    /*
     * We must add the req to waiting reply list before 
     * really sending the msg, for in smp architecture,
     * it will cause the queuing operation postponed after
     * we have received the reply from peer.
     */


    // TODO  changd by mayl , should set start pos send data first, 
    //
    if(conn->remote_msg_start_idx >= 512 && conn->next_bitmap_start > 0){
        	while(0 == (err = __amp_rdma_recvsb_half(conn, conn->remote_msg_end_idx / 8, (1016 - conn->remote_msg_end_idx) / 8)));
    	}

    if(conn->remote_msg_start_idx >=  conn->remote_msg_end_idx){
        	while(0 == (err = __amp_rdma_recvsb(conn)));
    	}

    if (req_type & AMP_DATA)  {
        /*stage2: send data*/
        AMP_DMSG("amp_send_async: conn:%p, send data\n", conn);
        req->req_stage = AMP_REQ_STAGE_DATA;
        //err = AMP_OP(conn_type, proto_senddata)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        //err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
	if(req_type & AMP_REQUEST){
				    
                    //AMP_ERROR("amp_send_async data: conn:%p, send request, , xid %llx, \n", conn, req->req_msg->amh_xid);
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }else if(req_type & AMP_REPLY){
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }


        if (err < 0) {
            AMP_ERROR("amp_send_async: senddata error, conn:%p, req:%p, err:%d\n",conn, req, err);
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&req->req_lock);
            amp_unlock(&conn->ac_lock);
            goto SEND_ERROR;
        }
    }

    // TODO change by mayl , modify start pos before write msg
    // ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //
    //
    if (req_type & AMP_REQUEST) {
	if(req_type & AMP_DATA){
		req->req_msg->data_pos = conn->remote_data_start_pos;
		req->req_msg->data_len = err;
		conn->remote_data_start_pos += err;
		if(conn->remote_data_start_pos % 4096 > 0){
			conn->remote_data_start_pos= (conn->remote_data_start_pos + 4096)/4096*4096;
		}
	}
	// mayl
        AMP_ERROR("amp_send_async: conn:%p, send request, amh_size %d\n", conn, req->req_msg->amh_size);
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
    } else {
        AMP_DMSG("amp_send_async: conn:%p, send reply\n", conn);
        if (conn->ac_type == AMP_CONN_TYPE_UDP)
            sout_addr = req->req_reply->amh_addr;
	if(req_type & AMP_DATA){
		req->req_reply->data_pos = conn->remote_data_start_pos;
		req->req_reply->data_len = err;
		conn->remote_data_start_pos += err;
		if(conn->remote_data_start_pos % 4096 > 0){
			conn->remote_data_start_pos= (conn->remote_data_start_pos + 4096)/4096*4096;
		}

 	}
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        if (0 != err)
            AMP_ERROR("amp_send_async: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
   
    }

#if 0
    // ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (req_type & AMP_REQUEST) {

        AMP_DMSG("amp_send_async: conn:%p, send request\n", conn);
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
    } else {
        AMP_DMSG("amp_send_async: conn:%p, send reply\n", conn);
        if (conn->ac_type == AMP_CONN_TYPE_UDP)
            sout_addr = req->req_reply->amh_addr;

        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        if (0 != err)
            AMP_ERROR("amp_send_async: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
   
    }

    ///////////////////////////////////////////////////////////////////////
#endif
    if (err < 0) {
        AMP_ERROR("amp_send_async: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&req->req_lock);
        amp_unlock(&conn->ac_lock);
        goto SEND_ERROR;
    }



        
    //req->req_send_state = AMP_REQ_SEND_END;
    amp_unlock(&req->req_lock);
    if(!need_ack)
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
    conn->ac_refcont --;
    amp_unlock(&conn->ac_lock);
#if 0
#ifdef __AMP_RECONFIRM_MSG
    if(need_ack){
        amp_htb_entry_t *htbentry = NULL;
        struct list_head *pos = NULL;
        struct list_head *nxt = NULL;
        amp_reconfirm_msg_t *reconf = NULL;
        amp_u32_t hashvalue = 0;
        amp_s32_t flag = 0;

        hashvalue = __amp_reconfirm_hash_c(req->req_reply->amh_sender_handle);
        htbentry = amp_reconfirm_hash_table_c + hashvalue;
        amp_lock(&htbentry->lock);
        list_for_each_safe(pos,nxt,&htbentry->queue){
            reconf = list_entry(pos, amp_reconfirm_msg_t, reconf_list);
            if(req->req_msg->amh_sender_handle == reconf->reconf_sender_handle &&
                    req->req_msg->amh_xid == reconf->reconf_xid &&
                    req->req_msg->amh_pid == reconf->reconf_pid){
                reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
                reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
                flag = 1;
                break;
            }
        }
        if(!flag){
            reconf = (amp_reconfirm_msg_t *)malloc(sizeof(amp_reconfirm_msg_t));
            if(NULL == reconf){
                AMP_ERROR("amp_send_sync malloc for amp_reconfig_msg_t failed\n");
                amp_unlock(&htbentry->lock);
                goto NEED_ACK;
            }
            memset(reconf, 0, sizeof(amp_reconfirm_msg_t));
            reconf->reconf_pid = req->req_msg->amh_pid;
            reconf->reconf_xid = req->req_msg->amh_xid;
            reconf->reconf_sender_handle = req->req_msg->amh_sender_handle;
            reconf->reconf_callback_handle = req->req_msg->amh_callback_handle;
            reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
            reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
            reconf->reconf_reply_msg = NULL;
            amp_lock_init(&reconf->reconf_lock);
            amp_lock(&reconf->reconf_lock);
            list_add(&reconf->reconf_list, &htbentry->queue);
            amp_unlock(&reconf->reconf_lock);
        }
        amp_unlock(&htbentry->lock);
    }
#endif
#endif

    AMP_DMSG("amp_send_async complete ......\n");
    if (need_ack) { /*waiting for ack*/
        AMP_LEAVE("amp_send_async: req:%p, need ack\n", req);
        if((amp_u64_t)req > amp_malloc_addr_max){
            amp_malloc_addr_max = (amp_u64_t)req;
        }
        if((amp_u64_t)req < amp_malloc_addr_min){
            amp_malloc_addr_min = (amp_u64_t)req;
        }
        
        if((amp_u64_t)req->req_msg > msg_malloc_addr_max){
            msg_malloc_addr_max = (amp_u64_t)req->req_msg;
        }
        if((amp_u64_t)req->req_msg < msg_malloc_addr_min){
            msg_malloc_addr_min = (amp_u64_t)req->req_msg;
        }


        amp_lock(&amp_waiting_reply_list_lock);
        //amp_lock(&req->req_lock);
        if(!__amp_within_waiting_reply_list(req))
            if (list_empty(&req->req_list)){
                list_add_tail(&req->req_list, &amp_waiting_reply_list);
            }
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
        //__amp_add_to_listen_fdset(conn);
    }else{
#if 0
 #ifdef __AMP_SOCKET_POOL
        AMP_ERROR("amp_send_sync: socket_pool reback conn: %p ......\n", conn);
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    }
    
    //__amp_add_to_listen_fdset(conn);
   
    if(req_type & AMP_REPLY)
    AMP_DMSG("amp_send_async: req: %p, conn: %p, handle: %llu, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg->amh_sender_handle, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
else if(req_type & AMP_REQUEST)
    AMP_DMSG("amp_send_async: req: %p, conn: %p, msg: %p, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
    
    //amp_lock(&conn->ac_lock);
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock);
    amp_sem_up(&conn->ac_sendsem);
    if (need_ack)
    {
        err = 0;
        //amp_sem_down(&req->req_waitsem);
        //AMP_DMSG("amp_send_sync: after down waitsem\n");
        //err = req->req_error;
    }else{
        err = 0;
        amp_sem_up(&req->req_waitsem);
    }

EXIT:   
    AMP_LEAVE("amp_send_async: leave, err:%d\n", err);
    return err;

SEND_ERROR:
    AMP_ERROR("amp_send_async: send error through conn:%p, sock: %d, err:%d\n", conn, conn->ac_sock, err);
    //amp_lock(&conn->ac_lock);    
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock); 
    amp_sem_up(&conn->ac_sendsem);

    /*if (conn->ac_type == AMP_CONN_TYPE_TCP)  {
        amp_lock(&conn->ac_lock);
        conn->ac_datard_count = 0;
        conn->ac_sched = 0;     
        amp_unlock(&conn->ac_lock);
    }*/
#if 0
#ifdef __AMP_SOCKET_POOL
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    if(!__amp_conn_test(conn)){
        goto SELECT_CONN;
    }

    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
    if (conn->ac_need_reconn) {
        /*
         * we must add the bad connection to the reconnect list.
         */
        if (conn->ac_state != AMP_CONN_RECOVER) {
            AMP_ERROR("amp_send_async: set conn:%p to recover\n", conn);
            conn->ac_state = AMP_CONN_RECOVER;  
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_async: someone else has set conn:%p to recover\n", conn);
            
    } else if (conn->ac_type == AMP_CONN_TYPE_TCP)   { 
        /*
         * Maybe it's in server side or it's realy need to be released, so we free it.
         */
        if (conn->ac_state != AMP_CONN_CLOSE) {
            AMP_ERROR("amp_send_async: set conn:%p to close\n", conn);
            conn->ac_state = AMP_CONN_CLOSE;
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_async: someone else has set conn:%p to close\n", conn);
        err = -ENETUNREACH;
        req->req_error = -ENETUNREACH;
        goto EXIT; 
    }
    goto SELECT_CONN;
}
int amp_send_async_err (amp_comp_context_t *ctxt,
                   amp_request_t *req,
                   amp_u32_t  type,
                   amp_u32_t id,
                   amp_s32_t resent)
{
    amp_s32_t  err = 0;
    amp_u32_t   pid;
    amp_u32_t   req_type = 0;
    amp_u32_t   conn_type = 0;
    amp_u32_t   flags = 0;
    amp_u32_t   need_ack = 0;
    amp_message_t  *msgp = NULL;
    amp_u64_t   xid;
    amp_time_t  tm;
    struct timeval  tv;
    struct sockaddr_in  sout_addr;
    //amp_u32_t   sendsize = 0;
    amp_connection_t *conn = NULL;

    AMP_DMSG("amp_send_async: enter\n");
    if (!req) {
        AMP_ERROR("amp_send_async: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_send_async: wrong type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("amp_send_async: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_send_async: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("amp_send_async: no acc_remote_conns in this context\n");
        err = -EINVAL;
        goto EXIT;
    }

#if 0
#ifndef __AMP_SOCKET_POOL 
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    if (list_empty(&(ctxt->acc_conns[type].acc_remote_conns[id].queue))) {
        AMP_ERROR("amp_send_sync: conn[%d][%d] the remote_conns is empty\n", type, id);
        err = -ENOTCONN;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        goto EXIT;
    }
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_INIT;
    req->req_remote_type = type;
    req->req_remote_id = id;
    req->req_ctxt = ctxt;
    req->req_error = 0; 
    if (resent)
        req->req_resent = 1;
    else 
        req->req_resent = 0;
    amp_unlock(&req->req_lock);

    if (req->req_type & AMP_REQUEST) {
        AMP_DMSG("amp_send_async: it's a request\n");
        if (!req->req_msg) {
            AMP_ERROR("amp_send_async: no msg for a request\n");
            err = -EINVAL;
            goto EXIT;
        }

        //if we need to jugge the header of amp_message_t, in order to judge the request is resend or not
        //2014-5-14
        msgp = req->req_msg;
        xid = ctxt->acc_this_id;
        amp_gettimeofday(&tv);
        tm.sec = tv.tv_sec;
        tm.usec = tv.tv_usec;
        pid = ctxt->acc_this_type;
	if(req->req_msglen >= 1024){
		AMP_ERROR("too large msg , should use iov !!\n");
		exit(1);
	}
        AMP_FILL_REQ_HEADER(msgp, \
                                    req->req_msglen - AMP_MESSAGE_HEADER_LEN, \
                                    req->req_type, \
                                    pid, \
                                    req, \
                                    xid, \
                                    tm,  \
                                    NULL);
    } else {
        AMP_DMSG("amp_send_async: it's a reply\n");
        if (!req->req_reply) {
            AMP_ERROR("amp_send_async: no req_reply for a reply\n");
            err = -EINVAL;
            goto EXIT;
        }
        req->req_reply->amh_size = req->req_replylen - AMP_MESSAGE_HEADER_LEN;
        msgp = req->req_reply;
        msgp->amh_type = req->req_type;
    }

    AMP_DMSG("amp_send_async: req:%p, req->req_msg:%p, req->req_reply:%p\n", \
                 req, req->req_msg, req->req_reply);

    req_type = req->req_type;

    if ((req_type != (AMP_REQUEST | AMP_MSG))  &&
            (req_type != (AMP_REQUEST | AMP_DATA)) &&
            (req_type != (AMP_REPLY | AMP_MSG)) &&
            (req_type != (AMP_REPLY | AMP_DATA))) {
        AMP_ERROR("__amp_send_async: wrong msg type: 0x%x\n", req_type);
        req->req_error = -EINVAL;
        err = -EINVAL;
        goto EXIT;
    }
    need_ack = req->req_need_ack;

#ifdef __TOKEN1__
while(1){
    amp_lock(&ctxt->acc_token_lock);
    if(ctxt->acc_token_num < ctxt->acc_token_total_num){
        ctxt->acc_token_num++;
        amp_unlock(&ctxt->acc_token_lock);
        break;
    }
    amp_unlock(&ctxt->acc_token_lock);
    usleep(500);
}
#endif


    /*if (req_type & AMP_REQUEST) {
        sendsize = req->req_msglen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov); 
    } else {
        sendsize = req->req_replylen;
        if (req_type & AMP_DATA)
            sendsize = sendsize + (4096 * req->req_niov);
    }*/
SELECT_CONN:
    if((req_type & AMP_REPLY) && NULL != req->req_conn && req->req_conn->ac_state == AMP_CONN_OK){
        conn = req->req_conn;
        amp_lock(&conn->ac_lock);
        conn->ac_stage = AMP_CONN_SELECTED;
        //conn->ac_weight += sendsize;
        conn->ac_last_reconn = time(NULL);
    }else{
        err = __amp_select_conn(req->req_remote_type, req->req_remote_id, req->req_ctxt, &conn);
        if (err) {
            switch(err)  {
                case 2:
                    AMP_ERROR("amp_send_async: no valid conn to peer (type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
                
                    if (req->req_resent && (req->req_type & AMP_REQUEST)) {
                        sleep(2);
                        __amp_add_resend_req(req);
                        if (need_ack)
                        {
                            amp_sem_down(&req->req_waitsem);
                            AMP_DMSG("amp_send_async: after down waitsem\n");
                            err = req->req_error;
                        }else{
                            err = 0;
                            amp_sem_up(&req->req_waitsem);
                        }

                        goto EXIT;
                    }
                    if(req->req_type & AMP_REPLY){
                        err = -ENETUNREACH;
                        req->req_error = err;
                        amp_sem_up(&req->req_waitsem);
                        goto EXIT;
                    }
                case 1:
                default:
                    AMP_ERROR("amp_send_async: no conn to peer(type:%d, id:%d, err:%d)\n", \
                                              req->req_remote_type, \
                                              req->req_remote_id, \
                                              err);
                    req->req_error = -ENOTCONN;
                    err = -ENOTCONN;
                    amp_sem_up(&req->req_waitsem);
                    goto EXIT;
            }
        }    
    }
    switch(conn->ac_state) {
        case AMP_CONN_BAD:
        case AMP_CONN_NOTINIT:
        case AMP_CONN_CLOSE:
        case AMP_CONN_RECOVER:
            conn->ac_refcont --;
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);
            AMP_WARNING("amp_send_async: conn:%p is not valid currently\n", conn);
            goto SELECT_CONN;
        default:
            break;
    }

    conn_type = conn->ac_type;
    if ((!AMP_HAS_TYPE(conn_type)) || 
            (!AMP_OP(conn_type, proto_sendmsg)) || 
            (!AMP_OP(conn_type, proto_senddata)))  {
        AMP_WARNING("amp_send_async: conn:%p, has no operations\n", conn);
        req->req_error = -ENOSYS;
        err = -ENOSYS;
        conn->ac_refcont --;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        goto EXIT;
    }   
    
    //conn->ac_weight += sendsize;
    //amp_unlock(&conn->ac_lock); //20160420

    amp_sem_down(&conn->ac_sendsem);

    if (conn->ac_state != AMP_CONN_OK) {
        AMP_WARNING("amp_send_async: before send, state of conn:%p is invalid:%d\n", conn, conn->ac_state);
        //amp_lock(&conn->ac_lock);
        //conn->ac_weight -= sendsize;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_sem_up(&conn->ac_sendsem);
        goto SEND_ERROR;
    }

    //amp_lock(&conn->ac_lock);/20160420
    /*lock request*/
    amp_lock(&req->req_lock);
    //req->req_send_state = AMP_REQ_SEND_START;
    req->req_conn = conn;
    req->req_stage = AMP_REQ_STAGE_MSG;
    if (req_type & AMP_DATA)
        flags = MSG_MORE;
    else
        flags = 0;


    sout_addr = conn->ac_remote_addr;
    /*
     * We must add the req to waiting reply list before 
     * really sending the msg, for in smp architecture,
     * it will cause the queuing operation postponed after
     * we have received the reply from peer.
     */
    if (req_type & AMP_REQUEST) {

        AMP_DMSG("amp_send_async: conn:%p, send request\n", conn);
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
    } else {
        AMP_DMSG("amp_send_async: conn:%p, send reply\n", conn);
        if (conn->ac_type == AMP_CONN_TYPE_UDP)
            sout_addr = req->req_reply->amh_addr;

        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_replylen, req->req_reply, flags);
        if (0 != err)
            AMP_ERROR("amp_send_async: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
   
    }
     
    if (err < 0) {
        AMP_ERROR("amp_send_async: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&req->req_lock);
        amp_unlock(&conn->ac_lock);
        goto SEND_ERROR;
    }



    if (req_type & AMP_DATA)  {
        /*stage2: send data*/
        AMP_DMSG("amp_send_async: conn:%p, send data\n", conn);
        req->req_stage = AMP_REQ_STAGE_DATA;
        //err = AMP_OP(conn_type, proto_senddata)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        err = AMP_OP(conn_type, proto_senddata)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);

        if (err < 0) {
            AMP_ERROR("amp_send_async: senddata error, conn:%p, req:%p, err:%d\n",conn, req, err);
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&req->req_lock);
            amp_unlock(&conn->ac_lock);
            goto SEND_ERROR;
        }
    }
    
    //req->req_send_state = AMP_REQ_SEND_END;
    amp_unlock(&req->req_lock);
    if(!need_ack)
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
    conn->ac_refcont --;
    amp_unlock(&conn->ac_lock);
#if 0
#ifdef __AMP_RECONFIRM_MSG
    if(need_ack){
        amp_htb_entry_t *htbentry = NULL;
        struct list_head *pos = NULL;
        struct list_head *nxt = NULL;
        amp_reconfirm_msg_t *reconf = NULL;
        amp_u32_t hashvalue = 0;
        amp_s32_t flag = 0;

        hashvalue = __amp_reconfirm_hash_c(req->req_reply->amh_sender_handle);
        htbentry = amp_reconfirm_hash_table_c + hashvalue;
        amp_lock(&htbentry->lock);
        list_for_each_safe(pos,nxt,&htbentry->queue){
            reconf = list_entry(pos, amp_reconfirm_msg_t, reconf_list);
            if(req->req_msg->amh_sender_handle == reconf->reconf_sender_handle &&
                    req->req_msg->amh_xid == reconf->reconf_xid &&
                    req->req_msg->amh_pid == reconf->reconf_pid){
                reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
                reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
                flag = 1;
                break;
            }
        }
        if(!flag){
            reconf = (amp_reconfirm_msg_t *)malloc(sizeof(amp_reconfirm_msg_t));
            if(NULL == reconf){
                AMP_ERROR("amp_send_sync malloc for amp_reconfig_msg_t failed\n");
                amp_unlock(&htbentry->lock);
                goto NEED_ACK;
            }
            memset(reconf, 0, sizeof(amp_reconfirm_msg_t));
            reconf->reconf_pid = req->req_msg->amh_pid;
            reconf->reconf_xid = req->req_msg->amh_xid;
            reconf->reconf_sender_handle = req->req_msg->amh_sender_handle;
            reconf->reconf_callback_handle = req->req_msg->amh_callback_handle;
            reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
            reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
            reconf->reconf_reply_msg = NULL;
            amp_lock_init(&reconf->reconf_lock);
            amp_lock(&reconf->reconf_lock);
            list_add(&reconf->reconf_list, &htbentry->queue);
            amp_unlock(&reconf->reconf_lock);
        }
        amp_unlock(&htbentry->lock);
    }
#endif
#endif

    AMP_DMSG("amp_send_async complete ......\n");
    if (need_ack) { /*waiting for ack*/
        AMP_LEAVE("amp_send_async: req:%p, need ack\n", req);
        if((amp_u64_t)req > amp_malloc_addr_max){
            amp_malloc_addr_max = (amp_u64_t)req;
        }
        if((amp_u64_t)req < amp_malloc_addr_min){
            amp_malloc_addr_min = (amp_u64_t)req;
        }
        
        if((amp_u64_t)req->req_msg > msg_malloc_addr_max){
            msg_malloc_addr_max = (amp_u64_t)req->req_msg;
        }
        if((amp_u64_t)req->req_msg < msg_malloc_addr_min){
            msg_malloc_addr_min = (amp_u64_t)req->req_msg;
        }


        amp_lock(&amp_waiting_reply_list_lock);
        //amp_lock(&req->req_lock);
        if(!__amp_within_waiting_reply_list(req))
            if (list_empty(&req->req_list)){
                list_add_tail(&req->req_list, &amp_waiting_reply_list);
            }
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
        //__amp_add_to_listen_fdset(conn);
    }else{
#if 0
 #ifdef __AMP_SOCKET_POOL
        AMP_ERROR("amp_send_sync: socket_pool reback conn: %p ......\n", conn);
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    }
    
    //__amp_add_to_listen_fdset(conn);
   
    if(req_type & AMP_REPLY)
    AMP_DMSG("amp_send_async: req: %p, conn: %p, handle: %llu, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg->amh_sender_handle, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
else if(req_type & AMP_REQUEST)
    AMP_DMSG("amp_send_async: req: %p, conn: %p, msg: %p, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
    
    //amp_lock(&conn->ac_lock);
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock);
    amp_sem_up(&conn->ac_sendsem);
    if (need_ack)
    {
        err = 0;
        //amp_sem_down(&req->req_waitsem);
        //AMP_DMSG("amp_send_sync: after down waitsem\n");
        //err = req->req_error;
    }else{
        err = 0;
        amp_sem_up(&req->req_waitsem);
    }

EXIT:   
    AMP_LEAVE("amp_send_async: leave, err:%d\n", err);
    return err;

SEND_ERROR:
    AMP_ERROR("amp_send_async: send error through conn:%p, sock: %d, err:%d\n", conn, conn->ac_sock, err);
    //amp_lock(&conn->ac_lock);    
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock); 
    amp_sem_up(&conn->ac_sendsem);

    /*if (conn->ac_type == AMP_CONN_TYPE_TCP)  {
        amp_lock(&conn->ac_lock);
        conn->ac_datard_count = 0;
        conn->ac_sched = 0;     
        amp_unlock(&conn->ac_lock);
    }*/
#if 0
#ifdef __AMP_SOCKET_POOL
        pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    if(!__amp_conn_test(conn)){
        goto SELECT_CONN;
    }

    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
    if (conn->ac_need_reconn) {
        /*
         * we must add the bad connection to the reconnect list.
         */
        if (conn->ac_state != AMP_CONN_RECOVER) {
            AMP_ERROR("amp_send_async: set conn:%p to recover\n", conn);
            conn->ac_state = AMP_CONN_RECOVER;  
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_async: someone else has set conn:%p to recover\n", conn);
            
    } else if (conn->ac_type == AMP_CONN_TYPE_TCP)   { 
        /*
         * Maybe it's in server side or it's realy need to be released, so we free it.
         */
        if (conn->ac_state != AMP_CONN_CLOSE) {
            AMP_ERROR("amp_send_async: set conn:%p to close\n", conn);
            conn->ac_state = AMP_CONN_CLOSE;
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
        } else 
            AMP_ERROR("amp_send_async: someone else has set conn:%p to close\n", conn);
        err = -ENETUNREACH;
        req->req_error = -ENETUNREACH;
        goto EXIT; 
    }
    goto SELECT_CONN;
}


int amp_send_async1 (amp_comp_context_t *ctxt,
                    amp_request_t *req,
                    amp_u32_t  type,
                    amp_u32_t id,
                    amp_s32_t resent)
{
    amp_s32_t  err = 0;
    amp_u32_t pid;
    amp_message_t *msgp = NULL;
    amp_u64_t xid;
    amp_time_t tm;
    struct timeval tv;
    
    AMP_ENTER("amp_send_async: enter\n");

    if (!req) {

        AMP_ERROR("amp_send_async: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_send_async: wrong type:%d\n", type);
        err = -EINVAL;
        goto EXIT;

    }

    if (!ctxt) {
        AMP_ERROR("amp_send_async: no context\n");
        err = -EINVAL;
        goto EXIT;
    }


    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_send_async: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("amp_send_async: no acc_remote_conns in this context\n");
        err = -EINVAL;
        goto EXIT;
    }
#if 0
#ifndef __AMP_SOCKET_POOL
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    if (list_empty(&(ctxt->acc_conns[type].acc_remote_conns[id].queue))) {
        AMP_ERROR("amp_send_async: the remote_conns is empty\n");
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        err = -ENOTCONN;
        goto EXIT;
    }
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    req->req_remote_type = type;
    req->req_remote_id = id;
    req->req_ctxt = ctxt;
    req->req_error = 0;
    if (resent)
        req->req_resent = 1;
    else 
        req->req_resent = 0;
    
    if (req->req_type & AMP_REQUEST) {
        msgp = req->req_msg;
        xid = ctxt->acc_this_id;
        amp_gettimeofday(&tv);
        tm.sec = tv.tv_sec;
        tm.usec = tv.tv_usec;
        pid = ctxt->acc_this_type;
        AMP_FILL_REQ_HEADER(msgp, \
                                    req->req_msglen - AMP_MESSAGE_HEADER_LEN, \
                                    req->req_type, \
                                    pid, \
                                    req, \
                                    xid, \
                                    tm,  \
                                    NULL);
    } else {
        AMP_DMSG("amp_send_async: it's a reply\n");
        if (!req->req_reply) {
            AMP_ERROR("amp_send_async: no req_reply for a reply\n");
            err = -EINVAL;
            goto EXIT;
        }
        req->req_reply->amh_size = req->req_replylen - AMP_MESSAGE_HEADER_LEN;
        msgp = req->req_reply;
        msgp->amh_type = req->req_type;
    }

#ifdef __TOKEN1__
while(1){
    amp_lock(&ctxt->acc_token_lock);
    if(ctxt->acc_token_num < ctxt->acc_token_total_num){
        ctxt->acc_token_num++;
        amp_unlock(&ctxt->acc_token_lock);
        break;
    }
    amp_unlock(&ctxt->acc_token_lock);
    usleep(500);
}
#endif


    amp_lock(&amp_sending_list_lock);
    //amp_lock(&req->req_lock);
    if(list_empty(&req->req_list))
        list_add_tail(&req->req_list, &amp_sending_list);
    else{
        list_del_init(&req->req_list);
        list_add_tail(&req->req_list, &amp_sending_list);
    }
    //amp_unlock(&req->req_lock);
    amp_unlock(&amp_sending_list_lock);
    amp_sem_up(&amp_process_out_sem);

        
EXIT:   
    AMP_LEAVE("amp_send_async: leave\n");
    return err;
}

int amp_send_async_callback (amp_comp_context_t *ctxt,
                    amp_request_t *req,
                    amp_u32_t  type,
                    amp_u32_t id,
                    amp_s32_t resent,
                    void (*callback) (amp_request_t *))
{
    amp_s32_t  err = 0;
    amp_u32_t pid;
    amp_message_t *msgp = NULL;
    amp_u64_t xid;
    amp_time_t tm;
    struct timeval tv;
    
    AMP_ENTER("amp_send_async_callback: enter\n");

    if (!req) {

        AMP_ERROR("amp_send_async_callback: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_send_async_callback: wrong type:%d\n", type);
        err = -EINVAL;
        goto EXIT;

    }

    if (!ctxt) {
        AMP_ERROR("amp_send_async_callback: no context\n");
        err = -EINVAL;
        goto EXIT;
    }


    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_send_async_callback: no acc_conns in context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("amp_send_async_callback: no acc_remote_conns in this context\n");
        err = -EINVAL;
        goto EXIT;
    }
#if 0
#ifndef __AMP_SOCKET_POOL
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    if (list_empty(&(ctxt->acc_conns[type].acc_remote_conns[id].queue))) {
        AMP_ERROR("amp_send_async_callback: the remote_conns is empty\n");
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        err = -ENOTCONN;
        goto EXIT;
    }
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    req->req_remote_type = type;
    req->req_remote_id = id;
    req->req_ctxt = ctxt;
    req->req_error = 0;
    if (resent)
        req->req_resent = 1;
    else 
        req->req_resent = 0;
    
    if (req->req_type & AMP_REQUEST) {
        msgp = req->req_msg;
        xid = ctxt->acc_this_id;
        amp_gettimeofday(&tv);
        tm.sec = tv.tv_sec;
        tm.usec = tv.tv_usec;
        pid = ctxt->acc_this_type;
        AMP_FILL_REQ_HEADER(msgp, \
                                    req->req_msglen - AMP_MESSAGE_HEADER_LEN, \
                                    req->req_type, \
                                    pid, \
                                    req, \
                                    xid, \
                                    tm,  \
                                    callback);
    } else {
        AMP_DMSG("amp_send_async_callback: it's a reply\n");
        if (!req->req_reply) {
            AMP_ERROR("amp_send_async_callback: no req_reply for a reply\n");
            err = -EINVAL;
            goto EXIT;
        }
        req->req_reply->amh_size = req->req_replylen - AMP_MESSAGE_HEADER_LEN;
        msgp = req->req_reply;
        msgp->amh_type = req->req_type;
    }

#ifdef __TOKEN1__
while(1){
    amp_lock(&ctxt->acc_token_lock);
    if(ctxt->acc_token_num < ctxt->acc_token_total_num){
        ctxt->acc_token_num++;
        amp_unlock(&ctxt->acc_token_lock);
        break;
    }
    amp_unlock(&ctxt->acc_token_lock);
    usleep(5000);
}
#endif

    amp_lock(&amp_sending_list_lock);
    //amp_lock(&req->req_lock);
    if(list_empty(&req->req_list))
        list_add_tail(&req->req_list, &amp_sending_list);
    else{
        list_del_init(&req->req_list); 
        list_add_tail(&req->req_list, &amp_sending_list);
    }
    //amp_unlock(&req->req_lock);
    amp_unlock(&amp_sending_list_lock);
    amp_sem_up(&amp_process_out_sem);

        
EXIT:   
    AMP_LEAVE("amp_send_async_callback: leave\n");
    return err;
}


// 2008-10-30
/*amp_thread_t *
__tmp (amp_comp_context_t *ctxt)
{
    amp_thread_t *threadp = NULL;

    threadp =(amp_thread_t *)amp_alloc(sizeof(amp_thread_t));
    return threadp;

}*/



// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_POLL

int amp_poll_fd_zero( struct pollfd *poll_list, amp_u32_t poll_size )
{
    amp_u32_t  i;
    amp_s32_t  rc = 0;

    for( i=0; i<poll_size; ++i )
    {
        poll_list[i].fd = -1;
        poll_list[i].revents &= (~POLLIN);
        poll_list[i].revents &= (~POLLPRI);
    }
    rc = 1;
    return rc;
}

int amp_poll_fd_set( amp_s32_t fd, struct pollfd *poll_list )
{
    amp_s32_t  rc = 1;
    AMP_DMSG("amp_poll_fd_set add fd: %d to listen set ......\n", fd);
    poll_list[fd].fd = fd;
    poll_list[fd].events = POLLIN|POLLPRI;

    return rc;
}

int amp_poll_fd_reset( amp_s32_t fd, struct pollfd *poll_list )
{
    amp_s32_t  rc = 0;
    poll_list[fd].revents &= (~POLLIN);
    poll_list[fd].revents &= (~POLLPRI);
    poll_list[fd].revents &= (~POLLOUT);
    poll_list[fd].revents &= (~POLLERR);
    poll_list[fd].revents &= (~POLLHUP);
    poll_list[fd].revents &= (~POLLNVAL);
    poll_list[fd].fd = fd;
    poll_list[fd].events = POLLIN|POLLPRI;

    rc = 1;
    return rc;
}

int amp_poll_fd_isset( amp_s32_t fd, struct pollfd *poll_list )
{
    amp_s32_t  rc = 0;

    if( poll_list[fd].fd < 0 )
    {
        rc = 0;
    }
    else if(((poll_list[fd].revents&POLLIN) == POLLIN) ||
                ((poll_list[fd].revents&POLLPRI) == POLLPRI) )
    {
        rc = 1;
    }
    return rc;
}

int amp_poll_fd_clr( amp_s32_t fd, struct pollfd *poll_list )
{
    amp_s32_t  rc = 0;
    AMP_DMSG("amp_poll_fd_clr: remove fd: poll_list[%d].fd : %d from listen set ......\n", fd, poll_list[fd].fd);
    poll_list[fd].fd = -1;
    poll_list[fd].revents &= (~POLLIN);
    poll_list[fd].revents &= (~POLLPRI);
    poll_list[fd].revents &= (~POLLOUT);
    poll_list[fd].revents &= (~POLLERR);
    poll_list[fd].revents &= (~POLLHUP);
    poll_list[fd].revents &= (~POLLNVAL);

    rc = 1;
    return rc;
}

#endif
#ifdef __AMP_LISTEN_EPOLL

int amp_epoll_fd_isset( amp_s32_t fd, struct epoll_event *ev, amp_s32_t nfds )
{
    amp_s32_t  i;
    amp_s32_t  rc = 0;

    for( i=0; i<nfds; ++i )
    {
        if( ev[i].data.fd == fd )
            return 1;
    }
    return rc;
}

int amp_epoll_fd_set( amp_s32_t fd, amp_s32_t epfd )
{
    amp_s32_t  rc = 1;
    struct epoll_event ev;

    memset(&ev, 0, sizeof(struct epoll_event));

    ev.data.fd = fd;
    ev.events = EPOLLIN|EPOLLPRI;//|EPOLLET;
    
    rc = epoll_ctl( epfd, EPOLL_CTL_ADD, fd, &ev );
    if(rc == -1)
        rc = epoll_ctl( epfd, EPOLL_CTL_MOD, fd, &ev);
    return rc;
}

int amp_epoll_fd_reset( amp_s32_t fd, amp_s32_t epfd )
{
    amp_s32_t  rc = 1;
    struct epoll_event ev;

    ev.data.fd = fd;
    ev.events = EPOLLIN|EPOLLPRI;//|EPOLLET;
    rc = epoll_ctl( epfd, EPOLL_CTL_MOD, fd, &ev);
    return rc;
}

int amp_epoll_fd_clear( amp_s32_t fd, amp_s32_t epfd )
{
    amp_s32_t  rc = 1;
    struct epoll_event ev;

    ev.data.fd = fd;
    rc = epoll_ctl( epfd, EPOLL_CTL_DEL, fd, &ev);
    return rc;
}

#endif
// -----------------------------------------------------------------


amp_comp_context_t *
amp_sys_init (amp_u32_t  type, amp_u32_t id)
{
    amp_s32_t err = 0;
    amp_comp_context_t *ctxt = NULL;
    amp_u32_t size;
    amp_u32_t i, j;
    amp_s32_t pipefd[2];
    amp_thread_t *lst_threadp = NULL;
    amp_u32_t arr_size;
    int rc = 0;
    int gidx = -1;
    

    rc = skyfs_net_get_var_conf();

#ifdef __AMP_ROCE__
    if(rc<0 || roce_v2_index <0){
	    AMP_ERROR("init RDMA dev failed \n");
	    return ctxt;

    }

#endif

amp_lock_init(&seq_lock);

#ifdef __AMP_RDMA__
    AMP_ERROR("amp_sys_init RDMA : enter, type:%d, id:%d\n", type, id);
#else
    AMP_ERROR("amp_sys_init TCP : enter, type:%d, id:%d\n", type, id);

#endif
    __amp_blockallsigs ();
    ctxt = (amp_comp_context_t *)amp_alloc(sizeof(amp_comp_context_t));
    if (!ctxt)  {
        AMP_ERROR("amp_sys_init: alloc context structure error, no mem\n");
        goto EXIT;
    }
    memset(ctxt, 0, sizeof(amp_comp_context_t));
    ctxt->acc_this_id = id;
    ctxt->acc_this_type = type;
    ctxt->acc_listen_conn = NULL;
    ctxt->acc_listen_thread = NULL;
#ifdef __TOKEN1__
    ctxt->acc_token_num = 0;
    ctxt->acc_token_total_num = 100000;
    amp_lock_init(&(ctxt->acc_token_lock));
#endif
    size = AMP_MAX_COMP_TYPE * sizeof(amp_comp_conns_t);
    ctxt->acc_conns = (amp_comp_conns_t *)amp_alloc(size);
    if (!ctxt->acc_conns)  {
        AMP_ERROR("amp_sys_init: alloc for acc_conns error\n");
        goto  ALLOC_CONNS_ERROR;
    }   
    memset(ctxt->acc_conns, 0, size);
   
    arr_size = AMP_SELECT_CONN_ARRAY_ALLOC_LEN * sizeof(amp_connection_t *);

    for (i=0; i<AMP_MAX_COMP_TYPE; i++) {
        amp_lock_init(&(ctxt->acc_conns[i].acc_lock));
        ctxt->acc_conns[i].acc_num = 0;
        ctxt->acc_conns[i].acc_remote_conns = (conn_queue_t *)amp_alloc(AMP_CONN_ADD_INCR * sizeof(conn_queue_t));
        if (!ctxt->acc_conns[i].acc_remote_conns) {
            AMP_ERROR("amp_sys_init: alloc acc_remote_conns error\n");
            goto ALLOC_REMOTE_CONNS_ERROR;
        }
        memset(ctxt->acc_conns[i].acc_remote_conns, 0, AMP_CONN_ADD_INCR * sizeof(conn_queue_t));

        for (j=0; j<AMP_CONN_ADD_INCR; j++) {
            INIT_LIST_HEAD(&(ctxt->acc_conns[i].acc_remote_conns[j].queue));
            pthread_mutex_init(&(ctxt->acc_conns[i].acc_remote_conns[j].queue_lock), NULL);
            ctxt->acc_conns[i].acc_remote_conns[j].conns = (amp_connection_t **)amp_alloc(arr_size);
            if (!ctxt->acc_conns[i].acc_remote_conns[j].conns) {
                AMP_ERROR("amp_sys_init: alloc conns for i:%d, j:%d\n", i, j);
                goto ALLOC_REMOTE_CONNS_ERROR;
            }
            memset(ctxt->acc_conns[i].acc_remote_conns[j].conns, 0, arr_size);
            ctxt->acc_conns[i].acc_remote_conns[j].total_num = AMP_SELECT_CONN_ARRAY_ALLOC_LEN;
            ctxt->acc_conns[i].acc_remote_conns[j].active_conn_num = 0;
#ifdef __AMP_SOCKET_POOL
            //INIT_LIST_HEAD(&ctxt->acc_conns[i].acc_remote_conns[j].allocd_queue);
            //ctxt->acc_conns[i].acc_remote_conns[j].allocd_num = 0;
#endif
        }
        
        ctxt->acc_conns[i].acc_alloced_num = AMP_CONN_ADD_INCR;
    }
    ctxt->acc_conn_table = (amp_connection_t **)amp_alloc(MAX_CONN_TABLE_LEN * sizeof(amp_connection_t *));
    if (!ctxt->acc_conn_table) {
        AMP_ERROR("amp_sys_init: alloc for connection table error, no mem\n");
        goto ALLOC_CONN_TABLE_ERROR;
    }
    memset(ctxt->acc_conn_table, 0, MAX_CONN_TABLE_LEN * sizeof(amp_connection_t *));

/*add by weizheng, 20170223*/
#ifdef __AMP_CONNS_DUPLEX
    /*ctxt->acc_conns_recv used to manage the conns which are accepted from the other point*/
    ctxt->acc_conns_recv = (amp_comp_conns_t *)amp_alloc(size);
    if (!ctxt->acc_conns_recv)  {
        AMP_ERROR("amp_sys_init: alloc for acc_conns_recv error\n");
        goto  ALLOC_CONNS_ERROR;
    }   
    memset(ctxt->acc_conns_recv, 0, size);

    for (i=0; i<AMP_MAX_COMP_TYPE; i++) {
        amp_lock_init(&(ctxt->acc_conns_recv[i].acc_lock));
        ctxt->acc_conns_recv[i].acc_num = 0;
        ctxt->acc_conns_recv[i].acc_remote_conns = (conn_queue_t *)amp_alloc(AMP_CONN_ADD_INCR * sizeof(conn_queue_t));
        if (!ctxt->acc_conns_recv[i].acc_remote_conns) {
            AMP_ERROR("amp_sys_init: alloc acc_conns_recv's acc_remote_conns error\n");
            goto ALLOC_REMOTE_CONNS_ERROR;
        }
        memset(ctxt->acc_conns_recv[i].acc_remote_conns, 0, AMP_CONN_ADD_INCR * sizeof(conn_queue_t));

        for (j=0; j<AMP_CONN_ADD_INCR; j++) {
            INIT_LIST_HEAD(&(ctxt->acc_conns_recv[i].acc_remote_conns[j].queue));
            pthread_mutex_init(&(ctxt->acc_conns_recv[i].acc_remote_conns[j].queue_lock), NULL);
            ctxt->acc_conns_recv[i].acc_remote_conns[j].conns = (amp_connection_t **)amp_alloc(arr_size);
            if (!ctxt->acc_conns_recv[i].acc_remote_conns[j].conns) {
                AMP_ERROR("amp_sys_init: alloc acc_conns_recv's conns for i:%d, j:%d\n", i, j);
                goto ALLOC_REMOTE_CONNS_ERROR;
            }
            memset(ctxt->acc_conns_recv[i].acc_remote_conns[j].conns, 0, arr_size);
            ctxt->acc_conns_recv[i].acc_remote_conns[j].total_num = AMP_SELECT_CONN_ARRAY_ALLOC_LEN;
            ctxt->acc_conns_recv[i].acc_remote_conns[j].active_conn_num = 0;
#ifdef __AMP_SOCKET_POOL
            //INIT_LIST_HEAD(&ctxt->acc_conns_recv[i].acc_remote_conns[j].allocd_queue);
            //ctxt->acc_conns_recv[i].acc_remote_conns[j].allocd_num = 0;
#endif
        }
        
        ctxt->acc_conns_recv[i].acc_alloced_num = AMP_CONN_ADD_INCR;
    }
#endif


// by Chen Zhuan at 2008-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT

    ctxt->acc_maxfd = 0;
    FD_ZERO(&ctxt->acc_readfds);

#endif
#ifdef __AMP_LISTEN_POLL

    ctxt->acc_maxfd = 0;
    ctxt->acc_poll_list = (struct pollfd *)amp_alloc( AMP_CONN_ADD_INCR * sizeof( struct pollfd ) );
    memset(ctxt->acc_poll_list,0,AMP_CONN_ADD_INCR * sizeof( struct pollfd ));
    amp_poll_fd_zero( ctxt->acc_poll_list, AMP_CONN_ADD_INCR );

#endif
#ifdef __AMP_LISTEN_EPOLL

    ctxt->acc_epfd = epoll_create( AMP_CONN_ADD_INCR );

#endif
// -----------------------------------------------------------------

    err = __amp_init_conn();
    if (err < 0) 
        goto EXIT;

    err = __amp_init_request();
    if (err < 0)  
        goto INIT_REQUEST_ERROR;

    amp_proto_interface_table_init ();
    err = pipe(pipefd);
    if (err < 0) {
        AMP_ERROR("amp_sys_init: create pipe error, err:%d\n", err);
        goto EXIT;
    }


// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT

    FD_SET(pipefd[0], &ctxt->acc_readfds);
    if (ctxt->acc_maxfd < pipefd[0])
        ctxt->acc_maxfd = pipefd[0];

#endif
#ifdef __AMP_LISTEN_POLL

    amp_poll_fd_set( pipefd[0], ctxt->acc_poll_list );
    if (ctxt->acc_maxfd < pipefd[0])
        ctxt->acc_maxfd = pipefd[0];

#endif
#ifdef __AMP_LISTEN_EPOLL

    amp_epoll_fd_set( pipefd[0], ctxt->acc_epfd );

#endif
// -----------------------------------------------------------------

    ctxt->acc_notifyfd = pipefd[1];
    ctxt->acc_srvfd = pipefd[0];


#ifdef __AMP_RDMA__
    ctxt->acc_rdma_device_list = ibv_get_device_list(&ctxt->acc_rdma_device_num);
    if(!ctxt->acc_rdma_device_list){
        err = -1;
        AMP_ERROR("cannot obtain infiniband NIC device list\n");        
        goto EXIT;
    }else{
        AMP_ERROR("infiniband NIC device num: %d\n", ctxt->acc_rdma_device_num);
        AMP_ERROR("infiniband NIC device name: %s\n", ibv_get_device_name(ctxt->acc_rdma_device_list[0]));
    }

    for(i = 0; i < ctxt->acc_rdma_device_num; i++){
	int found_active = 0;
        ctxt->acc_rdma_context = ibv_open_device(ctxt->acc_rdma_device_list[i]);
        if(!ctxt->acc_rdma_context){
            AMP_ERROR("cannot get context for %s\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
            err = -1;
            goto EXIT;
        }

#if 1
        struct ibv_device_attr dev_attr;

        rc = ibv_query_device(ctxt->acc_rdma_context, &dev_attr);
        if(rc){
            AMP_ERROR("Get ibv_device attr failed for %s\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
            err = -1;
            goto EXIT;
        }
        for (j = 1; j <= dev_attr.phys_port_cnt; j++){
            struct ibv_port_attr port_attr;
            rc = ibv_query_port(ctxt->acc_rdma_context, j, &port_attr);
            if(rc){
                AMP_ERROR("Get port attr failed for port %d\n", j);
                err = -1;
                goto EXIT;
            }
            if(port_attr.state == IBV_PORT_ACTIVE){
                AMP_ERROR("The device %s was opened, active\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
		found_active = 1;
		break;
            }else{
                AMP_ERROR("The device %s was opened, but not active\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
            }
        }
#endif


        AMP_ERROR("The device %s was opened\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
        //if(NULL != strstr(ibv_get_device_name(ctxt->acc_rdma_device_list[i]), "mlx4_0")){
        if(NULL == strstr(ibv_get_device_name(ctxt->acc_rdma_device_list[i]), active_rdma_dev_name)){
        // for LVSUAN
	   if(found_active)
		   found_active = 0;
        }
#if 0
        char * p = strstr(ibv_get_device_name(ctxt->acc_rdma_device_list[i]), "mlx");
        if(NULL != p && NULL != strstr(p, "_0")){
            break;
        }
#endif
	if(found_active){
		AMP_ERROR("found an active device %s, return now\n",  ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
		break;
	}

        rc = ibv_close_device(ctxt->acc_rdma_context); 
        if(rc){
            AMP_ERROR("ERROR, Failed to close device %s\n", ibv_get_device_name(ctxt->acc_rdma_device_list[i]));
            err = -1;
            goto EXIT;
        }
    }


    ctxt->acc_rdma_channel = NULL;
#ifdef __AMP_RDMA_EVENT__
    ctxt->acc_rdma_channel = ibv_create_comp_channel(ctxt->acc_rdma_context);     
    if(!ctxt->acc_rdma_channel){
        AMP_ERROR("cannot create completion channel\n");
        err = -1;
        goto EXIT;
    }
#endif
    
    ctxt->acc_rdma_pd = ibv_alloc_pd(ctxt->acc_rdma_context);
    if(!ctxt->acc_rdma_pd){
        AMP_ERROR("protect domain malloc failed\n");
        err = -1;
        goto EXIT;
    }
    
    ctxt->acc_rdma_scq = ibv_create_cq(ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, ctxt->acc_rdma_channel, 0);
    if(!ctxt->acc_rdma_scq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }
    
    ctxt->acc_rdma_rcq = ibv_create_cq(ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, ctxt->acc_rdma_channel, 0);
    if(!ctxt->acc_rdma_rcq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }

#ifdef __AMP_RDMA_EVENT__
    if(ibv_req_notify_cq(ctxt->acc_rdma_rcq, 0)){
        AMP_ERROR("cannot request CQ notification\n");
        err = -1;
        goto EXIT;
    }
#endif


    ctxt->acc_rdma_port_num = 1;
    if(ibv_query_port(ctxt->acc_rdma_context, ctxt->acc_rdma_port_num, &ctxt->acc_rdma_port_attr)){
        AMP_ERROR("cannot find the context for the coresponded port\n");
        err = -1;
        goto EXIT;
    }else{
        AMP_ERROR("local LID = 0x%x\n", ctxt->acc_rdma_port_attr.lid);
    }

    ibv_query_port(ctxt->acc_rdma_context, ctxt->acc_rdma_port_num, &ctxt->acc_rdma_port_attr);

#ifdef __AMP_ROCE__
     gidx = roce_v2_index;
#endif
    ibv_query_gid(ctxt->acc_rdma_context, ctxt->acc_rdma_port_num, gidx, &ctxt->acc_rdma_gid);

    AMP_DMSG("local address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", 0, 0, 0, ctxt->acc_rdma_gid.raw[0], ctxt->acc_rdma_gid.raw[1], ctxt->acc_rdma_gid.raw[2], ctxt->acc_rdma_gid.raw[3],ctxt->acc_rdma_gid.raw[4], ctxt->acc_rdma_gid.raw[5], ctxt->acc_rdma_gid.raw[6], ctxt->acc_rdma_gid.raw[7], ctxt->acc_rdma_gid.raw[8], ctxt->acc_rdma_gid.raw[9], ctxt->acc_rdma_gid.raw[10], ctxt->acc_rdma_gid.raw[11], ctxt->acc_rdma_gid.raw[12], ctxt->acc_rdma_gid.raw[13], ctxt->acc_rdma_gid.raw[14], ctxt->acc_rdma_gid.raw[15]);


/*
    ctxt->acc_rdma_local_config.addr = htonll((uintptr_t)ctxt->acc_rdma_buffer);
    ctxt->acc_rdma_local_config.rkey = htonll(ctxt->acc_rdma_memory_region->rkey);
    ctxt->acc_rdma_local_config.qp_num = htonll(ctxt->acc_rdma_qp->qp_num);
    ctxt->acc_rdma_local_config.lid = htons(ctxt->acc_rdma_port_attr.lid);
*/

#endif

    err = __amp_threads_init (ctxt);
    if (err < 0)
        goto INIT_THREAD_ERROR;
    
    lst_threadp = __amp_start_listen_thread(ctxt);
    if (!lst_threadp) {
        AMP_ERROR("amp_sys_init: start listen thread error\n");
        err = -1;
        goto INIT_REQUEST_ERROR;
    }
    ctxt->acc_listen_thread = lst_threadp;

    /*
     * modified by weizheng 2014-1-2, start netmorn thread, SOCKRAW is used by icmp ptotocol, not root user cannot use this function
     */
#ifdef __AMP_ICMP_NETMORN
    ctxt->acc_netmorn_thread = __amp_start_netmorn_thread(ctxt);
    if(!ctxt->acc_netmorn_thread)
    {
        AMP_ERROR("amp_sys_init: start net mornitor thread error\n");
        goto INIT_REQUEST_ERROR;
    }
#endif


 
EXIT:
    AMP_LEAVE("amp_sys_init: leave\n");
    return ctxt;

INIT_REQUEST_ERROR:
INIT_THREAD_ERROR:
ALLOC_CONN_TABLE_ERROR:
ALLOC_REMOTE_CONNS_ERROR:
    if (ctxt->acc_conns)  {
        for (i=0; i<AMP_MAX_COMP_TYPE; i++)  {
            if (ctxt->acc_conns[i].acc_remote_conns) {
                for (j=0; j<AMP_CONN_ADD_INCR; j++) {
                    if (ctxt->acc_conns[i].acc_remote_conns[j].conns)
                        amp_free(ctxt->acc_conns[i].acc_remote_conns[j].conns,arr_size);
                }
                amp_free(ctxt->acc_conns[i].acc_remote_conns, AMP_CONN_ADD_INCR * sizeof(conn_queue_t));
            }
        }
        amp_free(ctxt->acc_conns, AMP_MAX_COMP_TYPE * sizeof(amp_comp_conns_t));
    }

ALLOC_CONNS_ERROR:
    if (ctxt)
        amp_free(ctxt, sizeof(amp_comp_context_t));
    ctxt = NULL;
    goto EXIT;

}

int 
amp_disconnect_peer (amp_comp_context_t *ctxt,
                     amp_u32_t remote_type,
                     amp_u32_t remote_id,
                     amp_u32_t forall)
{
    amp_s32_t err = 0;
    amp_connection_t *conn = NULL;
    struct list_head *head = NULL;
    amp_comp_conns_t *cmp_conns = NULL;
    conn_queue_t     *cnq = NULL;
    amp_u32_t i;

    amp_s32_t   sockfd = -1;

    AMP_ENTER("amp_disconnect_peer: enter,type:%d,id:%d,forall:%d\n", \
                   remote_type, remote_id, forall);

    if (remote_type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("amp_disconnect_peer: wrong type: %d\n", \
                          remote_type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("amp_disconnect_peer: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    

    if (!ctxt->acc_conns) {
        AMP_ERROR("amp_disconnect_peer: no acc_conns in ctxt\n");
        err = 1;
        goto EXIT;
    }
    
    cmp_conns = &(ctxt->acc_conns[remote_type]);

    if (remote_id <= 0 || remote_id >= cmp_conns->acc_alloced_num) {
        AMP_ERROR("amp_disconnect_peer: wrong id:%d\n", remote_id);
        err = -EINVAL;
        goto EXIT;
    }

    if (!cmp_conns->acc_remote_conns) {
        AMP_ERROR("amp_disconnect_peer: no remote conns \n");
        err = 1;
        goto EXIT;
    }

    pthread_mutex_lock(&ctxt->acc_lock);
    for (i=0; i<cnq->active_conn_num; i++) {
        conn = cnq->conns[i];
        if (!conn)
            continue;
        if(conn->ac_sock > 0){
            ctxt->acc_conn_table[conn->ac_sock] = NULL;
        }
    }
    pthread_mutex_unlock(&ctxt->acc_lock);

    pthread_mutex_lock(&(cmp_conns->acc_remote_conns[remote_id].queue_lock));
    cnq = &(cmp_conns->acc_remote_conns[remote_id]);
    head = &(cmp_conns->acc_remote_conns[remote_id].queue); 

    if (cnq->active_conn_num <= 0) {
        AMP_ERROR("amp_disconnect_peer: type:%d, id:%d, active_conn_num:%d ,wrong\n", remote_type, remote_id, cnq->active_conn_num);
        err = 1;
        pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[remote_id].queue_lock));
        goto EXIT;
    }

    for (i=0; i<cnq->active_conn_num; i++) {
        conn = cnq->conns[i];
        if (!conn)
            continue;
        cnq->conns[i] = NULL;

        AMP_DMSG("amp_disconnect_peer: to disconnect conn:%p\n", conn);
        sockfd = conn->ac_sock;
        
        if (conn->ac_sock >= 0)
            AMP_OP(conn->ac_type, proto_disconnect)(&conn->ac_sock);
        else
            AMP_ERROR("amp_disconnect_peer: close conn:%p, no sock\n", conn);
        
        amp_lock(&conn->ac_lock);
        if(!list_empty(&conn->ac_list))
            list_del_init(&conn->ac_list);
        amp_unlock(&conn->ac_lock);

        amp_lock(&amp_reconn_conn_list_lock);
        amp_lock(&conn->ac_lock);
        if (!list_empty(&conn->ac_reconn_list) &&
                    conn->ac_state != AMP_CONN_CLOSE) {
            AMP_DMSG("amp_disconnect_peer: conn is on reconn list\n");
            list_del_init(&conn->ac_reconn_list);
        }
        conn->ac_state = AMP_CONN_BAD;
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);

        amp_sem_down(&conn->ac_recvsem);
        amp_sem_down(&conn->ac_sendsem);

        amp_lock(&conn->ac_lock);
        conn->ac_sock = -1;
        //conn->ac_weight = 0;
        amp_unlock(&conn->ac_lock);

        amp_sem_up(&conn->ac_recvsem);
        amp_sem_up(&conn->ac_sendsem);
        __amp_free_conn(conn);

        if (!forall)
            break;
    }
    pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[remote_id].queue_lock));

    if (forall) {
        AMP_DMSG("amp_disconnect_peer: to wake all resend reqs\n");
        __amp_remove_resend_reqs(conn, 1, 1);
        cnq->active_conn_num = 0;
    }

EXIT:
    AMP_LEAVE("amp_disconnect_peer: leave\n");
    return err;
}

int
amp_sys_finalize (amp_comp_context_t *cmp_ctxt)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;
    struct list_head *head = NULL;
    amp_connection_t *conn = NULL;
    amp_u32_t i, j;
    amp_u32_t arr_size;
    conn_queue_t *cnq = NULL;
    
    AMP_ENTER("amp_sys_finalize: enter\n");

    if (!cmp_ctxt)  {
        AMP_ERROR("amp_sys_finalize: no component context\n");
        goto EXIT;
    }

    /*
     * firstly stop the listen thread.
     */
    if (cmp_ctxt->acc_listen_thread)  {
        threadp = cmp_ctxt->acc_listen_thread;
        err = __amp_stop_listen_thread(cmp_ctxt);
        if (err < 0) 
            goto EXIT;
        amp_free(threadp, sizeof(amp_thread_t));
        cmp_ctxt->acc_listen_thread = NULL;
    }
    /*
     * get down the listen connection
     */
    if (cmp_ctxt->acc_listen_conn) {
        conn = cmp_ctxt->acc_listen_conn;
        AMP_OP(conn->ac_type, proto_disconnect)(&conn->ac_sock);
        __amp_free_conn(conn);
        cmp_ctxt->acc_listen_conn = NULL;
    }

#ifdef __AMP_ICMP_NETMORN
    err = __amp_stop_netmorn_thread(cmp_ctxt);
    if (err < 0)
        AMP_ERROR("amp_sys_finalize: stop netmorn thread error, err:%d\n", err);
#endif

    /*
     * get down the other connections
     */
    amp_sem_down(&amp_reconn_finalize_sem);
    if (cmp_ctxt->acc_conns)  {
        for (i=0; i<AMP_MAX_COMP_TYPE; i++) {
            amp_lock(&(cmp_ctxt->acc_conns[i].acc_lock));
            for (j=0; j<cmp_ctxt->acc_conns[i].acc_alloced_num; j++) {
                head = &(cmp_ctxt->acc_conns[i].acc_remote_conns[j].queue);
                cnq = &(cmp_ctxt->acc_conns[i].acc_remote_conns[j]);
                while (!list_empty(head))  {
                    conn = list_entry(head->next, amp_connection_t, ac_list);
                    /*
                     * we must check where dose this conn hang on before we free it.
                     */
                    AMP_OP(conn->ac_type, proto_disconnect)(&conn->ac_sock);

                    amp_lock(&amp_dataready_conn_list_lock);
                    amp_lock(&conn->ac_lock);
                    if (!list_empty(&conn->ac_dataready_list)) {
                        list_del_init(&conn->ac_dataready_list);
                    }
                    amp_unlock(&conn->ac_lock);
                    amp_unlock(&amp_dataready_conn_list_lock);

                    amp_lock(&amp_reconn_conn_list_lock);
                    amp_lock(&conn->ac_lock);
                    if (!list_empty(&conn->ac_reconn_list))  {
                        list_del_init(&conn->ac_reconn_list);
                    }
                    amp_unlock(&conn->ac_lock);
                    amp_unlock(&amp_reconn_conn_list_lock);

                    amp_lock(&conn->ac_lock);
                    if(!list_empty(&conn->ac_list))
                        list_del_init(&conn->ac_list);
                    amp_unlock(&conn->ac_lock);

                    AMP_DMSG("amp_sys_finalize: disconnect conn:%p\n", conn);
                    __amp_free_conn(conn);
                    AMP_DMSG("amp_sys_finalize: conn:%p freed\n", conn);
                }
                arr_size = cnq->total_num * sizeof(amp_connection_t *);

                if (cnq->conns)
                    amp_free(cnq->conns, arr_size);
            }
            amp_unlock(&(cmp_ctxt->acc_conns[i].acc_lock));
            amp_free(cmp_ctxt->acc_conns[i].acc_remote_conns, \
                                 sizeof(conn_queue_t) * cmp_ctxt->acc_conns[i].acc_alloced_num);
            
        }
        amp_sem_up(&amp_reconn_finalize_sem);

        amp_free(cmp_ctxt->acc_conns, sizeof(amp_comp_conns_t) * AMP_MAX_COMP_TYPE);
        cmp_ctxt->acc_conns = NULL;
    }   
    if (cmp_ctxt->acc_conn_table)
        amp_free(cmp_ctxt->acc_conn_table, MAX_CONN_TABLE_LEN * sizeof(amp_connection_t *));
    __amp_threads_finalize ();
    __amp_finalize_conn ();
    __amp_finalize_request ();
// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------

    #ifdef __amp_listen_poll
    if(cmp_ctxt->acc_poll_list){
        free(cmp_ctxt->acc_poll_list);
    }
    #endif
    #ifdef __AMP_LISTEN_EPOLL
    close( cmp_ctxt->acc_epfd );
    #endif
// -----------------------------------------------------------------
    amp_free(cmp_ctxt, sizeof(amp_comp_context_t));
    cmp_ctxt = NULL;
    
EXIT:
    AMP_LEAVE("amp_sys_finalize: leave\n");
    return err;

}

/*
int 
amp_config (amp_s32_t cmd, void *conf, amp_s32_t len)
{
    return 0;

}*/
