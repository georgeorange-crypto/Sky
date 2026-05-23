/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include <amp_request.h>
struct list_head  amp_sending_list;
amp_lock_t        amp_sending_list_lock;

struct list_head  amp_waiting_reply_list;
amp_lock_t        amp_waiting_reply_list_lock;

amp_lock_t        amp_free_request_lock;
amp_u32_t         amp_seqno_generator;
amp_lock_t        amp_seqno_lock;

/**
 *get a new seqno of message
 */
amp_u32_t
__amp_get_seqno(void)
{
    amp_u32_t  newxid;
    amp_lock(&amp_seqno_lock);
    newxid = amp_seqno_generator++;
    amp_unlock(&amp_seqno_lock);
    return newxid;
}

/*
 * initialization the fundation of request
 */ 
int
__amp_init_request(void)
{
    amp_s32_t err = 0;

    AMP_ENTER("__amp_init_request: enter\n");

    INIT_LIST_HEAD(&amp_sending_list);
    amp_lock_init(&amp_sending_list_lock);

    INIT_LIST_HEAD(&amp_waiting_reply_list);
    amp_lock_init(&amp_waiting_reply_list_lock);
    
    amp_lock_init(&amp_free_request_lock);
    amp_lock_init(&amp_seqno_lock);
    amp_seqno_generator = 1;

    AMP_LEAVE("__amp_init_request: leave\n");
    return err;
}

/*
 * finalize
 */ 
int 
__amp_finalize_request (void)
{
    amp_s32_t  err = 0;

    AMP_ENTER("__amp_finalize_request: enter\n");

    AMP_LEAVE("__amp_finalize_request: leave\n");
    return err;
}

/*
 * alloc a request
 */ 
int
__amp_alloc_request (amp_request_t **retreq)
{
    amp_s32_t err = 0;
    amp_request_t *reqp = NULL;

    AMP_ENTER("__amp_alloc_request: enter\n");
        
    reqp = (amp_request_t *)malloc(sizeof(amp_request_t));

    if (!reqp) {
        AMP_WARNING("__amp_alloc_request: alloc request error\n");
        err = -ENOMEM;
        return err;
    }

    memset(reqp, 0, sizeof(amp_request_t));

    INIT_LIST_HEAD(&reqp->req_list);
    amp_sem_init_locked(&reqp->req_waitsem);
    amp_lock_init(&reqp->req_lock);
    reqp->req_state = AMP_REQ_STATE_INIT;
    reqp->req_iov = NULL;
    reqp->req_msg = NULL;
    reqp->req_reply = NULL;
    reqp->req_refcount = 1;  /*initialized to 1*/
    
    *retreq = reqp;

    AMP_LEAVE("__amp_alloc_request: leave, req:%p\n", reqp);
    return err;
}

void __amp_req_list_clear(amp_request_t *req){
    amp_u32_t hashvalue;
    amp_htb_entry_t *htbentry = NULL;

    if(!list_empty(&req->req_list))
    {
        hashvalue = __amp_hash(req->req_remote_type, req->req_remote_id);
        htbentry = amp_resend_hash_table + hashvalue;

        amp_lock(&htbentry->lock);
        //amp_lock(&req->req_lock);
        
        if(__amp_within_resend_req(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&htbentry->lock);
        
        amp_lock(&amp_sending_list_lock);
        //amp_lock(&req->req_lock);
        if(__amp_within_sending_list(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_sending_list_lock);

        amp_lock(&amp_waiting_reply_list_lock);
        //amp_lock(&req->req_lock);
        if(__amp_within_waiting_reply_list(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
    }
    return;
}

/*
 * free a request
 */ 
int
__amp_free_request (amp_request_t *req)
{
    amp_s32_t err = 0;
    amp_u32_t hashvalue;
    amp_htb_entry_t *htbentry = NULL;

    if (!req) {
        AMP_ERROR("__amp_free_request: no request\n");
        err = -EINVAL;
        goto EXIT;
    }

    hashvalue = __amp_hash(req->req_remote_type, req->req_remote_id);
    htbentry = amp_resend_hash_table + hashvalue;

    //amp_lock(&amp_free_request_lock);

    //amp_lock(&req->req_lock);
    //req->req_refcount --;

    //if (req->req_refcount > 0) {
    //    amp_unlock(&req->req_lock);
    //    amp_unlock(&amp_free_request_lock);
    //    goto EXIT;
    //}
    //amp_unlock(&req->req_lock);
 
    if(!list_empty(&req->req_list))
    {
        amp_lock(&htbentry->lock);
        //amp_lock(&req->req_lock);
        if(__amp_within_resend_req(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&htbentry->lock);
        
        amp_lock(&amp_sending_list_lock);
        //amp_lock(&req->req_lock);
        if(__amp_within_sending_list(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_sending_list_lock);
        
        amp_lock(&amp_waiting_reply_list_lock);
        //amp_lock(&req->req_lock);
        if(__amp_within_waiting_reply_list(req))
            list_del_init(&req->req_list);
        //amp_unlock(&req->req_lock);
        amp_unlock(&amp_waiting_reply_list_lock);
    }
 
    amp_lock(&amp_free_request_lock);
    __amp_free(req);
    amp_unlock(&amp_free_request_lock);

EXIT:
    AMP_LEAVE("__amp_free_request: leave\n");
    return err;
}

#if 0
struct amp_mrpool * mr_alloc(struct __amp_comp_context * ctxt, int size){
        struct amp_mrpool * mp = NULL;
        struct list_head *pos, *n;
        amp_lock(&ctxt->mrpool_lock);
        list_for_each_safe(pos, n, &ctxt->mr_free_list){
                mp = list_entry(pos, amp_mrpool, mr_list);
                list_del_init(&mp->mr_list);
                list_add_tail(&mp->mr_list, &ctxt->mr_work_list);
                ctxt->acc_mr_work_num ++;
                ctxt->acc_mr_free_num --;
                mp->mem_size = size;
                break;
        }
        amp_unlock(&ctxt->mrpool_lock);
        return mp;
}

int mr_free(struct __amp_comp_context * ctxt, struct amp_mrpool *mp){
        amp_lock(&ctxt->mrpool_lock);
        list_del_init(&mp->mr_list);
        list_add_tail(&mp->mr_list, &ctxt->mr_free_list);
        ctxt->acc_mr_work_num --;
        ctxt->acc_mr_free_num ++;
        amp_unlock(&ctxt->mrpool_lock);
        return 0;
}
#endif

int __amp_mr_bitmap_set(uint8_t *bitmap_data, int data_pos, int t_size){
	int start_idx = (data_pos - AMP_RDMA_SB_MSG_SIZE)/4096;
	int end_pos = data_pos + t_size;
	int j = 0;

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
	return 0;
}

/*
 * judge whether two message header is equal or not.
 */
int  __amp_reqheader_equal(amp_message_t  *src,  amp_message_t *dst)
{
    amp_u32_t  equal = 0;
    if((amp_u64_t)src > msg_malloc_addr_max || (amp_u64_t)src < msg_malloc_addr_min){
        goto EXIT;
    }
    AMP_ENTER("__amp_reqheader_equal: enter\n");
    if(src == NULL || dst == NULL){
        AMP_WARNING("__amp_reqheader_equal: src is %p, dst is %p\n", src, dst);
        goto EXIT;
    }
    if (src->amh_send_ts.usec != dst->amh_send_ts.usec){
        AMP_WARNING("__amp_reqheader_equal: amh_send_ts.usec not equal, src:%lld, dst:%lld\n",src->amh_send_ts.usec,dst->amh_send_ts.usec);
        goto EXIT;
    }
    if (src->amh_send_ts.sec != dst->amh_send_ts.sec){
        AMP_WARNING("__amp_reqheader_equal: amh_send_ts.sec not equal, src:%lld, dst:%lld\n",src->amh_send_ts.sec,dst->amh_send_ts.sec);
        goto EXIT;
    }
    if (src->amh_magic != dst->amh_magic){
        AMP_WARNING("__amp_reqheader_equal: amh_magic not equal, src:%u, dst:%u\n",src->amh_magic,dst->amh_magic);
        goto EXIT;
    }
    if (src->amh_sender_handle != dst->amh_sender_handle){
        AMP_WARNING("__amp_reqheader_equal: amh_sender_handle not equal, src:%lld, dst:%lld\n",src->amh_sender_handle,dst->amh_sender_handle);
        goto EXIT;
    }
    if (src->amh_pid != dst->amh_pid){
        AMP_WARNING("__amp_reqheader_equal: amh_pid not equal, src:%u, dst:%u\n",src->amh_pid,dst->amh_pid);
        goto EXIT;
    }
    if (src->amh_xid != dst->amh_xid){
        AMP_WARNING("__amp_reqheader_equal: amh_xid not equal, src:%lld, dst:%lld\n",src->amh_xid,dst->amh_xid);
        goto EXIT;
    }

    equal = 1;  

EXIT:
    AMP_LEAVE("__amp_reqheader_equal: leave\n");

    return equal;
}

/*end of file*/
