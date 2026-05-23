/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/
#ifndef __AMP_CONN_H_
#define __AMP_CONN_H_

#include  <amp_sys.h>
#include  <amp_types.h>
#include  <amp_help.h>

/*state of connection*/
#define AMP_CONN_OK       (0x00000010) /*it's ok*/
#define AMP_CONN_RECOVER  (0x00000020) /*during recover*/
#define AMP_CONN_BAD      (0x00000040) /*unuseable*/
#define AMP_CONN_NOTINIT  (0x00000080) /*haven't initialized yet*/
#define AMP_CONN_CLOSE    (0x00000100)

#define AMP_CONN_SELECTED (0x00001000)
#define AMP_CONN_NOT_SELECTED (0x00002000)

#define AMP_CONN_TIMEWAIT_INTERVAL (15)

/*state of bh*/
#define AMP_CONN_BH_DATAREADY    (0x00001000)
#define AMP_CONN_BH_STATECHANGE  (0x00002000)
#define AMP_CONN_RETRY_TIMES      (5)
#define AMP_CONN_RECONN_INTERVAL  (5)   /*after these seconds , we than do reconnection*/
#define AMP_CONN_RECONN_MAXTIMES (60)   /*max reconnection times*/
#define AMP_CONN_MAXIP_PER_NODE  (16)

//note the relation between AMP_CONN_MAXIP_PER_NODE with AMP_SELECT_CONN_ARRAY_ALLOC_LEN

/*type of connection*/
enum amp_conn_type {
    AMP_CONN_TYPE_TCP = 1,
    AMP_CONN_TYPE_UDP,
    AMP_CONN_TYPE_GM,
    AMP_CONN_TYPE_RDMA,
    AMP_CONN_TYPE_MAX,
};


/*direction of connection*/
#define AMP_CONN_DIRECTION_CONNECT  (0x00000100)
#define AMP_CONN_DIRECTION_ACCEPT   (0x00000200)
#define AMP_CONN_DIRECTION_LISTEN   (0x00000400)

/*reconnection list*/
extern struct list_head  amp_reconn_conn_list;
extern amp_lock_t        amp_reconn_conn_list_lock;

/*when data coming into this conn, the data-ready callback queue 
 * this conn to this list
 */
extern struct list_head  amp_dataready_conn_list;
extern amp_lock_t        amp_dataready_conn_list_lock;


extern amp_u32_t         amp_reconn_thread_started;
extern amp_u32_t         amp_wakeup_thread_started;

extern amp_u64_t         amp_malloc_addr_max;
extern amp_u64_t         amp_malloc_addr_min;
extern amp_u64_t         msg_malloc_addr_max;
extern amp_u64_t         msg_malloc_addr_min;


/*
 * hash table for resend request
 */
struct __amp_htb_entry {
	struct list_head  queue;
    amp_lock_t         lock;
};
typedef struct __amp_htb_entry amp_htb_entry_t;

#define AMP_RESEND_HTB_SIZE   (1 << 14)
extern amp_htb_entry_t   *amp_resend_hash_table;

#ifdef __AMP_RECONFIRM_MSG
#define AMP_RECONFIRM_HTB_SIZE  (1 << 20)
extern amp_htb_entry_t   *amp_reconfirm_hash_table_c;
extern amp_htb_entry_t   *amp_reconfirm_hash_table_s;
int __amp_within_reconfirm_htb(amp_request_t *);
int __amp_within_reconfirm_htb_c(amp_request_t *);
int __amp_remove_reconfirm_req(amp_request_t * req);
extern amp_u32_t __amp_reconfirm_hash(amp_u32_t type, amp_u32_t id, amp_u64_t amh_sender_handle);
extern amp_u32_t __amp_reconfirm_hash_c(amp_u64_t amh_sender_handle);
#endif

//#define AMP_CONN_ADD_INCR   (8192)   /*every time we add this  much connections in comp_conns*/

#define AMP_CONN_ADD_INCR   (64)   /*every time we add this  much connections in comp_conns*/

// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_POLL

extern int amp_poll_fd_zero( struct pollfd *poll_list, amp_u32_t poll_size );
extern int amp_poll_fd_set( amp_s32_t fd, struct pollfd *poll_list );
extern int amp_poll_fd_reset( amp_s32_t fd, struct pollfd *poll_list );
extern int amp_poll_fd_isset( amp_s32_t fd, struct pollfd *poll_list );
extern int amp_poll_fd_clr( amp_s32_t fd, struct pollfd *poll_list );

#endif
#ifdef __AMP_LISTEN_EPOLL

extern int amp_epoll_fd_isset( amp_s32_t fd, struct epoll_event *ev, amp_s32_t nfds );
extern int amp_epoll_fd_set( amp_s32_t fd, amp_s32_t epfd );
extern int amp_epoll_fd_clear( amp_s32_t fd, amp_s32_t epfd );
extern int amp_epoll_fd_reset( amp_s32_t fd, amp_s32_t epfd );

#endif
// -----------------------------------------------------------------

amp_u32_t  __amp_hash(amp_u32_t type, amp_u32_t id);
void __amp_add_resend_req(amp_request_t *req);
void __amp_remove_resend_req(amp_request_t *req);
void __amp_remove_resend_reqs(amp_connection_t *conn, amp_u32_t force, amp_u32_t no_conns);
void __amp_remove_waiting_reply_reqs(amp_connection_t *conn, amp_u32_t fore, amp_u32_t no_conns);
amp_request_t * __amp_find_resend_req(amp_u32_t type, amp_u32_t id);
int __amp_request_addr_within_waiting_reply_list(amp_request_t * req);
int __amp_within_waiting_reply_list(amp_request_t * req);
int __amp_within_resend_req(amp_request_t * req);
int __amp_within_sending_list(amp_request_t * req);
int __amp_within_reconn_conn_list(amp_connection_t * conn);
/*some functions*/
int __amp_init_conn(void);      /*called when module init*/
int __amp_finalize_conn(void);  /*called when module cleanup*/
int __amp_alloc_conn(amp_connection_t **conn);  /*alloc one*/
int __amp_free_conn(amp_connection_t *conn);   /*free one*/

int __amp_enqueue_conn(amp_connection_t *conn, amp_comp_context_t *ctxt);
int __amp_dequeue_conn(amp_connection_t *conn, amp_comp_context_t *ctxt);
#ifdef __AMP_CONNS_DUPLEX
int __amp_enqueue_recv_conn(amp_connection_t *conn, amp_comp_context_t *ctxt);
int __amp_dequeue_recv_conn(amp_connection_t *conn, amp_comp_context_t *ctxt);
#endif
int __amp_dequeue_invalid_conn(amp_connection_t * conn, amp_comp_context_t *ctxt);

int __amp_select_conn(amp_u32_t type,  amp_u32_t id, amp_comp_context_t *ctxt,  amp_connection_t **retconn);

/*to revoke related need resend requests*/
void __amp_revoke_resend_reqs(amp_connection_t *conn);
int __amp_connect_server (amp_connection_t *conn);

int __amp_do_connection(amp_s32_t **retsock, struct sockaddr_in *addr, amp_u32_t conn_type, amp_u32_t direction);
int __amp_accept_connection (amp_s32_t *sockparent, amp_connection_t *childconn);

int __amp_conn_test(amp_connection_t *);
int __amp_conn_exchange(amp_connection_t *);

int __amp_add_to_listen_fdset (amp_connection_t *conn);

#ifdef __AMP_RDMA__
int amp_post_recv(amp_connection_t *conn, int n);
int __amp_post_send(amp_connection_t *conn, int opcode);
int amp_post_send(amp_connection_t *conn);
#endif

#endif

/*end of file*/
