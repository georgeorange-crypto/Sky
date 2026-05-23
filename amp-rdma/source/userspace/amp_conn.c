/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/

#include <amp_conn.h>
#include <amp_request.h>
#include <amp_protos.h>
#include <amp_thread.h>


static int used_mtu = IBV_MTU_1024; 
//static int used_mtu = IBV_MTU_4096; 
#ifdef __FOR_ROCE_V2__
//TODO mayl , the 2 line below is set for LVSUAN
extern int  roce_v2_index;
static int valid_gid_index = 5; 
#endif

struct list_head  amp_reconn_conn_list;
amp_lock_t        amp_reconn_conn_list_lock;
struct list_head  amp_dataready_conn_list;
amp_lock_t        amp_dataready_conn_list_lock;
amp_lock_t        amp_free_conn_lock;
amp_u32_t         amp_reconn_thread_started = 0;
amp_u32_t         amp_wakeup_thread_started = 0;
amp_htb_entry_t   *amp_resend_hash_table = NULL;
amp_htb_entry_t   *amp_reconfirm_hash_table_c = NULL;
amp_htb_entry_t   *amp_reconfirm_hash_table_s = NULL;
amp_u64_t         amp_malloc_addr_max = 0;
amp_u64_t         amp_malloc_addr_min = 0x7FFFFFFFFFFFFF;
amp_u64_t         msg_malloc_addr_max = 0;
amp_u64_t         msg_malloc_addr_min = 0x7FFFFFFFFFFFFF;
/*
 * initialization 
 */ 
#ifdef __FOR_ROCE_V2__
extern int get_gid_index();
extern int get_opt_mtu();
#endif

int
__amp_init_conn(void)
{
    amp_htb_entry_t  *bucket = NULL;
#ifdef __AMP_RECONFIRM_MSG
    amp_s32_t i = 0;
#endif

    INIT_LIST_HEAD(&amp_reconn_conn_list);
    amp_lock_init(&amp_reconn_conn_list_lock);

    INIT_LIST_HEAD(&amp_dataready_conn_list);
    amp_lock_init(&amp_dataready_conn_list_lock);
    amp_lock_init(&amp_free_conn_lock);

    amp_resend_hash_table = 
        (amp_htb_entry_t *)amp_alloc(sizeof(amp_htb_entry_t) * AMP_RESEND_HTB_SIZE);
    if (!amp_resend_hash_table) {
        AMP_ERROR("__amp_init_conn: alloc memory for amp_resend_hash_table error\n");
        return -ENOMEM;
    }
    memset(amp_resend_hash_table, 0, sizeof(amp_htb_entry_t) * AMP_RESEND_HTB_SIZE);

    for(bucket = amp_resend_hash_table + AMP_RESEND_HTB_SIZE - 1; 
            bucket >= amp_resend_hash_table; bucket --) {

        INIT_LIST_HEAD(&bucket->queue);
        amp_lock_init(&bucket->lock);
    }
#ifdef __AMP_RECONFIRM_MSG
    amp_reconfirm_hash_table_c = (amp_htb_entry_t *)amp_alloc(sizeof(amp_htb_entry_t) * AMP_RECONFIRM_HTB_SIZE);
    if(!amp_reconfirm_hash_table_c){
        AMP_ERROR("__amp_init_conn: alloc memory for amp_reconfirm_hash_table_c error\n");
        return -ENOMEM;
    }
    memset(amp_reconfirm_hash_table_c, 0, sizeof(amp_htb_entry_t) * AMP_RECONFIRM_HTB_SIZE);
    //for(bucket = amp_reconfirm_hash_table_c + AMP_RECONFIRM_HTB_SIZE - 1; bucket >= amp_reconfirm_hash_table_c; bucket --) {
    for(i = 0; i < AMP_RECONFIRM_HTB_SIZE; i++) {
        bucket = amp_reconfirm_hash_table_c + i;
        INIT_LIST_HEAD(&bucket->queue);
        amp_lock_init(&bucket->lock);
    }
#if 1
    amp_reconfirm_hash_table_s = (amp_htb_entry_t *)amp_alloc(sizeof(amp_htb_entry_t) * AMP_RECONFIRM_HTB_SIZE);
    if(!amp_reconfirm_hash_table_s){
        AMP_ERROR("__amp_init_conn: alloc memory for amp_reconfirm_hash_table_s error\n");
        return -ENOMEM;
    }
    memset(amp_reconfirm_hash_table_s, 0, sizeof(amp_htb_entry_t) * AMP_RECONFIRM_HTB_SIZE);
    //for(bucket = amp_reconfirm_hash_table_c + AMP_RECONFIRM_HTB_SIZE - 1; bucket >= amp_reconfirm_hash_table_c; bucket --) {
    for(i = 0; i < AMP_RECONFIRM_HTB_SIZE; i++) {
        bucket = amp_reconfirm_hash_table_s + i;
        INIT_LIST_HEAD(&bucket->queue);
        amp_lock_init(&bucket->lock);
    }
#endif
#endif
    return 0;

}

/*
 * finalize 
 */ 
int 
__amp_finalize_conn ()
{
    amp_request_t *req = NULL;
    amp_htb_entry_t *htbe = NULL;
    amp_u32_t i;

    AMP_ENTER("__amp_finalize_conn: enter\n");

    /*
     * free resend hash table.
     */
    AMP_DMSG("__amp_finalize_conn: check resend reqs\n");
    for (i = 0; i<AMP_RESEND_HTB_SIZE; i++) {
        htbe = amp_resend_hash_table + i;
        amp_lock(&htbe->lock);
        while (!list_empty(&htbe->queue))  {
            req = list_entry(htbe->queue.next, amp_request_t, req_list);
            //amp_lock(&req->req_lock);
            req->req_error = -EINTR;
            list_del_init(&req->req_list);
            //amp_unlock(&req->req_lock);
            amp_sem_up(&req->req_waitsem);//modified by weizheng 2013-11-18
        }
        amp_unlock(&htbe->lock);
    }
    amp_free(amp_resend_hash_table, sizeof(amp_htb_entry_t) * AMP_RESEND_HTB_SIZE);

#ifdef __AMP_RECONFIRM_MSG
    for (i = 0; i < AMP_RECONFIRM_HTB_SIZE; i++) {
        amp_reconfirm_msg_c_t * reconf_c = NULL;
        //amp_reconfirm_msg_t * reconf = NULL;
        if(NULL != amp_reconfirm_hash_table_c){ 
            htbe = amp_reconfirm_hash_table_c + i;
            amp_lock(&htbe->lock);
            while (!list_empty(&htbe->queue))  {
                reconf_c = list_entry(htbe->queue.next, amp_reconfirm_msg_c_t, reconf_list);
                amp_lock(&reconf_c->reconf_lock);
                list_del_init(&reconf_c->reconf_list);
                amp_unlock(&reconf_c->reconf_lock);
                __amp_free(reconf_c);
            }
            amp_unlock(&htbe->lock);
        }
#if 1    
        if(NULL != amp_reconfirm_hash_table_s){
            amp_reconfirm_msg_t * reconf = NULL;
            htbe = amp_reconfirm_hash_table_s + i;
            amp_lock(&htbe->lock);
            while (!list_empty(&htbe->queue))  {
                reconf = list_entry(htbe->queue.next, amp_reconfirm_msg_t, reconf_list);
                list_del_init(&reconf->reconf_list);
                __amp_free(reconf->reconf_reply_msg);
                __amp_free(reconf);
            }
            amp_unlock(&htbe->lock);
        }
#endif
    }
    if(amp_reconfirm_hash_table_c)
        amp_free(amp_reconfirm_hash_table_c, sizeof(amp_htb_entry_t *) * AMP_RECONFIRM_HTB_SIZE);
    if(amp_reconfirm_hash_table_s)
        amp_free(amp_reconfirm_hash_table_s, sizeof(amp_htb_entry_t *) * AMP_RECONFIRM_HTB_SIZE);
#endif
    /*
     * free request in send list.
     */
    AMP_DMSG("__amp_finalize_conn: check sending list\n");
    amp_lock(&amp_sending_list_lock);
    while (!list_empty(&amp_sending_list)) {
        req = list_entry(amp_sending_list.next, amp_request_t, req_list);
        //amp_lock(&req->req_lock);
        list_del_init(&req->req_list);
        req->req_error = -EINTR;
        //amp_unlock(&req->req_lock);
        amp_sem_up(&req->req_waitsem);
    }
    amp_unlock(&amp_sending_list_lock);


    /*
     * free all waiting for reply request
     */
    AMP_DMSG("__amp_finalize_conn: check waiting reply list\n");

    amp_lock(&amp_waiting_reply_list_lock);
    while  (!list_empty(&amp_waiting_reply_list)) {
        req = list_entry(amp_waiting_reply_list.next, amp_request_t, req_list);
        //amp_lock(&req->req_lock);
        list_del_init(&req->req_list);
        req->req_error = -EINTR;
        //amp_unlock(&req->req_lock);
        amp_sem_up(&req->req_waitsem);
    }
    amp_unlock(&amp_waiting_reply_list_lock);
    
    /*
     * interruptible all resend request
     */
    AMP_LEAVE("__amp_finalize_conn: leave\n");
    return 0;
}

int 
__amp_alloc_conn (amp_connection_t **retconn)
{
    amp_s32_t err = 0;
    amp_connection_t *conn;

    AMP_ENTER("__amp_alloc_conn: enter\n");

    conn = (amp_connection_t *)malloc(sizeof(amp_connection_t));

    if (!conn) {
        AMP_ERROR("__amp_alloc_conn: alloc for conn error\n");
        err = -ENOMEM;
        goto EXIT;
    }
    /*
     * Initialization
     */ 
    memset(conn, 0, sizeof(amp_connection_t));
    INIT_LIST_HEAD(&conn->ac_list);
    INIT_LIST_HEAD(&conn->ac_reconn_list);
    INIT_LIST_HEAD(&conn->ac_dataready_list);

        
    conn->ac_state = AMP_CONN_NOTINIT;
    conn->ac_sock = -1;
    conn->ac_refcont = 1;
    amp_lock_init(&conn->ac_lock);
    amp_sem_init2(&conn->ac_sendsem, AMP_SRVOUT_THR_INIT_NUM);
    amp_sem_init2(&conn->ac_recvsem, AMP_SRVIN_THR_INIT_NUM);
    amp_sem_init_locked(&conn->ac_listen_sem);

    *retconn = conn;
    
EXIT:
    AMP_LEAVE("__amp_alloc_conn: leave, conn:%p\n", conn);
    return err;
}


int 
__amp_free_conn (amp_connection_t *conn)
{
    amp_s32_t type = conn->ac_remote_comptype;
    amp_s32_t id = conn->ac_remote_id;

    AMP_ENTER("__amp_free_conn: enter,conn:%p, refcont:%d\n", conn, conn->ac_refcont);
    if (!conn) {
        AMP_ERROR("__amp_free_conn: no connecton\n");
        goto EXIT;
    }
    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_list)){
        list_del_init(&conn->ac_list);
    }
    amp_unlock(&conn->ac_lock);

    amp_lock(&amp_dataready_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if (!list_empty(&conn->ac_dataready_list)) {
        list_del_init(&conn->ac_dataready_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_dataready_conn_list_lock);
            
    amp_lock(&amp_reconn_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_reconn_list)){
        list_del_init(&conn->ac_reconn_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_reconn_conn_list_lock);

    amp_lock(&amp_free_conn_lock);
    amp_lock(&conn->ac_lock);
    if (!conn->ac_refcont) {
        AMP_ERROR("__amp_free_conn: conn:%p, its refcont is zero now\n", conn);
    }
    
    conn->ac_refcont --;
    
    if (conn->ac_refcont) {
#ifdef __AMP_CONNS_DUPLEX
        if(conn->ac_duplex==0){
            list_add_tail(&conn->ac_list, &(conn->ac_ctxt->acc_conns[type].acc_remote_conns[id].queue));        
        }else{
            list_add_tail(&conn->ac_list, &(conn->ac_ctxt->acc_conns_recv[type].acc_remote_conns[id].queue));
        }
#else
        list_add_tail(&conn->ac_list, &(conn->ac_ctxt->acc_conns[type].acc_remote_conns[id].queue));
#endif
        amp_unlock(&conn->ac_lock);
        amp_unlock(&amp_free_conn_lock);
        goto EXIT;
    }
#if 0
    if(!list_empty(&conn->ac_list))
        list_del_init(&conn->ac_list);
#endif
    amp_unlock(&conn->ac_lock);
    
    memset(conn, 0, sizeof(amp_connection_t));
    AMP_LEAVE("__amp_free_conn: fully free conn:%p\n", conn);
    __amp_free(conn);
    amp_unlock(&amp_free_conn_lock);

EXIT:
    AMP_LEAVE("__amp_free_conn: leave\n");
    return 0;
}

/*
 * queue a new created connection to the specificed context.
 */
int 
__amp_enqueue_conn(amp_connection_t *conn, amp_comp_context_t *ctxt)
{
    amp_s32_t  err = 0;
    amp_u32_t  type;
    amp_u32_t  id;
    struct list_head *head = NULL;
    conn_queue_t  *cnq = NULL;
    amp_u32_t i, j;
    amp_connection_t **orig_conns = NULL;
    amp_u32_t orig_num;
    amp_u32_t realloc_num;
    amp_u32_t realloc_size;

    
    AMP_ENTER("__amp_queue_conn: enter, conn:%p\n", conn);

    if (!ctxt)  {
        AMP_ERROR("__amp_queue_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn)  {
        AMP_ERROR("__amp_queue_conn: no connection\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns) {
        AMP_ERROR("__amp_queue_conn: no acc_conns in ctxt\n");
        err = -EINVAL;
        goto EXIT;
    }


    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;
    
    AMP_DMSG("__amp_queue_conn: type:%d, id:%d\n", type, id);

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_queue_conn: type(%d) is too large\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("__amp_queue_conn: no remote conns in comp_conns of type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }


    head = &(ctxt->acc_conns[type].acc_remote_conns[id].queue);
    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    cnq = &(ctxt->acc_conns[type].acc_remote_conns[id]);

    list_add_tail(&conn->ac_list, head);
    AMP_DMSG("__amp_queue_conn: before add conn, cnq:%p, active_conn_num:%d,total_num:%d\n", cnq, cnq->active_conn_num, cnq->total_num);
    for (i=0; i<cnq->active_conn_num; i++) {
        if (!cnq->conns[i])
            break;
    }
    AMP_DMSG("__amp_queue_conn: after search, i:%d\n", i);
    if (i >= cnq->active_conn_num) {
        if (cnq->active_conn_num >= cnq->total_num) {
            AMP_DMSG("__amp_queue_conn: need realloc the conns\n");
            orig_conns = cnq->conns;
            orig_num = cnq->total_num;
            realloc_num = cnq->total_num + AMP_SELECT_CONN_ARRAY_ALLOC_LEN;
            realloc_size = realloc_num * sizeof(amp_connection_t *);
            AMP_DMSG("__amp_queue_conn: orig_num:%d, realloc_num:%d\n", \
                                 orig_num, realloc_num);

            cnq->conns = (amp_connection_t **)amp_alloc(realloc_size);
            if (!cnq->conns) {
                AMP_ERROR("__amp_queue_conn: realloc for conns error\n");
                err = -ENOMEM;
                goto EXIT;
            }
            memset(cnq->conns, 0, realloc_size);
            for(j=0; j<orig_num; j++)
                cnq->conns[j] = orig_conns[j];
            amp_free(orig_conns, orig_num * sizeof(amp_connection_t *));
            cnq->total_num = realloc_num;
        }
        cnq->conns[i] = conn;
        cnq->active_conn_num++;
    } else {
        cnq->conns[i] = conn;
    }
#ifdef __AMP_RDMA__
    INIT_LIST_HEAD(&conn->listen_list);
    list_add_tail(&conn->listen_list, &amp_rdma_listen_group[(conn->ac_remote_comptype * conn->ac_remote_id * AMP_CONN_NUM + i ) % AMP_RDMA_LISTEN_THREAD_NUM]);
    //list_add_tail(&conn->listen_list, &amp_rdma_listen_group[(conn->ac_remote_comptype ) % AMP_RDMA_LISTEN_THREAD_NUM]);

#endif
    AMP_DMSG("__amp_queue_conn: after add conn, active_conn_num:%d, total_num:%d\n", cnq->active_conn_num, cnq->total_num);
EXIT:
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    AMP_LEAVE("__amp_queue_conn: leave\n");
    return err;
}


#ifdef __AMP_CONNS_DUPLEX
/*
 * queue a new created connection to the specificed context.
 */
int 
__amp_enqueue_recv_conn(amp_connection_t *conn, amp_comp_context_t *ctxt)
{
    amp_s32_t  err = 0;
    amp_u32_t  type;
    amp_u32_t  id;
    struct list_head *head = NULL;
    conn_queue_t  *cnq = NULL;
    amp_u32_t i, j;
    amp_connection_t **orig_conns = NULL;
    amp_u32_t orig_num;
    amp_u32_t realloc_num;
    amp_u32_t realloc_size;

    
    AMP_ENTER("__amp_queue_recv_conn: enter, conn:%p\n", conn);

    if (!ctxt)  {
        AMP_ERROR("__amp_queue_recv_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn)  {
        AMP_ERROR("__amp_queue_rec_conn: no connection\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns_recv) {
        AMP_ERROR("__amp_queue_recv_conn: no acc_conns in ctxt\n");
        err = -EINVAL;
        goto EXIT;
    }


    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;
    
    AMP_DMSG("__amp_queue_recv_conn: type:%d, id:%d\n", type, id);

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_queue_recv_conn: type(%d) is too large\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns_recv[type].acc_remote_conns) {
        AMP_ERROR("__amp_queue_recv_conn: no remote conns in comp_conns of type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }


    head = &(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue);
    pthread_mutex_lock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));
    cnq = &(ctxt->acc_conns_recv[type].acc_remote_conns[id]);

    list_add_tail(&conn->ac_list, head);
    AMP_DMSG("__amp_queue_recv_conn: before add conn, cnq:%p, active_conn_num:%d,total_num:%d\n", cnq, cnq->active_conn_num, cnq->total_num);
    for (i=0; i<cnq->active_conn_num; i++) {
        if (!cnq->conns[i])
            break;
    }
    AMP_DMSG("__amp_queue_recv_conn: after search, i:%d\n", i);
    if (i >= cnq->active_conn_num) {
        if (cnq->active_conn_num >= cnq->total_num) {
            AMP_DMSG("__amp_queue_recv_conn: need realloc the conns\n");
            orig_conns = cnq->conns;
            orig_num = cnq->total_num;
            realloc_num = cnq->total_num + AMP_SELECT_CONN_ARRAY_ALLOC_LEN;
            realloc_size = realloc_num * sizeof(amp_connection_t *);
            AMP_DMSG("__amp_queue_recv_conn: orig_num:%d, realloc_num:%d\n", orig_num, realloc_num);

            cnq->conns = (amp_connection_t **)amp_alloc(realloc_size);
            if (!cnq->conns) {
                AMP_ERROR("__amp_queue_recv_conn: realloc for conns error\n");
                err = -ENOMEM;
    		pthread_mutex_unlock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));
                goto EXIT;
            }
            memset(cnq->conns, 0, realloc_size);
            for(j=0; j<orig_num; j++)
                cnq->conns[j] = orig_conns[j];
            amp_free(orig_conns, orig_num * sizeof(amp_connection_t *));
            cnq->total_num = realloc_num;
        }
        cnq->conns[i] = conn;
        cnq->active_conn_num++;
    } else {
        cnq->conns[i] = conn;
    }
    AMP_DMSG("__amp_queue_recv_conn: after add conn, active_conn_num:%d, total_num:%d\n", cnq->active_conn_num, cnq->total_num);

    pthread_mutex_unlock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));
EXIT:
    AMP_LEAVE("__amp_queue_recv_conn: leave\n");
    return err;
}

/*
 * remove a connection from context
 */
int 
__amp_dequeue_recv_conn(amp_connection_t *conn, amp_comp_context_t *ctxt)
{

    amp_s32_t  err = 0;
    amp_u32_t  type;
    amp_u32_t  id;
    amp_u32_t  i;
    conn_queue_t *cnq = NULL;
/*
 * modified by weizheng 2013-12-12 when this function is called, the conn's sock is
 * -1, at this monment, if we use the variable, we will cause free segment  
*/
    amp_s32_t  sockfd = conn->ac_sock;

    AMP_ENTER("__amp_dequeue_recv_conn: enter, conn:%p\n", conn);

    if (!ctxt)  {
        AMP_ERROR("__amp_dequeue_recv_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn)  {
        AMP_ERROR("__amp_dequeue_recv_conn: no connection\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns_recv) {
        AMP_ERROR("__amp_dequeue_recv_conn: no acc_conns in ctxt\n");
        err = -EINVAL;
        goto EXIT;
    }


    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;

    AMP_DMSG("__amp_dequeue_recv_conn: type:%d, id:%d\n", type, id);

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_dequeue_recv_conn: type(%d) is too large\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns_recv[type].acc_remote_conns) {
        AMP_ERROR("__amp_dequeue_recv_conn: no remote conns in comp_conns of type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    AMP_DMSG("__amp_dequeue_recv_conn: dequeue the conn\n");
    pthread_mutex_lock(&ctxt->acc_lock);
#ifdef __AMP_LISTEN_SELECT
    FD_CLR(conn->ac_sock,&ctxt->acc_readfds);
#endif
#ifdef __AMP_LISTEN_POLL
    amp_poll_fd_clr(conn->ac_sock, ctxt->acc_poll_list);
#endif
#ifdef __AMP_LISTEN_EPOLL
    amp_epoll_fd_clear(conn->ac_sock, ctxt->acc_epfd);
#endif
    ctxt->acc_conn_table[sockfd] = NULL;
    pthread_mutex_unlock(&ctxt->acc_lock);


    pthread_mutex_lock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));

    cnq = &(ctxt->acc_conns_recv[type].acc_remote_conns[id]);

    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_list))
        list_del_init(&conn->ac_list);
    amp_unlock(&conn->ac_lock);
    

    amp_lock(&amp_dataready_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if (!list_empty(&conn->ac_dataready_list)) {
        list_del_init(&conn->ac_dataready_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_dataready_conn_list_lock);
    
    amp_lock(&amp_reconn_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_reconn_list)){
        list_del_init(&conn->ac_reconn_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_reconn_conn_list_lock);

    for (i=0; i<cnq->active_conn_num; i++) {
        if(cnq->conns[i] == conn)
            break;
    }
    AMP_DMSG("__amp_dequeue_recv_conn: after search, i:%d\n", i);
    if (i >= cnq->active_conn_num) {
        AMP_ERROR("__amp_dequeue_recv_conn: not find the conn:%p in select queue\n", conn);
        pthread_mutex_unlock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));
        goto EXIT;
    }

    cnq->conns[i] = NULL;
    cnq->active_conn_num --;
    if (i < cnq->active_conn_num){
        cnq->conns[i] = cnq->conns[cnq->active_conn_num];
        cnq->conns[cnq->active_conn_num]  = NULL;
    }

    AMP_DMSG("__amp_dequeue_recv_conn: finished, active_conn_num:%d, total_num:%d\n", cnq->active_conn_num, cnq->total_num);
    

    while((cnq->active_conn_num > 0) && (!cnq->conns[cnq->active_conn_num - 1]))
        cnq->active_conn_num --;
    
    AMP_DMSG("__amp_dequeue_recv_conn: active_conn_num:%d, total_num:%d\n", \
                 cnq->active_conn_num, cnq->total_num);
    if(cnq->active_conn_num == 0){
        __amp_remove_resend_reqs(conn, 1, 1);
        __amp_remove_waiting_reply_reqs(conn, 1, 1);
    }else{
        __amp_remove_resend_reqs(conn, 1, 0);
        __amp_remove_waiting_reply_reqs(conn, 1, 0);
    }
 
    pthread_mutex_unlock(&(ctxt->acc_conns_recv[type].acc_remote_conns[id].queue_lock));
    
EXIT:
    AMP_LEAVE("__amp_dequeue_recv_conn: leave\n");
    return err;
}
#endif

/*
 * remove the invalid connection  */
/*int
__amp_dequeue_invalid_conn(amp_connection_t * conn, amp_comp_context_t *ctxt)
{
    conn_queue_t * cnq = NULL;
    amp_connection_t * tmp_conn = NULL;
    struct list_head * pos = NULL;
    struct list_head * nxt = NULL;
    amp_request_t * req = NULL;
    amp_u32_t type = conn->ac_remote_comptype;
    amp_u32_t id = conn->ac_remote_id;
    amp_u32_t ip = conn->ac_remote_ipaddr;
    amp_u32_t port = conn->ac_remote_port;
    amp_s32_t sock = conn->ac_sock;
    amp_u32_t i;
    amp_s32_t err = 0;
    amp_s32_t *sock_fd = NULL; 
    amp_u32_t  sock_num = 0;
    if(!ctxt){
        AMP_ERROR("__amp_dequeue_invalid_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if(!conn){
        AMP_ERROR("__amp_dequeue_invalid_conn: no conn\n");
        err = -EINVAL;
        goto EXIT;
    }

    if(!ctxt->acc_conns) {
        AMP_ERROR("__amp_dequeue_invalid_conn: no acc_conns in ctxt\n");
        err = -EINVAL;
        goto EXIT;
    }


    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_dequeue_invalid_conn: type(%d) is too large\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("__amp_dequeue_invalid_conn: no remote conns in comp_conns of type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    cnq = &(ctxt->acc_conns[type].acc_remote_conns[id]);
    if(cnq->active_conn_num > 0){
        sock_fd = (amp_s32_t *)malloc(cnq->active_conn_num * sizeof(amp_s32_t));
        if(NULL == sock_fd){
            AMP_ERROR("amp_dequeue_invalid_conn: no mem for dequeued invalid sock\n");
            err = -ENOMEM;
	    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
            goto EXIT;
        }
        memset(sock_fd,0,sizeof(amp_s32_t)*cnq->active_conn_num);
    }
    for(i=0; i<cnq->active_conn_num;) {
        tmp_conn = cnq->conns[i];
        if((tmp_conn != ctxt->acc_listen_conn) &&
           (tmp_conn->ac_remote_comptype == type) &&
           (tmp_conn->ac_remote_id == id) &&
           (tmp_conn->ac_remote_ipaddr == ip) &&
           (tmp_conn->ac_remote_port == port)){
            AMP_ERROR("__amp_dequeue_invalid_conn: dequeue invalid conn:%p, remote_type:%d, remote_id:%d, remote_ip:%u, sock:%d\n",tmp_conn, type, id, ip, tmp_conn->ac_sock);
            sock_fd[sock_num++] = tmp_conn->ac_sock;
            amp_lock(&amp_dataready_conn_list_lock);
            amp_lock(&tmp_conn->ac_lock);
            if (!list_empty(&tmp_conn->ac_dataready_list)) {
                list_del_init(&tmp_conn->ac_dataready_list);
            }
            amp_unlock(&tmp_conn->ac_lock);
            amp_unlock(&amp_dataready_conn_list_lock);
    
            amp_lock(&tmp_conn->ac_lock);
            if(!list_empty(&tmp_conn->ac_list))
                list_del_init(&tmp_conn->ac_list);
            amp_unlock(&tmp_conn->ac_lock);
            
            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&tmp_conn->ac_lock);
            if(!list_empty(&tmp_conn->ac_reconn_list)){
                list_del_init(&tmp_conn->ac_reconn_list);
            }
            amp_unlock(&tmp_conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            if(tmp_conn->ac_sock != sock){
                shutdown(tmp_conn->ac_sock,SHUT_RDWR); 
                close(tmp_conn->ac_sock);
                tmp_conn->ac_sock = -1;
            }
            cnq->conns[i] = NULL;
            if(i >= cnq->active_conn_num - 1){
                cnq->active_conn_num --;
                i++;
            }else {
                cnq->conns[i] = cnq->conns[cnq->active_conn_num - 1];
                cnq->conns[cnq->active_conn_num - 1]  = NULL;
                cnq->active_conn_num --;
            }
            //ctxt->acc_conn_table[tmp_conn->ac_sock] = NULL;
        }else
            i++;
    }   
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));

    amp_lock(&amp_waiting_reply_list_lock);
    list_for_each_safe(pos, nxt, &amp_waiting_reply_list)
    {
        req = list_entry(pos, amp_request_t, req_list);
        if(type == req->req_remote_type && id == req->req_remote_id && ip == req->req_conn->ac_remote_ipaddr && port == req->req_conn->ac_remote_port)
        {
            //amp_lock(&req->req_lock);
            list_del_init(&req->req_list);
            //amp_unlock(&req->req_lock);
            if((req->req_type & AMP_REQUEST) && req->req_resent)
                __amp_add_resend_req(req); 
        }
    }
    amp_unlock(&amp_waiting_reply_list_lock);
    pthread_mutex_lock(&ctxt->acc_lock);
    for(i=0;i<sock_num;i++){
#ifdef __AMP_LISTEN_SELECT
        FD_CLR(sock_fd[i],&ctxt->acc_readfds);
#endif
#ifdef __AMP_LISTEN_POLL
        amp_poll_fd_clr(sock_fd[i], ctxt->acc_poll_list);
#endif
#ifdef __AMP_LISTEN_EPOLL
        amp_epoll_fd_clear(sock_fd[i], ctxt->acc_epfd);
#endif
    } 
    pthread_mutex_unlock(&ctxt->acc_lock);
    if(NULL != sock_fd)
        __amp_free(sock_fd);

EXIT:
    return err;
}
*/
/*
 * remove a connection from context
 */
int 
__amp_dequeue_conn(amp_connection_t *conn, amp_comp_context_t *ctxt)
{

    amp_s32_t  err = 0;
    amp_u32_t  type;
    amp_u32_t  id;
    amp_u32_t  i;
    conn_queue_t *cnq = NULL;
/*
 * modified by weizheng 2013-12-12 when this function is called, the conn's sock is
 * -1, at this monment, if we use the variable, we will cause free segment  
*/
    amp_s32_t  sockfd = conn->ac_sock;

    AMP_ENTER("__amp_dequeue_conn: enter, conn:%p\n", conn);

    if (!ctxt)  {
        AMP_ERROR("__amp_dequeue_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn)  {
        AMP_ERROR("__amp_dequeue_conn: no connection\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns) {
        AMP_ERROR("__amp_dequeue_conn: no acc_conns in ctxt\n");
        err = -EINVAL;
        goto EXIT;
    }


    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;

    AMP_DMSG("__amp_dequeue_conn: type:%d, id:%d\n", type, id);

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_dequeue_conn: type(%d) is too large\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt->acc_conns[type].acc_remote_conns) {
        AMP_ERROR("__amp_dequeue_conn: no remote conns in comp_conns of type:%d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    AMP_DMSG("__amp_dequeue_conn: dequeue the conn\n");
    pthread_mutex_lock(&ctxt->acc_lock);
#ifdef __AMP_LISTEN_SELECT
    FD_CLR(conn->ac_sock,&ctxt->acc_readfds);
#endif
#ifdef __AMP_LISTEN_POLL
    amp_poll_fd_clr(conn->ac_sock, ctxt->acc_poll_list);
#endif
#ifdef __AMP_LISTEN_EPOLL
    amp_epoll_fd_clear(conn->ac_sock, ctxt->acc_epfd);
#endif
    ctxt->acc_conn_table[sockfd] = NULL;
    pthread_mutex_unlock(&ctxt->acc_lock);


    pthread_mutex_lock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));

    cnq = &(ctxt->acc_conns[type].acc_remote_conns[id]);

    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_list))
        list_del_init(&conn->ac_list);
    amp_unlock(&conn->ac_lock);

    amp_lock(&amp_dataready_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if (!list_empty(&conn->ac_dataready_list)) {
        list_del_init(&conn->ac_dataready_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_dataready_conn_list_lock);
    
    amp_lock(&amp_reconn_conn_list_lock);
    amp_lock(&conn->ac_lock);
    if(!list_empty(&conn->ac_reconn_list)){
        list_del_init(&conn->ac_reconn_list);
    }
    amp_unlock(&conn->ac_lock);
    amp_unlock(&amp_reconn_conn_list_lock);

    for (i=0; i<cnq->active_conn_num; i++) {
        if(cnq->conns[i] == conn)
            break;
    }
    AMP_DMSG("__amp_dequeue_conn: after search, i:%d\n", i);
    if (i >= cnq->active_conn_num) {
        AMP_ERROR("__amp_dequeue_conn: not find the conn:%p in select queue\n", conn);
        pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        goto EXIT;
    }

    cnq->conns[i] = NULL;
    cnq->active_conn_num --;
    if (i < cnq->active_conn_num){
        cnq->conns[i] = cnq->conns[cnq->active_conn_num];
        cnq->conns[cnq->active_conn_num]  = NULL;
    }

    AMP_DMSG("__amp_dequeue_conn: finished, active_conn_num:%d, total_num:%d\n", cnq->active_conn_num, cnq->total_num);

    while((cnq->active_conn_num > 0) && (!cnq->conns[cnq->active_conn_num - 1]))
        cnq->active_conn_num --;
    
    AMP_DMSG("__amp_dequeue_conn: active_conn_num:%d, total_num:%d\n", \
                 cnq->active_conn_num, cnq->total_num);
    if(cnq->active_conn_num == 0){
        __amp_remove_resend_reqs(conn, 1, 1);
        __amp_remove_waiting_reply_reqs(conn, 1, 1);
    }else{
        __amp_remove_resend_reqs(conn, 1, 0);
        __amp_remove_waiting_reply_reqs(conn, 1, 0);
    }
 
    pthread_mutex_unlock(&(ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
    
EXIT:
    AMP_LEAVE("__amp_dequeue_conn: leave\n");
    return err;
}

/*
 * select a connection belong to specific context.
 * return value:
 *                  0 - normal and retconn contain the returned connection
 *                  1 - no connection connected with the remote peer
 *                  2 - no valid connection with the remote peer.
 *                <0 - something wrong.
 */
#ifdef __AMP_SOCKET_POOL
int 
__amp_select_conn(amp_u32_t type,  
                  amp_u32_t id, 
                  amp_comp_context_t *ctxt,  
                  amp_connection_t **retconn)
{
    amp_s32_t  err = 0;
    //amp_s32_t  times = 0;
    struct list_head *head = NULL;
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_comp_conns_t *cmp_conns = NULL;
    conn_queue_t *cnq = NULL;

    AMP_ENTER("__amp_select_conn: enter\n");

    if (type > AMP_MAX_COMP_TYPE || type <= 0 || id <= 0) {
        AMP_ERROR("__amp_select_conn: wrong type: %d, id: %d\n", type, id);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("__amp_select_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    

    if (!ctxt->acc_conns) {
        AMP_ERROR("__amp_select_conn: no acc_conns in ctxt\n");
        err = 1;
        goto EXIT;
    }
    
    cmp_conns = &(ctxt->acc_conns[type]);

    if (id < 0 || id >= cmp_conns->acc_alloced_num) {
        AMP_ERROR("__amp_select_conn: wrong id:%d\n", id);
        err = -EINVAL;
        goto EXIT;

    }

    if (!cmp_conns->acc_remote_conns) {
        AMP_ERROR("__amp_select_conn: no remote conns \n");
        err = 1;
        goto EXIT;
    }

    cnq = &(cmp_conns->acc_remote_conns[id]);
    head = &(cmp_conns->acc_remote_conns[id].queue);    


    while(1){


        pthread_mutex_lock(&(cmp_conns->acc_remote_conns[id].queue_lock));
        if (cnq->active_conn_num <= 0) {
            AMP_ERROR("__amp_select_conn: type:%d, id:%d, active_conn_num:%d ,wrong\n", type, id, cnq->active_conn_num);
            err = 1;
            pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
            goto EXIT;
        }
        
        err = 2;
        *retconn = NULL;

#if 0
        if(cnq->active_conn_num <= cnq->allocd_num || list_empty(head)){
            err = 1;
            times++;
            AMP_ERROR("__amp_select_conn: type: %d, id: %d, active_conn_num: %d, available_num:%d\n", type,id, cnq->active_conn_num, cnq->active_conn_num-cnq->allocd_num);
            if(times < 5)
                sleep(1);
            else
                break;
        }
#endif
        list_for_each_safe(pos, nxt, head){
            err = 0;
            *retconn = list_entry(pos, amp_connection_t, ac_list);

            if((*retconn)->ac_sock <= 0){
                continue;
            }

            if(pthread_mutex_trylock(&((*retconn)->ac_lock))){
                continue;
            }
            
        /*    if ((*retconn)->ac_state != AMP_CONN_OK){ 
                amp_unlock(&((*retconn)->ac_lock));
                continue;
            }
          */  
            //TODO
            (*retconn)->ac_refcont ++;

            (*retconn)->ac_stage = AMP_CONN_SELECTED;

            //amp_lock(&((*retconn)->ac_lock));
            list_del_init(&((*retconn)->ac_list));
            //list_add_tail(&((*retconn)->ac_list), &cnq->allocd_queue);
            list_add_tail(&((*retconn)->ac_list), &cnq->queue);
            //cnq->allocd_num ++;
            (*retconn)->ac_last_reconn = time(NULL);
            pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
            goto EXIT;
        }

        pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
        usleep(500);
    }

    AMP_DMSG("__amp_select_conn: get connection:%p\n", *retconn);

EXIT:
    AMP_LEAVE("__amp_select_conn: leave\n");
    return err;
}
#else
int 
__amp_select_conn(amp_u32_t type,  
                  amp_u32_t id, 
                  amp_comp_context_t *ctxt,  
                  amp_connection_t **retconn)
{
    struct list_head *head = NULL;
    amp_comp_conns_t *cmp_conns = NULL;
    conn_queue_t *cnq = NULL;
    amp_u32_t count = 0;
    amp_u32_t beginidx = 0;
    amp_u32_t i = 0;
    amp_s32_t  err = 0;

    AMP_ENTER("__amp_select_conn: enter\n");

    if (type > AMP_MAX_COMP_TYPE) {
        AMP_ERROR("__amp_select_conn: wrong type: %d\n", type);
        err = -EINVAL;
        goto EXIT;
    }

    if (!ctxt) {
        AMP_ERROR("__amp_select_conn: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    

    if (!ctxt->acc_conns) {
        AMP_ERROR("__amp_select_conn: no acc_conns in ctxt\n");
        err = 1;
        goto EXIT;
    }
    
    cmp_conns = &(ctxt->acc_conns[type]);

    if (id <= 0 || id >= cmp_conns->acc_alloced_num) {
        AMP_ERROR("__amp_select_conn: wrong id:%d\n", id);
        err = -EINVAL;
        goto EXIT;

    }

    if (!cmp_conns->acc_remote_conns) {
        AMP_ERROR("__amp_select_conn: no remote conns \n");
        err = 1;
        goto EXIT;
    }
    pthread_mutex_lock(&(cmp_conns->acc_remote_conns[id].queue_lock));

    cnq = &(cmp_conns->acc_remote_conns[id]);
    head = &(cmp_conns->acc_remote_conns[id].queue);    
    
    if (list_empty(head)) {
        AMP_ERROR("__amp_select_conn: no connection corresponding to type:%d, id:%d\n",
                          type, id);
        err = 1;
        pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
        goto EXIT;
    }
    if (cnq->active_conn_num <= 0) {
        AMP_ERROR("__amp_select_conn: type:%d, id:%d, active_conn_num:%d ,wrong\n", type, id, cnq->active_conn_num);
        err = 1;
        pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
        goto EXIT;
    }

    
    err = 2;
    *retconn = NULL;

    struct list_head *pos, *nxt;
        AMP_ERROR("amp_select_conn: start ......\n");
        list_for_each_safe(pos, nxt, head){
            *retconn = list_entry(pos, amp_connection_t, ac_list);
            if (*retconn && (*retconn)->ac_state != AMP_CONN_OK){ 
                AMP_ERROR("AMP_SELECT_CONN ...... CONN[%p] IS NOT OK ...\n", *retconn);
                continue;
            }
            if (*retconn && (*retconn)->ac_sock <= 0){ 
                AMP_ERROR("AMP_SELECT_CONN ...... CONN[%p]->ac_sock is %d  ...\n", (*retconn)->ac_sock);
                continue;
            }

            if ((*retconn)->ac_stage == AMP_CONN_SELECTED){ 
            
                AMP_ERROR("AMP_SELECT_CONN ...... CONN[%p] IS SELECTED ...\n", *retconn);
                continue;
            }
            if(pthread_mutex_trylock(&((*retconn)->ac_lock))){
                count++;
                AMP_ERROR("AMP_SELECT_CONN ...... CONN[%p] IS LOCKED ...\n", *retconn);
                continue;
                if(count  >= cnq->active_conn_num && (*retconn)->ac_state == AMP_CONN_OK){
                    continue;
                    //amp_lock(&((*retconn)->ac_lock));
                }else{
                    continue;
                }
            }
            //TODO
            (*retconn)->ac_refcont ++;
            (*retconn)->ac_stage = AMP_CONN_SELECTED;
            err = 0;
            AMP_ERROR("amp_select_conn: conn: %p , sock: %d ......\n", *retconn, (*retconn)->ac_sock);
            list_del_init(&((*retconn)->ac_list));
            list_add_tail(&((*retconn)->ac_list), &cnq->queue);
            (*retconn)->ac_last_reconn = time(NULL);
            pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));
            goto EXIT;
        }

    i = 0;

    do{
        amp_connection_t  *conn = NULL;

        conn = cnq->conns[i];
        if (conn && conn->ac_state == AMP_CONN_OK) {
            *retconn = conn;
            err = 0;
            break;
        }
        i = (i + 1) % cnq->active_conn_num;
    } while (i != beginidx);

    if (*retconn) {
       amp_lock(&((*retconn)->ac_lock));
        //(*retconn)->ac_weight += sendsize;
        (*retconn)->ac_last_reconn = time(NULL);
        AMP_DMSG("__amp_select_conn: get connection:%p\n", *retconn);
        (*retconn)->ac_stage = AMP_CONN_SELECTED;
        list_del_init(&((*retconn)->ac_list));
        list_add_tail(&((*retconn)->ac_list), &cnq->queue);
    }
    pthread_mutex_unlock(&(cmp_conns->acc_remote_conns[id].queue_lock));

EXIT:
    AMP_LEAVE("__amp_select_conn: leave\n");
    return err;
}
#endif
/*
 * revoke all request waiting for resend to the peer corresponding to this 
 * conn.
 */
void
__amp_revoke_resend_reqs(amp_connection_t *conn)
{
    amp_request_t *req  = NULL;
    amp_u32_t type;
    amp_u32_t id;

    AMP_ENTER("__amp_revoke_resend_reqs: enter\n");

    type = conn->ac_remote_comptype;
    id = conn->ac_remote_id;
    while ((req = __amp_find_resend_req(type, id)))  {
        if (req->req_state == AMP_REQ_STATE_RESENT)  {
            AMP_ERROR("__amp_revoke_resend_reqs: find one req:%p\n", req);
            __amp_remove_resend_req(req);
            amp_lock(&amp_sending_list_lock);
            //amp_lock(&req->req_lock);
            if(!__amp_within_sending_list(req)){
                list_add_tail(&req->req_list, &amp_sending_list);
            }
            req->req_state = AMP_REQ_STATE_NORMAL;
            //amp_unlock(&req->req_lock);
            amp_unlock(&amp_sending_list_lock);
            amp_sem_up(&amp_process_out_sem);
        }
    }

    AMP_LEAVE("__amp_revoke_resend_reqs: leave\n");
    return;
}


/*
 * do a connection
 */
int
__amp_do_connection(amp_s32_t **retsock, 
                    struct sockaddr_in *addr,
                    amp_u32_t conn_type,
                    amp_u32_t direction) 
{
    amp_s32_t  err = 0;

    AMP_ENTER("__amp_do_connection: enter\n");

    if (!AMP_HAS_TYPE(conn_type)) {
        AMP_ERROR("__amp_do_connection: no type: %d\n", conn_type);
        err = -ENOSYS;
        goto EXIT;
    }

    if (!AMP_OP(conn_type, proto_connect)) {
        AMP_ERROR("__amp_do_connection: no amp_proto_connect op to type: %d\n", conn_type);
        err = -ENOSYS;
        goto EXIT;
    }
    err = AMP_OP(conn_type, proto_connect)(NULL, \
                                               (void **)retsock, \
                                               (void *)addr, \
                                               direction);
    if (err < 0) 
        AMP_ERROR("__amp_do_connection: connect error, err:%d\n", err);
    else
        AMP_DMSG("__amp_do_connection: retsock: %d\n", **retsock);

EXIT:
    AMP_LEAVE("__amp_do_connection: leave\n");
    return err;
}


/*
 * connect test.
 */ 
int 
__amp_conn_test (amp_connection_t *conn)
{
    amp_s32_t    err = 0;
    amp_message_t   hello_msg;

    AMP_ENTER("__amp_conn_test: enter, conn:%p\n", conn);
return -1;
    if (!conn) {
        AMP_ERROR("__amp_conn_test: no conn provided\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn->ac_remote_ipaddr || !conn->ac_remote_port) {
        AMP_ERROR("__amp_conn_test: the address of remote comp is error\n");
        err = -EINVAL;
        goto EXIT;
    }

    /*
     * send hello to server.
     */
    memset(&hello_msg, 0, sizeof(amp_message_t));
    hello_msg.amh_magic = AMP_REQ_MAGIC;
    hello_msg.amh_type = AMP_HELLO;
    hello_msg.amh_pid = conn->ac_this_type;
    hello_msg.amh_xid = ((amp_u64_t)conn->ac_this_id) << 32;
    //err = AMP_OP(conn->ac_type, proto_sendmsg)(&conn->ac_sock, NULL, 0, sizeof(hello_msg), &hello_msg, 0);
    err = AMP_OP(conn->ac_type, proto_sendmsg_init)(conn, NULL, 0, sizeof(hello_msg), &hello_msg, 0);
    if (err < 0) {
        AMP_ERROR("__amp_conn_test: send hello msg to server error, err:%d\n", err);
        goto EXIT;
    }
    /*
     * receive ack from server
     */
    /*err = AMP_OP(conn->ac_type, proto_recvmsg)(conn, NULL, 0,  sizeof(hello_msg), &hello_msg, 0);      
    //err = AMP_OP(conn->ac_type, proto_recvmsg)(&conn->ac_sock, NULL, 0,  sizeof(hello_msg), &hello_msg, 0);      
    if (err < 0) {
        AMP_ERROR("amp_conn_test: read hello_ack from server error, err:%d\n", err);
        goto EXIT;
    }
    if (hello_msg.amh_type != AMP_HELLO_ACK) {
        err = -EINVAL;
        AMP_ERROR("__amp_conn_test: get a wrong hello ack, type:%d\n", hello_msg.amh_type);
        goto EXIT;
    }*/

EXIT:
    AMP_LEAVE("__amp_conn_test: leave\n");
    return err;
}

#ifdef __AMP_RDMA__
    enum ibv_mtu pp_mtu_to_enum(int num){
        switch(num){
            case 256:  return IBV_MTU_256;
            case 512:  return IBV_MTU_512;
            case 1024: return IBV_MTU_1024;
            case 2048: return IBV_MTU_2048;
            case 4096: return IBV_MTU_4096;
            default: return -1;
        }
    }

int amp_post_recv(amp_connection_t *conn, int n)
{
    int rc = 0;
	struct ibv_sge list = {
		.addr	= (uintptr_t) conn->ac_rdma_recv_buf,
		.length = AMP_RDMA_MR_SIZE,
		.lkey	= conn->ac_rdma_recv_mr->lkey
	};
	struct ibv_recv_wr wr = {
		.wr_id	    = AMP_RECV_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
	};
	struct ibv_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i){
        rc = ibv_post_recv(conn->ac_rdma_qp, &wr, &bad_wr);
		if (rc){
            AMP_ERROR("AMP_POST_RECV post recv failed, rc: %d, errno: %d\n", rc, errno);
            break;
        }
    }

	return i;
}

int amp_post_send(amp_connection_t *conn)
{
	struct ibv_sge list = {
		.addr	= (uintptr_t) conn->ac_rdma_send_buf,
		.length = AMP_RDMA_MR_SIZE,
		.lkey	= conn->ac_rdma_send_mr->lkey
	};
	struct ibv_send_wr wr = {
		.wr_id	    = AMP_SEND_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IBV_WR_SEND,
		.send_flags = IBV_SEND_SIGNALED, 
	};
    if(list.length < AMP_RDMA_MAX_INLINE_DATA)
        wr.send_flags |= IBV_SEND_INLINE;
	struct ibv_send_wr *bad_wr;

	return ibv_post_send(conn->ac_rdma_qp, &wr, &bad_wr);
}

int __amp_post_send(amp_connection_t *conn, int opcode){
    struct ibv_send_wr sr;
    struct ibv_sge     sge;
    struct ibv_send_wr *bad_wr = NULL;
    int    rc  = 0;

    memset(&sge, 0, sizeof(sge));

    sge.addr = (uint64_t)conn->ac_rdma_send_buf;
    sge.length = AMP_RDMA_MR_SIZE;
    sge.lkey = conn->ac_rdma_send_mr->lkey;

    memset(&sr, 0, sizeof(sr));

    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = opcode;
    sr.send_flags = IBV_SEND_SIGNALED;

    if(opcode != IBV_WR_SEND){
        //sr.wr.rdma.remote_addr = 

    }
    
    rc = ibv_post_send(conn->ac_rdma_qp, &sr, &bad_wr);
    if(rc){
        AMP_ERROR("failed to post SR\n");
    }else{
        switch(opcode){
            case IBV_WR_SEND:
                AMP_ERROR("Send Request was posted\n");
                break;
            case IBV_WR_RDMA_READ:
                AMP_ERROR("RDMA Read Request was posted\n");
                break;
            case IBV_WR_RDMA_WRITE:
                AMP_ERROR("RDMA Write Request was posted\n");
                break;
            default:
                AMP_ERROR("Unknown Request was posted\n");
                break;
        }
    }
    return rc;
}


static int amp_connect_ctx(amp_connection_t * conn)
{
	// changed  by mayl
	
	int sel_mtu = get_opt_mtu();
	used_mtu = pp_mtu_to_enum(sel_mtu);
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		//.path_mtu		= IBV_MTU_1024,
		.path_mtu		= used_mtu,
		.dest_qp_num		= conn->ac_rdma_remote_config.qp_num,
		.rq_psn			= conn->ac_rdma_remote_config.psn,
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 0x12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= conn->ac_rdma_remote_config.lid,
			.sl		= 1,
			.src_path_bits	= 0,
			.port_num	= conn->ac_ctxt->acc_rdma_port_num
		}
	};

	//if (conn->ac_ctxt->acc_rdma_gid.global.interface_id) {
	if(1){
		char gid_str[64];
		char * gid_pos = &gid_str[0];
		int slen = 0;
		memset(gid_str,0,64);
	#ifdef __AMP_ROCE__
		attr.ah_attr.grh.hop_limit = 0xff; // 1
		//attr.ah_attr.grh.dgid = conn->ac_ctxt->acc_rdma_gid;
		//mayl: TODO ,copy dgid from remote gid
	//#ifdef __AMP_ROCE__
		attr.ah_attr.is_global = 1;
		memcpy(&(attr.ah_attr.grh.dgid.raw),&(conn->ac_rdma_remote_config.gid) ,16);
		//attr.ah_attr.grh.sgid_index = 3; // 0
		valid_gid_index = get_gid_index();
		attr.ah_attr.grh.sgid_index = valid_gid_index; // 0
		AMP_ERROR("amp_connect_ctx rtr: ib_port_num = %d, sgid_index %d D_GID:\n", attr.ah_attr.port_num, attr.ah_attr.grh.sgid_index);
		for (int i = 0; i<16; i++){
			uint8_t xx = attr.ah_attr.grh.dgid.raw[i];
			slen = sprintf(gid_pos, "%02x-", xx);
			gid_pos += slen;

		}

		AMP_ERROR(":%s: \n", gid_str);
	#else
		//attr.ah_attr.grh.dgid = conn->ac_ctxt->acc_rdma_gid;
	#endif

	}
	if (ibv_modify_qp(conn->ac_rdma_qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
		AMP_ERROR("Failed to modify QP to RTR, err %d\n", errno);
		return 1;
	}


	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 0x12;
	attr.retry_cnt	    = 6;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = conn->ac_rdma_local_config.psn;
	attr.max_rd_atomic  = 1;
	if (ibv_modify_qp(conn->ac_rdma_qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
		AMP_ERROR("Failed to modify QP to RTS\n");
		return 1;
	}

    //int rc = amp_post_recv(conn, AMP_RDMA_MAX_CQE_NUM);
    //int rc = amp_post_recv(conn, 10);
    //AMP_ERROR("-------amp_post_recv, rc: %d------\n", rc);

	return 0;
}


#if 0
//not work
int __amp_conn_exchange (amp_connection_t *conn)
{
    amp_s32_t    err = 0;
    amp_s32_t    rc = 0;
    amp_request_t *req = NULL;

    AMP_ERROR("__amp_conn_exchange: enter, conn:%p\n", conn);

    if (!conn) {
        AMP_ERROR("__amp_conn_test: no conn provided\n");
        rc = -EINVAL;
        return rc;
    }

    if (!conn->ac_remote_ipaddr || !conn->ac_remote_port) {
        AMP_ERROR("__amp_conn_test: the address of remote comp is error\n");
        rc = -EINVAL;
        return rc;
    }

    
    amp_request_t *reqp = NULL;
    rc = __amp_alloc_request(&req);
    if(rc) {
        rc = -ENOMEM;
        return rc;
    }
    req->req_msg = (amp_message_t *)malloc(sizeof(amp_message_t));
    if(!req->req_msg){
        rc = -ENOMEM;
        return rc;
    }

    memset(req->req_msg, 0, sizeof(amp_message_t));
    req->req_msglen = sizeof(amp_message_t);
    req->req_need_ack = 1; 
    reqp->req_resent = 1;
    reqp->req_type = AMP_REQUEST | AMP_MSG;
    reqp->req_niov = 0;
    reqp->req_iov = NULL;
    reqp->req_reply = NULL;

    req->req_msg->rdma_addr = conn->ac_rdma_local_config.addr;
    req->req_msg->rdma_rkey = conn->ac_rdma_local_config.rkey;
    req->req_msg->qp_num = conn->ac_rdma_local_config.qp_num;
    req->req_msg->psn = conn->ac_rdma_local_config.psn;
    req->req_msg->lid = conn->ac_rdma_local_config.lid;
    memcpy(req->req_msg->gid, conn->ac_ctxt->acc_rdma_gid.raw, 16);

    req->req_msg->amh_type = AMP_RDMA_HELLO;

    amp_lock(&req->req_lock);
    //err = AMP_OP(AMP_CONN_TYPE_TCP, proto_sendmsg)(&conn->ac_sock, &conn->ac_remote_addr, sizeof(conn->ac_remote_addr), req->req_msglen, req->req_msg, 0);
    err = AMP_OP(AMP_CONN_TYPE_TCP, proto_sendmsg)(conn, &conn->ac_remote_addr, sizeof(conn->ac_remote_addr), req->req_msglen, req->req_msg, 0);
    if(err < 0){
        AMP_ERROR("amp_send_sync: sendmsg error, conn:%p, req:%p, err:%d\n", conn, req, err);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&req->req_lock);
        return -1;
    }


    amp_unlock(&req->req_lock);

    amp_lock(&amp_waiting_reply_list_lock);

    list_add_tail(&req->req_list, &amp_waiting_reply_list);

    amp_unlock(&amp_waiting_reply_list_lock);

    amp_unlock(&req->req_lock);


    amp_sem_down(&req->req_waitsem);

    amp_message_t *reqp_msg = req->req_reply;

    conn->ac_rdma_remote_config.addr =  reqp_msg->rdma_addr;
    conn->ac_rdma_remote_config.rkey = reqp_msg->rdma_rkey;
    conn->ac_rdma_remote_config.qp_num = reqp_msg->qp_num;
    conn->ac_rdma_remote_config.lid = reqp_msg->lid;
    memcpy(conn->ac_rdma_remote_config.gid, reqp_msg->gid, 16);

    return 0;
 
}
#endif



#endif

/*
 * connect to a server.
 */ 
int 
__amp_connect_server (amp_connection_t *conn)
{
    amp_s32_t    err = 0;
    amp_s32_t  *sock = NULL;
    struct sockaddr_in sin;
    amp_message_t   hello_msg;
    char gid[33];
    //struct timeval tv_start,tv_end;
    //amp_u64_t c_total;

    AMP_ERROR("__amp_connect_server: enter, conn:%p\n", conn);

    if (!conn) {
        AMP_ERROR("__amp_connect_server: no conn provided\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn->ac_remote_ipaddr || !conn->ac_remote_port) {
        AMP_ERROR("__amp_connect_server: the address of remote comp is error\n");
        err = -EINVAL;
        goto EXIT;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(conn->ac_remote_ipaddr);
    sin.sin_port = htons(conn->ac_remote_port);

    err = __amp_do_connection(&sock, &sin, conn->ac_type, AMP_CONN_DIRECTION_CONNECT);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: connect error, type: %d, id: %d\n", conn->ac_remote_comptype, conn->ac_remote_id);
        goto EXIT;
    }
    conn->ac_sock = *sock;

    /*
     * send hello to server.
     */
    memset(&hello_msg, 0, sizeof(amp_message_t));
    hello_msg.amh_magic = AMP_REQ_MAGIC;
    hello_msg.amh_type = AMP_HELLO;
    hello_msg.amh_pid = conn->ac_this_type;
    hello_msg.amh_xid = conn->ac_this_id;
#ifdef __AMP_CONNS_DUPLEX
    hello_msg.amh_duplex = conn->ac_duplex;
#endif

#ifdef __AMP_RDMA__
    
    conn->ac_rdma_send_buf = malloc(AMP_RDMA_MR_SIZE); 
    memset(conn->ac_rdma_send_buf, 0, AMP_RDMA_MR_SIZE);

    conn->ac_rdma_recv_buf = malloc(AMP_RDMA_MR_SIZE);
    memset(conn->ac_rdma_recv_buf, 0, AMP_RDMA_MR_SIZE);

    //int mr_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
    int mr_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    conn->ac_rdma_send_mr = ibv_reg_mr(conn->ac_ctxt->acc_rdma_pd, conn->ac_rdma_send_buf, AMP_RDMA_MR_SIZE, mr_flag);
    conn->ac_rdma_recv_mr = ibv_reg_mr(conn->ac_ctxt->acc_rdma_pd, conn->ac_rdma_recv_buf, AMP_RDMA_MR_SIZE, mr_flag);
    if(!conn->ac_rdma_send_mr){
        AMP_ERROR("the memory region for send is malloced failed\n");
        err = -1;
        goto EXIT;
    }
    if(!conn->ac_rdma_recv_mr){
        AMP_ERROR("the memory region for recv is malloc failed\n");
        err = -1;
        goto EXIT;
    }
#ifdef __AMP_RDMA_EVENT__
    conn->ac_rdma_channel = ibv_create_comp_channel(conn->ac_ctxt->acc_rdma_context);
    if(!conn->ac_rdma_channel){
        AMP_ERROR("cannot create completion channel\n");
        err = -1;
        goto EXIT;
    }
#endif

    conn->ac_rdma_scq = ibv_create_cq(conn->ac_ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, conn->ac_rdma_channel, 0);
    if(!conn->ac_rdma_scq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }

    conn->ac_rdma_rcq = ibv_create_cq(conn->ac_ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, conn->ac_rdma_channel, 0);
    if(!conn->ac_rdma_rcq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }
    memset(&conn->ac_rdma_qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    conn->ac_rdma_qp_init_attr.qp_type = IBV_QPT_RC;
    conn->ac_rdma_qp_init_attr.sq_sig_all = 1;
    //conn->ac_rdma_qp_init_attr.send_cq = conn->ac_ctxt->acc_rdma_scq;
    conn->ac_rdma_qp_init_attr.send_cq = conn->ac_rdma_scq;
    //conn->ac_rdma_qp_init_attr.recv_cq = conn->ac_ctxt->acc_rdma_rcq;
    conn->ac_rdma_qp_init_attr.recv_cq = conn->ac_rdma_rcq;
    conn->ac_rdma_qp_init_attr.cap.max_recv_wr = AMP_RDMA_MAX_CQE_NUM;
    conn->ac_rdma_qp_init_attr.cap.max_send_wr = AMP_RDMA_MAX_CQE_NUM;
    conn->ac_rdma_qp_init_attr.cap.max_recv_sge = 1;
    conn->ac_rdma_qp_init_attr.cap.max_send_sge = 1;
    conn->ac_rdma_qp_init_attr.cap.max_inline_data = AMP_RDMA_MAX_INLINE_DATA;

    conn->ac_rdma_qp = ibv_create_qp(conn->ac_ctxt->acc_rdma_pd, &conn->ac_rdma_qp_init_attr);
    if(!conn->ac_rdma_qp){
        AMP_ERROR("conn->ac_rdma_qp malloced failed\n");
        err = -1;
        goto EXIT;
    }


    /*pkey: identify a partion that the port belongs to, like vlan in ethernet network*/
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = conn->ac_ctxt->acc_rdma_port_num,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
    };

    if(ibv_modify_qp(conn->ac_rdma_qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)){
        AMP_ERROR("failed to modify QP to INIT\n");
        goto EXIT;
    }
    
    //memset(conn->ac_rdma_recv_buf, 0xFF, 1024);
    //conn->ac_rdma_local_config.addr = (uint64_t)conn->ac_rdma_recv_buf + 1024 * 1024;
    conn->ac_rdma_local_config.addr = (uint64_t)conn->ac_rdma_recv_buf;
    conn->ac_rdma_local_config.rkey = conn->ac_rdma_recv_mr->rkey;
    conn->ac_rdma_local_config.qp_num = conn->ac_rdma_qp->qp_num;
    conn->ac_rdma_local_config.lid = conn->ac_ctxt->acc_rdma_port_attr.lid;
    conn->ac_rdma_local_config.psn = lrand48() & 0xffffff;
    conn->remote_msg_start_idx = 0;
    conn->remote_msg_end_idx = 1016;
    conn->remote_data_start_pos = AMP_RDMA_SB_MSG_SIZE;
    conn->remote_data_end_pos = AMP_RDMA_MR_SIZE;
    conn->next_idx = 0;
    conn->last_idx = 0;
    conn->next_bitmap_start = 0;
    conn->next_data_start = AMP_RDMA_SB_MSG_SIZE;
    conn->last_data_end_pos = AMP_RDMA_MR_SIZE;

#ifdef __AMP_ROCE__
    // mayl acc_rdma_gid come from rdma interface GID
    inet_ntop(AF_INET6, &conn->ac_ctxt->acc_rdma_gid.raw, gid, sizeof(gid));
#endif

    AMP_ERROR("do conn %p Locate address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s, FOR connect type: %d, id: %d\n", conn, conn->ac_rdma_local_config.lid, conn->ac_rdma_local_config.qp_num, conn->ac_rdma_local_config.psn, gid, conn->ac_remote_comptype, conn->ac_remote_id);


    hello_msg.rdma_addr = conn->ac_rdma_local_config.addr;
    hello_msg.rdma_rkey = conn->ac_rdma_local_config.rkey;
    hello_msg.qp_num = conn->ac_rdma_local_config.qp_num;
    hello_msg.psn = conn->ac_rdma_local_config.psn;
    hello_msg.lid = conn->ac_rdma_local_config.lid;
#ifdef __AMP_ROCE__
    memcpy(hello_msg.gid, conn->ac_ctxt->acc_rdma_gid.raw, 16);
#endif

    AMP_DMSG("send amp_hello connect type: %d, id: %d\n", conn->ac_remote_comptype, conn->ac_remote_id);
#endif

    //amp_gettimeofday(&tv_start);
    //err = AMP_OP(conn->ac_type, proto_sendmsg)((void *)sock, &sin, sizeof(sin), sizeof(hello_msg), &hello_msg, 0);
    err = AMP_OP(conn->ac_type, proto_sendmsg_init)((void *)conn, &sin, sizeof(sin), sizeof(hello_msg), &hello_msg, 0);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: write header error, type: %d, id: %d, err:%d\n", conn->ac_remote_comptype, conn->ac_remote_id, err);
        goto EXIT_ERR;
    }
    /*
     * receive ack from server
     */
    //err = AMP_OP(conn->ac_type, proto_recvmsg)((void*)sock, &sin, sizeof(sin),  sizeof(hello_msg), &hello_msg, 0);      
    err = AMP_OP(conn->ac_type, proto_recvmsg_init)((void*)conn, &sin, sizeof(sin),  sizeof(hello_msg), &hello_msg, 0);      
    if (err < 0) {
        AMP_ERROR("amp_connect_server: read from server error, type: %d, id: %d, err:%d\n", conn->ac_remote_comptype, conn->ac_remote_id, err);
        goto EXIT_ERR;
    }
    //amp_gettimeofday(&tv_end);
    //c_total = sizeof(hello_msg) * 1000000 / ((tv_end.tv_sec -tv_start.tv_sec)* 1000000 +tv_end.tv_usec -tv_start.tv_usec);
    //c_total = ((c_total == 0) ? 1 : c_total);
    //conn->ac_conn_weigh = (conn->ac_conn_weigh == 0) ? (c_total) : ((c_total + conn->ac_conn_weigh * 9) / 10);
    if (hello_msg.amh_type != AMP_HELLO_ACK) {
        err = -EINVAL;
        AMP_ERROR("__amp_connect_server: get a wrong hello ack, type:%d\n", hello_msg.amh_type);
        goto EXIT_ERR;
    }

    err = AMP_OP(conn->ac_type, proto_init)((void *)sock, AMP_CONN_DIRECTION_CONNECT);
    if (err < 0) {
        AMP_ERROR("amp_connect_server: int socket error, err:%d\n", err);
        goto EXIT_ERR;
    }
   
    struct sockaddr_in slo;
    amp_u32_t slen = sizeof(slo);
    err = getsockname(*sock, (struct sockaddr *) &slo, &slen);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: get local name error, err:%d\n", err);
        goto EXIT_ERR;
    }
    conn->ac_this_port = ntohs(slo.sin_port); 
    conn->ac_remote_addr = sin;
    //conn->ac_sock = *sock;
    conn->ac_state = AMP_CONN_OK;

#ifdef __AMP_RDMA__
    conn->ac_rdma_remote_config.addr = hello_msg.rdma_addr;
    conn->ac_rdma_remote_config.rkey = hello_msg.rdma_rkey;
    conn->ac_rdma_remote_config.qp_num = hello_msg.qp_num;
    conn->ac_rdma_remote_config.psn = hello_msg.psn;
    conn->ac_rdma_remote_config.lid = hello_msg.lid;

#ifdef __AMP_ROCE__
    memcpy(conn->ac_rdma_remote_config.gid, hello_msg.gid, 16);
// mayl: just for print info
    inet_ntop(AF_INET6, &conn->ac_rdma_remote_config.gid, gid, sizeof(gid));
    AMP_ERROR("Remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s, FOR connect: type: %d, id: %d\n", conn->ac_rdma_remote_config.lid, conn->ac_rdma_remote_config.qp_num, conn->ac_rdma_remote_config.psn, gid, conn->ac_remote_comptype, conn->ac_remote_id);

    AMP_ERROR("connect ctx in connect server\n");
#endif
    amp_connect_ctx(conn);

    //INIT_LIST_HEAD(&conn->listen_list);
    //list_add_tail(&conn->listen_list, &amp_rdma_listen_group[conn->ac_remote_id % AMP_RDMA_LISTEN_THREAD_NUM]);

#endif

    amp_free(sock, sizeof(amp_s32_t));


    //__amp_conn_exchange (conn);
EXIT:

    AMP_ERROR("__amp_connect_server: leave\n");
    return err;

EXIT_ERR:
    AMP_OP(conn->ac_type, proto_disconnect)((void*)sock);
    amp_free(sock, sizeof(amp_s32_t));

    AMP_ERROR("__amp_connect_server: exit ERROR 1\n");
    return err;

}

/*
 * connect to a server.
 */ 
/*int 
__amp_connect_server_bk (amp_connection_t *conn)
{
    amp_s32_t    err = 0;
    amp_s32_t  *sock = NULL;
    struct sockaddr_in sin;
    amp_message_t   hello_msg;
    //struct timeval tv_start,tv_end;
    //amp_u64_t c_total;

    AMP_ENTER("__amp_connect_server: enter, conn:%p\n", conn);

    if (!conn) {
        AMP_ERROR("__amp_connect_server: no conn provided\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!conn->ac_remote_ipaddr || !conn->ac_remote_port) {
        AMP_ERROR("__amp_connect_server: the address of remote comp is error\n");
        err = -EINVAL;
        goto EXIT;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(conn->ac_remote_ipaddr);
    sin.sin_port = htons(conn->ac_remote_port);

    err = __amp_do_connection(&sock, &sin, conn->ac_type, AMP_CONN_DIRECTION_CONNECT);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: connect error, type: %d, id: %d\n", conn->ac_remote_comptype, conn->ac_remote_id);
        goto EXIT;
    }

    memset(&hello_msg, 0, sizeof(amp_message_t));
    hello_msg.amh_magic = AMP_REQ_MAGIC;
    hello_msg.amh_type = AMP_HELLO;
    hello_msg.amh_pid = conn->ac_this_type;
    hello_msg.amh_xid = conn->ac_this_id;
#ifdef __AMP_CONNS_DUPLEX
    hello_msg.amh_duplex = conn->ac_duplex;
#endif
    //amp_gettimeofday(&tv_start);
    //err = AMP_OP(conn->ac_type, proto_sendmsg)((void *)sock, &sin, sizeof(sin), sizeof(hello_msg), &hello_msg, 0);
    err = AMP_OP(conn->ac_type, proto_sendmsg_init)((void *)conn, &sin, sizeof(sin), sizeof(hello_msg), &hello_msg, 0);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: write header error, type: %d, id: %d, err:%d\n", conn->ac_remote_comptype, conn->ac_remote_id, err);
        goto EXIT_ERR;
    }

    //err = AMP_OP(conn->ac_type, proto_recvmsg)((void*)sock, &sin, sizeof(sin),  sizeof(hello_msg), &hello_msg, 0);      
    err = AMP_OP(conn->ac_type, proto_recvmsg_init)((void*)conn, &sin, sizeof(sin),  sizeof(hello_msg), &hello_msg, 0);      
    if (err < 0) {
        AMP_ERROR("amp_connect_server: read from server error, type: %d, id: %d, err:%d\n", conn->ac_remote_comptype, conn->ac_remote_id, err);
        goto EXIT_ERR;
    }
    //amp_gettimeofday(&tv_end);
    //c_total = sizeof(hello_msg) * 1000000 / ((tv_end.tv_sec -tv_start.tv_sec)* 1000000 +tv_end.tv_usec -tv_start.tv_usec);
    //c_total = ((c_total == 0) ? 1 : c_total);
    //conn->ac_conn_weigh = (conn->ac_conn_weigh == 0) ? (c_total) : ((c_total + conn->ac_conn_weigh * 9) / 10);
    if (hello_msg.amh_type != AMP_HELLO_ACK) {
        err = -EINVAL;
        AMP_ERROR("__amp_connect_server: get a wrong hello ack, type:%d\n", hello_msg.amh_type);
        goto EXIT_ERR;
    }

    err = AMP_OP(conn->ac_type, proto_init)((void *)sock, AMP_CONN_DIRECTION_CONNECT);
    if (err < 0) {
        AMP_ERROR("amp_connect_server: int socket error, err:%d\n", err);
        goto EXIT_ERR;
    }
   
    struct sockaddr_in slo;
    amp_u32_t slen = sizeof(slo);
    err = getsockname(*sock, (struct sockaddr *) &slo, &slen);
    if (err < 0) {
        AMP_ERROR("__amp_connect_server: get local name error, err:%d\n", err);
        goto EXIT_ERR;
    }
    conn->ac_this_port = ntohs(slo.sin_port); 
    conn->ac_remote_addr = sin;
    conn->ac_sock = *sock;
    conn->ac_state = AMP_CONN_OK;
    
    amp_free(sock, sizeof(amp_s32_t));

EXIT:

    AMP_LEAVE("__amp_connect_server: leave\n");
    return err;

EXIT_ERR:
    AMP_OP(conn->ac_type, proto_disconnect)((void*)sock);
    amp_free(sock, sizeof(amp_s32_t));
    return err;

}
*/

/*
 * accept a connection from client
 * 
 * sockparent - the listen socket
 * childconn - the new conn need to be initialized after create it
 */
int 
__amp_accept_connection (amp_s32_t *sockparent, amp_connection_t *childconn)
{
    amp_s32_t err = 0;
    amp_message_t  msghd;
    amp_u32_t conn_type;
    amp_s32_t *childsock = NULL;
    struct sockaddr_in sin;
    struct sockaddr_in slo;
    amp_u32_t  slen;
    char gid[33];

    AMP_ENTER("__amp_accept_connection: enter, childconn:%p\n", childconn);

    if (!sockparent) {
        AMP_ERROR("__amp_accept_connection: no parent socket\n");
        err = -EINVAL;
        goto EXIT;
    }

    if (!childconn) {
        AMP_ERROR("__amp_accept_connection: no child conn\n");
        err = -EINVAL;
        goto EXIT;
    }
    
    conn_type = childconn->ac_type;
    

    if (conn_type != AMP_CONN_TYPE_TCP) {
        AMP_ERROR("__amp_accept_connection: not tcp, so needn't connection\n");
        err = -EINVAL;
        goto EXIT;
    }

    /*
     * 1. accept the connection
     */
    err = AMP_OP(conn_type, proto_connect)((void*)sockparent,\
                                               (void**)&childsock, NULL, \
                                               AMP_CONN_DIRECTION_ACCEPT);
    
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: accept error, err:%d\n", err);
        goto EXIT;
    }

    if (!childsock) {
        AMP_ERROR("__amp_accept_connection: no child sock return\n");
        err = -EPROTO;
        goto EXIT;
    }

    slen = sizeof(sin);
    err = getpeername(*childsock, (struct sockaddr *) &sin, &slen);
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: get name error, err:%d\n", err);
        goto EXIT_ERR;
    }
    
    slen = sizeof(slo);
    err = getsockname(*childsock, (struct sockaddr *) &slo, &slen);
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: get local name error, err:%d\n", err);
        goto EXIT_ERR;
    }
    
    
    childconn->ac_sock = *childsock;


    /*
     * accept the hello msg.
     */
    //err = AMP_OP(conn_type, proto_recvmsg)((void *)childsock, NULL,  0, sizeof(amp_message_t), &msghd, 0);
    err = AMP_OP(conn_type, proto_recvmsg_init)((void *)childconn, NULL,  0, sizeof(amp_message_t), &msghd, 0);
    
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: receive msg head error, err:%d\n", err);
        goto EXIT_ERR;
    }

    if (msghd.amh_magic != AMP_REQ_MAGIC) {
        err = -1;
        AMP_ERROR("__amp_accept_connection: receive msg head error, err:%d\n", err);
        goto EXIT_ERR;
    }

    if (msghd.amh_type != AMP_HELLO) {
        err = -1;
        AMP_ERROR("__amp_accept_connection: receive msg head error, err:%d\n", err);
        goto EXIT_ERR;  

    }

    /*
     * initialize the connection.
     */
    AMP_ERROR("__amp_accept_connection: remote_id:%lld, remote_type:%d\n", msghd.amh_xid, msghd.amh_pid);

    childconn->ac_remote_id = msghd.amh_xid;
    childconn->ac_remote_comptype = msghd.amh_pid;
    childconn->ac_remote_ipaddr = ntohl(sin.sin_addr.s_addr);
    childconn->ac_remote_port = ntohs(sin.sin_port);
    childconn->ac_this_port = ntohs(slo.sin_port);
    childconn->ac_remote_addr = sin;
    childconn->ac_need_reconn = 0;// by weizheng 2013-12-20 the server's reconn falsg, as the recomand to define, --1:reconn, 0 :no reconn
    childconn->ac_state = AMP_CONN_OK;

#ifdef __AMP_CONNS_DUPLEX
    childconn->ac_duplex = (0 == msghd.amh_duplex ? 1 : 0);
#endif
    /*
     * init the socket.
     */
    err = AMP_OP(conn_type, proto_init)((void* )childsock, AMP_CONN_DIRECTION_ACCEPT);
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: init socket error, err:%d\n", err);
        goto EXIT_ERR;
    }
    
    childconn->ac_state = AMP_CONN_OK;

#ifdef __AMP_RDMA__
    
    AMP_ERROR("__amp_accept_connection: for RDMA\n");
    childconn->ac_rdma_remote_config.addr = msghd.rdma_addr;
    childconn->ac_rdma_remote_config.rkey = msghd.rdma_rkey;
    childconn->ac_rdma_remote_config.qp_num = msghd.qp_num;
    childconn->ac_rdma_remote_config.psn = msghd.psn;
    childconn->ac_rdma_remote_config.lid = msghd.lid;
    memcpy(childconn->ac_rdma_remote_config.gid, msghd.gid, 16);
    childconn->remote_msg_start_idx = 0;
    childconn->remote_msg_end_idx = 1016;
    childconn->remote_data_start_pos = AMP_RDMA_SB_MSG_SIZE;
    childconn->remote_data_end_pos = AMP_RDMA_MR_SIZE;
    childconn->next_idx = 0;
    childconn->last_idx = 0;
    childconn->next_bitmap_start = 0;
    childconn->next_data_start = AMP_RDMA_SB_MSG_SIZE;
    childconn->last_data_end_pos = AMP_RDMA_MR_SIZE;

    childconn->ac_rdma_send_buf = malloc(AMP_RDMA_MR_SIZE); 
    memset(childconn->ac_rdma_send_buf, 0, AMP_RDMA_MR_SIZE);

    childconn->ac_rdma_recv_buf = malloc(AMP_RDMA_MR_SIZE);
    memset(childconn->ac_rdma_recv_buf, 0, AMP_RDMA_MR_SIZE);

    int mr_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;

    childconn->ac_rdma_send_mr = ibv_reg_mr(childconn->ac_ctxt->acc_rdma_pd, childconn->ac_rdma_send_buf, AMP_RDMA_MR_SIZE, mr_flag);
    childconn->ac_rdma_recv_mr = ibv_reg_mr(childconn->ac_ctxt->acc_rdma_pd, childconn->ac_rdma_recv_buf, AMP_RDMA_MR_SIZE, mr_flag);
    if(!childconn->ac_rdma_send_mr){
        AMP_ERROR("the memory region for send is malloced failed\n");
        err = -1;
        goto EXIT;
    }
    if(!childconn->ac_rdma_recv_mr){
        AMP_ERROR("the memory region for recv is malloc failed\n");
        err = -1;
        goto EXIT;
    }

#ifdef __AMP_RDMA_EVENT__
    childconn->ac_rdma_channel = ibv_create_comp_channel(childconn->ac_ctxt->acc_rdma_context);
    if(!childconn->ac_rdma_channel){
        AMP_ERROR("cannot create completion channel\n");
        err = -1;
        goto EXIT;
    }
#endif

    childconn->ac_rdma_scq = ibv_create_cq(childconn->ac_ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, childconn->ac_rdma_channel, 0);
    if(!childconn->ac_rdma_scq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }

    childconn->ac_rdma_rcq = ibv_create_cq(childconn->ac_ctxt->acc_rdma_context, AMP_RDMA_MAX_CQE_NUM, NULL, childconn->ac_rdma_channel, 0);
    if(!childconn->ac_rdma_rcq){
        AMP_ERROR("complete queue malloc failed\n");
        err = -1;
        goto EXIT;
    }

    memset(&childconn->ac_rdma_qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    childconn->ac_rdma_qp_init_attr.qp_type = IBV_QPT_RC;
    childconn->ac_rdma_qp_init_attr.sq_sig_all = 1;
    //childconn->ac_rdma_qp_init_attr.send_cq = childconn->ac_ctxt->acc_rdma_scq;
    childconn->ac_rdma_qp_init_attr.send_cq = childconn->ac_rdma_scq;
    //childconn->ac_rdma_qp_init_attr.recv_cq = childconn->ac_ctxt->acc_rdma_rcq;
    childconn->ac_rdma_qp_init_attr.recv_cq = childconn->ac_rdma_rcq;
    childconn->ac_rdma_qp_init_attr.cap.max_recv_wr = AMP_RDMA_MAX_CQE_NUM;
    childconn->ac_rdma_qp_init_attr.cap.max_send_wr = AMP_RDMA_MAX_CQE_NUM;
    childconn->ac_rdma_qp_init_attr.cap.max_recv_sge = 1;
    childconn->ac_rdma_qp_init_attr.cap.max_send_sge = 1;
    childconn->ac_rdma_qp_init_attr.cap.max_inline_data = AMP_RDMA_MAX_INLINE_DATA;

    childconn->ac_rdma_qp = ibv_create_qp(childconn->ac_ctxt->acc_rdma_pd, &childconn->ac_rdma_qp_init_attr);
    if(!childconn->ac_rdma_qp){
        AMP_ERROR("conn->ac_rdma_qp malloced failed\n");
        err = -1;
        goto EXIT;
    }

    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = childconn->ac_ctxt->acc_rdma_port_num,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
    };

    if(ibv_modify_qp(childconn->ac_rdma_qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)){
        AMP_ERROR("failed to modify QP to INIT\n");
        goto EXIT;
    }
    
    //childconn->ac_rdma_local_config.addr = (uint64_t)childconn->ac_rdma_recv_buf + 1024 * 1024;
    childconn->ac_rdma_local_config.addr = (uint64_t)childconn->ac_rdma_recv_buf;
    childconn->ac_rdma_local_config.rkey = childconn->ac_rdma_recv_mr->rkey;
    childconn->ac_rdma_local_config.qp_num = childconn->ac_rdma_qp->qp_num;

    childconn->ac_rdma_local_config.lid = childconn->ac_ctxt->acc_rdma_port_attr.lid;
    childconn->ac_rdma_local_config.psn = lrand48() & 0xffffff;

    inet_ntop(AF_INET6, &childconn->ac_rdma_remote_config.gid, gid, sizeof(gid));


    AMP_ERROR(" child conn %p Accepted RDMA Remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s, FOR connect: type: %d, id: %d\n", childconn ,childconn->ac_rdma_local_config.lid, childconn->ac_rdma_local_config.qp_num, childconn->ac_rdma_local_config.psn, gid, childconn->ac_remote_comptype, childconn->ac_remote_id);


    msghd.rdma_addr = childconn->ac_rdma_local_config.addr;
    msghd.rdma_rkey = childconn->ac_rdma_local_config.rkey;
    msghd.qp_num = childconn->ac_rdma_local_config.qp_num;
    msghd.psn = childconn->ac_rdma_local_config.psn;
    msghd.lid = childconn->ac_rdma_local_config.lid;
    //TODO: mayl should get the correct local gid and exchange to peer node
    //memcpy(msghd.gid, childconn->ac_rdma_local_config.gid, 16);
    memcpy(msghd.gid, childconn->ac_ctxt->acc_rdma_gid.raw, 16);
    memcpy(childconn->ac_rdma_local_config.gid, childconn->ac_ctxt->acc_rdma_gid.raw, 16);

#endif
    /*
     * send back ack.
     */
    msghd.amh_type = AMP_HELLO_ACK;
    //err = AMP_OP(conn_type, proto_sendmsg)(childsock, NULL, 0, sizeof(amp_message_t), &msghd, 0);
    err = AMP_OP(conn_type, proto_sendmsg_init)(childconn, NULL, 0, sizeof(amp_message_t), &msghd, 0);
    if (err < 0) {
        AMP_ERROR("__amp_accept_connection: send back hello ack error, err:%d\n", err);
        childconn->ac_state = AMP_CONN_BAD;
        goto EXIT_ERR;
    }


#ifdef __AMP_RDMA__

    AMP_ERROR("connect ctx in accept connection\n");
    amp_connect_ctx(childconn);

    //INIT_LIST_HEAD(&childconn->listen_list);
    //list_add_tail(&childconn->listen_list, &amp_rdma_listen_group[childconn->ac_remote_id * AMP_CONN_NUM  % AMP_RDMA_LISTEN_THREAD_NUM]);

#endif

    
    amp_free(childsock, sizeof(amp_s32_t));
    err = 0;      

    AMP_DMSG("__amp_accept_conn: remote %d-%d:%d, sock: %d\n", childconn->ac_remote_comptype, childconn->ac_remote_id, childconn->ac_remote_port, childconn->ac_sock);
EXIT:
    AMP_LEAVE("__amp_accept_connection: leave\n");
    return err;

EXIT_ERR:
    AMP_OP(conn_type, proto_disconnect)((void*)childsock);
    amp_free(childsock, sizeof(amp_s32_t));
    return err;
}
    


/*
 * return the hash value.
 */
amp_u32_t 
__amp_hash(amp_u32_t type, amp_u32_t id)
{
    amp_u32_t  hashvalue;

    hashvalue = ((~type) ^ ~(id)) & ((type << 8) ^ (id << 7));

    hashvalue = hashvalue % AMP_RESEND_HTB_SIZE;

    return hashvalue;
}
#ifdef __AMP_RECONFIRM_MSG
amp_u32_t
__amp_reconfirm_hash(amp_u32_t type, amp_u32_t id, amp_u64_t amh_sender_handle)
{
    amp_u32_t hashvalue;
    //hashvalue = ((~type) ^ ~(id)) & ((type << 8) ^ (id << 7));
    //hashvalue = hashvalue & ((amh_sender_handle & 0xffffff) ^ ((amh_sender_handle >> 24 )& 0xffffff));
    hashvalue = amh_sender_handle & 0xFFFFFFFF >> 7;
    hashvalue = hashvalue % AMP_RESEND_HTB_SIZE;
    return hashvalue;
}
    
amp_u32_t
__amp_reconfirm_hash_c(amp_u64_t amh_sender_handle)
{
    amp_u32_t hashvalue;
    hashvalue = ((amh_sender_handle & 0xffffff) ^ ((amh_sender_handle >> 24 )& 0xffffff));
    hashvalue = hashvalue % AMP_RESEND_HTB_SIZE;
    return hashvalue;
}
#endif

/*
 * add a request to a resend hash table.
 */
void 
__amp_add_resend_req(amp_request_t *req)
{
    amp_u32_t  hashvalue;
    amp_htb_entry_t *htbentry = NULL;

    hashvalue = __amp_hash(req->req_remote_type, req->req_remote_id);
    htbentry = amp_resend_hash_table + hashvalue;
    amp_lock(&htbentry->lock);
    //amp_lock(&req->req_lock);
    if(!__amp_within_resend_req(req) && list_empty(&req->req_list))
    {
        list_add_tail(&req->req_list, &htbentry->queue);
    }
    req->req_state = AMP_REQ_STATE_RESENT;
    //amp_unlock(&req->req_lock);
    amp_unlock(&htbentry->lock);

    AMP_LEAVE("__amp_add_resend_req: leave\n");
    return;
}

/*
 * remove the request from the resend hash table.
 */
void __amp_remove_resend_req(amp_request_t *req)
{
    amp_u32_t hashvalue;
    amp_htb_entry_t *htbentry = NULL;

    AMP_ENTER("__amp_remove_resend_req: enter, req:%p\n", req);
    if (req->req_state != AMP_REQ_STATE_RESENT)  {
        AMP_WARNING("__amp_remove_resend_req: remove non resend req\n");
        goto EXIT;
    }
        

    hashvalue = __amp_hash(req->req_remote_type, req->req_remote_id);
    htbentry = amp_resend_hash_table + hashvalue;

    amp_lock(&htbentry->lock);
    //amp_lock(&req->req_lock);
    if(__amp_within_resend_req(req))
        list_del_init(&req->req_list);
    //amp_unlock(&req->req_lock);
    amp_unlock(&htbentry->lock);
    
EXIT:
    AMP_LEAVE("__amp_remove_resend_req: leave\n");
    return;
}

void
__amp_remove_resend_reqs(amp_connection_t *conn, amp_u32_t force, amp_u32_t no_conns)
{
    amp_htb_entry_t *htbentry = NULL;
    amp_request_t *req = NULL;
    struct list_head * pos = NULL;
    struct list_head * nxt = NULL;
    amp_u32_t remote_type = conn->ac_remote_comptype;
    amp_u32_t remote_id = conn->ac_remote_id;

    htbentry = amp_resend_hash_table + __amp_hash(remote_type, remote_id);
    amp_lock(&htbentry->lock);
    list_for_each_safe(pos, nxt, &htbentry->queue)
    {
        req = list_entry(pos, amp_request_t, req_list);
        if((!req->req_resent||force) && req->req_conn == conn)
        {
            //amp_lock(&req->req_lock);
            list_del_init(&req->req_list);
            if(no_conns)
                req->req_error = -ENOTCONN;
            else
                req->req_error = -ENETUNREACH;
            //amp_unlock(&req->req_lock);
            amp_sem_up(&req->req_waitsem);
        }
    }
    amp_unlock(&htbentry->lock);
    return;
}

void
__amp_remove_waiting_reply_reqs(amp_connection_t *conn, amp_u32_t force, amp_u32_t no_conns)
{
    amp_request_t *req = NULL;
    struct list_head * pos = NULL;
    struct list_head * nxt = NULL;

    amp_lock(&amp_waiting_reply_list_lock);
    if(list_empty(&amp_waiting_reply_list)){
        amp_unlock(&amp_waiting_reply_list_lock);
        return;
    }
    list_for_each_safe(pos, nxt, &amp_waiting_reply_list)
    {
        req = list_entry(pos, amp_request_t, req_list);
        if((!req->req_resent||force) && req->req_conn == conn)
        {
            //amp_lock(&req->req_lock);
            list_del_init(&req->req_list);
            if(no_conns)
                req->req_error = -ENOTCONN;
            else
                req->req_error = -ENETUNREACH;
            //amp_unlock(&req->req_lock);
            amp_sem_up(&req->req_waitsem);
        }
    }
    amp_unlock(&amp_waiting_reply_list_lock);
    return;
}

int 
__amp_within_waiting_reply_list(amp_request_t * req)
{
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_request_t * tmpreq = NULL;
    amp_u32_t type = req->req_remote_type;
    amp_u32_t id = req->req_remote_id;

    list_for_each_safe(pos, nxt, &amp_waiting_reply_list) {
        tmpreq = list_entry(pos, amp_request_t, req_list);
        if(tmpreq->req_remote_id != id || tmpreq->req_remote_type != type)
            continue;
        if (tmpreq != req)
            continue;
        return 1;
    }
    return 0;
}

int 
__amp_within_resend_req( amp_request_t * req)
{
    amp_htb_entry_t *htbentry = NULL;
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_request_t * tmpreq = NULL;
    amp_u32_t type = req->req_remote_type;
    amp_u32_t id = req->req_remote_id;

    htbentry = amp_resend_hash_table + __amp_hash(type, id);

    list_for_each_safe(pos, nxt, &htbentry->queue) {
        tmpreq = list_entry(pos, amp_request_t, req_list);
        if(tmpreq->req_remote_id != id || tmpreq->req_remote_type != type)
            continue;
        if (tmpreq != req)
            continue;
        return 1;
    }
    return 0;
}

#ifdef __AMP_RECONFIRM_MSG
int
__amp_within_reconfirm_htb(amp_request_t * req)
{
    amp_htb_entry_t *htbentry = NULL;
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_reconfirm_msg_t * reconf = NULL;
    amp_u64_t amh_sender_handle = req->req_msg->amh_sender_handle;
    amp_u64_t amh_xid = req->req_msg->amh_xid;
    amp_u64_t bt_time = 0;
    amp_time_t amh_send_ts = req->req_msg->amh_send_ts;
    amp_u32_t amh_pid = req->req_msg->amh_pid;
    amp_u32_t hashvalue = __amp_reconfirm_hash(amh_pid, (amp_u32_t)(amh_xid >> 32), amh_sender_handle);
    amp_u32_t err = 0;
    htbentry = amp_reconfirm_hash_table_s + hashvalue;
    
    amp_lock(&htbentry->lock);
    list_for_each_safe(pos, nxt, &htbentry->queue){
        reconf = list_entry(pos, amp_reconfirm_msg_t, reconf_list);
        if(amh_sender_handle != reconf->reconf_sender_handle ||
                amh_xid != reconf->reconf_xid || 
                amh_pid != reconf->reconf_pid)
            continue;

        bt_time = (amh_send_ts.sec - reconf->reconf_send_ts.sec)*1000000 + amh_send_ts.usec - reconf->reconf_send_ts.usec;
   
        if(bt_time < 0){
            //the request is not right, is old, no process
            goto PROCESSING_REQUEST;
        }else if(bt_time > 0){
            //free old struct
            amp_lock(&reconf->reconf_lock);
            list_del_init(&reconf->reconf_list);
            amp_unlock(&reconf->reconf_lock);
            __amp_free(reconf->reconf_reply_msg);
            __amp_free(reconf);
            goto NEW_REQUEST;
        }else{
            if(NULL != reconf->reconf_reply_msg){
                goto PROCESSED_REQUEST;
            }
            goto PROCESSING_REQUEST;
        }
    }

NEW_REQUEST:
    reconf = (amp_reconfirm_msg_t *)malloc(sizeof(amp_reconfirm_msg_t));
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

    err = 0;
    goto EXIT;
PROCESSING_REQUEST:
    err = 1;
    goto EXIT;
PROCESSED_REQUEST:
    req->req_reply = reconf->reconf_reply_msg;
    req->req_replylen = reconf->reconf_size + sizeof(amp_message_t);
    req->req_need_ack = 0;
    req->req_type = AMP_REPLY | AMP_MSG;
    err = 2;
    goto EXIT;
EXIT:
    amp_unlock(&htbentry->lock);
    return err;
}

int
__amp_within_reconfirm_htb_c(amp_request_t * req)
{
    amp_htb_entry_t *htbentry = NULL;
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_reconfirm_msg_c_t * reconf = NULL;
    if(NULL == req->req_msg){
        return -EINVAL;
    }
    amp_u64_t amh_sender_handle = req->req_msg->amh_sender_handle;
    amp_u64_t amh_xid = req->req_msg->amh_xid;
    amp_u32_t amh_pid = req->req_msg->amh_pid;
    amp_u32_t hashvalue = __amp_reconfirm_hash_c(amh_sender_handle);
    amp_u32_t err = 0;
    htbentry = amp_reconfirm_hash_table_c + hashvalue;
    
    amp_lock(&htbentry->lock);
    list_for_each_safe(pos, nxt, &htbentry->queue){
        reconf = list_entry(pos, amp_reconfirm_msg_c_t, reconf_list);
        if(amh_sender_handle != reconf->reconf_sender_handle ||
                amh_xid != reconf->reconf_xid || 
                amh_pid != reconf->reconf_pid)
            continue;
            goto RECONFIRM_UPDATE;
    }

    reconf = (amp_reconfirm_msg_c_t *)malloc(sizeof(amp_reconfirm_msg_c_t));
    if(NULL == reconf){
        AMP_ERROR("__amp_within_reconfirm_htb_c malloc for reconf failed\n");
        err = -ENOMEM;
        goto EXIT;
    }
    memset(reconf, 0, sizeof(amp_reconfirm_msg_c_t));
    amp_lock_init(&reconf->reconf_lock);
    amp_lock(&reconf->reconf_lock);
    list_add(&reconf->reconf_list, &htbentry->queue);
    amp_unlock(&reconf->reconf_lock);

RECONFIRM_UPDATE:
    reconf->reconf_pid = req->req_msg->amh_pid;
    reconf->reconf_xid = req->req_msg->amh_xid;
    reconf->reconf_sender_handle = req->req_msg->amh_sender_handle;
    reconf->reconf_send_ts.sec = req->req_msg->amh_send_ts.sec;
    reconf->reconf_send_ts.usec = req->req_msg->amh_send_ts.usec;
    err = 0;
    
EXIT:
    amp_unlock(&htbentry->lock);
    return err;
}
#endif

int 
__amp_within_sending_list(amp_request_t * req)
{
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_request_t * tmpreq = NULL;

    list_for_each_safe(pos, nxt, &amp_sending_list) {
        tmpreq = list_entry(pos, amp_request_t, req_list);
        if (tmpreq != req)
            continue;
        return 1;
    }

    return 0;
}
    
int 
__amp_within_reconn_conn_list(amp_connection_t * conn)
{
    struct list_head *pos = NULL;
    struct list_head *nxt = NULL;
    amp_connection_t * tmpconn = NULL;

    list_for_each_safe(pos, nxt, &amp_reconn_conn_list) {
        tmpconn = list_entry(pos, amp_connection_t, ac_reconn_list);
        if (tmpconn != conn)
            continue;
        return 1;
    }

    return 0;
}


/*
 * find any resend request corresponding with this type and id.
 */
amp_request_t *
__amp_find_resend_req(amp_u32_t type, amp_u32_t id)
{
    amp_request_t *req = NULL;
    amp_u32_t hashvalue;
    amp_htb_entry_t *htbentry = NULL;
    struct list_head *pos;
    
    AMP_ENTER("__amp_find_resend_req: enter\n");
    AMP_ENTER("__amp_find_resend_req: type:%d, id:%d\n", type, id);
    
    hashvalue = __amp_hash(type, id);

    htbentry = amp_resend_hash_table + hashvalue;
    amp_lock(&htbentry->lock);

    list_for_each(pos, &htbentry->queue) {
        req = list_entry(pos, amp_request_t, req_list);
        if (req->req_remote_type != type)
            continue;
        if (req->req_remote_id != id)
            continue;

        AMP_LEAVE("__amp_find_resend_req: find one req:%p\n", req);
        goto EXIT;
    }

    req = NULL;

EXIT:
    amp_unlock(&htbentry->lock);
    AMP_LEAVE("__amp_find_resend_req: leave\n");
    return req;
}

int
__amp_add_to_listen_fdset(amp_connection_t *conn)
{
    amp_comp_context_t *ctxt = NULL;
    amp_s32_t  err = 0;
    amp_s32_t  sockfd;
    amp_s32_t  notifyid = 1;

    AMP_DMSG("__amp_add_to_listen_fdset: enter, conn:%p, fd:%d\n",
                 conn, conn->ac_sock);
    ctxt = conn->ac_ctxt;
    if (!ctxt) {
        AMP_ENTER("__amp_add_to_listen_fdset: no context\n");
        err = -EINVAL;
        goto EXIT;
    }
    sockfd = conn->ac_sock;
    pthread_mutex_lock(&ctxt->acc_lock);
    ctxt->acc_conn_table[sockfd] = conn;
    AMP_DMSG("__amp_add_to_listen_fdset: ctxt->acc_conn_table[%d] : %p ......\n", sockfd, conn);

// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT

    FD_SET(sockfd, &ctxt->acc_readfds);
    if (sockfd > ctxt->acc_maxfd)
        ctxt->acc_maxfd = sockfd;

#endif
#ifdef __AMP_LISTEN_POLL

    amp_poll_fd_set( sockfd, ctxt->acc_poll_list );
    if (sockfd > ctxt->acc_maxfd)
        ctxt->acc_maxfd = sockfd;

#endif
#ifdef __AMP_LISTEN_EPOLL

    amp_epoll_fd_set( sockfd, ctxt->acc_epfd );

#endif
// -----------------------------------------------------------------

    pthread_mutex_unlock(&ctxt->acc_lock);
    write(ctxt->acc_notifyfd, (char *)&notifyid, sizeof(amp_s32_t));
EXIT:
    AMP_LEAVE("__amp_add_to_listen_fdset: leave\n");
    return 0;
}
/*end of file*/
