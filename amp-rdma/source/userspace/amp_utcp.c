/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */
/*          Rongfeng Tang                      */
/***********************************************/
#include <amp_tcp.h>
#include <amp_thread.h>

int send_cnt = 0;
static uint64_t send_write_cnt = 0;
static uint64_t send_write_time = 0;

static inline int 
__amp_tcp_general_init (amp_s32_t sock)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    struct timeval tv;
    int option;
    struct linger linger;

    AMP_ENTER("__amp_tcp_general_init: enter\n");

    /*
     * set recv and send timeout
     */ 
    tv.tv_sec = AMP_ETHER_SNDTIMEO;
    tv.tv_usec = 0;

    rc = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (amp_s8_t*)&tv, sizeof(tv));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: set sndtimeo error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }

    tv.tv_sec = AMP_ETHER_RCVTIMEO;

    rc = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (amp_s8_t *)&tv, sizeof(tv));
    if (rc) { 
        AMP_ERROR("__amp_tcp_general_init: set rcvtimeout error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }

    //TODO
    /*
     * close nangle
     */ 
    option = 1;
    rc = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (amp_s8_t *)&option, sizeof(option));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: close nangle error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }

    /*
     * set send and rcv buffer size
     */ 
    option = AMP_ETHER_SNDBUF;
    rc = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (amp_s8_t *)&option, sizeof(option));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: set sndbuf error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }
    
    option = AMP_ETHER_RCVBUF;
    rc = setsockopt(sock, SOL_SOCKET, SO_RCVBUF,(amp_s8_t *)&option, sizeof(option));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: set rcvbuf error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }

    /*
     * close linger
     */ 
    linger.l_onoff = 1;
    linger.l_linger = 0;

    rc = setsockopt(sock, SOL_SOCKET, SO_LINGER, (amp_s8_t *)&linger, sizeof(linger));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: close linger error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }

    option = -1;
    rc = setsockopt(sock, SOL_TCP, TCP_LINGER2, (amp_s8_t *)&option,
           sizeof(option));
    if (rc) {
        AMP_ERROR("__amp_tcp_general_init: close linger2 error, rc:%d\n", errno);
        err = -errno;
        goto EXIT;
    }
    
    option = 1;
    rc = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&option, sizeof(option));
    if (rc < 0) {
        AMP_ERROR("__amp_tcp_general_init: set keepalive error, rc:%d, errno:%d\n", rc, errno);
        err = -errno;
        goto EXIT;
    }

    option = AMP_KEEP_IDLE;
    rc = setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, (char *)&option, sizeof(option));
    if (rc < 0) {
        AMP_ERROR("__amp_tcp_general_init: set keep idle error, rc:%d, errno:%d\n", rc, errno);
        err = -errno;
        goto EXIT;
    }

    option = AMP_KEEP_INTVL;
    rc = setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, (char *)&option, sizeof(option));
    if (rc < 0) {
        AMP_ERROR("__amp_tcp_general_init: set keep intvl error,rc:%d,errno:%d\n", rc, errno);
        err = -errno;
        goto EXIT;
    }

    option = AMP_KEEP_COUNT;
    rc = setsockopt(sock, SOL_TCP, TCP_KEEPCNT, (char *)&option, sizeof(option));
    if (rc < 0) {
        AMP_ERROR("__amp_tcp_general_init: set keep count error,rc:%d,errno:%d\n", rc, errno);
        err = -errno;
        goto EXIT;
    }
EXIT:
    AMP_LEAVE("__amp_tcp_general_init: leave\n");
    return err;
}

/* 
 * send data to peer by tcp protocol.
 * return: 0 - normal, <0 - abnormal
 */ 
int
__amp_tcp_sendmsg (void *protodata,
                   void *addr,
                   amp_u32_t addr_len,
                   amp_u32_t len, 
                   void *bufp,
                   amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_s32_t sock = -1;
    amp_s32_t  slen;
    amp_s8_t  *tmpbufp = NULL;
    amp_u32_t sleep_time = 0;    
    amp_connection_t *conn = (amp_connection_t *)protodata;

    sock = conn->ac_sock;

    AMP_DMSG("__amp_tcp_sendmsg enter, len:%d\n", len);

    //sock = *((amp_u32_t *)protodata);

    if (sock < 0) {
        AMP_ERROR("__amp_tcp_sendmsg: no sock\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!bufp || !len) {
        AMP_ERROR("__amp_tcp_sendmsg: no buffer\n");
        err = -EINVAL;
        goto EXIT;
    }

    tmpbufp = bufp;
    slen = len;

    while (slen) {
        sleep_time = 0;
RE_WRITE:
        rc = write(sock, tmpbufp, slen);
        if (rc <= 0) {
            AMP_ERROR("__amp_tcp_sendmsg: send msg error, errno:%d\n", errno);
            if (errno == EAGAIN || errno == EINTR){
                sleep_time ++;
                //sleep(2);
                usleep(5000);
                AMP_ERROR("__amp_tcp_sendmsg: send error, rc:%d, slen: %d, errno:%di, sleep_time: %d\n", rc, slen, errno, sleep_time);
                if(sleep_time < AMP_CONN_RETRY_TIMES)
                    goto RE_WRITE;
            }
            
            AMP_ERROR("__amp_tcp_sendmsg: sned error, slen: %d, rc: %d, errno: %d\n", slen, rc, errno);
            if (rc == 0)
                rc = -ECONNABORTED;

            err = -errno;
            goto EXIT;
        }
        
        tmpbufp += rc;
        slen -= rc;

        if (rc < slen) 
            AMP_WARNING("__amp_tcp_sendmsg: sent:%d, total_len: %d, slen:%d, less\n", rc, len, slen);

    }
EXIT:
    AMP_LEAVE("__amp_tcp_sendmsg: leave\n");
    return err;
}

#ifdef __AMP_RDMA__

#if 0
int
__amp_rdma_sendmsg_data (void *protodata, amp_u32_t msg_len, void *msg_bufp, amp_u32_t niov, amp_kiov_t *iov, amp_u64_t remote_add, amp_u32_t rkey, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;


    if (!msg_bufp || !msg_len) {
        AMP_ERROR("__amp_rdma_sendmsg: no buffer\n");
        err = -EINVAL;
        goto EXIT;
    }
    amp_s32_t rc = 0;
    amp_u32_t sleep_time = 0;
    amp_s8_t  *addrp = NULL;
    amp_kiov_t *kiov;
    amp_u32_t iovs;
    int i = 0;
    int len = 0;
 
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_wc wc;
    int poll_result = 0;
    struct timeval tv, tv1, tv2, tv3;

    //gettimeofday(&tv, NULL);
   
    //memset(&sge, 0, sizeof(sge));

    //memset(conn->ac_rdma_send_buf, 0, AMP_RDMA_MR_SIZE);

    memcpy(conn->ac_rdma_send_buf, msg_bufp, msg_len);
    int * tlen;
    tlen = (int *)((char *)conn->ac_rdma_send_buf + msg_len);
    * tlen = msg_len;

    kiov = iov;
    len = 4;
//struct timeval tv1, tv2;
//gettimeofday(&tv1, NULL);
    for(i = 0; i < niov; i++){
        addrp = (amp_s8_t *)kiov->ak_addr + kiov->ak_offset;
        memcpy(conn->ac_rdma_send_buf + msg_len + sizeof(int) + len, addrp, kiov->ak_len);
        len += kiov->ak_len;
        kiov ++;
    }

    int * size = (int *)(conn->ac_rdma_send_buf + msg_len + sizeof(int));
    *size = len - 4; 
    size = (int *)(conn->ac_rdma_send_buf + msg_len + sizeof(int) + len);
    *size = len - 4;



    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = msg_len + 4 + len + 4;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    //sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr - 1024 * 1024;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    //sr.opcode = IBV_WR_SEND_WITH_IMM;
    //sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }

    gettimeofday(&tv1, NULL); 
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;
//gettimeofday(&tv2,NULL);
    if(err){
        AMP_ERROR("__amp_rdma_sendmsg: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_cnt: %d\n", send_cnt);
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_len: %d, remote_type:%d, remote_id: %d\n", len, conn->ac_remote_comptype, conn->ac_remote_id);
    }

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    

gettimeofday(&tv3,NULL);
AMP_DMSG("__amp_rdma_sendmsg--exec_time: %ld\n", tv3.tv_usec - tv1.tv_usec + 1000000*(tv3.tv_sec - tv1.tv_sec));

//    AMP_ERROR("TIME statistic: %lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n", tv.tv_sec, tv.tv_usec, tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv3.tv_sec, tv3.tv_usec);

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_sendmsg: read complete queue failed\n");
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_sendmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

EXIT:
    AMP_LEAVE("__amp_rdma_sendmsg: leave\n");
    return err;



//gettimeofday(&tv2, NULL);
//AMP_ERROR("__AMP_RDMA_SENDDATA_WRITE: COPY USE TIME: %llu us\n", tv2.tv_usec - tv1.tv_usec + 1000000*(tv2.tv_sec - tv1.tv_sec));

    int * size = (int *)(conn->ac_rdma_send_buf + 1024 * 1024);
    *size = len - 4; 
    size = (int *)(conn->ac_rdma_send_buf + 1024 * 1024 + len);
    *size = len - 4;

    //sge.addr = (uint64_t)conn->ac_rdma_send_buf + 1024 * 1024;
    sge.addr = (uint64_t)conn->ac_rdma_send_buf + 1024 * 1024 + 4;
    //sge.length = len + 4;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    //sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    //sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 1024 * 1024;
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 4;
    //AMP_ERROR("__AMP_RDMA_SENDDATA_WRITE: REMOTE_ADDR:%p, senddata: %d\n", sr.wr.rdma.remote_addr, len + 4);
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    //sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_senddata_write: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_senddata_write: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_senddata_write: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

__amp_rdma_send_complete_msg(protodata, addr, addr_len, len-4, bufp, flags);

 
EXIT:
    AMP_LEAVE("__amp_rdma_senddata_write: leave\n");
    return err;

}
#endif
int
__amp_rdma_sendmsg (void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t len, void *bufp, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;


    //temp changed by mayl
    AMP_ERROR("__amp_rdma_sendmsg enter, len:%d\n", len);
    //AMP_DMSG("__amp_rdma_sendmsg enter, len:%d\n", len);

    if (!bufp || !len) {
        AMP_ERROR("__amp_rdma_sendmsg: no buffer\n");
        err = -EINVAL;
        goto EXIT;
    }

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_wc wc;
    int poll_result = 0;
    struct timeval tv, tv1, tv2, tv3;

    //gettimeofday(&tv, NULL);
   
    //memset(&sge, 0, sizeof(sge));

    //memset(conn->ac_rdma_send_buf, 0, AMP_RDMA_MR_SIZE);
    memcpy(conn->ac_rdma_send_buf, bufp, len);
    int * tlen;
    tlen = (int *)((char *)conn->ac_rdma_send_buf + len);
    * tlen = len;

    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = len + 4;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    //sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    //sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr - 1024 * 1024;
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    //sr.opcode = IBV_WR_SEND_WITH_IMM;
    //sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }

    gettimeofday(&tv1, NULL); 
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;
//gettimeofday(&tv2,NULL);
    if(err){
        AMP_ERROR("__amp_rdma_sendmsg: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_cnt: %d\n", send_cnt);
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_len: %d, remote_type:%d, remote_id: %d\n", len, conn->ac_remote_comptype, conn->ac_remote_id);
    }

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    

gettimeofday(&tv3,NULL);
AMP_DMSG("__amp_rdma_sendmsg--exec_time: %ld\n", tv3.tv_usec - tv1.tv_usec + 1000000*(tv3.tv_sec - tv1.tv_sec));

//    AMP_ERROR("TIME statistic: %lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n", tv.tv_sec, tv.tv_usec, tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv3.tv_sec, tv3.tv_usec);

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_sendmsg: read complete queue failed\n");
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_sendmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

EXIT:
    AMP_LEAVE("__amp_rdma_sendmsg: leave\n");
    return err;
}
	
int
__amp_rdma_sendmsg_idx (void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t len, void *bufp, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    //temp changed by mayl
    //AMP_ERROR("__amp_rdma_sendmsg_idx` enter, len:%d, conn %p \n", len, conn);


    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_wc wc;
    int poll_result = 0;
    volatile int remote_idx = conn->remote_msg_start_idx;
    struct timeval tv, tv1, tv2, tv3;
#if 0
    if(remote_idx >= 512 && conn->next_bitmap_start > 0){
    	while(0 == (err = __amp_rdma_recvsb_half(protodata, conn->remote_msg_end_idx / 8, (1016 - conn->remote_msg_end_idx) / 8)));
    }

    if(remote_idx >=  conn->remote_msg_end_idx){
    	while(0 == (err = __amp_rdma_recvsb(protodata)));
    	remote_idx = conn->remote_msg_start_idx;
    }
#endif
    memcpy((char *)conn->ac_rdma_send_buf + (remote_idx + AMP_RDMA_SB_BLK_NUM) * 1024, bufp, len);
    
    
    int * tlen;
    tlen = (int *)((char *)conn->ac_rdma_send_buf + (remote_idx + AMP_RDMA_SB_BLK_NUM) * 1024 + len);
    * tlen = len;

    sge.addr = (uint64_t)conn->ac_rdma_send_buf + (remote_idx + AMP_RDMA_SB_BLK_NUM) * 1024;
    sge.length = len + 4;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    sr.wr.rdma.remote_addr = (uint64_t)conn->ac_rdma_remote_config.addr + 1024 * (remote_idx + AMP_RDMA_SB_BLK_NUM);
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }

    gettimeofday(&tv1, NULL); 
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;
//gettimeofday(&tv2,NULL);
    if(err){
        AMP_ERROR("__amp_rdma_sendmsg: post_send failed, err: %d, errno: %d, len %d, send_cnt %d\n", err, errno, len, send_cnt);
    }else{
	    int qp_num = ((amp_message_t *)bufp)->qp_num;
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_cnt: %d\n", send_cnt);
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_len: %d, remote_type:%d, remote_id: %d, remote_idx: %d-%d<%llu>, msg_seqno: %d\n", len, conn->ac_remote_comptype, conn->ac_remote_id, remote_idx, conn->remote_msg_end_idx, sr.wr.rdma.remote_addr, qp_num);
    }

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    

gettimeofday(&tv3,NULL);
AMP_DMSG("__amp_rdma_sendmsg--exec_time: %ld\n", tv3.tv_usec - tv1.tv_usec + 1000000*(tv3.tv_sec - tv1.tv_sec));

//    AMP_ERROR("TIME statistic: %lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n", tv.tv_sec, tv.tv_usec, tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv3.tv_sec, tv3.tv_usec);

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_sendmsg: read complete queue failed\n");
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_sendmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
	    // added by mayl for debug
	    exit(1);
        }
    }
    conn->remote_msg_start_idx ++;

EXIT:
    AMP_LEAVE("__amp_rdma_sendmsg: leave\n");
    return err;
}


int __amp_rdma_send_complete_msg (void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t len, void *bufp, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_sendmsg enter, len:%d\n", len);


    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_wc wc;
    int poll_result = 0;
    struct timeval tv, tv1, tv2, tv3;

    //gettimeofday(&tv, NULL);
   
    //memset(&sge, 0, sizeof(sge));

    //memset(conn->ac_rdma_send_buf, 0, AMP_RDMA_MR_SIZE);
    //memcpy(conn->ac_rdma_send_buf, bufp, len);
    int * tlen;
    tlen = (int *)((char *)conn->ac_rdma_send_buf);
    * tlen = len;

    sge.addr = (uint64_t)(conn->ac_rdma_send_buf);
    sge.length = 4;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    //sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    //sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 1024 * 1024;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    //sr.opcode = IBV_WR_SEND_WITH_IMM;
    //sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }

    gettimeofday(&tv1, NULL); 
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;
//gettimeofday(&tv2,NULL);
    if(err){
        AMP_ERROR("__amp_rdma_sendmsg: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_cnt: %d\n", send_cnt);
        //AMP_ERROR("__amp_rdma_sendmsg: post_send success, send_len: %d, remote_type:%d, remote_id: %d\n", len, conn->ac_remote_comptype, conn->ac_remote_id);
    }

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    

gettimeofday(&tv3,NULL);
AMP_DMSG("__amp_rdma_sendmsg--exec_time: %ld\n", tv3.tv_usec - tv1.tv_usec + 1000000*(tv3.tv_sec - tv1.tv_sec));

//    AMP_ERROR("TIME statistic: %lu-%lu\n%lu-%lu\n%lu-%lu\n%lu-%lu\n", tv.tv_sec, tv.tv_usec, tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv3.tv_sec, tv3.tv_usec);

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_sendmsg: read complete queue failed\n");
    }else{
        //AMP_ERROR("__amp_rdma_sendmsg: found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_sendmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

EXIT:
    AMP_LEAVE("__amp_rdma_sendmsg: leave\n");
    return err;
}
   
#if 1
    
int
__amp_rdma_cas (void *protodata,
                   void *addr,
                   amp_u32_t addr_len,
                   amp_u32_t len, 
                   void *bufp,
                   amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_cas enter, len:%d\n", len);

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
   
    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = 8;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    //sr.wr.atomic.remote_addr = conn->ac_rdma_remote_config.addr - 1024 * 1024;
    sr.wr.atomic.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.atomic.rkey = conn->ac_rdma_remote_config.rkey;
    sr.wr.atomic.compare_add = 0ULL;
    sr.wr.atomic.swap = 0xFFFFFFFFFFULL; 

    sr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;

    //sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    //sr.send_flags = IBV_SEND_SIGNALED;

    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_cas: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_cas: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    


    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_cas: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }else{
            int *success = conn->ac_rdma_send_buf;
            if(*success == 0){
                //AMP_ERROR("REMOTE process end\n");
                return 1;
            }
        }
    }
    AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}

int
__amp_rdma_recvmsg (void *protodata,
                   void *addr,
                   amp_u32_t addr_len,
                   amp_u32_t len, 
                   void *bufp,
                   amp_u32_t flags)
{
    volatile amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_recvmsg enter, len:%d\n", len);

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    volatile amp_u64_t * success = (amp_u64_t *)conn->ac_rdma_send_buf;
   
    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = 8;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;


    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_recvmsg: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_recvmsg: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    


    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_recvmsg: COMPLETE FAILED: %s (%d), len %d\n", ibv_wc_status_str(wc.status), wc.status, len);
        }else{
	    char *ptr = (char *)conn->ac_rdma_send_buf;
	    //AMP_ERROR("--opcode: %d, size: %d, cnt: %d---%llu %d %d %d %d %d %d %d %d, wc.opcode: %d, wc.bytelen: %d\n", sr.opcode, sr.sg_list->length, poll_result, *success, ptr[0],ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], wc.opcode, wc.byte_len);

            if(0UL == *(amp_u64_t *)success){
                //AMP_ERROR("REMOTE process end\n");
                return 1;
            }
        }
    }
    //AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}
	
int 
__amp_rdma_writesbv(void *protodata, int start_pos, int len)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_ENTER("__amp_rdma_writesb: enter\n");

    memset((char *)conn->ac_rdma_send_buf + start_pos, 0, len);
    sge.addr = (uint64_t)conn->ac_rdma_send_buf + start_pos;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + start_pos;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_writesb: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_writesb: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_writesb: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

 
EXIT:
    AMP_LEAVE("__amp_rdma_writesb: leave\n");
    return err;
}

int
__amp_rdma_recvsb (void *protodata)
{
    volatile amp_s32_t err = 0;
    amp_s32_t rc = 0;
    int i = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_recvsb enter\n");

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    volatile uint8_t * bitmap = (uint8_t *)conn->ac_rdma_send_buf;
   
    sge.addr = (uint64_t)((char *)conn->ac_rdma_send_buf);
    sge.length = 128;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = (uint64_t)conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;

    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_recvmsg: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_recvmsg: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_recvmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }else{

                //AMP_ERROR("REMOTE process end\n");
		for(i = 0; i < 1016/8; i ++){
			if(bitmap[i] == 0xFF){
				if(0 == i){
					conn->remote_msg_start_idx = 0;
				}
				conn->remote_msg_end_idx = 8 * ( i + 1);
			}	
		}

		if(conn->remote_msg_start_idx == 0 && (conn->remote_msg_end_idx >= 512 || conn->remote_msg_end_idx  == 1016)){

			if(conn->remote_msg_end_idx == 1016){
				{
					conn->remote_data_start_pos = AMP_RDMA_SB_MSG_SIZE;
					conn->remote_data_end_pos = AMP_RDMA_MR_SIZE;
					conn->next_data_start = AMP_RDMA_SB_MSG_SIZE;
					conn->last_data_end_pos = AMP_RDMA_MR_SIZE;
				}
				__amp_rdma_writesbv(protodata, 0, 1024 * AMP_RDMA_SB_BLK_NUM);
				conn->next_bitmap_start = 0;
			}else{
				__amp_rdma_writesbv(protodata, 0, conn->remote_msg_end_idx/8);
				conn->next_bitmap_start = conn->remote_msg_end_idx/8;
			}
			return 1;
		}else{
			//AMP_ERROR("amp_rdma_recvsb: ms_range: <%d, %d>, %d-%d-%d-%d-%d-%d---%d-%d, data_range: <%d, %d>\n", conn->remote_msg_start_idx, conn->remote_msg_end_idx, bitmap[0], bitmap[1], bitmap[2], bitmap[3], bitmap[4], bitmap[5], bitmap[126], bitmap[127], conn->remote_data_start_pos, conn->remote_data_end_pos);
			return 0;
		}
        }
    }
    //AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}

	
int
__amp_rdma_recvsb_half (void *protodata, int start, int len)
{
    volatile amp_s32_t err = 0;
    amp_s32_t rc = 0;
    int i = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_recvsb enter\n");

    int start_idx = 0;
    int end_idx = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    volatile uint8_t * bitmap = (uint8_t *)conn->ac_rdma_send_buf + start;
   
    sge.addr = (uint64_t)((char *)conn->ac_rdma_send_buf) + start;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = (uint64_t)conn->ac_rdma_remote_config.addr + start;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;

    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_recvmsg: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_recvmsg: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_recvmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }else{

                //AMP_ERROR("REMOTE process end\n");
		for(i = start; i < 1016/8; i ++){
			if(bitmap[i - start] == 0xFF){
				if(i == start){
					start_idx = i * 8;
				}
				end_idx = 8 * ( i + 1);
			}	
		}

		if(start_idx == start * 8 && end_idx == 1016){
			__amp_rdma_writesbv(protodata, start, len);
			conn->remote_msg_end_idx = 1016;
			conn->next_bitmap_start = 0;
			return 1;
		}else{
			//AMP_ERROR("amp_rdma_recvsb_half: ms_range: <%d, %d>, %d-%d-%d-%d-%d-%d---%d-%d, data_range: <%d, %d>\n", start * 8, 1016, bitmap[0], bitmap[1], bitmap[2], bitmap[3], bitmap[4], bitmap[5], bitmap[126], bitmap[127], conn->remote_data_start_pos, conn->remote_data_end_pos);
			return 0;
		}
        }
    }
    //AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}
	
int
__amp_rdma_recvsb_data_half (void *protodata, int start, int len)
{
    volatile amp_s32_t err = 0;
    amp_s32_t rc = 0;
    int i = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_recvsb enter\n");

    int start_idx = 0;
    int end_idx = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    uint8_t * bitmap = (uint8_t *)conn->ac_rdma_send_buf + 512;
   
    sge.addr = (uint64_t)(conn->ac_rdma_send_buf) + 512 + start;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = (uint64_t)conn->ac_rdma_remote_config.addr  + 512 + start;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;

    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_recvmsg: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_recvmsg: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_recvmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }else{
		
		int start_pos = AMP_RDMA_SB_MSG_SIZE + start * 8 * 4096;
		int end_pos = start_pos;
		bitmap = (uint8_t *)(conn->ac_rdma_send_buf + 512);
		for(i = start; i < (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/ 4096 / 8; i++){
			if(bitmap[i] == 0xFF){
				if(start_pos > i * 4096 * 8 + AMP_RDMA_SB_MSG_SIZE){
					start_pos =  AMP_RDMA_SB_MSG_SIZE + i * 4096 * 8;
				}
				end_pos = (i + 1) * 4096 * 8 + AMP_RDMA_SB_MSG_SIZE;
			}else{
				break;
			}
		}


                //AMP_ERROR("REMOTE process end\n");
		for(i = (end_pos - AMP_RDMA_SB_MSG_SIZE) / 4096; i < (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE) / 4096; i ++){
			if(bitmap[i / 8] & ((0x1 << (i % 8)))){
				end_pos += 4096;
			}else{
				break;
			}
		}

		if(start_pos == (start * 4096 * 8 + AMP_RDMA_SB_MSG_SIZE) && (end_pos == AMP_RDMA_MR_SIZE || end_pos == conn->last_data_end_pos)){
			__amp_rdma_writesbv(protodata, start + 512, len);
			conn->remote_data_end_pos = AMP_RDMA_MR_SIZE;
			conn->next_data_start = AMP_RDMA_SB_MSG_SIZE;
			conn->last_data_end_pos = AMP_RDMA_MR_SIZE;
			return 1;
		}else{
			//AMP_ERROR("amp_rdma_recvsb_data_half<%d, %d>: ms_range: <%d, %d>, %d-%d-%d-%d-%d-%d----%d,%d---%d-%d, data_range: <%d, %d>, cur_range: <%d, %d>, next_data_start: %d, last_data_end_pos: %d\n", start, len, conn->remote_msg_start_idx, conn->remote_msg_end_idx,  bitmap[0], bitmap[1], bitmap[2], bitmap[3], bitmap[4], bitmap[5], bitmap[start], bitmap[start -1], bitmap[510], bitmap[511], conn->remote_data_start_pos, conn->remote_data_end_pos, start_pos, end_pos, conn->next_data_start, conn->last_data_end_pos);
			return 0;
		}
        }
    }
    //AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}

	
int 
__amp_rdma_writesb(void *protodata)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_ENTER("__amp_rdma_writesb: enter\n");

    memset(conn->ac_rdma_send_buf, 0, 128);
    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = (conn->remote_msg_end_idx - conn->remote_msg_start_idx)/8;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_writesb: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_writesb: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_writesb: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

 
EXIT:
    AMP_LEAVE("__amp_rdma_writesb: leave\n");
    return err;
}
	
int
__amp_rdma_recvsb_data (void *protodata)
{
    volatile amp_s32_t err = 0;
    amp_s32_t rc = 0;
    int i = 0;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_recvsb enter\n");

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    uint8_t * bitmap = (uint8_t *)((uint64_t)conn->ac_rdma_send_buf + 512);
   
    sge.addr = (uint64_t)conn->ac_rdma_send_buf + 512;
    sge.length = (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/4096/8;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 512;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;

    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(rc){
        AMP_ERROR("__amp_rdma_recvmsg: post_send failed, rc: %d, errno: %d\n", rc, errno);
    }else{
        //AMP_ERROR("__amp_rdma_recvmsg: post_send success\n");
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("read complete queue failed\n");
    }else{
        //AMP_ERROR("found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_recvmsg: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }else{

		int start_pos = AMP_RDMA_SB_MSG_SIZE;
		int end_pos = start_pos;

		for(i = 0; i < (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/4096 / 8; i++){
			if(bitmap[i] == 0xFF){
				if(start_pos > i * 4096 * 8 + AMP_RDMA_SB_MSG_SIZE){
					start_pos =  AMP_RDMA_SB_MSG_SIZE + i * 4096 * 8;
				}
				end_pos = (i + 1) * 4096 * 8 + AMP_RDMA_SB_MSG_SIZE;
			}else{
				break;
			}
		}
		
		for(i = (end_pos - AMP_RDMA_SB_MSG_SIZE)/4096; i < (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/4096; i++){
			if(bitmap[i / 8 ] & (0x1 << (i % 8))){
				end_pos += 4096;
			}else{
				break;
			}
		}
		//AMP_ERROR("amp_rdma_recvsb_data: ms_range: <%d, %d>, %d-%d-%d-%d-%d-%d---%d, %d---%d-%d, data_range: <%d, %d>, cur_range: <%d, %d>, next_data_start:<%d, %d>\n", conn->remote_msg_start_idx, conn->remote_msg_end_idx, bitmap[0], bitmap[1], bitmap[2], bitmap[3], bitmap[4], bitmap[5], bitmap[(end_pos - AMP_RDMA_SB_MSG_SIZE )/8 -1], bitmap[(end_pos-AMP_RDMA_SB_MSG_SIZE) / 8], bitmap[(end_pos-AMP_RDMA_SB_MSG_SIZE) / 8 + 1], bitmap[(end_pos-AMP_RDMA_SB_MSG_SIZE) / 8 + 2], conn->remote_data_start_pos, conn->remote_data_end_pos, start_pos, end_pos, conn->next_data_start, conn->last_data_end_pos);
		//if((start_pos == AMP_RDMA_SB_MSG_SIZE) && (end_pos == conn->remote_data_start_pos)){
		if((start_pos == AMP_RDMA_SB_MSG_SIZE) && (end_pos == AMP_RDMA_MR_SIZE || end_pos == conn->remote_data_start_pos || end_pos >= AMP_RDMA_MR_SIZE / 2)){
			if(end_pos == conn->remote_data_start_pos || end_pos == AMP_RDMA_MR_SIZE){
				conn->remote_data_start_pos = AMP_RDMA_SB_MSG_SIZE;
				conn->remote_data_end_pos = AMP_RDMA_MR_SIZE;
				__amp_rdma_writesbv(protodata, 512, (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE) / 4096 / 8);
				conn->next_data_start = AMP_RDMA_SB_MSG_SIZE;
				conn->last_data_end_pos = AMP_RDMA_MR_SIZE;
			}else{
				conn->next_data_start = end_pos - end_pos % (4096 * 8);
				conn->last_data_end_pos = conn->remote_data_start_pos;
				conn->remote_data_start_pos = start_pos;
				conn->remote_data_end_pos = conn->next_data_start;
				__amp_rdma_writesbv(protodata, 512, (conn->next_data_start - start_pos) / 4096 / 8);
			}
                	return 1;
		}else{
			//AMP_ERROR("amp_rdma_recvsb_data: ms_range: <%d, %d>, %d-%d-%d-%d-%d-%d---%d-%d, data_range: <%d, %d>, cur_range: <%d, %d>, next_data_start:<%d, %d>\n", conn->remote_msg_start_idx, conn->remote_msg_end_idx, bitmap[0], bitmap[1], bitmap[2], bitmap[3], bitmap[4], bitmap[5], bitmap[478], bitmap[479], conn->remote_data_start_pos, conn->remote_data_end_pos, start_pos, end_pos, conn->next_data_start, conn->last_data_end_pos);
			return 0;
		}
        }
    }
    //AMP_ERROR("REMOTE processing\n");

EXIT:
    AMP_LEAVE("__amp_rdma_recvmsg: leave\n");
    return 0;
}
	
int 
__amp_rdma_writesb_data(void *protodata)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_ENTER("__amp_rdma_writesb: enter\n");

    memset(conn->ac_rdma_send_buf + 512, 0, (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/4096 / 8);
    sge.addr = (uint64_t)conn->ac_rdma_send_buf + 512;
    sge.length = (AMP_RDMA_MR_SIZE - AMP_RDMA_SB_MSG_SIZE)/4096 / 8;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 512;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_writesb: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_writesb: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_writesb: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_writesb: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

 
EXIT:
    AMP_LEAVE("__amp_rdma_writesb: leave\n");
    return err;
}




#endif
#if 0    
int 
__amp_rdma_senddata_read(void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t niov, amp_kiov_t *iov, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_s32_t sock;
    amp_u32_t sleep_time = 0;
    amp_s8_t  *addrp = NULL;
    amp_kiov_t *kiov;
    amp_u32_t len = 0;
    amp_u32_t iovs;
    char *bufp;

    AMP_ENTER("__amp_rmda_senddata: enter\n");

    
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_DMSG("__amp_rdma_senddata enter, len:%d\n", len);

    if (!bufp || !len) {
        AMP_ERROR("__amp_rdma_senddata: no buffer\n");
        err = -EINVAL;
        goto EXIT;
    }

    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
   
    memset(&sge, 0, sizeof(sge));

    kiov = iov;


    int i = 0;

    for(i = 0; i < niov; i++){
        addrp = (amp_s8_t *)kiov->ak_addr + kiov->ak_offset;

        memcpy(conn->ac_rdma_send_buf + len, addrp, kiov->ak_len);

        len += kiov->ak_len;
        kiov ++;
    }


    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_SEND_WITH_IMM;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_senddata: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
        AMP_ERROR("__amp_rdma_senddata: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_senddata: read complete queue failed\n");
    }else{
        AMP_ERROR("__amp_rdma_senddata: found in the complete queue\n");
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_senddata: COMPLETE FAILED: %d\n",wc.status);
        }
    }

 
EXIT:
    AMP_LEAVE("__amp_rdma_senddata: leave\n");
    return err;
}
#endif

    
int 
__amp_rdma_senddata_write(void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t niov, amp_kiov_t *iov, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_s32_t sock;
    amp_u32_t sleep_time = 0;
    amp_s8_t  *addrp = NULL;
    amp_kiov_t *kiov;
    amp_u32_t len = 0;
    amp_u32_t iovs;
    char *bufp;
    int i = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    amp_connection_t * conn = (amp_connection_t *)protodata;

    AMP_ENTER("__amp_rmda_senddata_write: enter\n");

    //memset(&sge, 0, sizeof(sge));
    //gettimeofday(&tv, NULL);

    kiov = iov;
    len = 4;
//struct timeval tv1, tv2;
//gettimeofday(&tv1, NULL);
    for(i = 0; i < niov; i++){
        addrp = (amp_s8_t *)kiov->ak_addr + kiov->ak_offset;
        memcpy(conn->ac_rdma_send_buf + len, addrp, kiov->ak_len);
        len += kiov->ak_len;
        kiov ++;
    }
//gettimeofday(&tv2, NULL);
//AMP_ERROR("__AMP_RDMA_SENDDATA_WRITE: COPY USE TIME: %llu us\n", tv2.tv_usec - tv1.tv_usec + 1000000*(tv2.tv_sec - tv1.tv_sec));

    int * size = (int *)(conn->ac_rdma_send_buf);
    *size = len - 4; 
    size = (int *)(conn->ac_rdma_send_buf + len);
    *size = len - 4;

    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = len + 4;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    //memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    
    //sr.imm_data = (conn->ac_this_type << 24) | (conn->ac_this_id);

    //TODO
    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + AMP_RDMA_SB_MSG_SIZE;
    //sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + 4;
    //sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr;
    //AMP_ERROR("__AMP_RDMA_SENDDATA_WRITE: REMOTE_ADDR:%p, senddata: %d\n", sr.wr.rdma.remote_addr, len + 4);
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    //sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(len < AMP_RDMA_MAX_INLINE_DATA){
        sr.send_flags |= IBV_SEND_INLINE;
    }
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_senddata_write: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }
    
    if(poll_result < 0){
        AMP_ERROR("__amp_rdma_senddata_write: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_senddata_write: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

//__amp_rdma_send_complete_msg(protodata, addr, addr_len, len-4, bufp, flags);

 
EXIT:
    AMP_LEAVE("__amp_rdma_senddata_write: leave\n");
    return err;
}
	
int 
__amp_rdma_senddata_write_pos(void *protodata, void *addr, amp_u32_t addr_len, amp_u32_t niov, amp_kiov_t *iov, amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_s32_t sock;
    amp_u32_t sleep_time = 0;
    amp_s8_t  *addrp = NULL;
    amp_kiov_t *kiov;
    amp_u32_t len = 0;
    amp_u32_t iovs;
    char *bufp;
    int i = 0;
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    struct timeval tv, tv1;
    amp_connection_t * conn = (amp_connection_t *)protodata;
    volatile int start_pos = conn->remote_data_start_pos;
    AMP_ENTER("__amp_rmda_senddata_write: enter\n");
    
    kiov = iov;
    int debug_write = 0;
    if(debug_write){
    	gettimeofday(&tv, NULL);
    }
    for(i = 0; i < niov; i++){
        addrp = (amp_s8_t *)kiov->ak_addr + kiov->ak_offset;
        len += kiov->ak_len;
        kiov ++;
    }
    //AMP_ERROR("__amp_rdma_senddata_write_pos: start_pos: %d, len: %d, end_pos: %d, next_data_start: %d, last_data_pos: %d\n", start_pos, len, conn->remote_data_end_pos, conn->next_data_start, conn->last_data_end_pos);

    if(start_pos + len >= AMP_RDMA_MR_SIZE / 2 && conn->next_data_start > AMP_RDMA_SB_MSG_SIZE){
    	while(0 == (rc = __amp_rdma_recvsb_data_half(protodata, (conn->next_data_start - AMP_RDMA_SB_MSG_SIZE)/ 4096 / 8, (AMP_RDMA_MR_SIZE - conn->next_data_start)/4096/8)));
    }
    if(conn->remote_data_start_pos >= conn->remote_data_end_pos || conn->remote_data_start_pos + len > conn->remote_data_end_pos){
	while(0 ==(rc =__amp_rdma_recvsb_data(protodata)));
    }

    start_pos = conn->remote_data_start_pos;
    kiov = iov;
    len = 0;
    for(i = 0; i < niov; i++){
        addrp = (amp_s8_t *)kiov->ak_addr + kiov->ak_offset;
        memcpy(conn->ac_rdma_send_buf + len, addrp, kiov->ak_len);
        len += kiov->ak_len;
        kiov ++;
    }

    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = len;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;

    sr.wr.rdma.remote_addr = conn->ac_rdma_remote_config.addr + start_pos;
    sr.wr.rdma.rkey = conn->ac_rdma_remote_config.rkey;

    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.send_flags = IBV_SEND_SIGNALED;
        
    err = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    send_cnt ++;

    if(err){
        AMP_ERROR("__amp_rdma_senddata_write: post_send failed, err: %d, errno: %d\n", err, errno);
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: post_send success, send_cnt: %d\n", send_cnt);
    }


    struct ibv_wc wc;
    int poll_result = 0;

    while(poll_result == 0){
        poll_result = ibv_poll_cq(conn->ac_rdma_scq, 1, &wc);
    }

   if(debug_write){
    	send_write_cnt++;
    	gettimeofday(&tv1, NULL);
    	send_write_time += (tv1.tv_usec+tv1.tv_sec*1000000);
    	send_write_time -= (tv.tv_usec+tv.tv_sec*1000000);
    	if(send_write_cnt %10000 == 5){
	    AMP_ERROR("amp send wdata cnt %lu, send wdata time %lu\n", send_write_cnt, send_write_time);
    	}	
    }


   if(poll_result < 0){
        AMP_ERROR("__amp_rdma_senddata_write: read complete queue failed\n");
    }else{
    //    AMP_ERROR("__amp_rdma_senddata_write: found in the complete queue, CQE_num: %d\n", poll_result);
        if(wc.status != IBV_WC_SUCCESS){
            AMP_ERROR("__amp_rdma_senddata_write: COMPLETE FAILED: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        }
    }

 
EXIT:
    AMP_LEAVE("__amp_rdma_senddata_write: leave\n");
    return len;
}



 
#endif
/*
 * send data to peer by tcp protocol.
 */
int 
__amp_tcp_senddata(void *protodata, 
                   void *addr,
                   amp_u32_t addr_len,
                   amp_u32_t niov,
                   amp_kiov_t *iov,
                   amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_s32_t sock;
    amp_u32_t sleep_time = 0;
    amp_s8_t  *addrp = NULL;
    amp_kiov_t *kiov;
    amp_u32_t slen;
    amp_u32_t iovs;
    char *bufp;
    amp_connection_t * conn = (amp_connection_t *)protodata;
    sock = conn->ac_sock;

    AMP_ENTER("__amp_tcp_senddata: enter\n");
    //sock = *((amp_s32_t *)protodata);

    if (sock < 0) {
        AMP_ERROR("__amp_tcp_senddata: no sock\n");
        err = EINVAL;
        goto EXIT;
    }

    if(!iov || !niov) {
        AMP_ERROR("__amp_tcp_senddata: no iov\n");
        err = -EINVAL;
        goto EXIT;
    }

    AMP_ERROR(" tcp send ivo by SOCK??: %d\n", conn->ac_sock);
    // ADDED BY MAYL FOR debug 
    {
    char * str1 = NULL;
    str1[0] = 0xaa;
    }
    iovs = niov;
    kiov = iov;

    AMP_DMSG("__amp_tcp_senddata: niov:%d, iov:%p\n", iovs, kiov);
    while (iovs) {
        addrp = (amp_s8_t *)kiov->ak_addr;
        if (!addrp) {
            AMP_ERROR("__amp_tcp_senddata: no buffer\n");
            AMP_ERROR("__amp_tcp_senddata: total iov:%d, current ios:%d\n",niov, iovs);
            err = -EINVAL;
            goto EXIT;
        }
        bufp = addrp;
        bufp +=  kiov->ak_offset;
        slen = kiov->ak_len;
        
        while (slen) {
            sleep_time = 0;
RE_WRITE:
            rc = write(sock, bufp, slen);
            if (rc <= 0) {
                if (errno == EAGAIN || errno == EINTR){
                    sleep_time ++;
                    //sleep(2);
                    usleep(5000);
                    AMP_ERROR("__amp_tcp_senddata: send error, rc:%d, slen: %d, errno:%d, sleep_time: %d\n", rc, slen, errno, sleep_time);
                    if(sleep_time < AMP_CONN_RETRY_TIMES)
                        goto RE_WRITE;
                }
                AMP_ERROR("__amp_tcp_senddata: send error, rc:%d, slen: %d, errno:%d\n", rc, slen, errno);
                if (rc == 0)
                    rc = -ECONNABORTED;
                err = -errno;
                goto EXIT;
            }

            bufp += rc;
            slen -= rc;
            
            if (rc < slen) 
                AMP_WARNING("__amp_tcp_senddata: sent:%d, slen:%d, total_len: %d, less\n", rc, kiov->ak_len, slen);

        }

        iovs --;
        kiov ++;
    }

EXIT:
    AMP_LEAVE("__amp_tcp_senddata: leave\n");
    return err;
}

/* 
 * recvmsg
 */ 
int 
__amp_tcp_recvmsg(void *protodata,
                  void *addr,
                  amp_u32_t addr_len,
                  amp_u32_t len,
                  void *bufp,
                  amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_u32_t slen;
    amp_s32_t sock;
    char  *tmpbufp = NULL;
    amp_u32_t sleep_time = 0;
    amp_connection_t * conn;
    AMP_ENTER("__amp_tcp_recvmsg: enter, len:%d\n", len);

    conn = (amp_connection_t *)protodata;
    sock = conn->ac_sock;

    //sock = *((amp_s32_t *)protodata);
    
    if (sock < 0) {
        AMP_ERROR("__amp_tcp_recvmsg: no socket\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    AMP_DMSG("__amp_tcp_rcvmsg: sock:%d\n", sock);

    if (!bufp || !len) {
        AMP_ERROR("__amp_tcp_recvmsg: no buffer\n");
        err = -EINVAL;
        goto EXIT;
    }

    tmpbufp = (amp_s8_t *)bufp;

    slen = len;
 
    sleep_time = 0;
    while (slen) {
        sleep_time = 0;
RE_READ:
        rc = read (sock, tmpbufp, slen);

        if (rc <= 0) {
            if (errno == EAGAIN || errno == EINTR){
                sleep_time ++;
                //sleep(2);
                usleep(5000);
                AMP_ERROR("__amp_tcp_recvmsg: recv error, rc:%d, slen: %d, errno:%d, sleep_time: %d\n", rc, slen, errno, sleep_time);
                if(sleep_time < AMP_CONN_RETRY_TIMES)
                    goto RE_READ;
            }
            
            AMP_ERROR("__amp_tcp_recvmsg: send error, slen: %d, rc: %d, errno: %d\n", slen, rc, errno);

            err = -errno;
            if (rc == 0)
                err = -ECONNABORTED;
            goto EXIT;
        }

        tmpbufp += rc;
        slen -= rc;
        
        if(rc < slen)
            AMP_WARNING("__amp_tcp_recvmsg: rc : %d, socket: %d, total_len: %d, need slen:%d ......\n", rc, sock, len, slen);

    }
    
    err = 0;

EXIT:
    AMP_LEAVE("__amp_tcp_recvmsg: leave\n");
    return err;

}


/*
 * recv data blocks
 */ 
int 
__amp_tcp_recvdata (void *protodata,
                    void *addr,
                    amp_u32_t addr_len,
                    amp_u32_t niov,
                    amp_kiov_t *iov,
                    amp_u32_t flags)
{
    amp_s32_t err = 0;
    amp_s32_t rc = 0;
    amp_u32_t slen;
    amp_u32_t iovs;
    amp_u32_t iov_no = 0;
    amp_s32_t sock;
    amp_u32_t sleep_time = 0;
    amp_kiov_t *kiov = NULL;
    amp_connection_t * conn = (amp_connection_t *)protodata;
    char *bufp = NULL;

    AMP_ENTER("_amp_tcp_recvdata: enter\n");

    //sock = *((amp_s32_t *)protodata);
    sock = conn->ac_sock;

    if (sock < 0) {
        AMP_ERROR("__amp_tcp_recvdata: no sock\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!iov || !niov) {
        AMP_ERROR("__amp_tcp_recvdata: no kiov, iov: %p, niov:%d\n", iov, niov);
        err = -EINVAL;
        goto EXIT;
    }

    kiov = iov;
    iovs = niov;

    while (iovs) {
        bufp = (amp_s8_t *) kiov->ak_addr;
        bufp += kiov->ak_offset;
        slen = kiov->ak_len;

        while (slen) {
            sleep_time = 0;
RE_READ:
            rc = read (sock, bufp, slen);
            if (rc <= 0) {
                if (errno == EAGAIN||errno == EINTR) {
                    sleep_time ++;
                    sleep(2);
                    //usleep(5000);
                    AMP_ERROR("__amp_tcp_recvdata: recv error, rc:%d, slen: %d, errno:%d, sleep_time: %d\n", rc, slen, errno, sleep_time);
                    if(sleep_time < AMP_CONN_RETRY_TIMES)
                        goto RE_READ;
                }

                AMP_ERROR("__amp_tcp_recvdata: recv error, niov: %d, iov_no: %d, slen: %d, rc: %d, errno: %d\n", niov, iov_no, slen, rc, errno);
                if(rc == 0)
                    err = -ECONNABORTED;
                else
                    err = -errno;
                goto EXIT;
            }
            bufp += rc;
            slen -= rc;
            
            if(rc < slen)
            AMP_WARNING("__amp_tcp_recvdata: rc : %d, socket: %d, total_len: %d, need slen:%d ......\n", rc, sock, kiov->ak_len, slen);

        }

        iovs --;
        kiov ++;
        iov_no++;
    }
EXIT:
    AMP_LEAVE("__amp_tcp_recvdata: leave\n");
    return err;
}

/*
 * doing a connect
 */ 
int 
__amp_tcp_connect (void *protodata_parent,
                   void **protodata_child,
                   void *addr,
                   amp_u32_t direction)
{
    amp_s32_t err;
    amp_s32_t  parent_sock, *new_sock = NULL;
    struct sockaddr_in *saddrp;
    //amp_s32_t  opt = 1;
    struct sockaddr_in remoteaddr;
    amp_u32_t addrlen;

    AMP_ENTER("__amp_tcp_connect: enter\n");
    switch (direction) {
        case AMP_CONN_DIRECTION_LISTEN:
            AMP_DEBUG(AMP_DEBUG_TCP|AMP_DEBUG_MSG, "__amp_tcp_connect: listen\n");
            if (!addr) {
                AMP_ERROR("__amp_tcp_connect: no address\n");
                err = -EINVAL;
                goto EXIT;
            }
            new_sock = amp_alloc(sizeof(amp_s32_t));
            *new_sock = -1;
            saddrp = (struct sockaddr_in *)addr;

            err = socket(AF_INET, SOCK_STREAM, 0);
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: create socket error, err:%d\n", errno);
                err = -errno;
                goto EXIT;
            }
            *new_sock = err;
            /*err = setsockopt(*new_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(amp_s32_t));
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: setopt error,err:%d\n", errno);
                close(*new_sock);
                goto EXIT;
            }*/
            err = bind(*new_sock, (struct sockaddr *)saddrp, sizeof(*saddrp));
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: bind error, err:%d\n", errno);
                close(*new_sock);
                goto EXIT;
            }

            err = listen(*new_sock, 65536);
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: listen error, err:%d\n", errno);
                close(*new_sock);
                goto EXIT;
            }
            break;

        case AMP_CONN_DIRECTION_ACCEPT:
            AMP_DEBUG(AMP_DEBUG_TCP|AMP_DEBUG_MSG, "__amp_tcp_connect: accept\n");
            if (!protodata_parent) {
                AMP_ERROR("__amp_tcp_connect: no parent socket\n");
                err = -EINVAL;
                goto EXIT;
            }
            parent_sock = *((amp_s32_t *)protodata_parent);
            bzero(&remoteaddr, sizeof(remoteaddr));
            addrlen = sizeof(remoteaddr);
            new_sock = amp_alloc(sizeof(amp_s32_t));
            err = accept(parent_sock, (struct sockaddr *)&remoteaddr, &addrlen);
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: accept error, err:%d\n", errno);
                goto EXIT;
            }
            *new_sock = err;
            break;
        case AMP_CONN_DIRECTION_CONNECT:
            AMP_DEBUG(AMP_DEBUG_TCP|AMP_DEBUG_MSG, "__amp_tcp_connect: connect\n");
            if (!addr) {
                AMP_ERROR("__amp_tcp_connect: no address\n");
                err = -EINVAL;
                goto EXIT;
            }
            saddrp = (struct sockaddr_in *)addr;
            err = socket(AF_INET, SOCK_STREAM, 0);
            
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: create socket error, err:%d\n", errno);
                goto EXIT;
            }
            new_sock = amp_alloc(sizeof(amp_s32_t));
            *new_sock = err;
            /*err = setsockopt(*new_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(amp_s32_t));
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: setopt error,err:%d\n", errno);
                close(*new_sock);
                goto EXIT;
            }*/

            err = connect(*new_sock, (struct sockaddr *)saddrp, sizeof(*saddrp));
            if (err < 0) {
                AMP_ERROR("__amp_tcp_connect: connect to server error, err:%d\n", errno);
                close(*new_sock);
                goto EXIT;
            }
            
            break;
        default:
            AMP_ERROR("__amp_tcp_connect: wrong direction:%d\n", direction);
            err = -EINVAL;
            break;
    }

    /*
     * now doing some initialization
     */

    err = __amp_tcp_general_init(*new_sock);
    if (err) {
        close(*new_sock);
        goto EXIT;
    }

    /*
     * return it to connection
     */ 
    *protodata_child = new_sock;

EXIT:
    if(err < 0)
        free(new_sock);
    AMP_LEAVE("__amp_tcp_connect: leave\n");
    return err;
}

/*
 * disconnect 
 */ 

int
__amp_tcp_disconnect (void *protodata)
{
    amp_s32_t err = 0;
    amp_s32_t  sock;
    AMP_ENTER("__amp_tcp_disconnect: enter\n");

    if (!protodata) {
        AMP_ERROR("__amp_tcp_disconnect: no sock\n");
        err = -EINVAL;
        goto EXIT;
    }

    sock = *((amp_s32_t *)protodata);
    AMP_LEAVE("__amp_tcp_disconnect: close sock:%d\n", sock);
    shutdown(sock,SHUT_RDWR);
    close(sock);
    *((amp_s32_t *)protodata) = -1;
EXIT:
    AMP_LEAVE("__amp_tcp_disconnect: leave\n");
    return err;
}

/*
 * init the  socket
 */
int 
__amp_tcp_init (void * protodata, amp_u32_t direction)
{
    amp_s32_t  err = 0;
    amp_s32_t sock;
    
    AMP_ENTER("__amp_tcp_init: enter\n");

    if (!protodata) {
        AMP_ERROR("__amp_tcp_init: no proto data\n");
        err = -EINVAL;
        goto EXIT;
    }

    sock = *((amp_s32_t *)protodata);

EXIT:
    AMP_LEAVE("__amp_tcp_init: leave\n");
    return err;
}


amp_proto_interface_t  amp_tcp_proto_interface = {
    type:                  AMP_CONN_TYPE_TCP,
    amp_proto_sendmsg_init:     __amp_tcp_sendmsg,
    amp_proto_sendmsg:     __amp_rdma_sendmsg_idx,
    amp_proto_senddata:    __amp_tcp_senddata,
    amp_proto_senddata_write:    __amp_rdma_senddata_write_pos,
    amp_proto_recvmsg_init:     __amp_tcp_recvmsg,
    amp_proto_recvmsg:     __amp_rdma_recvmsg,
    amp_proto_recvdata:    __amp_tcp_recvdata,
    amp_proto_connect:     __amp_tcp_connect,
    amp_proto_disconnect:  __amp_tcp_disconnect,
    amp_proto_init:        __amp_tcp_init
};

#if 0
amp_proto_interface_t  amp_rdma_proto_interface = {
    type:                  AMP_CONN_TYPE_RDMA,
    amp_proto_sendmsg:     __amp_rdma_sendmsg,
    amp_proto_senddata:    __amp_rdma_senddata,
    amp_proto_recvmsg:     __amp_rdma_recvmsg,
    amp_proto_recvdata:    __amp_rdma_recvdata,
    amp_proto_connect:     __amp_tcp_connect,
    amp_proto_disconnect:  __amp_tcp_disconnect,
    amp_proto_init:        __amp_tcp_init
};
#endif



/*end of file*/
