/***********************************************/
/*       Async Message Passing Library         */
/*       Copyright  2005                       */
/*                                             */
/*  Author:                                    */ 
/*          Rongfeng Tang                      */
/***********************************************/
#ifndef __AMP_TYPES_H_
#define __AMP_TYPES_H_

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "amp_sys.h"


#ifdef __AMP_RDMA__
#include <stdint.h>
//#include <sys/param.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <byteswap.h>
#include <netdb.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t
htonll(uint64_t x)
{
        return bswap_64(x);
}
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

#define AMP_RDMA_MAX_CQE_NUM 2048
#define AMP_RDMA_MAX_INLINE_DATA 512

#define AMP_RDMA_MR_SIZE (17 * 1024 * 1024)

// TODO changed by mayl
#define AMP_RDMA_LISTEN_THREAD_NUM (10)

#define AMP_CONN_NUM (12)

#define AMP_RDMA_SB_BLK_NUM (1)

#define AMP_RDMA_SB_MSG_SIZE (1048576)


struct amp_rdma_config{
    uint64_t addr;//缓冲区地址
    uint32_t rkey;//远程操作权限rkey
    uint32_t qp_num;//qp编号
    uint32_t psn;
    uint16_t lid;//端口LID
    uint8_t gid[16];//端口的全局ID
}__attribute__((packed));

enum {
    AMP_RECV_WRID = 1,
    AMP_SEND_WRID = 2,
};


#endif


/*generic types*/
typedef unsigned char   amp_u8_t;
typedef char            amp_s8_t;
typedef unsigned short  amp_u16_t;
typedef short           amp_s16_t;
typedef unsigned int    amp_u32_t;
typedef int             amp_s32_t;
typedef unsigned long long  amp_u64_t;
typedef long long           amp_s64_t;

/*amp time used*/
struct __amp_time {
	amp_u64_t  sec;    /*second*/
	amp_u64_t  usec;   /*micro second*/
};
typedef struct __amp_time amp_time_t;


/*data block vector*/
struct __amp_kiov {
	char        *ak_addr;
	amp_u32_t   ak_len;
	amp_u32_t   ak_offset;
	amp_u32_t   ak_flen;
	amp_u32_t   ak_foffset;
	amp_u64_t   ak_flag;    /*flags indicate the attribute of this page*/
};
typedef struct __amp_kiov  amp_kiov_t;

typedef pthread_mutex_t  amp_lock_t;
typedef sem_t            amp_sem_t;

struct __amp_comp_context;
typedef struct __amp_comp_context amp_comp_context_t;

struct __amp_connection;
typedef struct __amp_connection  amp_connection_t;

struct __amp_message {
	amp_u32_t  amh_size; /*the size of the payload, NOT including this header size*/
	amp_u32_t  amh_magic;
#ifdef __AMP_RDMA__
    uint64_t rdma_addr;
    uint32_t rdma_rkey;
    uint32_t qp_num;
    uint32_t data_pos;
    uint32_t data_len;
    uint32_t psn;
    uint16_t lid;
    uint8_t gid[16];
#endif

	amp_u32_t  amh_type; /*AMP_REQUEST or AMP_REPLY*/
	amp_u32_t  amh_pid;  /*in mormal msg is who send this message, in hello msg, it's comtype*/
	amp_u64_t  amh_sender_handle;
    amp_u64_t  amh_callback_handle;//add by weizheng,2015-01-15
	amp_u64_t  amh_xid;  /*in hello msg, amh_xid is the id of this component; in other msg, first 32 bit is the id of component, the last 32 bit is seqno*/
	amp_time_t amh_send_ts;	
	struct sockaddr_in amh_addr; /*for receiving udp message*/
//#ifdef __AMP_CONNS_DUPLEX
    amp_u32_t  amh_duplex;
//#endif
};
typedef struct __amp_message amp_message_t;
#define AMP_MESSAGE_HEADER_LEN (sizeof(amp_message_t))

// add definition  by mayl
#define AMP_LISTEN_CNT (1)

struct __amp_request {
	struct list_head      req_list;
    amp_message_t        *req_msg;
    amp_message_t        *req_reply;
    amp_kiov_t           *req_iov;
    amp_comp_context_t   *req_ctxt;
    amp_connection_t     *req_conn;
    amp_sem_t             req_waitsem; /*process waits on here for finishing*/ 
	amp_lock_t            req_lock;
    
    //amp_u32_t             req_send_state;
    amp_u32_t             req_type;    /*AMP_DATA(MSG)*/
	amp_u32_t             req_msglen;     /*length of req_msg */
	amp_u32_t             req_replylen;   /*length of req_reply*/
	amp_u32_t             req_state;
	amp_u32_t             req_niov;
	amp_s32_t             req_error;       /*0 - normally send request and      
	                                                        receive reply, else something wrong*/
	amp_u32_t             req_remote_type;   /*type of dst component*/
	amp_u32_t             req_remote_id;       /*id of dst component*/
	amp_u32_t             req_need_ack;
	amp_u32_t             req_need_free;
	amp_u32_t             req_replied;
	amp_u32_t             req_refcount;
	amp_u32_t             req_stage;  /*in which stage now*/
	amp_u32_t             req_resent;  /*not 0 - add to conn when it is error, 0 - not just return*/
	struct sockaddr_in  req_remote_addr;  /*the sock address of remote peer, used for reply in udp*/
	void *                req_mp;
	amp_u32_t             req_mp_pos;
	amp_u32_t             req_mp_size;
};
typedef struct __amp_request amp_request_t;



/*type of connection structure*/
struct __amp_connection {
	struct list_head ac_list;        /*used by fs*/
    struct list_head listen_list;
	struct list_head ac_reconn_list; /*add to reconnection list*/
	struct list_head ac_dataready_list; /*add to dataready list*/
    struct sockaddr_in   ac_remote_addr;   /*combined with ipaddr and port*/
    amp_comp_context_t *ac_ctxt;  /*context to which this connection belongs to */

   	amp_u32_t        ac_state;       /*state of connection, e.g. AMP_CONN_OK */
    amp_u32_t        ac_stage;
//#ifdef __AMP_CONNS_DUPLEX
    amp_u32_t        ac_duplex;      /*0: send, 1: receive*/
//#endif
	amp_u32_t        ac_type;        /*type of connection, e.g. AMP_CONN_TYPE_TCP */
	amp_u32_t        ac_need_reconn; /*only allow client  reconnect to server*/
	amp_u32_t        ac_remote_ipaddr;
	
    amp_u32_t        ac_remote_port;
	amp_u32_t        ac_remote_comptype; /*used by fs, AMP_CLIENT, AMP_MDS...*/
	amp_u32_t        ac_remote_id;              /*used by fs*/
	amp_u32_t        ac_this_type; /*type of this component, AMP_CLIENT, AMP_MDS...*/
	
    amp_u32_t        ac_this_id;     /*id of this component*/
	amp_u32_t        ac_this_port; /*port of this connection, used by listen connection*/
	amp_s32_t        ac_sock;
	amp_u32_t        ac_refcont;
	
    amp_u32_t        ac_sched;             /*this conn is queued for receiving*/
	amp_u32_t        ac_datard_count; /*how many times we received the data ready callback*/ 
	amp_u32_t        ac_weight;            /*wait of this connection*/
    /*something about reconnection*/
    amp_u32_t        ac_remain_times; /*how many times remain for retry*/
    amp_u64_t        ac_last_reconn;    /*the latest reconnection time, or as heartbeat time*/
    amp_u64_t        ac_conn_weigh; 
    amp_u64_t        ac_payload;          /*how many bytes want to be sent through this connection*/
    amp_lock_t       ac_lock;            /*protect for changing this connection*/
	amp_sem_t        ac_sendsem;
	amp_sem_t        ac_recvsem;
	amp_sem_t        ac_listen_sem;   /*the accept thread sleep on this sem*/

	/*something about reconnection*/

	/*
	* the callback for queue the received request, provided by fs layer
	* if not provided, then just drop the request.
	* return value: 1 - queued successfully, else - something wrong.
	*/
	int (*ac_queue_cb) (amp_request_t *req);

	/*
	* used by server to alloc page to be used for receiving data bulk. MUST PROVIDE it.
	* this cb just alloc pages for 
	* each vector.
	* return value: 0 - alloc successfully, <0 - something wrong.
	*/
	int (*ac_allocpage_cb) (void *opaque, amp_u32_t *niov, amp_kiov_t **iov);
	void (*ac_freepage_cb)(amp_u32_t niov, amp_kiov_t **iov);

#ifdef __AMP_RDMA__
    struct ibv_qp * ac_rdma_qp;
    struct ibv_cq * ac_rdma_scq;
    struct ibv_cq * ac_rdma_rcq;
    struct ibv_comp_channel * ac_rdma_channel;

    struct ibv_qp_init_attr ac_rdma_qp_init_attr;

    struct amp_rdma_config   ac_rdma_local_config;
    struct amp_rdma_config   ac_rdma_remote_config;

    struct ibv_mr * ac_rdma_send_mr;
    struct ibv_mr * ac_rdma_recv_mr;

    char *ac_rdma_send_buf;
    char *ac_rdma_recv_buf;

    int remote_data_start_pos;
    int remote_data_end_pos;
    int remote_msg_start_idx;
    int remote_msg_end_idx;
    int last_idx;
    int next_idx;
    int next_bitmap_start;
    int next_data_start;
    int last_data_end_pos;
#endif
};
#ifdef __AMP_RECONFIRM_MSG
struct __amp_reconfirm_msg{
    struct list_head reconf_list;
    pthread_mutex_t reconf_lock;
    amp_u32_t reconf_size;
    amp_u32_t reconf_pid;
    amp_u64_t reconf_xid;
    amp_u64_t reconf_sender_handle;
    amp_u64_t reconf_callback_handle;
    amp_time_t reconf_send_ts;
    amp_message_t *reconf_reply_msg;
};
struct __amp_reconfirm_msg_c{
    struct list_head reconf_list;
    pthread_mutex_t reconf_lock;
    amp_u32_t reconf_pid;
    amp_u64_t reconf_xid;
    amp_u64_t reconf_sender_handle;
    amp_time_t reconf_send_ts;
};
typedef struct __amp_reconfirm_msg_c amp_reconfirm_msg_c_t;
typedef struct __amp_reconfirm_msg amp_reconfirm_msg_t;
#endif
/*
 * the number of connections for each kind of components, its main purpose is to record the relationship with 
 * other file system component.
 * keep with stripe size(10,2,4)
 */
#define AMP_SELECT_CONN_ARRAY_ALLOC_LEN   (64)
struct __conn_queue {
	pthread_mutex_t queue_lock;
	struct  list_head  queue;
	amp_connection_t   **conns;  /*for selecting*/
	amp_u32_t          total_num; /*how long of the conns array*/
	amp_u32_t          active_conn_num; /*how many eff conns in the conns*/
#ifdef __AMP_SOCKET_POOL
    //struct  list_head  allocd_queue;
    //amp_s32_t          allocd_num;
#endif
};
typedef struct __conn_queue conn_queue_t;

struct __amp_comp_conns {
	amp_u32_t  acc_num;  /*number of valid remote connections*/
	amp_u32_t  acc_alloced_num; /*alloced number of acc_remote_conns*/
	amp_lock_t  acc_lock;  /*lock for changing this structure*/
	//struct list_head  *acc_remote_conns;   /*remote connection table, indexed by remote component id*/	
	conn_queue_t  *acc_remote_conns;
};
typedef struct __amp_comp_conns amp_comp_conns_t;

/*thread structures*/
struct __amp_thread {
	amp_u32_t         at_seqno;    /*our sequence*/
	amp_sem_t         at_startsem;
	amp_sem_t         at_downsem;
	amp_u32_t         at_shutdown; /*1 -  to shutdown, 0 - not*/
	amp_u32_t         at_isup;     /*1 - is up, 0 - not*/
	pthread_t          at_thread_id;
	void *at_provite;  /*for provite use, as to listen thread, it's a connection structure.*/

};
typedef struct __amp_thread amp_thread_t;

#define MAX_CONN_TABLE_LEN  (65535)//(8192)/*modify by weizheng, 20170223*/

/*message passing system component context*/
struct __amp_comp_context {
	amp_u32_t  acc_this_type;  /*type of this component*/
	amp_u32_t  acc_this_id;      /*id of for this component within its type*/
	amp_u32_t  acc_this_listen_id; // added by mayl for multi channel
	// added by mayl
	amp_connection_t *acc_listen_conns[AMP_LISTEN_CNT];    /*listen connection for this component , BY mayl*/
	amp_thread_t     *acc_listen_threads[AMP_LISTEN_CNT];  /*threads for listen by mayl */
	amp_connection_t *acc_listen_conn;    /*listen connection for this component*/
	amp_thread_t     *acc_listen_thread;  /*threads for listen*/
	amp_thread_t     *acc_netmorn_thread;
	amp_comp_conns_t *acc_conns;               /*connections with other component*/
//#ifdef __AMP_CONNS_DUPLEX
	amp_comp_conns_t *acc_conns_recv;               /*acc_conns and acc_conns_recv manage the send and recv conns*/
//#endif
	amp_connection_t  **acc_conn_table; /*all connection stored in this array indexed by fd*/
	pthread_mutex_t   acc_lock;
	// added by mayl
	amp_u32_t conn_type;

#ifdef __AMP_RDMA__
    struct ibv_device ** acc_rdma_device_list;
    struct ibv_context *acc_rdma_context;

    struct ibv_pd * acc_rdma_pd;
    struct ibv_comp_channel * acc_rdma_channel;
    struct ibv_cq * acc_rdma_scq;
    struct ibv_cq * acc_rdma_rcq;

    struct ibv_port_attr acc_rdma_port_attr;

    union ibv_gid  acc_rdma_gid;

    int acc_rdma_device_num;
    int acc_rdma_port_num;

#endif

#ifdef __TOKEN1__
    pthread_mutex_t   acc_token_lock;
#endif
// by Chen Zhuan at 2009-02-05
// -----------------------------------------------------------------
#ifdef __AMP_LISTEN_SELECT

	fd_set            acc_readfds;
	amp_s32_t         acc_maxfd;
	
#endif
#ifdef __AMP_LISTEN_POLL

	struct pollfd  *acc_poll_list;	// by ChenZhuan at 2008-10-31
	amp_s32_t         acc_maxfd;
	
#endif
#ifdef __AMP_LISTEN_EPOLL

	amp_s32_t  acc_epfd;			// by Chen Zhuan at 2008-11-03
	
#endif
// -----------------------------------------------------------------

	amp_u32_t         acc_notifyfd;     /*used to break the select*/
	amp_u32_t         acc_srvfd;        /*server side used fd*/
#ifdef __TOKEN1__
    amp_u32_t         acc_token_total_num;
    amp_u32_t         acc_token_num;
#endif
};


/*storage system component type*/
enum amp_comp_type {
	AMP_CLIENT = 1,   /*client of storage system*/
	AMP_OSD = 2,        /*meta data server*/
	AMP_MDS = 3,        /*object based storage server*/
	AMP_MGNT = 4,     /*management node*/
	AMP_SNBD = 5,      /*super nbd*/
	AMP_MOD = 6,
	AMP_MPS = 7,
	AMP_COMP_MAX,   /*max number*/
};

#define AMP_MAX_COMP_TYPE  (8)


#endif

/*end of file*/
