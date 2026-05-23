/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */
/*          Rongfeng Tang                      */
/***********************************************/

#include <amp_help.h>
#include <amp_thread.h>
#include <amp_conn.h>
#include <amp_request.h>
#include <amp_protos.h>
#include <assert.h>

amp_sem_t amp_process_in_sem;
amp_sem_t amp_process_out_sem;
amp_sem_t amp_reconn_sem;
amp_sem_t amp_netmorn_sem;
amp_sem_t amp_reconn_finalize_sem;

amp_u32_t amp_srvin_thread_num = 0;
amp_u32_t amp_srvout_thread_num = 0;
amp_u32_t amp_reconn_thread_num = 0;
amp_u32_t amp_wakeup_thread_num = 0;
amp_u32_t amp_token_generate_thread_num = 0;

amp_thread_t  *amp_srvin_threads = NULL;
amp_thread_t  *amp_srvout_threads = NULL;
amp_thread_t  *amp_reconn_threads = NULL;
amp_thread_t  *amp_token_generate_threads = NULL;
amp_thread_t  *amp_wakeup_threads = NULL;

#ifdef __AMP_RDMA__
amp_thread_t * amp_rdma_listen_threads = NULL;
amp_u32_t      amp_rdma_listen_thread_num = 0;
struct list_head *amp_rdma_listen_group = NULL;
#endif

amp_lock_t    amp_threads_lock;

amp_s32_t     amp_listen_sockfd = -1;
amp_u32_t     link_disconn_times = 0;
amp_u64_t     link_disconn_total_times = 0;

/* 
 * block signals
 */
void
__amp_blockallsigs ()
{
    sigset_t  thread_sigs;

    sigemptyset (&thread_sigs);
    sigaddset (&thread_sigs, SIGALRM);
    sigaddset (&thread_sigs, SIGTERM);
    sigaddset (&thread_sigs, SIGHUP);
    //sigaddset (&thread_sigs, SIGINT);
    sigaddset (&thread_sigs, SIGQUIT);
    sigaddset (&thread_sigs, SIGPIPE);
    pthread_sigmask (SIG_BLOCK, &thread_sigs, NULL);

    AMP_LEAVE("__amp_blockallsigs: leave\n");
}


/*
 * recv message from a connection.
 *
 * return value: 0 - normal, the msgp contain the nearly recved message.
 *                   <0 - something wrong.
 */
int 
__amp_recv_msg (amp_connection_t *conn, amp_message_t **retmsgp)
{
    amp_message_t  *msgp = NULL;
    amp_s8_t *bufp = NULL;
    amp_message_t  header;
    amp_u32_t conn_type;
    amp_s32_t err = -1;
    amp_u32_t  size;

    AMP_ENTER("__amp_recv_msg: enter, conn:%p\n", conn);

    if (!retmsgp) {
        AMP_ERROR("__amp_recv_msg: no return address\n");
        err = -EINVAL;
        goto EXIT;
    }

    conn_type = conn->ac_type;

    switch (conn_type)  {
        case AMP_CONN_TYPE_UDP:
            msgp = (amp_message_t *)amp_alloc(AMP_MAX_MSG_SIZE);
            if (!msgp) {
                AMP_ERROR("__amp_recv_msg: cannot alloc msgp\n");
                err = -ENOMEM;
                goto EXIT;
            }
            memset(msgp, 0, AMP_MAX_MSG_SIZE);

            /*
             * receive the msg
             */
            //err = AMP_OP(conn_type, proto_recvmsg)(&conn->ac_sock, &msgp->amh_addr, sizeof(msgp->amh_addr), AMP_MAX_MSG_SIZE, msgp, 0);
            err = AMP_OP(conn_type, proto_recvmsg)(conn, &msgp->amh_addr, sizeof(msgp->amh_addr), AMP_MAX_MSG_SIZE, msgp, 0);

            
            if (err < 0) {
                amp_free(msgp, AMP_MAX_MSG_SIZE);
                goto EXIT;
            }

             err = 0;            
            break;
        case AMP_CONN_TYPE_TCP:
            /*
             * firstly receive the msg header
             */

            AMP_ERROR(" TCP RDMA receive size: %d\n", sizeof(header));
            //err = AMP_OP(conn_type, proto_recvmsg)(&conn->ac_sock, NULL, 0, sizeof(header), &header, 0);
            err = AMP_OP(conn_type, proto_recvmsg)(conn, NULL, 0, sizeof(header), &header, 0);

            if (err < 0) {
                AMP_ERROR("__amp_recv_msg: receive msg header error, type: %d, id: %d, conn: %p<%d>, err:%d\n", conn->ac_remote_comptype, conn->ac_remote_id, conn, conn->ac_sock, err);
                goto EXIT;
            }

            size = header.amh_size;
            AMP_ERROR("__amp_recv_msg: header.amh_size:%d, \n", size);
            if (size == 0) 
                AMP_WARNING("__amp_recv_msg: the size of payload is zero\n");
            
            msgp = (amp_message_t *)amp_alloc(size + sizeof(header));
            if (!msgp) {
                AMP_ERROR("__amp_recv_msg: no memory\n");
                err = -ENOMEM;
                goto EXIT;
            }
            memset(msgp, 0, size + sizeof(header));
            
            if (size)   {   
                bufp = (amp_s8_t *)msgp + sizeof(header);

//RECV_BODY:
                /*
                 * receive the remain message
                 */
                //err = AMP_OP(conn_type, proto_recvmsg)(&conn->ac_sock, NULL, 0, size, bufp, 0);
                err = AMP_OP(conn_type, proto_recvmsg)(conn, NULL, 0, size, bufp, 0);
                if (err < 0) {
                    free(msgp);
                    AMP_ERROR("__amp_recv_msg: receive msg error, err:%d\n", err);
                    goto EXIT;
                }
            }

            msgp->amh_magic = header.amh_magic;
            msgp->amh_addr = header.amh_addr;
            msgp->amh_pid = header.amh_pid;
            msgp->amh_sender_handle = header.amh_sender_handle;
            //msgp->amh_message_handle = header.amh_message_handle;
            msgp->amh_callback_handle = header.amh_callback_handle;
            msgp->amh_send_ts = header.amh_send_ts;
            msgp->amh_size = header.amh_size;
            msgp->amh_type = header.amh_type;
            msgp->amh_xid = header.amh_xid;
            err = 0;
            break;
        default:
            err = -1;
            AMP_ERROR("__amp_recv_msg: wrong conn type: %d\n", conn_type);
            goto EXIT;
    }
    *retmsgp = msgp;

EXIT:
    AMP_LEAVE("__amp_recv_msg: leave\n");
    return err;
}

/*
 * general create thread function.
 */ 
int
__amp_create_thread(void* (*thrfunc)(void *), amp_thread_t *thread)
{
    amp_s32_t err = 0;

    AMP_ENTER("__amp_create_thread: enter\n");
    
    if (!thrfunc) {
        AMP_ERROR("__amp_create_thread: no thread func\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    if (!thread) {
        AMP_ERROR("__amp_create_thread: no arg\n");
        err = -EINVAL;
        goto EXIT;
    }

    err = pthread_create(&thread->at_thread_id, NULL, thrfunc, (void *)thread);
    if (err) {
        AMP_ERROR("__amp_create_thread: create thread error, err:%d\n", err);
        goto EXIT;
    }

    amp_sem_down(&thread->at_startsem);
    err = 0;
    
EXIT:
    AMP_LEAVE("__amp_create_thread: leave, err:%d\n", err);
    return err;
}

/*
 * general stop thread function
 */ 
int
__amp_stop_thread (amp_thread_t *threadp, amp_sem_t *wakeup_sem)
{
    amp_s32_t err = 0;

    AMP_ENTER("__amp_stop_thread: enter\n");
    if (!threadp) {
        AMP_ERROR("__amp_stop_thread: no arg\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!wakeup_sem) {
        AMP_ERROR("__amp_stop_thread: no wake up sem\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!threadp->at_isup) {
        AMP_DMSG("__amp_stop_thread: at_isup is zero\n");
        goto EXIT;
    }

    threadp->at_shutdown = 1;

    amp_sem_up(wakeup_sem);
    amp_sem_down(&threadp->at_downsem);

EXIT:
    AMP_ENTER("__amp_stop_thread: leave\n");
    return err;
}
    
#ifdef __AMP_RDMA__ 

static int amp_poll_completion(amp_connection_t * conn, int wrid_id){
    struct ibv_wc wc;
    int poll_result;
    unsigned long start_time_msec;
    unsigned long cur_time_msec;
    struct timeval cur_time;
    int rc = 0;

    gettimeofday(&cur_time, NULL);
    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);

    do{
        switch(wrid_id){
            case AMP_RECV_WRID:
                //poll_result = ibv_poll_cq(conn->ac_ctxt->acc_rdma_rcq, 1, &wc);
                poll_result = ibv_poll_cq(conn->ac_rdma_rcq, 1, &wc);
                break;
            case AMP_SEND_WRID:  
                //poll_result = ibv_poll_cq(conn->ac_ctxt->acc_rdma_scq, 1, &wc);
                poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
                break;
            default:
                  AMP_ERROR("wrid_id is not AMP_RECV_WRID or AMP_SEND_WRID\n");
                  break;
        }
        gettimeofday(&cur_time, NULL);
        cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
    }while(poll_result == 0 && cur_time_msec - start_time_msec < 2000);

    if(poll_result < 0){
        AMP_ERROR("poll CQ failed\n");
        rc = -1;
    }else if(poll_result == 0){
        AMP_ERROR("completion wasnot found in the CQ after timeout\n");
        rc = -1;
    }else{
        AMP_ERROR("Completion was found in CQ with status 0x%x\n", wc.status);
        
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", wc.status, wc.vendor_err);
            rc = -1;
        }
    }
    return rc;
}

#if 0
void * __amp_rdma_listen_thread (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t    seqno;
    amp_comp_context_t *ctxt = NULL;
    amp_s8_t     *recvmsg = NULL;
#ifdef __AMP_RECONFIRM_MSG
    struct list_head * pos = NULL;
    struct list_head * head = NULL;
#endif
    int use_event = 0;
    int remote_type = 0;
    int remote_id = 0;
    int msg_len = 0;
    struct ibv_wc wc[512];
    int ne, i;

    amp_connection_t * conn = NULL;

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    amp_sem_up(&threadp->at_startsem);

    AMP_ERROR("ENTER AMP_RDMA_LISTEN_THREAD\n");

    int num_cq_events = 0;
    while(1){
        if (threadp->at_shutdown) {
            AMP_ERROR("__amp_rdma_listen_thread: to exit, threadp:%p\n", threadp);
            goto EXIT;
        }

        for(i = 0; i < 1; i++){
#ifdef __AMP_RDMA_EVENT__
            struct ibv_cq *ev_cq;
            void          *ev_ctx;
            use_event = 1;

            if(ibv_get_cq_event(ctxt->acc_rdma_channel, &ev_cq, &ev_ctx)){
                AMP_ERROR("__amp_rdma_listen_thread: Failed to get cq_event\n");
                return NULL;
            }
            
            //++num_cq_events;
            
            if(ev_cq != ctxt->acc_rdma_rcq){
                AMP_ERROR("__amp_rdma_listen_thread: CQ event for unknown CQ %p\n", ev_cq); 
                return NULL;
            }

            if(ibv_req_notify_cq(ctxt->acc_rdma_rcq, 0)){
                AMP_ERROR("__amp_rdma_listen_thread: cannot request CQ notification\n");
                return NULL;
            }
            ibv_ack_cq_events(ev_cq, 1);
#endif

#if 0
            do{
                ne = ibv_poll_cq(ctxt->acc_rdma_rcq, 1, &wc[i]);
                if(ne < 0){
                    AMP_ERROR("__amp_rdma_listen_thread: Pool CQ failed %d\n", ne);
                    return NULL;
                }
                if(ne == 1){
                    ++num_cq_events;
                }
            }while(!use_event && ne < 1);
            
            AMP_ERROR("__amp_rdma_listen_thread: Pool CQ, num of CQE: %d\n", ne);
#endif
#ifdef __AMP_RDMA_EVENT__
            //ibv_ack_cq_events(ev_cq, 1);
#endif
        }
        
        do{
                ne = ibv_poll_cq(ctxt->acc_rdma_rcq, 1, wc);
                if(ne < 0){
                    AMP_ERROR("__amp_rdma_listen_thread: Pool CQ failed %d\n", ne);
                    return NULL;
                }
                if(ne == 1){
                    //++num_cq_events;
                }
            }while(!use_event && ne < 1);
            
//            AMP_ERROR("__amp_rdma_listen_thread: Pool CQ, num of CQE: %d\n", ne);



        //ne = num_cq_events;

        for(i = 0; i < ne; ++i){
            if(wc[i].status != IBV_WC_SUCCESS){
                AMP_ERROR("__amp_rdma_listen_thread: Failed status %s (%d) for wr_id %d\n", ibv_wc_status_str(wc[i].status), wc[i].status, (int)wc[i].wr_id);                
                return NULL;
            }else{
                
                amp_request_t  *req = NULL;
                amp_message_t  *msgp = NULL;
                amp_message_t  *header = NULL;

                msg_len = wc[i].byte_len;
                remote_type = wc[i].imm_data >> 24;
                remote_id = wc[i].imm_data & 0xFFFFFF;


 
                __amp_alloc_request(&req);//by weizheng 2013-11-18, alloc for request, refcount =1
                req->req_remote_id = remote_id;
                req->req_remote_type = remote_type;


                msgp = (amp_message_t *)amp_alloc(msg_len);
                req->req_msg = msgp;

                conn = ctxt->acc_conns[remote_type].acc_remote_conns[remote_id].conns[0];
#if 1
                {
                    struct ibv_recv_wr rr;
                    struct ibv_sge sge;
                    struct ibv_recv_wr *bad_wr = NULL;
   
                    memset(&sge, 0, sizeof(sge));
                    sge.addr = (uint64_t)conn->ac_rdma_recv_buf;
                    //sge.length = msg_len;
                    //sge.addr = (uint64_t)conn->ac_rdma_recv_buf;
                    sge.length = AMP_RDMA_MR_SIZE;
                    sge.lkey = conn->ac_rdma_recv_mr->lkey;

                    memset(&rr, 0, sizeof(rr));
                    rr.next = NULL;
                    rr.wr_id = 0;
                    rr.sg_list = &sge;
                    rr.num_sge = 1;
                        
                    int rc = ibv_post_recv(conn->ac_rdma_qp, &rr, &bad_wr);

                    if(rc){
                        AMP_ERROR("__amp_rdma_listen_thread: post_recv failed, rc: %d, errno: %d\n", rc, errno);
                    }else{
  //                      AMP_ERROR("__amp_rdma_listen_thread: post_recv success\n");
                    }
                }
#endif
                memcpy(msgp, conn->ac_rdma_recv_buf, msg_len);

                int size = msgp->amh_size;
                int req_type = msgp->amh_type;

                if (size == 0) {
                    AMP_WARNING("__amp_rdma_listen_thread: the size of payload is zero\n");
                }else{
    //                AMP_ERROR("__amp_rdma_listen_thread: receive byte_len : %d, immdata: %d, wr_id: %zu, type: %zu, id: %zu\n", wc[i].byte_len, wc[i].imm_data, wc[i].wr_id, wc[i].imm_data >> 24, wc[i].imm_data & 0xFFFFFF);
                
//                    AMP_ERROR("__amp_rdma_listen_thread: sizeof(header): %d, msg_len: %d, header.amh_size: %d, req_type: %d\n", sizeof(header), msg_len, size, req_type);
                
                
                }
                
                amp_u32_t niov = 0;
                amp_kiov_t *iov = NULL;
                recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);

                if(req_type & AMP_DATA){
#ifdef __AMP_RDMA_EVENT__
            struct ibv_cq *ev_cq;
            void          *ev_ctx;
            use_event = 1;

            if(ibv_get_cq_event(ctxt->acc_rdma_channel, &ev_cq, &ev_ctx)){
                AMP_ERROR("__amp_rdma_listen_thread: Failed to get cq_event\n");
                return NULL;
            }
            
            //++num_cq_events;
            
            if(ev_cq != ctxt->acc_rdma_rcq){
                AMP_ERROR("__amp_rdma_listen_thread: CQ event for unknown CQ %p\n", ev_cq); 
                return NULL;
            }

            if(ibv_req_notify_cq(ctxt->acc_rdma_rcq, 0)){
                AMP_ERROR("__amp_rdma_listen_thread: cannot request CQ notification\n");
                return NULL;
            }
            ibv_ack_cq_events(ev_cq, 1);
#endif

                    do{
                        ne = ibv_poll_cq(ctxt->acc_rdma_rcq, 1, wc);
                        if(ne < 0){
                            AMP_ERROR("__amp_rdma_listen_thread: Pool CQ failed %d\n", ne);
                            return NULL;
                        }else{
                            AMP_DMSG("__amp_rdma_listen_thread: Pool CQ success %d, byte_len: %d\n", ne, wc[i].byte_len);
                        }
                    }while(!use_event && ne < 1);
                    i = 0; 
                    if(wc[i].status != IBV_WC_SUCCESS){
                        AMP_ERROR("__amp_rdma_listen_thread: Failed status %s (%d) for wr_id %d\n", ibv_wc_status_str(wc[i].status), wc[i].status, (int)wc[i].wr_id);                
                    }

                    AMP_DMSG("__amp_rdma_listen_thread: Pool CQ2, num of CQE: %d\n", ne);

                    
                    


                    if(conn->ac_allocpage_cb){
                        conn->ac_allocpage_cb(recvmsg, &niov, &iov);
                    }

                    AMP_DMSG("niov: %d-------\n", niov);

                    AMP_DMSG("__AMP_RDMA_LISTEN_THREAD: RECV_BUF: %p, %p, %p, %p\n", conn->ac_rdma_recv_buf, conn->ac_rdma_send_buf, conn->ac_rdma_recv_buf + 1024 * 1024, conn->ac_rdma_send_buf + 1024 * 1024);

                    int pos = 0;
                    int j = 0;
                    for(j = 0; j < niov; j++){
                        AMP_DMSG("MEMCPY: PAGE:%d, niov:%d, len: %d, ak_addr:%p\n", j, niov, iov[j].ak_len, iov[j].ak_addr);
                        //memcpy(iov[i].ak_addr, conn->ac_rdma_recv_buf + 1024 * 1024 + pos, iov[i].ak_len);
                        if(req_type & AMP_REPLY){
                            memcpy(iov[j].ak_addr, conn->ac_rdma_recv_buf + 1024 * 1024 + pos +4, iov[j].ak_len);
                            //memcpy(iov[i].ak_addr, conn->ac_rdma_recv_buf + pos, iov[i].ak_len);

                            AMP_DMSG("AMP_REPLY\n");

                        }else{
                            memcpy(iov[j].ak_addr, conn->ac_rdma_recv_buf + 1024 * 1024 + pos + 4, iov[j].ak_len);
                            //memcpy(iov[i].ak_addr, conn->ac_rdma_send_buf + pos, iov[i].ak_len);
                            AMP_DMSG("AMP_REQUEST\n");
                        }
                        AMP_DMSG("-------- %d %d %d %d %d %d %d %d %d %d %d\n", iov[j].ak_addr[0], iov[j].ak_addr[1], iov[j].ak_addr[2], iov[j].ak_addr[3],iov[j].ak_addr[4],iov[j].ak_addr[5],iov[j].ak_addr[6],iov[j].ak_addr[7], iov[j].ak_addr[8],iov[j].ak_addr[9], iov[j].ak_addr[10]);
                        pos += iov[j].ak_len;
                    }

                    req->req_niov = niov;
                    req->req_iov = iov;
                    AMP_DMSG("-----niov: %d-------\n", niov);
#if 1
                {
                    struct ibv_recv_wr rr;
                    struct ibv_sge sge;
                    struct ibv_recv_wr *bad_wr = NULL;
   
                    memset(&sge, 0, sizeof(sge));
                    sge.addr = (uint64_t)conn->ac_rdma_recv_buf;
                    //sge.length = msg_len;
                    //sge.addr = (uint64_t)conn->ac_rdma_recv_buf;
                    sge.length = AMP_RDMA_MR_SIZE;
                    sge.lkey = conn->ac_rdma_recv_mr->lkey;

                    memset(&rr, 0, sizeof(rr));
                    rr.next = NULL;
                    rr.wr_id = 0;
                    rr.sg_list = &sge;
                    rr.num_sge = 1;
                        
                    int rc = ibv_post_recv(conn->ac_rdma_qp, &rr, &bad_wr);

                    if(rc){
                        AMP_ERROR("__amp_rdma_listen_thread: post_recv failed, rc: %d, errno: %d\n", rc, errno);
                    }else{
                        AMP_ERROR("__amp_rdma_listen_thread: post_recv success\n");
                    }
                }
#endif

                }

                if(req_type == (AMP_REQUEST | AMP_MSG) || req_type == (AMP_REQUEST | AMP_DATA)){
                    req->req_msg = msgp;
                    req->req_type = req_type;
                    req->req_conn = conn;
                    
                    req->req_niov = niov;
                    req->req_iov = iov;
  
                    req->req_msglen = msgp->amh_size + sizeof(amp_message_t);
                    AMP_DMSG("__amp_rdma_listen_thread: before queue request, req:%p\n", req);
                    AMP_DMSG("__amp_rdma_listen_thread: conn:%p, ac_queue_cb:%p\n", conn, conn->ac_queue_cb);

                    conn->ac_queue_cb(req);
                }else if(req_type == (AMP_REPLY | AMP_MSG) || req_type == (AMP_REPLY | AMP_DATA)){

                    amp_request_t * tmpreqp = NULL;
                    tmpreqp = (amp_request_t *)((unsigned long)msgp->amh_sender_handle);

                    __amp_free_request(req);
                    req = tmpreqp;
                    amp_lock(&amp_waiting_reply_list_lock);
                    if(__amp_within_waiting_reply_list(req)){
                        list_del_init(&req->req_list);
                    }

                    req->req_conn = conn;

                    amp_unlock(&amp_waiting_reply_list_lock);
                    
                    req->req_niov = niov;
                    req->req_iov = iov;
  
/*
                    do{
                        ne = ibv_poll_cq(ctxt->acc_rdma_rcq, 1, wc);
                        if(ne < 0){
                            AMP_ERROR("__amp_rdma_listen_thread: Pool CQ failed %d\n", ne);
                            return NULL;
                        }
                    }while(!use_event && ne < 1);


                    amp_u32_t niov = 0;
                    amp_kiov_t *iov = NULL;
                    recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);


                    if(conn->ac_allocpage_cb){
                        err = conn->ac_allocpag_cb(recvmsg, &niov, &iov);
                    }

                    int pos = 0;
                    for(i = 0; i < niov; i++){
                        memcpy(iov[i].ak_addr, conn->ac_rdma_recv_buf, iov[i].ak_len);
                        pos += iov[i].ak_len;
                    }
                    req->req_niov = niov;
                    req->req_iov = iov;
*/

                    if(req->req_need_ack){
                        req->req_reply = msgp;
                        req->req_replylen = msg_len;

                        if(NULL != (void *)msgp->amh_callback_handle){
                            void (*callback)(amp_request_t *);
                            callback = (void(*)(amp_request_t *))(msgp->amh_callback_handle);
                            (*callback)(req);
                        }else{
                            amp_sem_up(&req->req_waitsem);
                        }
                    }
                }
                

               /* 
                amp_lock(&amp_dataready_conn_list_lock);
                amp_lock(&conn->ac_lock);
                if(list_empty(&conn->ac_dataready_list))
                    list_add_tail(&conn->ac_dataready_list, &amp_dataready_conn_list);
                amp_unlock(&conn->ac_lock);
                amp_unlock(&amp_dataready_conn_list_lock);
                amp_sem_up(&amp_process_in_sem);
*/
#if 0 
                for( i=0; i<0; i++ ) {
                    fd = events[i].data.fd;
                    amp_connection_t *tmpconn = NULL;
                    if( fd < 0 )
                        continue;
                    if( fd == listen_sockfd )
                        continue;
                    if( fd == ctxt->acc_srvfd )
                        continue;

                    pthread_mutex_lock(&ctxt->acc_lock);
                    amp_epoll_fd_clear( fd, ctxt->acc_epfd );
                    tmpconn = ctxt->acc_conn_table[fd];
                    if (!tmpconn) {
                        AMP_ERROR( "__amp_listen_thread: no connection for fd:%d, i=%d\n", fd, i );
                        pthread_mutex_unlock(&ctxt->acc_lock);
                        continue;
                    }
                    pthread_mutex_unlock(&ctxt->acc_lock);
                    
                    amp_lock(&amp_dataready_conn_list_lock);
                    amp_lock(&tmpconn->ac_lock);
                    if(list_empty(&tmpconn->ac_dataready_list))
                        list_add_tail(&tmpconn->ac_dataready_list, &amp_dataready_conn_list);
                    amp_unlock(&tmpconn->ac_lock);
                    amp_unlock(&amp_dataready_conn_list_lock);
                    amp_sem_up(&amp_process_in_sem);
                }
#endif
            }
            switch((int)wc[i].wr_id){
                case AMP_SEND_WRID:
                    AMP_ERROR("__amp_rdma_listen_thread: SEND..\n");
                    break;
                case AMP_RECV_WRID:
                    //AMP_ERROR("__amp_rdma_listen_thread: RECEIVE..\n");
                    break;
                default:
                    AMP_ERROR("__amp_rdma_listen_thread: Completion for unknown wr_id %d\n", (int)wc[i].wr_id);
                    return NULL;
            }
        }
#ifdef __AMP_RDMA_EVENT__
        //ibv_ack_cq_events(ev_cq, 1);
#endif
    }

EXIT:
    amp_sem_up(&threadp->at_downsem);
    AMP_DMSG("__amp_rdma_listen_thread: leave: %ld\n",    pthread_self());
    return 0;
}
#endif
void * 
__amp_rdma_listen_thread_20211002 (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t    seqno;
    amp_comp_context_t *ctxt = NULL;
    amp_s8_t     *recvmsg = NULL;
#ifdef __AMP_RECONFIRM_MSG
    struct list_head * pos = NULL;
    struct list_head * head = NULL;
#endif
    int use_event = 0;
    int remote_type = 0;
    int remote_id = 0;
    int msg_len = 0;
    struct ibv_wc wc[512];
    int ne, i;
    int *hlen, *tlen;

    amp_connection_t * conn = NULL;

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    amp_sem_up(&threadp->at_startsem);
    conn_queue_t *cnq = NULL;

    AMP_ERROR("ENTER AMP_RDMA_LISTEN_THREAD\n");

    int num_cq_events = 0;
    int kk = 0, jj = 0;
    while(1){
        if (threadp->at_shutdown) {
            AMP_ERROR("__amp_rdma_listen_thread: to exit, threadp:%p\n", threadp);
            goto EXIT;
        }

        kk = seqno;
        //for(kk = 0; kk < AMP_COMP_MAX; kk++)
        {
        //for(kk = 0; kk < 4; kk++){
            for(jj = 0; jj < AMP_CONN_ADD_INCR; jj++){
            //for(jj = 0; jj < 4; jj++){
                cnq = &(ctxt->acc_conns[kk].acc_remote_conns[jj]);
                if(cnq->active_conn_num <= 0){
                    continue;
                }
                int ii = 0;
                for(ii = 0; ii < cnq->active_conn_num; ii++){
                    conn = cnq->conns[ii];
                    if(!conn){
                        break;
                    }
                    amp_request_t  *req = NULL;
                    amp_message_t  *msgp = NULL;
                    amp_message_t  *header = NULL;
                    int msg_len = 0;

                    int req_type = 0;
                    amp_u32_t niov = 0;
                    amp_kiov_t *iov = NULL;
                     

                    header = (amp_message_t *)conn->ac_rdma_recv_buf; 
                    int * tlen = NULL;
                    int retry_time = 0;


                    /*while(0 == header->amh_magic){
                        retry_time ++;
                        if(retry_time > 64){
                            break;
                        }
                    }

                    if(retry_time > 64){
                        continue;
                    }*/

                    while(1){
                        retry_time = 0; 
                        while(0 == header->amh_magic){
                            retry_time ++;
                            if(retry_time > 64){
                                break;
                            }
                        } 
                        
                        if(retry_time > 64){
                            continue;
                        }

                        
                        msg_len = sizeof(amp_message_t) + header->amh_size;

                        tlen = (int *)((char *)conn->ac_rdma_recv_buf + msg_len);
                        
                        retry_time = 0;

                        do{
                            if(*tlen == msg_len){
                                break;
                            }
                            retry_time ++;
                            if(retry_time > 64){
                                break;
                            }
                        }while(*tlen != msg_len);
                        
                        if(*tlen == msg_len && msg_len != 0){
                            break;
                        }
                        
                    }

                    if(*tlen != msg_len ||  msg_len == 0){
                        continue;
                    }

                    msg_len = sizeof(amp_message_t) + header->amh_size;
                    
                    msgp = (amp_message_t *)amp_alloc(msg_len);
                    memcpy(msgp, conn->ac_rdma_recv_buf, msg_len);

                    __amp_alloc_request(&req);//by weizheng 2013-11-18, alloc for request, refcount =1
                    req->req_msg = msgp;

                    req_type = msgp->amh_type;
                    
                    remote_type = msgp->amh_pid;
                    remote_id = msgp->amh_xid >> 32;

                    req->req_remote_type = remote_type;
                    req->req_remote_id = remote_id;

//AMP_ERROR("MSG_LEN: %d  %d-%d-%d  %d--%d\n", msg_len, kk, jj, ii, remote_type, remote_id);

                    recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);
                    
                    if(req_type & AMP_REPLY){
                        amp_request_t  *tmpreqp = NULL;

                        tmpreqp = (amp_request_t *)((unsigned long) msgp->amh_sender_handle);

                        __amp_free_request(req);

                        req = tmpreqp;

                        amp_lock(&amp_waiting_reply_list_lock);
                        if(__amp_within_waiting_reply_list(req)){
                            list_del_init(&req->req_list);
                        }
                        req->req_conn = conn;
                        amp_unlock(&amp_waiting_reply_list_lock);
    
                    }
        
                    if(req_type & AMP_DATA){
                        int retry_time_data = 0;
                        int t_size = 0;
                        int j = 0;
                        int pos = 4;
                        
                        if(conn->ac_allocpage_cb){
                            conn->ac_allocpage_cb(recvmsg, &niov, &iov);
                        }

                        for(j = 0; j < niov; j ++){
                            t_size += iov[j].ak_len;
                        }
                        
                        hlen = (int *)((char *)conn->ac_rdma_recv_buf + 1024 * 1024);
                        tlen = (int *)((char *)conn->ac_rdma_recv_buf + 1024 * 1024 + t_size + 4);
                       

                        while( 1 ){
                            usleep(2);
                        //while( * hlen == * tlen && 0 != *hlen){
                            if(*hlen == *tlen  && t_size == *hlen){
                                break;
                            }
                            retry_time_data ++;
                            if(retry_time_data > 64){
                                //break;
                            }
                        }

                        for(j = 0; j < niov; j++){
                            //AMP_ERROR("MEMCPY: PAGE:%d, niov:%d, len: %d, ak_addr:%p\n", j, niov, iov[j].ak_len, iov[j].ak_addr);
                            if(req_type & AMP_REPLY){
                                memcpy(iov[j].ak_addr, conn->ac_rdma_recv_buf + 1024 * 1024 + pos, iov[j].ak_len);
                            }else{
                                memcpy(iov[j].ak_addr, conn->ac_rdma_recv_buf + 1024 * 1024 + pos, iov[j].ak_len);
                            }
                            pos += iov[j].ak_len;
                        }

                        *hlen = 0;
                        *tlen = 0;

                        req->req_niov = niov;
                        req->req_iov = iov;

                    }
                    
                    //TODO
                    header->amh_magic = 0;


                    if(req_type == (AMP_REQUEST | AMP_MSG) || req_type == (AMP_REQUEST | AMP_DATA)){
                        req->req_msg = msgp;
                        req->req_type = req_type;
                        req->req_conn = conn;
                        
                        req->req_niov = niov;
                        req->req_iov = iov;
  
                        req->req_msglen = msgp->amh_size + sizeof(amp_message_t);
                        AMP_DMSG("__amp_rdma_listen_thread: before queue request, req:%p\n", req);
                        AMP_DMSG("__amp_rdma_listen_thread: conn:%p, ac_queue_cb:%p\n", conn, conn->ac_queue_cb);

                        conn->ac_queue_cb(req);
                    }else if(req_type == (AMP_REPLY | AMP_MSG) || req_type == (AMP_REPLY | AMP_DATA)){

                        
                        req->req_niov = niov;
                        req->req_iov = iov;
  

                        if(req->req_need_ack){
                            req->req_reply = msgp;
                            req->req_replylen = msg_len;

                            if(NULL != (void *)msgp->amh_callback_handle){
                                void (*callback)(amp_request_t *);
                                callback = (void(*)(amp_request_t *))(msgp->amh_callback_handle);
                                (*callback)(req);
                            }else{
                                amp_sem_up(&req->req_waitsem);
                            }
                        }
                    }
                }
            }
        }

    }

EXIT:
    amp_sem_up(&threadp->at_downsem);
    AMP_DMSG("__amp_rdma_listen_thread: leave: %ld\n",    pthread_self());
    return 0;
}

void * 
__amp_rdma_listen_thread (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t    seqno;
    amp_comp_context_t *ctxt = NULL;
    amp_s8_t     *recvmsg = NULL;
    int use_event = 0;
    int remote_type = 0;
    int remote_id = 0;
    int req_type = 0;
    int msg_len = 0;
    int retry_time = 0;
    int ne, i;
    volatile int *hlen, *tlen;
    struct list_head *pos, *n;
    amp_connection_t * conn = NULL;

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    amp_sem_up(&threadp->at_startsem);
    conn_queue_t *cnq = NULL;
    int h_ready = 0;
    int len_zero = 0;

#ifdef __CPU_QOS_FOR_LISTEN__
    int sleep_flag = 0;
    int sleep_cnt = 0;
    int sleep_time_cnt = 0;


	struct timeval tv, tv1, tv2;
	gettimeofday(&tv, NULL);
	gettimeofday(&tv1, NULL);
	
	long int __SLEEP_CNT =  1000000;
	long int __SLEEP_TIME = 100000;
	
	long int used_usec = 0;
	long int sleep_usec = 0;
	
	long int work_interval = 5 * 60 * 1000000;
	long int work_thresh = 0;
	long int work_usec = 0;
#endif

    AMP_ERROR("EnTER AMP_RDMA_LISTEN_THREAD 1\n");

    while(1){
        if (threadp->at_shutdown) {
            AMP_ERROR("__amp_rdma_listen_thread: to exit, threadp:%p\n", threadp);
            goto EXIT;
        }

        list_for_each_safe(pos, n, &amp_rdma_listen_group[seqno]){
            conn = list_entry(pos, amp_connection_t, listen_list);
        
                    amp_request_t  *req = NULL;
                    amp_message_t  *msgp = NULL;
                    amp_message_t  *header = NULL;
                    amp_u32_t niov = 0;
                    amp_kiov_t *iov = NULL;
		    volatile uint8_t *bitmap = (uint8_t *)conn->ac_rdma_recv_buf;
		    volatile uint8_t *bitmap_data = (uint8_t *)conn->ac_rdma_recv_buf + 512;

		    for(i = conn->next_idx; i < 1016; i++){
		    //for(i = next_idx; i < 1016; i++){
		        if(0 == i % 8){
				if(bitmap[i / 8] == 0xFF){
					i += 7;
					continue;
				}
			}

			h_ready ++;

#if 0
			if(h_ready %1000000 == 0){
				AMP_ERROR("hready %d, len failed %d, conn %p\n", h_ready, len_zero ,conn );
			}
#endif

		    	header = (amp_message_t *)((char *)conn->ac_rdma_recv_buf + 1024 * (i + AMP_RDMA_SB_BLK_NUM));
			if(0 == header->amh_size){
				//AMP_ERROR("msg[%d]: header->amh_size is 0, for conn: %p\n", i, conn);
				len_zero++;
				if(bitmap[i / 8] & (0x1 << (i % 8))){
					continue;
				}
				break;
			}
                    	msg_len = sizeof(amp_message_t) + header->amh_size;
                    	tlen = (int *)((char *)header + msg_len);
			if(*tlen != msg_len){
				if(bitmap[i / 8] & (0x1 << (i % 8))){
					continue;
				}
				break;

				continue;
			}
#ifdef __CPU_QOS_FOR_LISTEN__
			sleep_cnt ++;
#endif
			
			req_type = header->amh_type;
                    	*tlen = 0;
                    
			//AMP_ERROR("RDMA alloc message len %d, type %d\n", msg_len, req_type);
                    	msgp = (amp_message_t *)amp_alloc(msg_len);
                    	memcpy(msgp, (char *)conn->ac_rdma_recv_buf + 1024 * (i + AMP_RDMA_SB_BLK_NUM), msg_len);
                    	__amp_alloc_request(&req);//by weizheng 2013-11-18, alloc for request, refcount =1

                    	req->req_msg = msgp;

                    	req_type = msgp->amh_type;
                    	
                    	remote_type = msgp->amh_pid;
                    	remote_id = msgp->amh_xid >> 32;

                    	req->req_remote_type = remote_type;
                    	req->req_remote_id = remote_id;
//AMP_ERROR("MSG_LEN: %d  %d-%d-%d  %d--%d\n", msg_len, kk, jj, ii, remote_type, remote_id);
//AMP_ERROR("amp_rdma_listen_thread[%d]: int msg_addr<%llu>, header->amh_size: %d, msg_seqno: %d, req_seqno: %d from %d-%d, bitmap:%d, next_bitmap: %d\n", i, (uint64_t)conn->ac_rdma_recv_buf + 1024 * (i + 1), header->amh_size, msgp->qp_num, seqno, remote_type, remote_id, bitmap[i/8], bitmap[i/8+1]);

                   	 recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);
                   	 
                   	 if(req_type & AMP_REPLY){
                   	     amp_request_t  *tmpreqp = NULL;

                   	     tmpreqp = (amp_request_t *)((unsigned long) msgp->amh_sender_handle);

                   	     __amp_free_request(req);

                   	     req = tmpreqp;

                   	     amp_lock(&amp_waiting_reply_list_lock);
                   	     if(__amp_within_waiting_reply_list(req)){
                   	         list_del_init(&req->req_list);
                   	     }
                   	     req->req_conn = conn;
                   	     amp_unlock(&amp_waiting_reply_list_lock);
    
                   	 }
        
                   	 if(req_type & AMP_DATA){
                   	 	int t_size = msgp->data_len;
				int data_pos = msgp->data_pos;
				//AMP_ERROR("amp recv data  len %d\n", msgp->data_len);
#if 0
				req->req_mp = (char *)conn->ac_rdma_recv_buf + data_pos;
				req->req_mp_pos = data_pos;
				req->req_mp_size = t_size;
#else
                   	 	int j = 0;
                   	  	int pos = 0;
				// added by mayl, req_iov have been allocated by sender
                        	if(req->req_iov != NULL){
					iov = req->req_iov;
					niov = req->req_niov;
				}else if (conn->ac_allocpage_cb){
                        	    conn->ac_allocpage_cb(recvmsg, &niov, &iov);
                        	}
				if(iov == NULL){
					AMP_ERROR(" Alloc iov pages failed ! \n");
					exit(1);
				}


                        	for(j = 0; j < niov; j++){
                        	    memcpy(iov[j].ak_addr, (char *)conn->ac_rdma_recv_buf + data_pos + pos, iov[j].ak_len);
                        	    pos += iov[j].ak_len;
                        	}
//AMP_ERROR("AMP_RDMA_LISTEN_THREAD: alloc_time: %llu us, wait_time: %llu us, COPY DATA USE TIME: %llu us   t_size: %d, data_pos: %d\n", tv2.tv_usec - tv1.tv_usec + 1000000 * (tv2.tv_sec - tv1.tv_sec), tv3.tv_usec - tv2.tv_usec + 1000000 * (tv3.tv_sec - tv2.tv_sec), tv4.tv_usec - tv3.tv_usec + 1000000 * (tv4.tv_sec - tv3.tv_sec), t_size, data_pos);
				
#if 1
				__amp_mr_bitmap_set(bitmap_data, data_pos, t_size);
#else
				int start_idx = (data_pos - AMP_RDMA_SB_MSG_SIZE)/4096;
				int end_pos = data_pos + t_size;
				if(end_pos % 4096 > 0){
					end_pos = end_pos + 4096;
				}
				int end_idx = (end_pos - AMP_RDMA_SB_MSG_SIZE) / 4096;
				for(j = start_idx; j < end_idx; j++){
					if((j % 8 == 0) && (end_idx - j >= 8)){
						bitmap_data[ j / 8] = 0xFF;
						j += 7;
						continue;
					}
					bitmap_data[j / 8] = (bitmap_data[ j / 8] | (0x1 << (j % 8)));
				}

#endif
				// changed by mayl
				if(req->req_iov == NULL){
					req->req_niov = niov;
                        		req->req_iov = iov;
				}
				if(niov == 0 || iov == NULL){
					AMP_ERROR("RDMA alloc pages cb failed , %d , %p ",niov, iov);
				}
//AMP_ERROR("AMP_RDMA_LISTEN_THREAD[%d], from<%d-%d>: t_size: %d, data_pos: %d, recvlen: %d, bitmap[%d]:%d, %d, %d, %d--%d, %d\n", i, conn->ac_remote_comptype, conn->ac_remote_id, t_size, data_pos, pos, start_idx, bitmap_data[start_idx/8], bitmap_data[start_idx/8 + 1], bitmap_data[start_idx/8 + 2], bitmap_data[start_idx/8+3], bitmap_data[end_idx/8 - 1], bitmap_data[end_idx/8]);
#endif
                    	}
                    
				AMP_DMSG("amp Rdma recv msg,  data len %d, msglen %d\n", msgp->data_len, msgp->amh_size);
                    	//TODO
                    	header->amh_size = 0;
                    	header->amh_magic = 0;
			bitmap[i / 8] = bitmap[i / 8] | (0x1 << (i % 8));

			if(i - conn->last_idx > 1){
				AMP_DMSG("ERROR[%d]: req_seqno: %d, lost_idx: %d, bitmap: %d, next_bitmap:%d, cur_amh_size: %d\n", i, seqno, conn->last_idx + 1, bitmap[(conn->last_idx + 1)/8], bitmap[(conn->last_idx + 1)/8 + 1], ((amp_message_t *)((char *)conn->ac_rdma_recv_buf + 1024 * (conn->last_idx + 1 + AMP_RDMA_SB_BLK_NUM)))->amh_size);
			}
			conn->last_idx = i;
			conn->next_idx = i+1;
			if(conn->next_idx == 1016){
				conn->next_idx = 0;
			}
			
//AMP_ERROR("amp_RDMA_listen_thread[%d]: int msg_addr<%llu>, header->amh_size: %d, msg_seqno: %d, req_seqno: %d from %d-%d, bitmap[%d]: %d\n", i, (uint64_t)conn->ac_rdma_recv_buf + 1024 * (i + 1), header->amh_size, msgp->qp_num, seqno, remote_type, remote_id, i / 8, bitmap[i / 8]);
			if(req_type == (AMP_REQUEST | AMP_MSG) || req_type == (AMP_REQUEST | AMP_DATA)){
                        	req->req_msg = msgp;
                        	req->req_type = req_type;
                        	req->req_conn = conn;
                        	
                        	req->req_msglen = msgp->amh_size + sizeof(amp_message_t);
                        	AMP_DMSG("__amp_rdma_listen_thread: before queue request, req:%p, try to queue cb\n", req);
                        	AMP_DMSG("__amp_rdma_listen_thread: conn:%p, ac_queue_cb:%p\n", conn, conn->ac_queue_cb);
                        	conn->ac_queue_cb(req);
                   	}else if(req_type == (AMP_REPLY | AMP_MSG) || req_type == (AMP_REPLY | AMP_DATA)){

                       		if(req->req_need_ack){
                       			req->req_reply = msgp;
                       			req->req_replylen = msg_len;

                       			if(NULL != (void *)msgp->amh_callback_handle){
                       			    void (*callback)(amp_request_t *);
                       			    callback = (void(*)(amp_request_t *))(msgp->amh_callback_handle);
                       			    (*callback)(req);
                       			}else{
                       			    amp_sem_up(&req->req_waitsem);
                       			}
                       		}
            		}

		    }

        }

#ifdef __CPU_QOS_FOR_LISTEN__
	//add by weizheng, start... 20240708
	if(sleep_cnt == 0){
		if(sleep_flag == 1 && sleep_time_cnt < __SLEEP_CNT){
			sleep_time_cnt ++;
		}
		sleep_flag = 1;
	}else{
		sleep_time_cnt = 0;
		sleep_flag = 0;
		//gettimeofday(&tv1, NULL);
		work_thresh = work_interval;
		gettimeofday(&tv, NULL);
	}

	if(sleep_flag == 1 && sleep_time_cnt == __SLEEP_CNT){
		gettimeofday(&tv2, NULL);
		//used_usec = (tv2.tv_sec - tv1.tv_sec)*1000000 + tv2.tv_usec- tv1.tv_usec;
		work_usec = (tv2.tv_sec - tv.tv_sec)*1000000 + tv2.tv_usec- tv.tv_usec;
		work_thresh -= work_usec;
		if(work_thresh <= 0){
			usleep(__SLEEP_TIME);
			gettimeofday(&tv, NULL);
			//gettimeofday(&tv1, NULL);
			work_thresh = 0;
			sleep_usec = (tv1.tv_sec - tv2.tv_sec)*1000000 + tv1.tv_usec- tv2.tv_usec;
			//AMP_DMSG("schedule for cpu frequency: used_usec: %ld, sleep_usec:%ld\n", used_usec, sleep_usec);
		}
		sleep_time_cnt = 0;
		sleep_flag = 0;
    	}
	sleep_cnt = 0;
	//add by weizheng, end...
#endif

    }

EXIT:
    amp_sem_up(&threadp->at_downsem);
    AMP_DMSG("__amp_rdma_listen_thread: leave: %ld\n",    pthread_self());
    return 0;
}

    
void * 
__amp_rdma_listen_thread_noidx (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t    seqno;
    amp_comp_context_t *ctxt = NULL;
    amp_s8_t     *recvmsg = NULL;
    int use_event = 0;
    int remote_type = 0;
    int remote_id = 0;
    int req_type = 0;
    int msg_len = 0;
    int retry_time = 0;
    int ne, i;
    volatile int *hlen, *tlen;
    struct list_head *pos, *n;
    amp_connection_t * conn = NULL;

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    amp_sem_up(&threadp->at_startsem);
    conn_queue_t *cnq = NULL;

    AMP_ERROR("ENTER AMP_RDMA_LISTEN_THREAD\n");

    while(1){
        if (threadp->at_shutdown) {
            AMP_ERROR("__amp_rdma_listen_thread: to exit, threadp:%p\n", threadp);
            goto EXIT;
        }

        list_for_each_safe(pos, n, &amp_rdma_listen_group[seqno]){
            conn = list_entry(pos, amp_connection_t, listen_list);
        
                    amp_request_t  *req = NULL;
                    amp_message_t  *msgp = NULL;
                    amp_message_t  *header = NULL;
                    amp_u32_t niov = 0;
                    amp_kiov_t *iov = NULL;

                    header = (amp_message_t *)conn->ac_rdma_recv_buf; 

                    msg_len = 0;
#if 1
		    if(0 == header->amh_size){
		    	continue;
		    }
#else
                    retry_time = 0; 
                    while(0 == header->amh_size){
                        retry_time ++;
                        if(retry_time > 16){
                            break;
                        }
                    } 
                    if(retry_time > 16){
                        continue;
                    }
#endif               
                    msg_len = sizeof(amp_message_t) + header->amh_size;
                    tlen = (int *)((char *)conn->ac_rdma_recv_buf + msg_len);
#if 1
		    if(*tlen != msg_len){
		    	continue;
		    }
#else
		    retry_time = 0;
		    while(*tlen != msg_len){
		        retry_time ++;
                        if(retry_time > 16){
                            break;
                        }
		    }
                    if(retry_time > 16){
                        continue;
                    }
#endif
		    req_type = header->amh_type;
		    if(req_type & AMP_DATA){
			retry_time = 0;
			hlen = (int *)((char *)conn->ac_rdma_recv_buf + 1024 * 1024);
#if 1
			if(*hlen == 0){
				continue;
			}
#else
			while(0 == *hlen){
				retry_time ++;
				if(retry_time > 16){
					break;
				}
			}
			if(retry_time > 16){
				continue;
			}
#endif
		    }


                    *tlen = 0;

                    msg_len = sizeof(amp_message_t) + header->amh_size;
                    
                    msgp = (amp_message_t *)amp_alloc(msg_len);
                    memcpy(msgp, conn->ac_rdma_recv_buf, msg_len);
                    __amp_alloc_request(&req);//by weizheng 2013-11-18, alloc for request, refcount =1
                    req->req_msg = msgp;

                    req_type = msgp->amh_type;
                    
                    remote_type = msgp->amh_pid;
                    remote_id = msgp->amh_xid >> 32;

                    req->req_remote_type = remote_type;
                    req->req_remote_id = remote_id;

//AMP_ERROR("MSG_LEN: %d  %d-%d-%d  %d--%d\n", msg_len, kk, jj, ii, remote_type, remote_id);
//AMP_ERROR("amp_rdma_listen_thread: msg_seqno: %d, req_seqno: %d from %d-%d\n", msgp->qp_num, seqno, remote_type, remote_id);

                    recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);
                    
                    if(req_type & AMP_REPLY){
                        amp_request_t  *tmpreqp = NULL;

                        tmpreqp = (amp_request_t *)((unsigned long) msgp->amh_sender_handle);

                        __amp_free_request(req);

                        req = tmpreqp;

                        amp_lock(&amp_waiting_reply_list_lock);
                        if(__amp_within_waiting_reply_list(req)){
                            list_del_init(&req->req_list);
                        }
                        req->req_conn = conn;
                        amp_unlock(&amp_waiting_reply_list_lock);
    
                    }
        
                    if(req_type & AMP_DATA){
                        int t_size = 0;
                        int j = 0;
                        int pos = 4;
                        
struct timeval tv1, tv2, tv3, tv4;
gettimeofday(&tv1, NULL);
                        if(conn->ac_allocpage_cb){
                            conn->ac_allocpage_cb(recvmsg, &niov, &iov);
                        }
gettimeofday(&tv2, NULL);
#if 0
                        for(j = 0; j < niov; j ++){
                            t_size += iov[j].ak_len;
                        }
#endif                   
                        hlen = (int *)((char *)conn->ac_rdma_recv_buf + 1024 * 1024);
			t_size = *hlen;
                        tlen = (int *)((char *)conn->ac_rdma_recv_buf + 1024 * 1024 + t_size + 4);

 			if(*hlen != *tlen){
				assert(0);
			}			
#if 0
//gettimeofday(&tv2, NULL);
                        while( 1 ){
                            if(*hlen == *tlen  && t_size == *hlen){
                                break;
                            }
                            usleep(2);
                        }
//gettimeofday(&tv3, NULL);
#endif
gettimeofday(&tv3, NULL);
                        for(j = 0; j < niov; j++){
                            //memcpy(iov[j].ak_addr, (char *)conn->ac_rdma_recv_buf + 1024 * 1024 + pos, iov[j].ak_len);
                            memcpy(iov[j].ak_addr, (char *)conn->ac_rdma_recv_buf + 1024 * 1024 + pos, iov[j].ak_len);
                            pos += iov[j].ak_len;
                        }
gettimeofday(&tv4, NULL);
//AMP_ERROR("AMP_RDMA_LISTEN_THREAD: alloc_time: %llu us, wait_time: %llu us, COPY DATA USE TIME: %llu us\n", tv2.tv_usec - tv1.tv_usec + 1000000 * (tv2.tv_sec - tv1.tv_sec), tv3.tv_usec - tv2.tv_usec + 1000000 * (tv3.tv_sec - tv2.tv_sec), tv4.tv_usec - tv3.tv_usec + 1000000 * (tv4.tv_sec - tv3.tv_sec));
                        *hlen = 0;
                        *tlen = 0;

                        req->req_niov = niov;
                        req->req_iov = iov;

                    }
                    
                    //TODO
                    header->amh_size = 0;
                    header->amh_magic = 0;


                    if(req_type == (AMP_REQUEST | AMP_MSG) || req_type == (AMP_REQUEST | AMP_DATA)){
                        req->req_msg = msgp;
                        req->req_type = req_type;
                        req->req_conn = conn;
                        
                        req->req_msglen = msgp->amh_size + sizeof(amp_message_t);
                        AMP_DMSG("__amp_rdma_listen_thread: before queue request, req:%p\n", req);
                        AMP_DMSG("__amp_rdma_listen_thread: conn:%p, ac_queue_cb:%p\n", conn, conn->ac_queue_cb);
#if 0
			struct timeval tv;
			gettimeofday(&tv, NULL);
			AMP_ERROR("receive time: %llu us\n", tv.tv_sec * 1000000 + tv.tv_usec - req->req_msg->amh_send_ts.sec * 1000000 - req->req_msg->amh_send_ts.usec);
#endif
                        conn->ac_queue_cb(req);
                    }else if(req_type == (AMP_REPLY | AMP_MSG) || req_type == (AMP_REPLY | AMP_DATA)){

                        if(req->req_need_ack){
                            req->req_reply = msgp;
                            req->req_replylen = msg_len;

                            if(NULL != (void *)msgp->amh_callback_handle){
                                void (*callback)(amp_request_t *);
                                callback = (void(*)(amp_request_t *))(msgp->amh_callback_handle);
                                (*callback)(req);
                            }else{
                                amp_sem_up(&req->req_waitsem);
                            }
                        }
            }
        }

    }

EXIT:
    amp_sem_up(&threadp->at_downsem);
    AMP_DMSG("__amp_rdma_listen_thread: leave: %ld\n",    pthread_self());
    return 0;
}

int  __amp_start_rdma_listen_thread (amp_comp_context_t *ctxt, amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_rdma_listen_thread: enter\n");
    if (seqno >= AMP_RDMA_LISTEN_THREAD_NUM) {
        AMP_ERROR("__amp_start_rdma_listen_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_rdma_listen_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = ctxt;

    err = __amp_create_thread(__amp_rdma_listen_thread, threadp);
    amp_lock(&amp_threads_lock);
    if (!err && (seqno > amp_rdma_listen_thread_num)) 
        amp_rdma_listen_thread_num =  seqno + 1;
    amp_unlock(&amp_threads_lock);

EXIT:
    AMP_LEAVE("__amp_start_rdma_listen_thread: leave\n");
    return err;
}

    
int
__amp_stop_rdma_listen_threads (void)
{
    amp_u32_t i;
    amp_thread_t  *threadp = NULL;

    AMP_DMSG("__amp_stop_rdma_listen_threads: enter, total_num:%d\n", amp_srvin_thread_num);
    
    for(i=0; i<AMP_RDMA_LISTEN_THREAD_NUM; i++) {
        threadp = &amp_rdma_listen_threads[i];
        if (!threadp->at_isup)
            continue;
        threadp->at_shutdown = 1;
    }
    
    
    for(i=0; i<AMP_RDMA_LISTEN_THREAD_NUM; i++) {
        threadp = &amp_rdma_listen_threads[i];
        if (!threadp->at_isup)
            continue;
        amp_sem_down(&threadp->at_downsem);
        threadp->at_isup = 0;
    }

    amp_rdma_listen_thread_num = 0;
        
    AMP_LEAVE("__amp_stop_rdma_listen_threads: leave\n");
    return 0;
}

int __amp_start_rdma_listen_threads (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_u32_t i;

    AMP_ENTER("__amp_start_rdma_listen_threads: enter\n");

    for (i=0; i < AMP_RDMA_LISTEN_THREAD_NUM; i++) {
        err = __amp_start_rdma_listen_thread (ctxt, i);
        if (err < 0)
            goto ERROR;
    }

EXIT:
    AMP_LEAVE("__amp_start_rdma_listen_threads: leave\n");
    return err;

ERROR:
    __amp_stop_rdma_listen_threads();
    goto EXIT;
}


#endif

/*
 * start a srvin thread
 *
 * seqno - the sequence of this thread, counting from 0
 */ 
int 
__amp_start_srvin_thread (amp_comp_context_t *ctxt, amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_srvin_thread: enter\n");
    if (seqno >= AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_start_srvin_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_srvin_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = ctxt;

    err = __amp_create_thread(__amp_serve_in_thread, threadp);
    amp_lock(&amp_threads_lock);
    if (!err && (seqno > amp_srvin_thread_num)) 
        amp_srvin_thread_num =  seqno + 1;
    amp_unlock(&amp_threads_lock);

EXIT:
    AMP_LEAVE("__amp_start_srvin_thread: leave\n");
    return err;
}

/*
 * start a srvout thread
 */ 
int
__amp_start_srvout_thread (amp_comp_context_t *ctxt, amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;
               
    AMP_ENTER("__amp_start_srvout_thread: enter\n");

    if (seqno>=AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_start_srvout_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }
                         
    threadp = &amp_srvout_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = ctxt;
                                   
    err = __amp_create_thread(__amp_serve_out_thread, threadp);
    
    amp_lock(&amp_threads_lock);

    if (!err && (seqno >= amp_srvout_thread_num)) 
        amp_srvout_thread_num = seqno + 1;

    amp_unlock(&amp_threads_lock);
    
EXIT:
    AMP_LEAVE("__amp_start_srvout_thread: leave, threadp:%p\n", threadp);
    return err;
}

/*
 * start a reconn thread
 */ 
int
__amp_start_reconn_thread (amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_reconn_thread: enter\n");

    if(amp_reconn_thread_started) {
        goto EXIT;
    }

    if (seqno>=AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_start_reconn_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_reconn_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;

    err = __amp_create_thread(__amp_reconn_thread, threadp);
   
    amp_lock(&amp_threads_lock);

    if (!err && (seqno >= amp_reconn_thread_num)) 
        amp_reconn_thread_num = seqno + 1;

    amp_unlock(&amp_threads_lock);

    amp_reconn_thread_started = 1;

EXIT:
    AMP_LEAVE("__amp_start_reconn_thread: leave\n");
    return err;
}

#ifdef __TOKEN1__
int
__amp_start_token_generate_thread (amp_comp_context_t *ctxt, amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_token_generate_thread: enter\n");


    if (seqno>=AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_start_token_generate_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_token_generate_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = (void *)ctxt;

    err = __amp_create_thread(__amp_token_generate_thread, threadp);

EXIT:
    AMP_LEAVE("__amp_start_token_generate_thread: leave\n");
    return err;
}
#endif

/*
 * start a netmorn thread
 */ 
amp_thread_t *
__amp_start_netmorn_thread (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_netmorn_thread: enter\n");
    
    if(!ctxt) {
        AMP_ERROR("__amp_start_netmorn_thread: no context\n");
        goto EXIT;
    }
    threadp = (amp_thread_t *)amp_alloc(sizeof(amp_thread_t));
    if (!threadp) {
        AMP_ERROR("__amp_start_netmorn_thread: alloc for thread error\n");
        goto EXIT;
    }
    memset(threadp, 0, sizeof(amp_thread_t));
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = 1;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = ctxt;

    err = __amp_create_thread(__amp_netmorn_thread2, threadp);
    if (err < 0) {
        AMP_ERROR("__amp_start_netmorn_thread: create thread error, err:%d\n", err);
        amp_free(threadp, sizeof(amp_thread_t));
        goto EXIT;
    }

EXIT:
    AMP_LEAVE("__amp_start_netmorn_thread: leave\n");
    return threadp;
}


/*
 * start initial number of srvin threads
 */ 
int
__amp_start_srvin_threads (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_u32_t i;

    AMP_ENTER("__amp_start_srvin_threads: enter\n");

    for (i=0; i < AMP_SRVIN_THR_INIT_NUM; i++) {
        err = __amp_start_srvin_thread (ctxt, i);
        if (err < 0)
            goto ERROR;
    }

EXIT:
    AMP_LEAVE("__amp_start_srvin_threads: leave\n");
    return err;

ERROR:
    __amp_stop_srvin_threads();
    goto EXIT;
}

    
/*
 * start initial number of srvout threads
 */ 
int 
__amp_start_srvout_threads (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_u32_t i;

    AMP_ERROR("__amp_start_srvout_threads: enter, totolnum:%d\n", amp_srvout_thread_num);

    for (i=0; i < AMP_SRVOUT_THR_INIT_NUM; i++) {
        err = __amp_start_srvout_thread (ctxt, i);
        if (err < 0)
            goto ERROR;
    }

EXIT:
    AMP_LEAVE("__amp_start_srvout_threads: leave\n");
    return err;

ERROR:
    __amp_stop_srvout_threads();
    goto EXIT;
}

int
__amp_start_reconn_threads(void)
{
    amp_s32_t err = 0;
    
    err = __amp_start_reconn_thread(0);
    if(err != 0){
        AMP_ERROR("amp_start_reconn_threads: start reconn thread error\n");
        goto EXIT;
    }
    err = __amp_start_wakeup_thread(0);
    if(err != 0){
        AMP_ERROR("amp_start_reconn_threads: start wakeup thread error\n");
    }

EXIT:
    return err;
}

/*
 * start initial number of reconn threads
 */ 
int
__amp_start_wakeup_thread (amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t*  threadp;

    AMP_ENTER("__amp_start_wakeup_thread: enter\n");
 
    if(amp_wakeup_thread_started) {
        goto EXIT;
    }

    if (seqno >= AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_start_wakeup_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_wakeup_threads[seqno];
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = seqno;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;

    err = __amp_create_thread(__amp_wakeup_reconn_thread, threadp);
    
    amp_lock(&amp_threads_lock);

    if (!err && (seqno >= amp_wakeup_thread_num)) 
        amp_wakeup_thread_num = seqno;

    amp_unlock(&amp_threads_lock);

    amp_wakeup_thread_started = 1;

EXIT:                   
    AMP_LEAVE("__amp_start_wakeup_thread: leave\n");
    return err;
}

/*
 * start a listen thread.
 */
amp_thread_t* 
__amp_start_listen_thread(amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_start_listen_thread: enter\n");

    if (!ctxt)  {
        AMP_ERROR("__amp_start_listen_thread: no context\n");
        goto EXIT;
    }


    threadp = (amp_thread_t *)amp_alloc(sizeof(amp_thread_t));
    if (!threadp) {
        AMP_ERROR("__amp_start_listen_thread: alloc threadp error\n");
        goto EXIT;
    }

    memset(threadp, 0, sizeof(amp_thread_t));
    amp_sem_init_locked(&threadp->at_startsem);
    amp_sem_init_locked(&threadp->at_downsem);
    threadp->at_seqno = ctxt->acc_this_id;
    threadp->at_shutdown = 0;
    threadp->at_isup = 0;
    threadp->at_provite = ctxt;

    err = __amp_create_thread(__amp_listen_thread, threadp);

    if (err < 0) {
        amp_free(threadp, sizeof(amp_thread_t));
        threadp = NULL;
        goto EXIT;
    }

EXIT:
    AMP_LEAVE("__amp_start_listen_thread: leave\n");
    return threadp;
    
}

int
__amp_stop_wakeup_thread(amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_stop_wakeup_thread: enter\n");

    if (seqno>=AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_stop_wakeup_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_wakeup_threads[seqno];

    if (!threadp->at_isup) {
        AMP_WARNING("__amp_stop_wakeup_thread: to stop a stopped thread\n");
        goto EXIT;
    }

    threadp->at_shutdown = 1;
    AMP_DMSG("__amp_stop_wakeup_thread: to stop wakeup thread:%p\n", threadp);
    amp_sem_up(&amp_reconn_sem);
    amp_sem_down(&threadp->at_downsem);
    
    if(seqno == (amp_wakeup_thread_num - 1)){
        amp_lock(&amp_threads_lock);
        amp_wakeup_thread_num--;
        amp_unlock(&amp_threads_lock);
    }
   
    memset(threadp, 0, sizeof(amp_thread_t));
    amp_wakeup_thread_started = 0;

EXIT:
    AMP_LEAVE("__amp_stop_wakeup_thread: leave\n");
    return err;
}

int
__amp_stop_reconn_thread (amp_u32_t seqno)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_stop_reconn_thread: enter\n");

    if (seqno>=AMP_MAX_THREAD_NUM) {
        AMP_ERROR("__amp_stop_reconn_thread: wrong seqno:%d\n", seqno);
        err = -EINVAL;
        goto EXIT;
    }

    threadp = &amp_reconn_threads[seqno];

    if (!threadp->at_isup) {
        AMP_WARNING("__amp_stop_reconn_thread: to stop a stopped thread\n");
        goto EXIT;
    }

    threadp->at_shutdown = 1;
    AMP_DMSG("__amp_stop_reconn_thread: to stop reconn thread:%p\n", threadp);
    amp_sem_up(&amp_reconn_sem);
    amp_sem_down(&threadp->at_downsem);
    
    if(seqno == (amp_reconn_thread_num - 1)){
        amp_lock(&amp_threads_lock);
        amp_reconn_thread_num--;
        amp_unlock(&amp_threads_lock);
    }
   
    memset(threadp, 0, sizeof(amp_thread_t));
    amp_reconn_thread_started = 0;

EXIT:
    AMP_LEAVE("__amp_stop_reconn_thread: leave\n");
    return err;
}

#ifdef __TOKEN1__ 
int
__amp_stop_token_generate_threads () 
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_stop_token_generate_thread: enter\n");

    threadp = &amp_token_generate_threads[0];

    if (!threadp->at_isup) {
        AMP_WARNING("__amp_stop_token_generate_thread: to stop a stopped thread\n");
        goto EXIT;
    }

    threadp->at_shutdown = 1;
    AMP_DMSG("__amp_stop_token_generate_thread: to stop reconn thread:%p\n", threadp);
    amp_sem_down(&threadp->at_downsem);

EXIT:
    AMP_LEAVE("__amp_stop_token_generate_thread: leave\n");
    return err;
}
#endif
/*
 * stop a reconn thread.
 */ 
int
__amp_stop_netmorn_thread (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;
    amp_thread_t *threadp = NULL;

    AMP_ENTER("__amp_stop_netmorn_thread: enter\n");

    threadp = ctxt->acc_netmorn_thread;
    if (!threadp) {
        AMP_ERROR("__amp_stop_netmorn_thread: no netmornitor thread to ctxt, type:%d,id:%d\n", \
                          ctxt->acc_this_type, ctxt->acc_this_id);
        err = -EINVAL;
        goto EXIT;
    }

    if (!threadp->at_isup) {
        AMP_ERROR("__amp_stop_netmorn_thread: to stop a stopped thread\n");
        goto EXIT;
    }
                                        

    threadp->at_shutdown = 1;
    AMP_DMSG("__amp_stop_netmorn_thread: to stop netmorn thread:%p\n", threadp);
    amp_sem_up(&amp_netmorn_sem);
    amp_sem_down(&threadp->at_downsem);

    ctxt->acc_netmorn_thread = NULL;
    amp_free(threadp, sizeof(amp_thread_t));

EXIT:
    AMP_LEAVE("__amp_stop_netmorn_thread: leave\n");
    return err;
}


/*
 * stop all srvin threads.
 */ 
int
__amp_stop_srvin_threads (void)
{
    amp_u32_t i;
    amp_thread_t  *threadp = NULL;

    AMP_DMSG("__amp_stop_srvin_threads: enter, total_num:%d\n", amp_srvin_thread_num);
    
    for(i=0; i<amp_srvin_thread_num; i++) {
        threadp = &amp_srvin_threads[i];
        if (!threadp->at_isup)
            continue;
        threadp->at_shutdown = 1;
    }

    for(i=0; i<amp_srvin_thread_num; i++) 
        amp_sem_up(&amp_process_in_sem);
    
    
    for(i=0; i<amp_srvin_thread_num; i++) {
        threadp = &amp_srvin_threads[i];
        if (!threadp->at_isup)
            continue;
        amp_sem_down(&threadp->at_downsem);
        threadp->at_isup = 0;
    }

    amp_srvin_thread_num = 0;
        
    AMP_LEAVE("__amp_stop_srvin_threads: leave\n");
    return 0;
}

/*
 * stop all srvout threads.
 */ 
int
__amp_stop_srvout_threads (void)
{
    amp_u32_t i;
    amp_thread_t  *threadp = NULL;

    AMP_DMSG("__amp_stop_srvout_threads: enter\n");

    for(i=0; i<amp_srvout_thread_num; i++) {
        threadp = &amp_srvout_threads[i];
        threadp->at_shutdown = 1;
    }

    for(i=0; i<amp_srvout_thread_num; i++) {
        threadp = &amp_srvout_threads[i];
        if (!threadp->at_isup)
            continue;
        AMP_DMSG("__amp_stop_srvout_threads: stop thread:%p\n", threadp);
        amp_sem_up(&amp_process_out_sem);
    }
        
    for(i=0; i<amp_srvout_thread_num; i++) {
        threadp = &amp_srvout_threads[i];
        if (!threadp->at_isup)
            continue;
        amp_sem_down(&threadp->at_downsem);
        threadp->at_isup = 0;
    }

    amp_srvout_thread_num = 0;

    AMP_LEAVE("__amp_stop_srvout_threads: leave\n");
    return 0;
}

int
__amp_stop_reconn_threads(void)
{
    __amp_stop_wakeup_thread(0);
    __amp_stop_reconn_thread(0);

    return 0;
}

/*
 * stop the listen thread belongs to  specific component context
 */
int
__amp_stop_listen_thread(amp_comp_context_t *ctxt)
{
    amp_thread_t  *threadp = NULL;
    AMP_ENTER("__amp_stop_listen_thread: enter\n");
    amp_s32_t  notifyid = 1;

    threadp = ctxt->acc_listen_thread;
    if (!threadp->at_isup) {
        AMP_DMSG("__amp_stop_listen_thread: to stop a stopped thread\n");
        goto EXIT;
    }
    pthread_mutex_lock(&ctxt->acc_lock);
    threadp->at_shutdown = 1;
    pthread_mutex_unlock(&ctxt->acc_lock);
    
    write(ctxt->acc_notifyfd, (char *)&notifyid, sizeof(amp_s32_t));
    amp_sem_down(&threadp->at_downsem);
    ctxt->acc_listen_thread = NULL;
    
EXIT:
    AMP_LEAVE("__amp_stop_listen_thread: leave\n");
    return 0;
}


/*
 * the callback of serving outcoming request threads.
 */
void*
__amp_serve_out_thread (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t   seqno;
    amp_request_t  *req = NULL;
    amp_connection_t *conn = NULL; 
    amp_comp_context_t *ctxt = NULL;
    amp_u32_t   conn_type;  /*tcp or udp*/
    amp_u32_t   req_type;    /*msg or data*/
    amp_u32_t type;
    amp_u32_t id;
    amp_s32_t   err = 0;
    amp_u32_t   flags = 0;
    amp_u32_t   need_ack = 0;
    struct sockaddr_in  sout_addr;
    //amp_u32_t  sendsize = 0;

    AMP_ENTER("__amp_serv_out_thread: enter, %ld\n", pthread_self());

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    threadp->at_thread_id = pthread_self();
    
    amp_sem_up(&threadp->at_startsem);
    AMP_DMSG("__amp_serve_out_thread: thread:%p, up\n", threadp);

    /*
     * ok, do the main work.
     */

AGAIN:
    AMP_ENTER("__amp_serve_out_thread: before sem\n");

    amp_sem_down(&amp_process_out_sem);
    if (threadp->at_shutdown) {
        AMP_DMSG("__amp_serve_out_thread: tell us to down\n"); 
        goto EXIT;
    }
    AMP_ENTER("__amp_serve_out_thread: get sem, threadp:%p\n", threadp);

    amp_lock(&amp_sending_list_lock);
    if (list_empty(&amp_sending_list)) {
        amp_unlock(&amp_sending_list_lock);
        goto AGAIN;
    }
    req = list_entry(amp_sending_list.next, amp_request_t, req_list);
    //amp_lock(&req->req_lock);
    list_del_init(&req->req_list);
    req->req_error = 0;
    //req->req_send_state = AMP_REQ_SEND_INIT;
    //amp_unlock(&req->req_lock);
    amp_unlock(&amp_sending_list_lock);
    //sendsize = 0;

    if (!req->req_msg && !req->req_reply) {
        AMP_ERROR("__amp_serve_out_thread: nothing need to be sent \n");
        req->req_error = -EINVAL;
        goto ERROR;
    }
    if(req->req_remote_type == 0 || req->req_remote_id == 0){
        AMP_ERROR("__amp_serve_out_thread: req: %p may be freed! IGNORE it,continue!!!!!\n", req);
        goto ERROR;
    }
    
    AMP_ENTER("__amp_serve_out_thread: to send req:%p\n", req); 
    
    req_type = req->req_type;
    type = req->req_remote_type;
    id = req->req_remote_id;

    if ((req_type != (AMP_REQUEST | AMP_MSG))  &&
            (req_type != (AMP_REQUEST | AMP_DATA)) &&
            (req_type != (AMP_REPLY | AMP_MSG)) &&
            (req_type != (AMP_REPLY | AMP_DATA))) {
        AMP_ERROR("__amp_serve_out_thread: wrong msg type: 0x%x\n", req_type);
        req->req_error = -EINVAL;
        goto ERROR;
    }
    
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
                    AMP_WARNING("__amp_serve_out_thread: no valid conn to peer (type:%d, id:%d, err:%d)\n", \
                                                req->req_remote_type, \
                                                req->req_remote_id, \
                                                err);
                
                    if (req->req_resent && (req->req_type & AMP_REQUEST)) {
                        __amp_add_resend_req(req);
                        goto AGAIN;
                    }
                    if(req->req_type & AMP_REPLY){
                        err = -ENETUNREACH;
                        req->req_error = err;
                        goto ERROR;
                    }
                case 1:
                    AMP_WARNING("__amp_serve_out: no conn to peer(type:%d, id:%d, err:%d)\n", req->req_remote_type, req->req_remote_id, err);
                default:
                    AMP_WARNING("__amp_serve_out_thread:amp_select_conn goto error\n");
                    req->req_error = -ENOTCONN;
                    err = req->req_error;
                    goto ERROR;
            }
        }
    }
       /*
        * the senders and receivers will all change this state
        */
    
    switch(conn->ac_state) {
        case AMP_CONN_BAD:
        case AMP_CONN_NOTINIT:
        case AMP_CONN_CLOSE:
        case AMP_CONN_RECOVER:
            conn->ac_refcont --;
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);
            AMP_WARNING("__amp_serve_out_thread: conn:%p is not valid currently\n", conn);
            goto SELECT_CONN;
        default:
            break;
    }

    /*
     * we get a good connection, so doing the following work.
     */
    conn_type = conn->ac_type;       
    if ((!AMP_HAS_TYPE(conn_type)) || 
           (!AMP_OP(conn_type, proto_sendmsg)) || 
           (!AMP_OP(conn_type, proto_senddata)))  {
        AMP_WARNING("__amp_serve_out_thread: conn:%p, has no operations\n", conn);
        req->req_error = -ENOSYS;
        conn->ac_refcont --;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        err = req->req_error;
        goto ERROR;
    }   
    
    
    //conn->ac_weight += sendsize;
    //amp_unlock(&conn->ac_lock); 
    amp_sem_down(&conn->ac_sendsem);

    AMP_DMSG("__amp_serve_out_thread: after down sem, req:%p, get conn:%p\n", req, conn);
    if (conn->ac_state != AMP_CONN_OK) {
        AMP_WARNING("__amp_serve_out_thread: before send, state of conn:%p is invalid:%d\n", \
                            conn, conn->ac_state);
        //amp_lock(&conn->ac_lock);
        //conn->ac_weight -= sendsize;
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        conn->ac_refcont --;
        amp_unlock(&conn->ac_lock);
        amp_sem_up(&conn->ac_sendsem);
        goto SELECT_CONN;

    }
    //amp_lock(&conn->ac_lock); 
    /*stage1: send msg*/
    amp_lock(&req->req_lock);
    req->req_conn = conn;
    //req->req_send_state = AMP_REQ_SEND_START;
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
    need_ack = req->req_need_ack;

    // TODO : SHOULD USE PROTO_SEND_DATA_WRITE
     if(conn->remote_msg_start_idx >= 512 && conn->next_bitmap_start > 0){
        	while(0 == (err = __amp_rdma_recvsb_half(conn, conn->remote_msg_end_idx / 8, (1016 - conn->remote_msg_end_idx) / 8)));
    	}

    if(conn->remote_msg_start_idx >=  conn->remote_msg_end_idx){
        	while(0 == (err = __amp_rdma_recvsb(conn)));
    	}


    if (req_type & AMP_DATA)  {
        /*stage2: send data*/
        AMP_DMSG("__amp_serv_out_thread: conn:%p, send data\n", conn);
        req->req_stage = AMP_REQ_STAGE_DATA;
        //err = AMP_OP(conn_type, proto_senddata)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        //err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
	if(req_type & AMP_REQUEST){

              AMP_ERROR("amp_serve out: conn:%p, send request data, , xid %llx, \n", conn, req->req_msg->amh_xid);
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }else if(req_type & AMP_REPLY){
        	    err = AMP_OP(conn_type, proto_senddata_write)(conn, &sout_addr, sizeof(sout_addr), req->req_niov, req->req_iov, 0);
        }


       if (err < 0) {
            AMP_ERROR("__amp_serve_out_thread: senddata error, conn:%p, req:%p, err:%d\n",\
                                  conn, req, err);
            amp_unlock(&req->req_lock);
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            conn->ac_state = AMP_CONN_RECOVER;  
            conn->ac_refcont --;
            amp_unlock(&conn->ac_lock);
            goto SEND_ERROR;
        }
    }


    if (req_type & AMP_REQUEST) {
	//  TODO   by mayl , change pos 
	if(req_type & AMP_DATA){
		req->req_msg->data_pos = conn->remote_data_start_pos;
		req->req_msg->data_len = err;
		conn->remote_data_start_pos += err;
		if(conn->remote_data_start_pos % 4096 > 0){
			conn->remote_data_start_pos= (conn->remote_data_start_pos + 4096)/4096*4096;
		}
	}

        AMP_DMSG("__amp_serve_out_thread: conn:%p, send request\n", conn);
        //err = AMP_OP(conn_type, proto_sendmsg)(&conn->ac_sock, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
        err = AMP_OP(conn_type, proto_sendmsg)(conn, &sout_addr, sizeof(sout_addr), req->req_msglen, req->req_msg, flags);
    } else {
        AMP_DMSG("__amp_serve_out_thread: conn:%p, send reply\n", conn);
        
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
    }
    
    if (err < 0) {
        AMP_ERROR("__amp_serve_out_thread: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
        amp_unlock(&req->req_lock);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        conn->ac_state = AMP_CONN_RECOVER;  
        conn->ac_refcont --;
        amp_unlock(&conn->ac_lock);
        goto SEND_ERROR;
    }

    
    AMP_DMSG("__amp_serve_out_thread: finished send, conn:%p\n", conn);
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
if(req_type & AMP_REPLY)
    AMP_ERROR("amp_serve_out: req: %p, conn: %p, handle: %llu, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg->amh_sender_handle, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
else if(req_type & AMP_REQUEST)
    AMP_ERROR("amp_serve_out: req: %p, conn: %p, msg: %p, type: %d, d_type: %d, d_id: %d, seqno: %llu\n", req, conn, req->req_msg, req->req_type, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);


    if (need_ack) { /*waiting for ack*/
        AMP_LEAVE("__amp_serve_out_thread: req:%p, need ack\n", req);
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
            if(list_empty(&req->req_list)){
                list_add_tail(&req->req_list, &amp_waiting_reply_list);
            }
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
        __amp_add_to_listen_fdset(conn);
    }else{
#if 0
#ifdef __AMP_SOCKET_POOL
        AMP_ERROR("__amp_serve_out_thread: socket_pool reback conn : %p ......\n", conn);
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

    //amp_lock(&conn->ac_lock);
    //conn->ac_weight -= sendsize;
    //amp_unlock(&conn->ac_lock);
    amp_sem_up(&conn->ac_sendsem);

    if (need_ack)
        goto AGAIN;
    else{
        amp_sem_up(&req->req_waitsem);
    }
    /*
     * do not need ack
     */
#if 0
    if (req->req_need_free) {
        if (req_type & AMP_REQUEST){
            amp_free(req->req_msg, req->req_msglen);
            req->req_msg = NULL;
        }
        else{
            amp_free(req->req_reply, req->req_replylen);
            req->req_reply = NULL;
        }
        if(req_type & AMP_DATA){
            amp_u32_t i = 0;
            for(i = 0; i < req->req_niov; i++){
                __amp_free(req->req_iov[i].ak_addr);
                req->req_iov[i].ak_addr = NULL;
                req->req_iov[i].ak_len = 0;
            }
            __amp_free(req->req_iov);
            req->req_iov = NULL;
        }

    }
#endif
    //mp_sem_up(&req->req_waitsem);  
    

    goto AGAIN;
     

SEND_ERROR:
    AMP_DMSG("__amp_serve_out_thread: send error through conn:%p, err:%d\n", conn, err);

    /*amp_lock(&conn->ac_lock);
    AMP_DMSG("__amp_serve_out_thread: before check\n");
    
    if (conn->ac_type == AMP_CONN_TYPE_TCP)  {
        conn->ac_datard_count = 0;
        conn->ac_sched = 0;     
    }
    //conn->ac_weight -= sendsize;
    amp_unlock(&conn->ac_lock);*/
 #ifdef __AMP_SOCKET_POOL
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    amp_lock(&conn->ac_lock);
    list_del_init(&conn->ac_list);
    list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
    if(__amp_within_reconn_conn_list(conn)){
        amp_unlock(&conn->ac_lock);
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_sem_up(&amp_reconn_sem);
        goto SELECT_CONN; 
    }
    amp_unlock(&conn->ac_lock);
    //ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
   
    if(!__amp_conn_test(conn)){
        goto SELECT_CONN; 
    }

    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);//modified by weizheng 2013-12-20
   
    amp_lock(&amp_reconn_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if (conn->ac_need_reconn) {//by weizheng 2013-12-10 if the quota right 
        /*
         * we must add the bad connection to the reconnect list.
         */

        if (conn->ac_state != AMP_CONN_RECOVER) {
            AMP_ERROR("__amp_serve_out_thread: set conn:%p to recover\n", conn);
            conn->ac_state = AMP_CONN_RECOVER;  
            if (list_empty(&conn->ac_reconn_list)){
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                amp_sem_up(&amp_reconn_sem);
            }
        } else 
            AMP_ERROR("__amp_serve_out_thread: someone else has set conn:%p to recover\n", conn);
            
    } else if (conn->ac_type == AMP_CONN_TYPE_TCP)   { 
        /*
         * Maybe it's in server side or it's realy need to be released, so we free it.
         */
        if (conn->ac_state != AMP_CONN_CLOSE) {
            AMP_ERROR("__amp_serve_out_thread: set conn:%p to close\n", conn);
            conn->ac_state = AMP_CONN_CLOSE;
            if (list_empty(&conn->ac_reconn_list)){
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                amp_sem_up(&amp_reconn_sem);
            }
        } else 
            AMP_ERROR("__amp_serve_out_thread: someone else has set conn:%p to close\n", conn);
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);
        
        err = -ENETUNREACH;
        req->req_error = err;
        goto ERROR;
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_reconn_conn_list_lock);
    amp_sem_up(&conn->ac_sendsem);
    goto SELECT_CONN;
                                
ERROR:
    AMP_ERROR("__amp_serve_out_thread: at ERROR, conn:%p, req:%p\n", conn, req);
    amp_sem_up(&req->req_waitsem);
    goto AGAIN;
    
EXIT:

    AMP_LEAVE("__amp_serve_out_thread: leave: %ld\n", pthread_self());
    amp_sem_up(&threadp->at_downsem);
    return 0;
}

/*
 * the callback of serving incoming msg requests
 */
void*
__amp_serve_in_thread (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_u32_t    seqno;
    amp_request_t  *req = NULL;
    amp_connection_t *conn = NULL; 
    amp_comp_context_t *ctxt = NULL;
    amp_u32_t    conn_type;  /*tcp or udp*/
    amp_u32_t    req_type;    /*msg or data*/
    amp_u32_t    type;
    amp_u32_t    id;
    amp_s32_t    err = 0;
    amp_s32_t    retry_time = 0;
    amp_s8_t     *recvmsg = NULL;
    amp_message_t  *msgp = NULL;
    struct timeval  tv_now;
#ifdef __AMP_RECONFIRM_MSG
    struct list_head * pos = NULL;
    struct list_head * head = NULL;
#endif
    //amp_u32_t i;
    
    AMP_ERROR("__amp_serv_in_thread: enter, %ld\n", pthread_self());

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = threadp->at_provite;
    threadp->at_isup = 1;
    amp_sem_up(&threadp->at_startsem);

AGAIN:
    amp_sem_down(&amp_process_in_sem);

    if (threadp->at_shutdown) {
        AMP_DMSG("__amp_serve_in_thread: to exit, threadp:%p\n", threadp);
        goto EXIT;
    }
    /*
     * the refcont of conn in dataready list is set to be more
     * than one
     */
    amp_lock(&amp_dataready_conn_list_lock);
    if (list_empty(&amp_dataready_conn_list)) {
        amp_unlock(&amp_dataready_conn_list_lock);
        goto AGAIN;
    }
    conn = list_entry(amp_dataready_conn_list.next, amp_connection_t, ac_dataready_list);
    amp_lock(&conn->ac_lock);

    //conn->ac_stage = AMP_CONN_SELECTED;
    if(!list_empty(&conn->ac_dataready_list)){
        list_del_init(&conn->ac_dataready_list);
    }
    else{
        //conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_dataready_conn_list_lock);
        goto AGAIN;
    }
    conn_type = conn->ac_type;
    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_dataready_conn_list_lock);

    AMP_DMSG("__amp_serve_in_thread: before recv msg header\n");
    /**
     * firstly get a request.
     */
    amp_lock(&conn->ac_lock);
    conn->ac_refcont ++;
    __amp_alloc_request(&req);//by weizheng 2013-11-18, alloc for request, refcount =1
    req->req_remote_id = conn->ac_remote_id;
    req->req_remote_type = conn->ac_remote_comptype;

        /*
     * receive the header
     */
    amp_sem_down(&conn->ac_recvsem);
    msgp = NULL;
    AMP_ERROR("__amp_serve_in_thread: conn : %p, start recv msg, type : %d, id : %d. ......\n", conn, type, id);

    err = __amp_recv_msg(conn, &msgp);
    if (err < 0) {
        conn->ac_refcont --;
        amp_unlock(&conn->ac_lock);

        AMP_WARNING("__amp_serve_in_thread: get msg header error, err:%d, conn:%p\n", err, conn);
        AMP_WARNING("__amp_serve_in_thread: remote_id:%d, remote_comptype:%d, ac_sock:%d\n", \
                             conn->ac_remote_id,conn->ac_remote_comptype, conn->ac_sock);
        if (msgp)
            amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
        __amp_free_request(req);

        if(err == -ECONNABORTED){
		    AMP_WARNING("__amp_serve_in_thread: get msg occur ECONNABORTED ERROR!\n");
		    amp_sem_up(&conn->ac_recvsem);
        	goto AGAIN;
	    }

        /*
         * this connection is error, need reconnection.
         */
        if(!__amp_conn_test(conn)){
            amp_sem_up(&conn->ac_recvsem);
            goto RECV_ERR;
        }

        AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);

        amp_lock(&amp_reconn_conn_list_lock);
        amp_lock(&conn->ac_lock);
        if (conn->ac_state == AMP_CONN_OK) {
            AMP_WARNING("__amp_serve_in_thread: resent the conn:%p\n", conn);   
            if (conn->ac_need_reconn) {
                conn->ac_state = AMP_CONN_RECOVER;
                AMP_ERROR("__amp_serve_in_thread: need recover\n");
            } else {
                conn->ac_state = AMP_CONN_CLOSE;            
                AMP_ERROR("__amp_serve_in_thread: close it\n");
            }
            if (list_empty(&conn->ac_reconn_list)) {
                AMP_ERROR("__amp_serve_in_thread: add conn:%p to reconn list\n", conn);
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                amp_sem_up(&amp_reconn_sem);//modified by weizheng 2013-11-18, not sure
            } else
                AMP_ERROR("__amp_serve_in_thread: conn:%p has added to reconn list\n", conn);
        }
        //conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);
        amp_sem_up(&conn->ac_recvsem);
        goto AGAIN;
    } 
    


    AMP_DMSG("__amp_serve_in_thread: judge amh_magic\n");
    if (msgp->amh_magic != AMP_REQ_MAGIC)  {
        AMP_ERROR("__amp_serve_in_thread: wrong request header, conn:%p,type:0x%x,magic:0x%x,length: %d\n", \
                        conn, 
                        msgp->amh_type,
                        msgp->amh_magic, 
                        msgp->amh_size);
        conn->ac_refcont --;
        amp_unlock(&conn->ac_lock);
        amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
        __amp_free_request(req);

        if(!__amp_conn_test(conn)){    
            amp_sem_up(&conn->ac_recvsem);//modified by weizheng 2013-11-18
            goto RECV_ERR;
        }

        AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);

        amp_lock(&amp_reconn_conn_list_lock);
        amp_lock(&conn->ac_lock);
        if (conn->ac_state == AMP_CONN_OK) {
            AMP_ERROR("__amp_serve_in_thread: reset the conn:%p\n", conn);
            if (conn->ac_need_reconn) {
                conn->ac_state = AMP_CONN_RECOVER;
                AMP_ERROR("__amp_serve_in_thread: conn:%p, need recover\n", conn);
            } else {
                conn->ac_state = AMP_CONN_CLOSE;            
                AMP_ERROR("__amp_serve_in_thread: conn:%p, close it\n", conn);
            }
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
        }
        //conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);

        amp_sem_up(&conn->ac_recvsem);//modified by weizheng 2013-11-18
        goto AGAIN;
    }

    req_type = msgp->amh_type;
     
    AMP_DMSG("__amp_serve_in_thread: judge req_type:%d\n", req_type);

    /*
     * verify the request
     */
    
     AMP_DMSG("__amp_serve_in_thread: judge what kind of msg\n");
    if(req_type == AMP_RDMA_HELLO /*|| req_type == AMP_RDMA_HELLO_ACK*/){
#ifdef __AMP_RDMA__

            conn->ac_rdma_remote_config.addr = msgp->rdma_addr;
            conn->ac_rdma_remote_config.rkey = msgp->rdma_rkey;
            conn->ac_rdma_remote_config.qp_num = msgp->qp_num;
            conn->ac_rdma_remote_config.psn = msgp->psn;
            conn->ac_rdma_remote_config.lid = msgp->lid;
            memcpy(conn->ac_rdma_remote_config.gid, msgp->gid, 16);

            
            msgp->rdma_addr = conn->ac_rdma_local_config.addr;
            msgp->rdma_rkey = conn->ac_rdma_local_config.rkey;
            msgp->qp_num = conn->ac_rdma_local_config.qp_num;
            msgp->psn = conn->ac_rdma_local_config.psn;
            msgp->lid = conn->ac_rdma_local_config.lid;
            memcpy(msgp->gid, conn->ac_rdma_local_config.gid, 16);

#endif

        if(req_type == AMP_RDMA_HELLO){
            msgp->amh_type = AMP_RDMA_HELLO_ACK;


            //err = AMP_OP(conn->ac_type, proto_sendmsg)(&conn->ac_sock, NULL, 0, sizeof(amp_message_t), &msgp, 0);
            err = AMP_OP(conn->ac_type, proto_sendmsg)(conn, NULL, 0, sizeof(amp_message_t), &msgp, 0);
            AMP_ERROR("__amp_serve_in_thread: send back hello test msg, err:%d\n", err);
            if(err){
                __amp_free_request(req);
                amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
                amp_unlock(&conn->ac_lock);
                amp_lock(&amp_reconn_conn_list_lock);
                amp_lock(&conn->ac_lock);
                if (conn->ac_state == AMP_CONN_OK) {
                    AMP_ERROR("__amp_serve_in_thread: reset the conn:%p\n", conn);
                    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
                    if(conn->ac_need_reconn) {
                        conn->ac_state = AMP_CONN_RECOVER;
                        AMP_ERROR("__amp_serve_in_thread: conn:%p, need recover\n", conn);
                    } else {
                        conn->ac_state = AMP_CONN_CLOSE;    
                        AMP_ERROR("__amp_serve_in_thread: conn:%p, close it\n", conn);
                    }
            
                    if (list_empty(&conn->ac_reconn_list))
                        list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                }
                //conn->ac_stage = AMP_CONN_NOT_SELECTED;
                amp_unlock(&conn->ac_lock);
                amp_unlock(&amp_reconn_conn_list_lock); 
                amp_sem_up(&conn->ac_recvsem);
                goto AGAIN;
            }
        //}else if(req_type == AMP_RDMA_HELLO_ACK){
            
        }
        __amp_free_request(req);
        amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
        amp_unlock(&conn->ac_lock);
        amp_sem_up(&conn->ac_recvsem);
        goto AGAIN; 
    }else if ((req_type != (AMP_REQUEST | AMP_MSG))  &&
             (req_type != (AMP_REQUEST | AMP_DATA)) &&
             (req_type != (AMP_REPLY | AMP_MSG)) &&
             (req_type != (AMP_REPLY | AMP_DATA))){

        AMP_ERROR("__amp_serve_in_thread: get wrong request header, conn:%p, type:%d\n", conn, req_type);
        conn->ac_refcont --;
        amp_unlock(&conn->ac_lock);
        if(!__amp_conn_test(conn)){
            __amp_free_request(req);
            amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
            amp_sem_up(&conn->ac_recvsem);
            goto RECV_ERR;
        }

        amp_lock(&amp_reconn_conn_list_lock);
        amp_lock(&conn->ac_lock);
        if (conn->ac_state == AMP_CONN_OK) {
            AMP_ERROR("__amp_serve_in_thread: reset the conn:%p\n", conn);
            AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
            if(conn->ac_need_reconn) {
                conn->ac_state = AMP_CONN_RECOVER;
                AMP_ERROR("__amp_serve_in_thread: conn:%p, need recover\n", conn);
            } else {
                conn->ac_state = AMP_CONN_CLOSE;    
                AMP_ERROR("__amp_serve_in_thread: conn:%p, close it\n", conn);
            }
            
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
        }
        //conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);

        __amp_free_request(req);
        amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
        amp_sem_up(&conn->ac_recvsem);//modified by weizheng 2013-11-18
        goto AGAIN;
    }

#ifdef __AMP_RECONFIRM_MSG
if(req_type & AMP_REPLY){
        amp_htb_entry_t *htbentry = NULL;
        amp_reconfirm_msg_c_t *reconf = NULL;
        struct list_head * nxt = NULL;
        amp_u32_t hashvalue = 0;

        hashvalue = __amp_reconfirm_hash_c(msgp->amh_sender_handle);
        htbentry = amp_reconfirm_hash_table_c + hashvalue;
        amp_lock(&htbentry->lock);
        list_for_each_safe(pos,nxt,&htbentry->queue){
            reconf = list_entry(pos, amp_reconfirm_msg_c_t, reconf_list);
            if(msgp->amh_sender_handle == reconf->reconf_sender_handle &&
                    msgp->amh_xid == reconf->reconf_xid &&
                    msgp->amh_pid == reconf->reconf_pid &&
                    msgp->amh_send_ts.sec == reconf->reconf_send_ts.sec &&
                    msgp->amh_send_ts.usec == reconf->reconf_send_ts.usec){
                AMP_ERROR("__amp_serve_in_thread[AMP_REPLY] has receive the same reply, req: 0x%llx\n", msgp->amh_sender_handle);
                __amp_free_request(req);
                if(conn_type == AMP_CONN_TYPE_TCP) 
                    amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
                else
                    amp_free(msgp, AMP_MAX_MSG_SIZE);
                amp_unlock(&htbentry->lock);
                conn->ac_refcont --;
                //conn->ac_stage = AMP_CONN_NOT_SELECTED;
                amp_unlock(&conn->ac_lock);
                goto ADD_BACK;
            }
        }
        amp_unlock(&htbentry->lock);
    }
#endif

    AMP_DMSG("__amp_serve_in_thread: is it a reply\n");
    if (req_type & AMP_REPLY)   {
        /*
         * get a reply, get the request
         */
        amp_request_t  *tmpreqp = NULL;
        //int within_waiting_reply_list = 0;
        
        AMP_DMSG("__amp_serve_in_thread: it's a reply\n");
        tmpreqp = (amp_request_t *)((unsigned long) msgp->amh_sender_handle);
#if 0
        /*amp_waiting_reply_list need to protect by rwlock, because of amp_waiting_reply_list will reset to NULL, 
         * and lock will limit the performance, 
         * in order to have high performance, here need rwlock to protect other threads modify amp_waiting_reply_list,not amp_lock*/
        amp_lock(&amp_waiting_reply_list_lock);
        head = &amp_waiting_reply_list;
        for (pos=head->next; pos != head;){
            amp_request_t * tmp = list_entry(pos, amp_request_t, req_list);
            if((amp_u64_t)tmp == msgp->amh_sender_handle){
                    within_waiting_reply_list = 1;
                    break;
            }
            pos=pos->next;
        }
        amp_unlock(&amp_waiting_reply_list_lock);
#endif
        if ((amp_u64_t)tmpreqp < amp_malloc_addr_min || 
              (amp_u64_t)tmpreqp > amp_malloc_addr_max ||
                //!within_waiting_reply_list ||
                msgp->amh_xid >> 32 != conn->ac_this_id || 
                msgp->amh_pid != conn->ac_this_type ||
                //(amp_u64_t)(msgp->amh_message_handle) != (amp_u64_t)(tmpreqp->req_msg) ||
                !__amp_reqheader_equal(tmpreqp->req_msg, msgp)) {

            //__amp_add_to_listen_fdset(conn);
            AMP_ERROR("__amp_serve_in_thread: conn:%p, local_tmp_req:%p, receive_req:%p, two header is not equal\n", conn, req, tmpreqp);
            __amp_free_request(req);

            if(conn_type == AMP_CONN_TYPE_TCP) 
                amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
            else
                amp_free(msgp, AMP_MAX_MSG_SIZE);
            //conn->ac_stage = AMP_CONN_NOT_SELECTED;
            conn->ac_refcont --;
            amp_unlock(&conn->ac_lock);
            goto ADD_BACK;
            
        } else {
            __amp_free_request(req);
            req = tmpreqp; //by weizheng 2013-11-18, switch to the orignal req, now req->req_refcount=2
            retry_time = 0;
RETRY_MATCH:
            amp_lock(&amp_waiting_reply_list_lock);
            //amp_lock(&req->req_lock);
            //req->req_send_state = AMP_REQ_RECV;
            if(__amp_within_waiting_reply_list(req)){
                list_del_init(&req->req_list);//delete from amp_wating_reply_list
            }else{
                retry_time ++;
                if(retry_time < 5){
                    //amp_unlock(&req->req_lock);
                    amp_unlock(&amp_waiting_reply_list_lock);
                    AMP_WARNING("__amp_serve_in_thread: receive reply msg, amh_send_handler: 0x%llx, seqno: %lld, not in the amp_waiting_reply_list, retry_time: %d  ......\n", msgp->amh_sender_handle, msgp->amh_xid & 0xFFFFFFFF, retry_time);
                    sleep(1);
                    goto RETRY_MATCH;
                }
                AMP_ERROR("__amp_serve_in_thread: receive reply msg, amh_send_handler: 0x%llx, seqno: %lld, not in the amp_waiting_reply_list ......\n", msgp->amh_sender_handle, msgp->amh_xid & 0xFFFFFFFF);
                if(conn_type == AMP_CONN_TYPE_TCP) 
                    amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
                else
                    amp_free(msgp, AMP_MAX_MSG_SIZE);
                //amp_unlock(&req->req_lock);
                amp_unlock(&amp_waiting_reply_list_lock);
                //conn->ac_stage = AMP_CONN_NOT_SELECTED;
                conn->ac_refcont --;
                amp_unlock(&conn->ac_lock);
                goto ADD_BACK;

            }
            req->req_conn = conn;
            //amp_unlock(&req->req_lock);
            amp_unlock(&amp_waiting_reply_list_lock);

        }       
    }

    /*
     * receive the data, if needed.
     */
    if (req_type & AMP_DATA)  {
        amp_u32_t niov = 0;
        amp_kiov_t  *iov = NULL;
        //amp_kiov_t  *iov_a = NULL;
        //amp_kiov_t  kiov;

        AMP_DMSG("__amp_serve_in_thread: has data\n");
        //TODO
// send data + recive data, not ensure the address of send data as same as the address of receive data, the receive address will realloc
        /*modify by weizheng, we use the alloc mem before, if the mem alloc before less than we need ,will make error*/
        /*if ((req_type & AMP_REPLY) && req->req_iov) { 
            if(conn -> ac_freepage_cb){
                conn->ac_freepage_cb(req->req_niov, &req->req_iov);
            }else{
                iov_a = req->req_iov;
                for(i = 0; i < req->req_niov; i++ ){
                    kiov = iov_a[i];
		            if(kiov.ak_addr){
			            free(kiov.ak_addr);
                        kiov.ak_len = 0;
                        kiov.ak_addr = NULL;
                    }
                }
	            free(req->req_iov);
            }
            req->req_niov = 0;
            req->req_iov = NULL;
        }*/
            niov = req->req_niov;
            iov = req->req_iov;
        /*if ((req_type & AMP_REPLY) && req->req_iov) { 
            niov = req->req_niov;
            iov = req->req_iov;
        }else*/
        {
            /*
             * if we have provided the alloc function, then calling it, else
             * use the default one.
             */
            recvmsg = (amp_s8_t *)((amp_s8_t *)msgp + AMP_MESSAGE_HEADER_LEN);
            if(conn->ac_allocpage_cb)
                err = conn->ac_allocpage_cb(recvmsg, &niov, &iov);
            else{
                AMP_ERROR("__amp_serve_in_thread: ac_allocpage_cb not defined!\n");
                err = -1;
            }
            if (err < 0) {
                conn->ac_refcont --;
                amp_unlock(&conn->ac_lock);

                /*
                 * we can be sure, no memory now, in order to lest the following requests
                 * continue bother us, reset it, so that the remote peer will send it again.
                 */
                AMP_ERROR("__amp_serve_in_thread: conn:%p, alloc page error, err:%d\n", conn, err);

                if ((req_type & AMP_REPLY) && req->req_need_ack) {
                    req->req_error = -ENOMEM;
                    amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
                    amp_sem_up(&req->req_waitsem);//modifid by weizheng 2013-12-23
                } else if(req_type & AMP_REQUEST) {
                    if(req->req_type & AMP_DATA){
                        if(conn->ac_freepage_cb && req->req_need_free)
                            conn->ac_freepage_cb(niov, &iov);
                    }
                    __amp_free(msgp);
                    __amp_free_request(req);
                }
                amp_sem_up(&conn->ac_recvsem);

                if(!__amp_conn_test(conn)){
                    goto RECV_ERR;    
                }

                amp_lock(&amp_reconn_conn_list_lock);
                amp_lock(&conn->ac_lock);
                if (conn->ac_state == AMP_CONN_OK) {
                    AMP_ERROR("__amp_serve_in_thread: alloc page error, resent conn:%p\n", conn);
                    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
                    if(conn->ac_need_reconn) {
                        conn->ac_state = AMP_CONN_RECOVER;
                    } else
                        conn->ac_state = AMP_CONN_CLOSE;            
                    if (list_empty(&conn->ac_reconn_list))
                        list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                }
                //conn->ac_stage = AMP_CONN_NOT_SELECTED;
                amp_unlock(&conn->ac_lock);
                amp_unlock(&amp_reconn_conn_list_lock);

                goto AGAIN;
            }
        }
        /*ok, the page is prepared, receive the data now*/
        //err = AMP_OP(conn_type, proto_recvdata)(&conn->ac_sock, &conn->ac_remote_addr, sizeof(conn->ac_remote_addr), niov, iov, 0);
        err = AMP_OP(conn_type, proto_recvdata)(conn, &conn->ac_remote_addr, sizeof(conn->ac_remote_addr), niov, iov, 0);
    
        if (err < 0) {
            conn->ac_refcont --;
            amp_unlock(&conn->ac_lock);

            AMP_ERROR("__amp_serve_in_thread: recv data error, conn:%p, err:%d\n", \
                                  conn, err);
            AMP_DMSG("__amp_serve_in_thread: it's tcp, state:%d\n", conn->ac_state);
            if (iov != req->req_iov && req->req_need_free)  /*free all the alloced space*/
            {
                conn->ac_freepage_cb(niov, &iov);
            }

            if(conn_type == AMP_CONN_TYPE_TCP) 
                amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
            else
                amp_free(msgp, AMP_MAX_MSG_SIZE);
            
            if(req_type & AMP_REQUEST){
                __amp_free_request(req);
            }else if (req_type & AMP_REPLY) {
                amp_lock(&req->req_lock);
                req->req_error = -EPROTO;
                amp_unlock(&req->req_lock);
                if(req->req_need_ack){
                    amp_sem_up(&req->req_waitsem); 
                }
            }
            amp_sem_up(&conn->ac_recvsem);

            if(!__amp_conn_test(conn)){
                goto RECV_ERR;
            }

            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if (conn->ac_state == AMP_CONN_OK) {
                AMP_ERROR("__amp_serve_in_thread: reset conn:%p\n", conn);
                if(err != -EINVAL)
                    AMP_OP(conn_type, proto_disconnect)((void *)&conn->ac_sock);
                if (conn->ac_need_reconn) {
                    AMP_ERROR("__amp_serve_in_thread: set conn:%p recover\n", conn);
                    conn->ac_state = AMP_CONN_RECOVER;
                } else {
                    AMP_ERROR("__amp_serve_in_thread: close conn:%p\n", conn);
                    conn->ac_state = AMP_CONN_CLOSE;            
                }
                if (list_empty(&conn->ac_reconn_list)){
                    list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                    amp_sem_up(&amp_reconn_sem);
                }
            } else 
                AMP_ERROR("__amp_serve_in_thread: conn:%p, state:%d\n", conn, conn->ac_state);
            //conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            
            goto AGAIN;
        }
        
        if (req->req_iov != iov) {
            req->req_niov = niov;
            req->req_iov = iov;
        }       
    }
        
    //__amp_add_to_listen_fdset(conn);
        
    AMP_DMSG("__amp_serve_in_thread: amp_type: %d, req: %p, conn: %p, s_type: %d, s_id: %d, seqno: %llu\n", req_type, req, conn, conn->ac_remote_comptype, conn->ac_remote_id, msgp->amh_xid & 0xFFFFFFFF);

    if (req_type & AMP_REQUEST) {
        AMP_DMSG("__amp_serve_in_thread: it's a request, queue it\n");
        req->req_msg = msgp;
        req->req_type = req_type;
        req->req_conn = conn;
        req->req_msglen = msgp->amh_size + sizeof(amp_message_t);
        AMP_DMSG("__amp_serve_in_thread: before queue request, req:%p\n", req);
        AMP_DMSG("__amp_serve_in_thread: conn:%p, ac_queue_cb:%p\n", conn, conn->ac_queue_cb);
        if (!conn->ac_queue_cb) 
            AMP_ERROR("__amp_serve_in_thread: no queue callback for conn:%p\n", conn);
        else{
#ifdef __AMP_RECONFIRM_MSG
            /*used for distinguish the repeat request*/
            if(req_type & AMP_REQUEST){
                amp_s32_t res;
                res = __amp_within_reconfirm_htb(req);
                switch(res){
                    case 2:
                        //the request message has already processe
                        AMP_ERROR("__amp_serve_in_thread: the received request message was processed, auto reply and return...\n");
                        amp_send_sync(conn->ac_ctxt, req, req->req_msg->amh_pid, req->req_msg->amh_xid >> 32, 1);
                    case 1:
                        // the request message is in processing
                        amp_gettimeofday(&tv_now);
                        if(tv_now.tv_sec - req->req_msg->amh_send_ts.sec < AMP_CONN_TIMEWAIT_INTERVAL){
                            AMP_ERROR("__amp_serve_in_thread: the received request message is in processing , discard directly and return... \n");
                            if(req_type & AMP_DATA){
                                for(i = 0; i < req->req_niov; i++){
                                    __amp_free(req->req_iov[i].ak_addr);    
                                }
                                __amp_free(req->req_iov);
                            }

                            __amp_free(req->req_msg);
                            __amp_free_request(req);
                            conn->ac_refcont --;
                            amp_unlock(&conn->ac_lock);
                            goto ADD_BACK;
                        }
                        break;
                    case 0:
                        break;
                    default:
                        AMP_ERROR("__amp_serve_in_thread: result of __amp_within_reconfirm_htb is error!\n");
                }
            }
#endif
            conn->ac_queue_cb(req);
        }
    } else if (req_type & AMP_REPLY) {

        if (req->req_need_ack) {            
            AMP_DMSG("__amp_serve_in_thread: need wait it\n");
            req->req_reply = msgp;
            req->req_replylen = msgp->amh_size + sizeof(amp_message_t);
            if(__amp_reqheader_equal(req->req_msg, msgp)){
                amp_gettimeofday(&tv_now);
                //req->req_msg->amh_send_ts.sec = tv_now.tv_sec;
                //req->req_msg->amh_send_ts.usec = tv_now.tv_usec;

#ifdef __AMP_RECONFIRM_MSG
                __amp_within_reconfirm_htb_c(req);
#endif

                if(NULL != (void *)msgp->amh_callback_handle){
                    void (*callback)(amp_request_t *);
                    callback = (void (*)(amp_request_t *))(msgp->amh_callback_handle);
                    (*callback)(req);
                }else{
                    amp_sem_up(&req->req_waitsem);
                }
                //conn->ac_stage = AMP_CONN_NOT_SELECTED;
            }
        } else {
            AMP_DMSG("__amp_serve_in_thread: needn't ack, free req_msg\n");     
            if(conn_type == AMP_CONN_TYPE_TCP) 
                amp_free(msgp, (msgp->amh_size + sizeof(amp_message_t)));
            else
                amp_free(msgp, AMP_MAX_MSG_SIZE);           
        } 
    }
    conn->ac_refcont --;
    amp_unlock(&conn->ac_lock);

ADD_BACK:
    AMP_DMSG("__amp_serve_in_thread: add back\n");
    amp_sem_up(&conn->ac_recvsem);
    __amp_add_to_listen_fdset(conn);
    goto AGAIN;

EXIT:
    AMP_DMSG("__amp_serve_in_thread: leave: %ld\n",    pthread_self());
    amp_sem_up(&threadp->at_downsem);
    return 0;

RECV_ERR:
#ifdef __AMP_SOCKET_POOL
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    amp_lock(&conn->ac_lock);
    list_del_init(&conn->ac_list);
    list_add_tail(&conn->ac_list, &(ctxt->acc_conns[type].acc_remote_conns[id].queue));
    amp_unlock(&conn->ac_lock);
    //ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
    goto AGAIN;

}

/*
 * the callback of serving reconnection service.
 * 
 * this thread need lock the conn and sendsem
 * to ensure that we only allow one reconnection thread.
 */
void * __amp_wakeup_reconn_thread(void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_s32_t remain_seconds;
#ifdef __AMP_RECONFIRM_MSG
    amp_u32_t times = 0;
    amp_u32_t i = 0;
    struct list_head * pos = NULL;
    struct list_head * nxt = NULL;
    amp_htb_entry_t *htbe = NULL;
    amp_reconfirm_msg_c_t *reconf_c = NULL;
    amp_reconfirm_msg_t *reconf = NULL;
    struct timeval tv;
#endif
    remain_seconds = AMP_CONN_RECONN_INTERVAL;
    threadp = (amp_thread_t *)argv;
    threadp->at_isup= 1;
    amp_sem_up(&threadp->at_startsem);
AGAIN:
    if (threadp->at_shutdown)
        goto EXIT;
    remain_seconds = sleep(remain_seconds);
    if (!remain_seconds) {
        remain_seconds = AMP_CONN_RECONN_INTERVAL;
        amp_sem_up(&amp_reconn_sem);
        amp_sem_up(&amp_netmorn_sem);
    }
    else if (remain_seconds < 0) {
        AMP_ERROR("__amp_wakeup_recon_thread: sleep returned:%d\n", remain_seconds);
        remain_seconds = AMP_CONN_RECONN_INTERVAL;
    }
#ifdef __AMP_RECONFIRM_MSG
    //times ++;
    //if(0 == times % (AMP_RECONFIRM_MSG_RECHECK_INTERVAL/AMP_CONN_RECONN_INTERVAL)){
        times = 0;
        amp_gettimeofday(&tv);
        for (i = 0; i < AMP_RECONFIRM_HTB_SIZE; i++) {
            htbe = amp_reconfirm_hash_table_c + i;
            amp_lock(&htbe->lock);
            list_for_each_safe(pos, nxt, &htbe->queue){
                reconf_c = list_entry(pos, amp_reconfirm_msg_c_t, reconf_list);
                if(tv.tv_sec - reconf_c->reconf_send_ts.sec > AMP_RECONFIRM_MSG_RECHECK_INTERVAL){
                    amp_lock(&reconf_c->reconf_lock);
                    list_del_init(&reconf_c->reconf_list);
                    amp_unlock(&reconf_c->reconf_lock);
                    __amp_free(reconf_c);
                }
            }
            amp_unlock(&htbe->lock);
     
            htbe = amp_reconfirm_hash_table_s + i;
            amp_lock(&htbe->lock);
            list_for_each_safe(pos, nxt, &htbe->queue){
                reconf = list_entry(pos, amp_reconfirm_msg_t, reconf_list);
                if(tv.tv_sec - reconf->reconf_send_ts.sec > AMP_RECONFIRM_MSG_RECHECK_INTERVAL){
                    amp_lock(&reconf->reconf_lock);
                    list_del_init(&reconf->reconf_list);
                    amp_unlock(&reconf->reconf_lock);
                    if(reconf->reconf_reply_msg)
                        __amp_free(reconf->reconf_reply_msg);
                    reconf->reconf_reply_msg = NULL;
                    __amp_free(reconf);
                }
            }
            amp_unlock(&htbe->lock);
        }
    //}
#endif
    goto AGAIN;
EXIT:
    amp_sem_up(&threadp->at_downsem);
    amp_sem_up(&amp_reconn_finalize_sem);
    return NULL;
}
#ifdef __TOKEN1__
void * __amp_token_generate_thread(void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_comp_context_t *ctxt = NULL;
    threadp = (amp_thread_t *)argv;
    ctxt = (amp_comp_context_t *)(threadp->at_provite);
    threadp->at_isup= 1;
    amp_sem_up(&threadp->at_startsem);

    while(1){
        if (threadp->at_shutdown){
            break;
        }
        
        amp_lock(&ctxt->acc_token_lock);
        
        ctxt->acc_token_num = 0;

        amp_unlock(&ctxt->acc_token_lock);


        sleep(1);
    }

    amp_sem_up(&threadp->at_downsem);
    return NULL;
}
#endif
void* __amp_reconn_thread(void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_comp_context_t *ctxt = NULL;
    amp_connection_t *conn; 
    amp_connection_t *tmpconn = NULL;
    amp_request_t *req = NULL;
    amp_comp_conns_t *cmp_conns = NULL;
    conn_queue_t   *cnq = NULL;
    struct list_head  *head = NULL;
    struct list_head  *pos = NULL;
    struct list_head  *nxt = NULL;

    amp_s32_t err = 0;
    amp_s32_t type;
    amp_s32_t id;
    amp_s32_t remain_times;
    amp_u32_t i;
    amp_u32_t seqno;
    amp_u32_t has_valid_conn;
    amp_u32_t no_conns;
    amp_u32_t force_notify;
    amp_u64_t last_conn_time = 0;
    sigset_t  thread_sigs;

    
    AMP_ENTER("__amp_reconn_thread: enter, %ld\n", pthread_self());

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;

    sigemptyset(&thread_sigs);
    sigaddset(&thread_sigs, SIGTERM);
    pthread_sigmask(SIG_UNBLOCK, &thread_sigs, NULL);

    threadp->at_isup= 1;
    amp_sem_up(&threadp->at_startsem);

    /*
     * doing main work
     */
DOWN_SEM:
    amp_sem_down(&amp_reconn_sem);
    amp_sem_down(&amp_reconn_finalize_sem);
    AMP_DMSG("__amp_reconn_thread: wake up\n");
    if (threadp->at_shutdown) 
        goto EXIT;
    
    while(1){
        
        amp_lock(&amp_reconn_conn_list_lock);
        if(list_empty(&amp_reconn_conn_list)){
            amp_unlock(&amp_reconn_conn_list_lock);
            break;
        }
        conn = list_entry(amp_reconn_conn_list.next, amp_connection_t, ac_reconn_list );
        amp_lock(&conn->ac_lock);
        if(!list_empty(&conn->ac_reconn_list)){
            list_del_init(&conn->ac_reconn_list);
        }else{
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            continue;
        }
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);
      
        AMP_ERROR("__amp_reconn_thread: to handle conn[%d, %d]:%p\n", conn->ac_remote_comptype, conn->ac_remote_id, conn);
        ctxt = conn->ac_ctxt;
        /*
         * insert by weizheng 2014-1-2 remove the sock form listen set in time
         */

        if (conn->ac_remote_port == 0) {
            AMP_ERROR("__amp_reconn_thread: the remote port of conn: %p is zero\n", conn);
            amp_lock(&conn->ac_lock);
            conn->ac_state = AMP_CONN_BAD;
            amp_unlock(&conn->ac_lock);
            __amp_dequeue_conn(conn, ctxt);
            __amp_free_conn(conn);//canot delete
            continue;
        }
        
        
        if ((conn->ac_state == AMP_CONN_CLOSE) && !conn->ac_need_reconn) {
            /*
             * if we're server, then we just free it.
             */
            AMP_ERROR("__amp_reconn_thread: close conn:%p, before dequeue conn, refcount:%d\n", \
                                  conn, conn->ac_refcont);
            amp_lock(&conn->ac_lock);
            conn->ac_state = AMP_CONN_BAD;
            //conn->ac_weight = 0;
            amp_unlock(&conn->ac_lock);
            __amp_dequeue_conn(conn, ctxt);
            __amp_free_conn(conn);//cannot delete
            continue;
        }

        AMP_ERROR("__amp_reconn_thread: need reconnect for conn:%p\n", conn);
#ifdef __AMP_CONNS_DUPLEX
	conn->ac_duplex = 0;
#endif 
        err = __amp_connect_server(conn);
        if (err < 0) {
            AMP_ERROR("__amp_reconn_thread: conn:%p, remaintimes:%d, error, err:%d\n", \
                                  conn, conn->ac_remain_times, err);
            has_valid_conn = 0;
            force_notify = 1;
            no_conns = 1;
            
            if (conn->ac_remain_times <= 0) { 
#if 1
                conn->ac_remain_times = AMP_CONN_RECONN_MAXTIMES; 
                force_notify = 1;
                sleep(60);
#else
                AMP_ERROR("__amp_reconn_thread: conn:%p cannot connect server, closed and free !!!\n",conn);
                amp_lock(&conn->ac_lock);
                conn->ac_state = AMP_CONN_BAD;
                amp_unlock(&conn->ac_lock); 
                __amp_dequeue_conn(conn, ctxt);
                __amp_free_conn(conn);//cannot delete
                continue;
#endif
            }

            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            
            conn->ac_remain_times --;
            if (list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
            
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);

            type = conn->ac_remote_comptype;
            id = conn->ac_remote_id;
            remain_times = conn->ac_remain_times;


            if (ctxt && (ctxt->acc_conns) && (remain_times >= (AMP_CONN_RECONN_MAXTIMES - 1))) {
                cmp_conns = &(ctxt->acc_conns[type]);
                if (!cmp_conns->acc_remote_conns)
                    continue;

                pthread_mutex_lock(&(cmp_conns->acc_remote_conns[id].queue_lock));
                cnq = &(cmp_conns->acc_remote_conns[id]);
                head = &(cmp_conns->acc_remote_conns[id].queue);    
    
                if (list_empty(head)) {
                    AMP_ERROR("__amp_reconn_thread: no connection corresponding to type:%d, id:%d\n", type, id);
                    pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
                    continue;
                }

                if (cnq->active_conn_num <= 0) {
                    AMP_ERROR("__amp_reconn_thread: type:%d, id:%d, active_conn_num:%d ,wrong\n", type, id, cnq->active_conn_num);
                    pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
                    continue;
                }

                for(i=0; i<cnq->active_conn_num; i++) {
                    tmpconn = cnq->conns[i];
                    if (tmpconn && (tmpconn != conn)) {
                        if(tmpconn->ac_state == AMP_CONN_OK){
                            has_valid_conn ++;
                            force_notify = 0;
                            no_conns = 0;
                        }
                    }
                }

                pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
                AMP_ERROR("__amp_reconn_thread: type:%d,id:%d,has_vaid_conn:%d\n", \
                                          type, id, has_valid_conn);
                
                __amp_remove_resend_reqs(conn, force_notify, no_conns);
                __amp_remove_waiting_reply_reqs(conn, force_notify, no_conns);
            }
            sleep(1);
            continue;       
        }
        

        amp_lock(&amp_reconn_conn_list_lock);
        amp_lock(&conn->ac_lock);
        conn->ac_state = AMP_CONN_OK;
        last_conn_time = conn->ac_last_reconn;
        conn->ac_last_reconn = time(NULL);
        last_conn_time = conn->ac_last_reconn - last_conn_time;
        conn->ac_remain_times = AMP_CONN_RECONN_MAXTIMES;
        list_del_init(&conn->ac_reconn_list);
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_reconn_conn_list_lock);
        AMP_ERROR("amp_reconn_thread: reconn success, connection lost during %lld seconds\n",last_conn_time);
        
        amp_lock(&amp_waiting_reply_list_lock);
        list_for_each_safe(pos, nxt, &amp_waiting_reply_list) 
        {
            req = list_entry(pos, amp_request_t, req_list);
            if((req->req_remote_type == conn->ac_remote_comptype) && 
                (req->req_remote_id == conn->ac_remote_id) &&
                (req->req_conn == NULL ||  req->req_conn == conn))
            {

                //amp_lock(&req->req_lock);
                list_del_init(&req->req_list);
                //amp_unlock(&req->req_lock);
                __amp_add_resend_req(req);
            }
        }
        amp_unlock(&amp_waiting_reply_list_lock);

        /*
         * revoke all need resend requests
         */
        __amp_revoke_resend_reqs (conn);
        __amp_add_to_listen_fdset(conn);

        if (threadp->at_shutdown) 
            goto EXIT;
    }

    if (threadp->at_shutdown)
        goto EXIT;
    amp_sem_up(&amp_reconn_finalize_sem);
    goto DOWN_SEM;      


EXIT:
    AMP_LEAVE("__amp_reconn_thread: leave: %ld\n", pthread_self());
    amp_sem_up(&threadp->at_downsem);
    return 0;
}

/*
 * the callback of network mornitor thread.
 * 
 */
struct {
    struct cmsghdr cm;
    struct in_pktinfo ipi;
} scmsg = {{sizeof(struct cmsghdr) + sizeof(struct in_pktinfo), SOL_IP, IP_PKTINFO}, {0, }};
#ifndef ICMP_FILTER
#define ICMP_FILTER  1
struct icmp_filter {
    amp_u32_t   data;
};
#endif

void __amp_install_filter(amp_s32_t icmp_sock, amp_s32_t ident)
{
    static struct sock_filter insns[] = {
            BPF_STMT(BPF_LDX|BPF_B|BPF_MSH, 0), 
            BPF_STMT(BPF_LD|BPF_H|BPF_IND, 4), 
            BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0xAAAA, 0, 1), 
            BPF_STMT(BPF_RET|BPF_K, ~0U),
            BPF_STMT(BPF_LD|BPF_B|BPF_IND, 0), 
            BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, ICMP_ECHOREPLY, 1, 0), 
            BPF_STMT(BPF_RET|BPF_K, 0xFFFFFFF), 
            BPF_STMT(BPF_RET|BPF_K, 0) 
        };
    static struct sock_fprog filter = {
        sizeof insns / sizeof(insns[0]),
        insns
    };

    /* Patch bpflet for current identifier. */
    insns[2] = (struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, htons(ident), 0, 1);

    if (setsockopt(icmp_sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)))
        AMP_ERROR("install_filter: failed to install socket filter\n");
}

void *
__amp_netmorn_thread2 (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_connection_t   *conn = NULL;
    amp_comp_context_t *ctxt = NULL;
    struct list_head *head = NULL;
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_u32_t        type;
    amp_u32_t        id;

    AMP_ENTER("__amp_netmorn_thread2: enter, %ld\n", pthread_self());

    threadp = (amp_thread_t *)argv;
    ctxt = (amp_comp_context_t *)(threadp->at_provite);
    threadp->at_isup= 1;
    threadp->at_thread_id = pthread_self();
    amp_sem_up(&threadp->at_startsem);

DOWN_SEM:
    amp_sem_down(&amp_netmorn_sem);
    AMP_DMSG("__amp_netmorn_thread2: wake up\n");

    for (type=0; type<AMP_MAX_COMP_TYPE; type++) {
        if (!ctxt->acc_conns[type].acc_remote_conns) {
            AMP_DMSG("__amp_netmorn_thread2: no acc_remote_conns for type:%d\n", type);
            continue;
        }
        if (!ctxt->acc_conns[type].acc_alloced_num) {
            AMP_DMSG("__amp_netmorn_thread2: acc_alloced_num is zero for type:%d\n", type);
            continue;
        }
        
        for (id=1; id<ctxt->acc_conns[type].acc_alloced_num; id++) {
            head = &(ctxt->acc_conns[type].acc_remote_conns[id].queue);
            if(list_empty(head)) {
                continue;
            }
            AMP_DMSG("__amp_netmorn_thread2: type:%d, id:%d\n", type, id);

            pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
            list_for_each_safe(pos, nxt, head){ 
                conn = list_entry(pos, amp_connection_t, ac_list);
                if (threadp->at_shutdown) {
                    AMP_ERROR("__amp_netmorn_thread2: before recvmsg, tell us to down\n");
                    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
                    goto EXIT;
                }

                if (conn->ac_state == AMP_CONN_OK) {
                    amp_lock(&conn->ac_lock); 
                    if(!__amp_conn_test(conn)){
                        amp_unlock(&conn->ac_lock);
                        continue;
                    }
                    AMP_ERROR("__amp_netmorn_thread2: conn(%d, %d): %p is not success, disconnect and reconn\n", type, id, conn);
                    AMP_OP(conn->ac_type, proto_disconnect)(&conn->ac_sock);
                    if (conn->ac_need_reconn)
                        conn->ac_state = AMP_CONN_RECOVER;
                    else
                        conn->ac_state = AMP_CONN_CLOSE;
                    amp_unlock(&conn->ac_lock);

                    amp_lock(&amp_reconn_conn_list_lock);
                    amp_lock(&conn->ac_lock);
                    if (list_empty(&conn->ac_reconn_list)) {
                        AMP_ERROR("__amp_netmorn_thread: add conn:%p to reconn list\n", conn);
                        list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                    }
                    amp_unlock(&conn->ac_lock);
                    amp_unlock(&amp_reconn_conn_list_lock);
                }
            }
            pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
            
            amp_sem_up(&amp_reconn_sem);//process the invalid conn in time

        }
    }

    /*
     * sleep for some time
     */
    if (threadp->at_shutdown)
        goto EXIT;

    goto DOWN_SEM;      

EXIT:
    AMP_LEAVE("__amp_netmorn_thread2: leave: %ld\n", pthread_self());
    amp_sem_up(&threadp->at_downsem);

    return NULL;
}


void *
__amp_netmorn_thread (void *argv)
{
    amp_thread_t *threadp = NULL;
    amp_connection_t   *conn = NULL;
    amp_comp_context_t *ctxt = NULL;
    amp_s32_t        err = 0;
    amp_u32_t     seqno;
    struct list_head *head = NULL;
    amp_s8_t         *sndbufp = NULL;
    amp_s8_t         *rcvbufp = NULL;
    conn_queue_t     *cnq = NULL;
    amp_u32_t        type;
    amp_u32_t        id;
    amp_u32_t       *raddr = NULL;
    amp_u32_t       *failaddr = NULL;
    amp_u32_t        raddr_num;
    amp_u32_t        failaddr_num;
    amp_u32_t        size;
    amp_s32_t        i, j;
    amp_s32_t        icmp_sock = -1;
    struct icmphdr   *icp = NULL;
    struct sockaddr_in whereto;
    struct sockaddr_in target;
    struct iovec      iov;
    struct msghdr     msg;
    struct timeval    tv;
    struct ip        *ip;
    amp_s32_t         ping_try_times;
    amp_s32_t         iphdrlen;
    amp_u32_t         datalen = 64;
    amp_s32_t         hold;
    amp_u16_t         ident = 0;
    amp_u16_t         seq = 0;
    struct icmp_filter  filt;
    amp_u32_t         sleep_time;
    
    AMP_ENTER("__amp_netmorn_thread: enter, %ld\n", pthread_self());

    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;
    ctxt = (amp_comp_context_t *)(threadp->at_provite);

    threadp->at_isup= 1;

    threadp->at_thread_id = pthread_self();

    amp_sem_up(&threadp->at_startsem);

    /*
     * doing main work
     */
    size = sizeof(amp_u32_t) * AMP_CONN_MAXIP_PER_NODE;
    raddr = (amp_u32_t *)malloc(size);
    if (!raddr) {
        AMP_ERROR("__amp_netmorn_thread: alloc for raddr error\n");
        goto EXIT;
    }
    memset(raddr, 0, size);
    failaddr = (amp_u32_t *)malloc(size);
    if (!failaddr) {
        AMP_ERROR("__amp_netmorn_thread: alloc for fail addr error\n");
        goto EXIT;
    }
    memset(failaddr, 0, size);
    raddr_num = 0;
    failaddr_num = 0;

    ident = ctxt->acc_this_type << 8;
    ident += ctxt->acc_this_id;
    
    AMP_DMSG("__amp_netmorn_thread: this type:%d, this id:%d, ident:%d\n",\
                 ctxt->acc_this_type, ctxt->acc_this_id, ident);
    
    sndbufp = (amp_s8_t *)malloc(4096);
    if (!sndbufp) {
        AMP_ERROR("__amp_netmorn_thread: alloc for buffer error\n");
        goto EXIT;
    }
    
    rcvbufp = (amp_s8_t *)malloc(4096);
    if (!rcvbufp) {
        AMP_ERROR("__amp_netmorn_thread: alloc for buffer error\n");
        goto EXIT;
    }

DOWN_SEM:
    amp_sem_down(&amp_netmorn_sem);
    AMP_DMSG("__amp_netmorn_thread: wake up\n");

    for (type=0; type<AMP_MAX_COMP_TYPE; type++) {
        if (!ctxt->acc_conns[type].acc_remote_conns) {
            AMP_DMSG("__amp_netmorn_thread: no acc_remote_conns for type:%d\n", type);
            continue;
        }
        if (!ctxt->acc_conns[type].acc_alloced_num) {
            AMP_DMSG("__amp_netmorn_thread: acc_alloced_num is zero for type:%d\n", type);
            continue;
        }
        
        for (id=1; id<ctxt->acc_conns[type].acc_alloced_num; id++) {
            head = &(ctxt->acc_conns[type].acc_remote_conns[id].queue);
            if(list_empty(head)) {
                continue;
            }
            AMP_DMSG("__amp_netmorn_thread: type:%d, id:%d\n", type, id);

            pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
            cnq = &(ctxt->acc_conns[type].acc_remote_conns[id]);
            raddr_num = 0;
            for (i=0; i<cnq->active_conn_num; i++) {
                conn = cnq->conns[i];
                if (!conn)
                    continue;

                AMP_DMSG("__amp_netmorn_thread: conn:%p\n", conn);
                if (conn->ac_state == AMP_CONN_OK) {
                    for (j=0; j<raddr_num; j++) {
                        if (raddr[j] == conn->ac_remote_ipaddr) 
                            break;
                    }
                    if (j>=raddr_num) {
                        AMP_DMSG("__amp_netmorn_thread: ipaddr:%d\n", \
                                                         conn->ac_remote_ipaddr);

                        raddr[raddr_num] = conn->ac_remote_ipaddr;
                        raddr_num ++;
                        if (raddr_num > AMP_CONN_MAXIP_PER_NODE) {
                            AMP_ERROR("__amp_netmorn_thread: radd_num:%d, too large\n", \
                                                                  raddr_num);
                            break;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
            
            /*do icmp*/
            failaddr_num = 0;
            for (i=0; i<raddr_num; i++) {
                ping_try_times =1;
                icmp_sock = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP);
                if (icmp_sock < 0) {
                    AMP_ERROR("__amp_netmorn_thread: create icmp sock error, err:%d\n", errno);
                    goto EXIT;
                } 
                hold = 65536;
                err = setsockopt(icmp_sock, SOL_SOCKET, SO_SNDBUF, (amp_s8_t *)&hold, sizeof(hold));
                if (err) {
                    AMP_ERROR("__amp_netmorn_thread: set send buffer error, err:%d\n", errno);
                    goto EXIT;
                }
                err = setsockopt(icmp_sock, SOL_SOCKET, SO_RCVBUF, (amp_s8_t *)&hold, sizeof(hold));
                if (err) {
                    AMP_ERROR("__amp_netmorn_thread: set recv buffer error, err:%d\n", errno);
                    goto EXIT;
                }
    
                tv.tv_usec = 0;
                tv.tv_sec = AMP_ETHER_SNDTIMEO;
                err = setsockopt(icmp_sock, SOL_SOCKET, SO_SNDTIMEO, (amp_s8_t *)&tv, sizeof(tv));
                if (err) {
                    AMP_ERROR("__amp_netmorn_thread: set send timeout error, err:%d\n", errno);
                    goto EXIT;
                }

                tv.tv_sec = AMP_ETHER_RCVTIMEO;
                err = setsockopt(icmp_sock, SOL_SOCKET, SO_RCVTIMEO, (amp_s8_t *)&tv, sizeof(tv));
                if (err) {
                    AMP_ERROR("__amp_netmorn_thread: set recv timeout error, err:%d\n", errno);
                    goto EXIT;
                }
                filt.data = ~((1<<ICMP_SOURCE_QUENCH)|
                                              (1<<ICMP_TIME_EXCEEDED)|
                                              (1<<ICMP_PARAMETERPROB)|
                                              (1<<ICMP_REDIRECT)|
                                              (1<<ICMP_ECHOREPLY));
    
                err = setsockopt(icmp_sock, SOL_RAW, ICMP_FILTER, (char*)&filt, sizeof(filt));
                if (err) {
                    AMP_ERROR("__amp_netmorn_thread: set icmp filter error, err:%d\n", errno);
                    goto EXIT;
                }
                __amp_install_filter(icmp_sock, ident);

                gettimeofday(&tv, NULL);
                memset(&whereto, 0, sizeof(whereto));
                whereto.sin_family = AF_INET;
                whereto.sin_addr.s_addr = htonl(raddr[i]);
                memset(sndbufp, 0, 4096);
                icp = (struct icmphdr *)sndbufp;
                icp->type = ICMP_ECHO;
                icp->code = 0;
                icp->checksum = 0;
                seq ++;
                icp->un.echo.id = ident;
                icp->un.echo.sequence = seq;
                datalen = 64;
                icp->checksum = __amp_nm_cksum((amp_u16_t *)icp, datalen);
                iov.iov_len = datalen;
                iov.iov_base = sndbufp;
                msg.msg_name = &whereto;
                msg.msg_namelen = sizeof(whereto);
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = &scmsg;
                msg.msg_controllen = sizeof(scmsg);
                msg.msg_flags = 0;
                AMP_DMSG("__amp_netmorn_thread: raddr:%d, id:%d, seq:%d\n", raddr[i], ident, seq);

                sleep_time = 0;
SEND_AGAIN:
                if (threadp->at_shutdown) {
                    AMP_ERROR("__amp_netmorn_thread: before sendmsg, tell us to down\n");
                    goto EXIT;
                }
                
                err = sendmsg(icmp_sock, &msg, 0);
                if (err < 0) {
                    if (errno == EAGAIN || errno == EINTR){
                        sleep_time++;
                        sleep(2);
                        if(sleep_time < AMP_CONN_RETRY_TIMES)
                            goto SEND_AGAIN;
                    }
                    AMP_ERROR("__amp_netmorn_thread: contact with node, type:%d,id:%d, error:%d\n", \
                                                  type, id, errno);
                    failaddr[failaddr_num] = raddr[i];
                    failaddr_num ++;
                    close(icmp_sock);
                    icmp_sock = -1;
                    continue;
                }
                
                /*to receive ack from remote icmp*/
                AMP_DMSG("__amp_netmorn_thread: recv icmp msg\n");

                sleep_time = 0;
RECV_AGAIN:
                if (threadp->at_shutdown) {
                    AMP_ERROR("__amp_netmorn_thread: before recvmsg, tell us to down\n");
                    goto EXIT;
                }

                memset(rcvbufp, 0, 4096);
                iov.iov_base = rcvbufp;
                iov.iov_len = 4096;
                msg.msg_name = (void *)&target;
                msg.msg_namelen = sizeof(target);
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_flags = MSG_DONTWAIT;
                msg.msg_control = NULL;
                msg.msg_controllen = 0;
                //MSG_DONTWAIT IS NON-BLOCK, IF THE NETWORK IS DELAY, RECVMSG RETURN -1
                err = recvmsg(icmp_sock, &msg, MSG_DONTWAIT);
                if (err < 0) {
                    if (errno==EAGAIN || errno==EINTR) {
                        sleep_time++;
                        sleep(2);
                        if (sleep_time < AMP_CONN_RETRY_TIMES)
                            goto RECV_AGAIN;
                    }
//REMOTE_FAILED:
                    AMP_ERROR("__amp_netmorn_thread: recv from node type:%d,id:%d error:%d\n", \
                                                  type, id, err);
                    failaddr[failaddr_num] = raddr[i];
                    failaddr_num ++;
                    close(icmp_sock);
                    icmp_sock = -1;
                    continue;
                }
                AMP_DMSG("__amp_netmorn_thread: recv err:%d\n", err);

                ip = (struct ip *)rcvbufp;
                iphdrlen = ip->ip_hl << 2;
                icp = (struct icmphdr *)(rcvbufp + iphdrlen);
                if ((err - iphdrlen) < sizeof(struct icmphdr)) {
                    AMP_ERROR("__amp_netmorn_thread: icmp packet is too small, type:%d,id:%d\n", \
                                                  type, id);
                    failaddr[failaddr_num] = raddr[i];
                    failaddr_num ++;
                    close(icmp_sock);
                    icmp_sock = -1;
                    continue;
                }
                if (icp->type == ICMP_ECHO){
                    AMP_DMSG("__amp_netmorn_thread: get echo, id:%d, seq:%d\n", \
                                                 icp->un.echo.id, icp->un.echo.sequence);
                    goto RECV_AGAIN;

                }
                if ((icp->type == ICMP_ECHOREPLY) && (icp->un.echo.id == ident)) {
                    if (icp->un.echo.sequence == seq) {
                        AMP_DMSG("__amp_netmorn_thread: get reponse from peer, type:%d, id:%d, seq:%d\n", 
                                                         type, icp->un.echo.id, icp->un.echo.sequence);
                    } else {
                        AMP_ERROR("__amp_netmorn_thread: remote node, type:%d,id:%d, unreachable\n", type, id); 
                        AMP_ERROR("__amp_netmorn_thread: icp type:%d, id:%d, seq:%d\n", \
                                                          icp->type, icp->un.echo.id, icp->un.echo.sequence);
                        AMP_ERROR("__amp_netmorn_thread: but ident:%d, seq:%d\n", ident, seq);
                        failaddr[failaddr_num] = raddr[i];
                        failaddr_num ++;
                    }
                    close(icmp_sock);
                    icmp_sock = -1;
                } else {
                    AMP_ERROR("__amp_netmorn_thread: get reponse, type:%d, id:%d, seq:%d\n", \
                                                  type, icp->un.echo.id, icp->un.echo.sequence);
                    AMP_ERROR("__amp_netmorn_thread: we want:id:%d, seq:%d\n", ident, seq);
                    goto RECV_AGAIN;
                }

            }
            AMP_DMSG("__amp_netmorn_thread: type:%d, id:%d, failaddr_num:%d\n", type, id, failaddr_num);

            if (failaddr_num) {
                pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
                /*set some connection to be reconn*/
                for (i=0; i<cnq->active_conn_num; i++) {
                    conn = cnq->conns[i];
                    if (!conn)
                        continue;
                    
                    if (conn->ac_state != AMP_CONN_OK) {
                        continue;
                    }
                    for (j=0; j<failaddr_num; j++) {
                        if (conn->ac_remote_ipaddr == failaddr[j]) {
                            AMP_ERROR("__amp_netmorn_thread: failed conn:%p\n", conn);
                            AMP_OP(conn->ac_type, proto_disconnect)(&conn->ac_sock);
                            amp_lock(&amp_reconn_conn_list_lock);
                            amp_lock(&conn->ac_lock);

                            if (conn->ac_need_reconn)
                                conn->ac_state = AMP_CONN_RECOVER;
                            else
                                conn->ac_state = AMP_CONN_CLOSE;

                            /*conn->ac_datard_count = 0;
                            conn->ac_sched = 0;*/

                            if (list_empty(&conn->ac_reconn_list)) {
                                AMP_ERROR("__amp_netmorn_thread: add conn:%p to reconn list\n", conn);
                                list_add_tail(&conn->ac_reconn_list, &amp_reconn_conn_list);
                            }
                            amp_unlock(&conn->ac_lock);
                            amp_unlock(&amp_reconn_conn_list_lock);
                        }
                    }
                }

                pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
                amp_sem_up(&amp_reconn_sem);//process the invalid conn in time
            }
        }
    }

    /*
     * sleep for some time
     */
    if (threadp->at_shutdown)
        goto EXIT;

    goto DOWN_SEM;      

EXIT:
    
    if (icmp_sock > 0) 
        close(icmp_sock);

    if (raddr)
        free(raddr);
    if (failaddr)
        free(failaddr);
    if (sndbufp)
        free(sndbufp);
    if (rcvbufp)
        free(rcvbufp);

    AMP_LEAVE("__amp_netmorn_thread: leave: %ld\n", pthread_self());
    amp_sem_up(&threadp->at_downsem);

    return NULL;
}


// by Chen Zhuan at 2009-02-05

#ifdef __AMP_LISTEN_EPOLL
//#define EPOLL_EVENT_SIZE 512 
#define EPOLL_EVENT_SIZE AMP_CONN_ADD_INCR
#endif


/*
 * thread for listen 
 */
void* __amp_listen_thread (void *argv)
{
    amp_thread_t  *threadp = NULL;
    amp_u32_t  seqno;
    amp_connection_t  *conn;
    amp_connection_t  *new_connp = NULL;
    amp_u32_t  conn_type;  /*tcp or udp*/
    amp_s32_t  err = 0;
    amp_comp_context_t  *ctxt = NULL;
    amp_s32_t  listen_sockfd;
    amp_u32_t  i;
    
// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT

    fd_set  cur_fdset;
    amp_u32_t  maxfd;

#endif
#ifdef __AMP_LISTEN_POLL

    amp_u32_t  maxfd;

#endif
#ifdef __AMP_LISTEN_EPOLL

    amp_s32_t  fd = 0;
    amp_s32_t  nfds = 0;
    struct epoll_event  /*ev,*/ events[EPOLL_EVENT_SIZE];

#endif
// -----------------------------------------------------------------

    AMP_ERROR("__amp_listen_thread: enter, %ld\n", pthread_self());
    threadp = (amp_thread_t *)argv;
    seqno = threadp->at_seqno;

    threadp->at_isup= 1;

    threadp->at_thread_id = pthread_self();
    amp_sem_up(&threadp->at_startsem);

    ctxt = (amp_comp_context_t *) (threadp->at_provite);
    if (!ctxt) {
        AMP_ERROR("__amp_listen_thread: no ctxt\n");
        goto EXIT;
    }

    conn_type = AMP_CONN_TYPE_TCP;

    while (1)
    {
        pthread_mutex_lock(&ctxt->acc_lock);
        conn = ctxt->acc_listen_conn;
        listen_sockfd = amp_listen_sockfd;
// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT
        cur_fdset = ctxt->acc_readfds;
        maxfd = ctxt->acc_maxfd;
#endif
#ifdef __AMP_LISTEN_POLL
        maxfd = ctxt->acc_maxfd;
#endif
// -----------------------------------------------------------------
        pthread_mutex_unlock(&ctxt->acc_lock);


// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT
        err = select( ctxt->acc_maxfd+1, &cur_fdset, NULL, NULL, NULL );
#endif
#ifdef __AMP_LISTEN_POLL
        err = poll( ctxt->acc_poll_list, ctxt->acc_maxfd+1, -1 );
        // amp_fd_print( ctxt->acc_poll_list, ctxt->acc_poll_size );
        // AMP_ERROR( "ctxt->acc_poll_size=%d, poll return %d\n", ctxt->acc_poll_size, err );
#endif
#ifdef __AMP_LISTEN_EPOLL
        nfds = epoll_wait( ctxt->acc_epfd, events, EPOLL_EVENT_SIZE, -1 );
        err = nfds;
#endif
// -----------------------------------------------------------------

        if (err < 0) {
            AMP_WARNING("__amp_listen_thread: select returned:%d\n", err);
            continue;
        }

// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT
        if( FD_ISSET( ctxt->acc_srvfd, &cur_fdset ) ) {
            amp_s32_t  notifyid;
            read(ctxt->acc_srvfd, &notifyid, sizeof(amp_s32_t));
            if (threadp->at_shutdown)
                goto EXIT;
            //FD_CLR(ctxt->acc_srvfd, &cur_fdset);
        }
#endif
#ifdef __AMP_LISTEN_POLL
        if( amp_poll_fd_isset( ctxt->acc_srvfd, ctxt->acc_poll_list ) ) {
            amp_s32_t  notifyid;
            read(ctxt->acc_srvfd, &notifyid, sizeof(amp_s32_t));
            if (threadp->at_shutdown)
                goto EXIT;
            //amp_fd_clr( ctxt->acc_srvfd, ctxt->acc_poll_list, ctxt->acc_poll_size );
        }
#endif
#ifdef __AMP_LISTEN_EPOLL
        if( amp_epoll_fd_isset( ctxt->acc_srvfd, events, nfds ) ) {
            amp_s32_t  notifyid;
            read( ctxt->acc_srvfd, &notifyid, sizeof(amp_s32_t) );
            if( threadp->at_shutdown )
                goto EXIT;
            /*for( i=0; i<nfds; i++ ) {
                if( events[i].data.fd == ctxt->acc_srvfd ) {
                    events[i].data.fd = -1;
                }
            }*/
        }
#endif
// -----------------------------------------------------------------


// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT
        if((listen_sockfd >= 0) && (FD_ISSET(listen_sockfd, &cur_fdset)))
#endif
#ifdef __AMP_LISTEN_POLL
        if((listen_sockfd >= 0) && (amp_poll_fd_isset( listen_sockfd, ctxt->acc_poll_list )))
#endif
#ifdef __AMP_LISTEN_EPOLL
        if((listen_sockfd >= 0) && (amp_epoll_fd_isset( listen_sockfd, events, nfds )))
#endif
// -----------------------------------------------------------------

        {
ALLOC_AGAIN:
            new_connp = NULL;
            err = __amp_alloc_conn(&new_connp);
            if (err)  {
                AMP_ERROR("__amp_listen_thread: alloc new connection error, err:%d\n", err);
                goto ALLOC_AGAIN;
            }

            new_connp->ac_type = AMP_CONN_TYPE_TCP;
            new_connp->ac_need_reconn = 1;//by weizheng 2013-11-19,reconn
            new_connp->ac_ctxt = ctxt;
            new_connp->ac_this_id = ctxt->acc_this_id;
            new_connp->ac_this_type = ctxt->acc_this_type;
            new_connp->ac_allocpage_cb = conn->ac_allocpage_cb;
            new_connp->ac_freepage_cb = conn->ac_freepage_cb;
            new_connp->ac_queue_cb = conn->ac_queue_cb;
            err = __amp_accept_connection (&listen_sockfd, new_connp);

            if (err < 0 && err != -ECONNABORTED) {
                AMP_ERROR("__amp_listen_thread: accept a new connection error, err:%d\n", err);
                __amp_free_conn(new_connp);
                goto LISTEN_DATA;
            }

            //__mp_dequeue_invalid_conn(new_connp,ctxt);//insert by weizheng 2014-01-19, before accept the new conn, dequeue the invalid conn
#ifdef __AMP_CONNS_DUPLEX
		if(new_connp->ac_duplex){
			err = __amp_enqueue_recv_conn(new_connp, ctxt);
			if(err < 0){
				AMP_ERROR("__amp_listen_thread: enqueue the recv connection error, err = %d\n", err);
                		AMP_OP(conn_type, proto_disconnect)(&new_connp->ac_sock);
                		memset(new_connp, 0, sizeof(amp_connection_t));
                		__amp_dequeue_recv_conn(new_connp,ctxt);
                		__amp_free_conn(new_connp);
                		goto LISTEN_DATA;
			}
		}else{
			err = __amp_enqueue_conn(new_connp, ctxt);
            		if (err < 0)  {
                		AMP_ERROR("__amp_listen_thread: enqueue the connection error, err = %d\n", err);
                		AMP_OP(conn_type, proto_disconnect)(&new_connp->ac_sock);
                		memset(new_connp, 0, sizeof(amp_connection_t));
                		__amp_dequeue_conn(new_connp,ctxt);
                		__amp_free_conn(new_connp);
                		goto LISTEN_DATA;
            		}

		}
		/*at this monent, we need to connect the client, organize the duplex conns*/
		/*add by weizheng, 20170223*/
		/*now when sys init, each componect connect each other*/
	
#else
            err = __amp_enqueue_conn(new_connp, ctxt);
            if (err < 0)  {
                AMP_ERROR("__amp_listen_thread: enqueue the connection error, err = %d\n", err);
                AMP_OP(conn_type, proto_disconnect)(&new_connp->ac_sock);
                memset(new_connp, 0, sizeof(amp_connection_t));
                __amp_dequeue_conn(new_connp,ctxt);
                __amp_free_conn(new_connp);
                goto LISTEN_DATA;
            }
#endif
            __amp_add_to_listen_fdset(new_connp);
            __amp_revoke_resend_reqs (new_connp);
            AMP_WARNING("__amp_listen_thread: finish accept conn:%p, remote_id:%d, remote_type:%d, sock:%d,refcount:%d\n", \
                                  new_connp, \
                                  new_connp->ac_remote_id, \
                                  new_connp->ac_remote_comptype,\
                                  new_connp->ac_sock, \
                                  new_connp->ac_refcont);
        }
LISTEN_DATA:
// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT
        for( i=0; i<(maxfd+1); i++ ) {
            amp_connection_t *tmpconn = NULL;           
            if (i == listen_sockfd)
                continue;
            if (i == ctxt->acc_srvfd)
                continue;
            if(!FD_ISSET(i, &cur_fdset)) 
                continue;
            
            pthread_mutex_lock(&ctxt->acc_lock);
            FD_CLR(i, &ctxt->acc_readfds);
            tmpconn = ctxt->acc_conn_table[i];
            if (!tmpconn) {
                AMP_ERROR("__amp_listen_thread: no connection for fd:%d\n", i);
                pthread_mutex_unlock(&ctxt->acc_lock);
                continue;
            }
            pthread_mutex_unlock(&ctxt->acc_lock);
            
            amp_lock(&amp_dataready_conn_list_lock);
            amp_lock(&tmpconn->ac_lock);
            if(list_empty(&tmpconn->ac_dataready_list))
                list_add_tail(&tmpconn->ac_dataready_list, &amp_dataready_conn_list);
#ifndef __AMP_SOCKET_POOL
            //tmpconn->ac_stage = AMP_CONN_SELECTED;
#endif
            amp_unlock(&tmpconn->ac_lock);
            amp_unlock(&amp_dataready_conn_list_lock);
            amp_sem_up(&amp_process_in_sem);
        }
#endif
#ifdef __AMP_LISTEN_POLL
        for( i=0; i<(maxfd+1); i++ ) {
            amp_connection_t *tmpconn = NULL;
            if( i == listen_sockfd )
                continue;
            if( i == ctxt->acc_srvfd )
                continue;
            if( !amp_poll_fd_isset( i, ctxt->acc_poll_list ) )
                continue;
            
            pthread_mutex_lock(&ctxt->acc_lock);
            amp_poll_fd_clr( i, ctxt->acc_poll_list );
            tmpconn = ctxt->acc_conn_table[i];
            if(!tmpconn){
                AMP_ERROR("__amp_listen_thread: no connection for fd:%d\n", i);
                pthread_mutex_unlock(&ctxt->acc_lock);
                continue;
            }
            pthread_mutex_unlock(&ctxt->acc_lock);
            
            AMP_ERROR("__amp_listen_thread: listen the conn, receive ......\n");

            amp_lock(&amp_dataready_conn_list_lock);
            amp_lock(&tmpconn->ac_lock);
            if(list_empty(&tmpconn->ac_dataready_list))
                list_add_tail(&tmpconn->ac_dataready_list, &amp_dataready_conn_list);
#ifndef __AMP_SOCKET_POOL
            //tmpconn->ac_stage = AMP_CONN_SELECTED;
#endif
            amp_unlock(&tmpconn->ac_lock);
            amp_unlock(&amp_dataready_conn_list_lock);
            amp_sem_up(&amp_process_in_sem);
        }
#endif

#ifdef __AMP_LISTEN_EPOLL
        for( i=0; i<nfds; i++ ) {
            fd = events[i].data.fd;
            amp_connection_t *tmpconn = NULL;
            if( fd < 0 )
                continue;
            if( fd == listen_sockfd )
                continue;
            if( fd == ctxt->acc_srvfd )
                continue;

            pthread_mutex_lock(&ctxt->acc_lock);
            amp_epoll_fd_clear( fd, ctxt->acc_epfd );
            tmpconn = ctxt->acc_conn_table[fd];
            if (!tmpconn) {
                AMP_ERROR( "__amp_listen_thread: no connection for fd:%d, i=%d\n", fd, i );
                pthread_mutex_unlock(&ctxt->acc_lock);
                continue;
            }
            pthread_mutex_unlock(&ctxt->acc_lock);
	    AMP_ERROR("SOCKET dataready ??\n");
            
            amp_lock(&amp_dataready_conn_list_lock);
            amp_lock(&tmpconn->ac_lock);
            if(list_empty(&tmpconn->ac_dataready_list))
                list_add_tail(&tmpconn->ac_dataready_list, &amp_dataready_conn_list);
#ifndef __AMP_SOCKET_POOL
            //tmpconn->ac_stage = AMP_CONN_SELECTED;
#endif
            amp_unlock(&tmpconn->ac_lock);
            amp_unlock(&amp_dataready_conn_list_lock);
            amp_sem_up(&amp_process_in_sem);
        }
#endif
// -----------------------------------------------------------------

    }
    
EXIT:
    AMP_LEAVE("__amp_listen_thread: leave: %ld\n", pthread_self());
    amp_sem_up(&threadp->at_downsem);
    return NULL;
}


/* 
 * thread fundation initialization
 */ 
int
__amp_threads_init (amp_comp_context_t *ctxt)
{
    amp_s32_t err = 0;

    AMP_ENTER("__amp_threads_init: enter\n");

    amp_sem_init_locked(&amp_process_in_sem);
    amp_sem_init_locked(&amp_process_out_sem);
    amp_sem_init_locked(&amp_reconn_sem);
    amp_sem_init_locked(&amp_netmorn_sem);
    amp_sem_init(&amp_reconn_finalize_sem);
    
    
    amp_srvin_thread_num = 0;
    amp_srvout_thread_num = 0;
    amp_reconn_thread_num = 0;
    amp_wakeup_thread_num = 0;
    amp_listen_sockfd = -1;

    amp_lock_init(&amp_threads_lock);
    
    amp_srvin_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_MAX_THREAD_NUM);
    if (!amp_srvin_threads) {
        AMP_ERROR("__amp_threads_init: malloc for srvin threads error\n");
        err = -ENOMEM;
        goto EXIT;
    }

    amp_srvout_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_MAX_THREAD_NUM);
    if (!amp_srvout_threads) {
        AMP_ERROR("__amp_threads_init: malloc for srvout threads error\n");
        err = -ENOMEM;
        amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        goto EXIT;
    }
    
    amp_reconn_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_MAX_THREAD_NUM);
    if (!amp_reconn_threads) {
        AMP_ERROR("__amp_threads_init: malloc for reconn threads error\n");
        err = -ENOMEM;
        amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_srvout_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        goto EXIT;
    }

    amp_wakeup_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_MAX_THREAD_NUM);
    if (!amp_wakeup_threads) {
        AMP_ERROR("__amp_threads_init: malloc for wakeup threads error\n");
        err = -ENOMEM;
        amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_srvout_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_reconn_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        goto EXIT;
    }
#ifdef __TOKEN1__ 
    amp_token_generate_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_MAX_THREAD_NUM);
    if (!amp_token_generate_threads) {
        AMP_ERROR("__amp_threads_init: malloc for reconn threads error\n");
        err = -ENOMEM;
        amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_srvout_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_reconn_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        amp_free(amp_wakeup_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
        goto EXIT;
    }
    memset(amp_token_generate_threads, 0, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);

#endif
    memset(amp_srvin_threads, 0, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    memset(amp_srvout_threads, 0, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    memset(amp_reconn_threads, 0, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    memset(amp_wakeup_threads, 0, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    /*
     * startup all the threads;
     */ 
    err = __amp_start_srvin_threads (ctxt);
    if (err < 0) 
        goto ERROR;

    err = __amp_start_srvout_threads(ctxt);
    if (err < 0) {
        __amp_stop_srvin_threads();
        goto ERROR;
    }
  
    err = __amp_start_reconn_threads();
    if(err < 0){
        __amp_stop_srvin_threads();
        __amp_stop_srvout_threads();
        goto ERROR;
    }
#ifdef __TOKEN1__
    err = __amp_start_token_generate_thread(ctxt, 0);
    if(err < 0){
        __amp_stop_srvin_threads();
        __amp_stop_srvout_threads();
        __amp_stop_reconn_threads();
        goto ERROR;
    }
#endif
#ifdef __AMP_RDMA__
    amp_rdma_listen_group = (struct list_head *)amp_alloc(sizeof(struct list_head) * AMP_RDMA_LISTEN_THREAD_NUM);
    if(!amp_rdma_listen_group){
        AMP_ERROR("__amp_sys_init: malloc for rdma_listen_group error\n");
        err = -ENOMEM;
        goto EXIT;
    }

    memset(amp_rdma_listen_group, 0, sizeof(struct list_head) * AMP_RDMA_LISTEN_THREAD_NUM);

    int i = 0;
    for(i = 0; i < AMP_RDMA_LISTEN_THREAD_NUM; i++){
        INIT_LIST_HEAD(&amp_rdma_listen_group[i]);
    }



    amp_rdma_listen_threads = 
        (amp_thread_t *)amp_alloc(sizeof(amp_thread_t) * AMP_RDMA_LISTEN_THREAD_NUM);
    if (!amp_rdma_listen_threads) {
        AMP_ERROR("__amp_threads_init: malloc for rdma listen threads error\n");
        err = -ENOMEM;
        goto EXIT;
    }


    err = __amp_start_rdma_listen_threads(ctxt);
    if(err < 0){
        __amp_stop_srvin_threads();
        __amp_stop_srvout_threads();
        __amp_stop_reconn_threads();
        goto ERROR;
    }

#endif
EXIT:
    AMP_LEAVE("__amp_threads_init: leave\n");
    return err;

ERROR:
    amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_srvout_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_reconn_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_wakeup_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_srvin_threads = NULL;
    amp_srvout_threads = NULL;
    amp_reconn_threads = NULL;
    amp_wakeup_threads = NULL;
    goto EXIT;
}

/*
 * threads finalize
 */ 
int
__amp_threads_finalize ()
{
    AMP_ENTER("__amp_threads_finalize: enter\n");

    /*
     * stop all threads
     */ 
    __amp_stop_srvin_threads();
    __amp_stop_srvout_threads();
    __amp_stop_reconn_threads();
#ifdef __TOKEN1__
    __amp_stop_token_generate_threads();
#endif
    /*
     * free all resources
     */ 
    amp_free(amp_srvin_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_srvout_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_wakeup_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_free(amp_reconn_threads, sizeof(amp_thread_t)* AMP_MAX_THREAD_NUM);
    amp_srvin_threads = NULL;
    amp_srvout_threads = NULL;
    amp_reconn_threads = NULL;
    amp_wakeup_threads = NULL;

    AMP_LEAVE("__amp_threads_finalize: leave\n");
    return 0;
}

/*end of file*/
