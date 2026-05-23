/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/
#include <amp_help.h>
#include<amp_conn.h>
#include<amp_request.h>
#include<amp_protos.h>
#include<amp_thread.h>

//amp_u64_t  amp_debug_mask = AMP_DEBUG_ERROR | AMP_DEBUG_WARNING |AMP_DEBUG_ENTRY | AMP_DEBUG_LEAVE | AMP_DEBUG_MSG;
//amp_u64_t  amp_debug_mask = AMP_DEBUG_ERROR | AMP_DEBUG_WARNING |AMP_DEBUG_ENTRY | AMP_DEBUG_LEAVE;
//amp_u64_t  amp_debug_mask = AMP_DEBUG_ERROR | AMP_DEBUG_WARNING;
amp_u64_t  amp_debug_mask = AMP_DEBUG_ERROR;

/*
 * totally reset the debug mask 
 */ 
void 
amp_reset_debug_mask (amp_u64_t newmask)
{
    amp_debug_mask = newmask;
}

void 
amp_add_debug_bits (amp_u64_t mask_bits) 
{
    amp_debug_mask |= mask_bits;
}

void 
amp_clear_debug_bits (amp_u64_t mask_bits)
{
    amp_debug_mask &= (~mask_bits);
}

/*
 *amp_sem_down2 belong to req's req->req_waitsem
 */

int
amp_sem_down2(amp_sem_t *req_waitsem)
{
    struct timespec ts;
    amp_request_t *req = NULL;
    amp_connection_t *conn = NULL;
    amp_u32_t err = 0;
    amp_u32_t conn_num = 0;
    amp_u32_t circle_num = 0;
    amp_u32_t time_wait = AMP_CONN_TIMEWAIT_INTERVAL;
    amp_u32_t type = 0;
    amp_u32_t id = 0;
TIMEWAIT:
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += time_wait;
    err = sem_timedwait(req_waitsem, &ts);
    req = list_entry(req_waitsem, amp_request_t, req_waitsem);
    conn = req->req_conn;
    type = req->req_remote_type;
    id = req->req_remote_id;
    
    if(circle_num >= AMP_CONN_RETRY_TIMES){
        __amp_req_list_clear(req);
        req->req_error = -ENETUNREACH;
        err = -ENETUNREACH;
        goto EXIT;
    }
    
    if(err && ETIMEDOUT == errno){
        if(req->req_type & AMP_REPLY){
            req->req_error = -ENETUNREACH;
            err = -ENETUNREACH;
            goto EXIT;
        }

        circle_num++;
        if(circle_num > AMP_CONN_RETRY_TIMES/2)
            time_wait += (AMP_CONN_TIMEWAIT_INTERVAL/2);
        conn_num = req->req_ctxt->acc_conns[req->req_remote_type].acc_remote_conns[req->req_remote_id].active_conn_num;
        
        if(conn && conn->ac_state == AMP_CONN_OK){
            AMP_ERROR("amp_sem_down_with_timeout_process<%d>: req[%p]-conn[%p]-sock[%d] send timeout, type: %d, id: %d, seqno: %llu ...\n", req->req_msg->qp_num, req, req->req_conn, req->req_conn->ac_sock, req->req_remote_type, req->req_remote_id, req->req_msg->amh_xid & 0xFFFFFFFF);
	    {
		    char * str1 = NULL;
		    AMP_ERROR("dump for debug , by mayl .....\n");
		    str1[0] = 0xEE;
		    
	    }
#ifdef __AMP_LISTEN_POLL
            if( !amp_poll_fd_isset( req->req_conn->ac_sock, req->req_ctxt->acc_poll_list ) ){
                AMP_ERROR("amp_sem_down_with_timeout_process: req[%p]-conn[%p]-sock[%d] send timeout, is not in listen ......\n",req, req->req_conn,req->req_conn->ac_sock);
            }

#endif
            if(0 == req->req_resent){
                __amp_req_list_clear(req);
                req->req_error = -ENOTCONN;
                err = -ENOTCONN;
                goto EXIT;
            } 
            if(3 > circle_num || conn->ac_sock == 0)
                goto RESEND;

            AMP_OP(conn->ac_type, proto_disconnect)((void *)&conn->ac_sock);
            amp_lock(&conn->ac_lock);
            if(conn->ac_need_reconn)
                conn->ac_state = AMP_CONN_RECOVER;
            else
                conn->ac_state = AMP_CONN_CLOSE;
            conn->ac_stage = AMP_CONN_NOT_SELECTED;
            amp_unlock(&conn->ac_lock);

            amp_lock(&amp_reconn_conn_list_lock);
            amp_lock(&conn->ac_lock);
            if(list_empty(&conn->ac_reconn_list))
                list_add_tail(&conn->ac_reconn_list,&amp_reconn_conn_list);
            amp_unlock(&conn->ac_lock);
            amp_unlock(&amp_reconn_conn_list_lock);
            amp_sem_up(&amp_reconn_sem);
            goto RESEND;
        }else if(conn_num == 0){
            __amp_req_list_clear(req);
            req->req_error = -ENOTCONN;
            err = -ENOTCONN;
            goto EXIT;
        }else
            goto RESEND;

        goto TIMEWAIT;
    }else if(err && EINTR == errno){
        goto TIMEWAIT;
    }else if(err == 0 && req->req_error == -ENETUNREACH){
        goto RESEND;
    }

EXIT:
    if(conn){
        amp_lock(&conn->ac_lock);
        conn->ac_stage = AMP_CONN_NOT_SELECTED;
        amp_unlock(&conn->ac_lock);
#if 0
#ifdef __AMP_SOCKET_POOL
        pthread_mutex_lock(&(req->req_ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
        amp_lock(&conn->ac_lock);
        list_del_init(&conn->ac_list);
        list_add_tail(&conn->ac_list, &(req->req_ctxt->acc_conns[type].acc_remote_conns[id].queue));
        amp_unlock(&conn->ac_lock);
        //req->req_ctxt->acc_conns[type].acc_remote_conns[id].allocd_num --;
        pthread_mutex_unlock(&(req->req_ctxt->acc_conns[type].acc_remote_conns[id].queue_lock));
#endif
#endif
    }
    return err;

RESEND:
    __amp_req_list_clear(req);
           
    amp_lock(&amp_sending_list_lock);
    //amp_lock(&req->req_lock);
    if(list_empty(&req->req_list))
        list_add_tail(&req->req_list, &amp_sending_list);
    //amp_unlock(&req->req_lock);
    amp_unlock(&amp_sending_list_lock);
    amp_sem_up(&amp_process_out_sem);
    goto TIMEWAIT;
}

/*end of file*/
