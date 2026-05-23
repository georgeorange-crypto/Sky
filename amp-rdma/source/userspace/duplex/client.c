#include "header.h"
/**
 * 双工
 */

amp_comp_context_t *this_ctxt;
amp_lock_t request_queue_lock;
amp_sem_t request_queue_sem;
LIST_HEAD(request_queue); //请求队列

int test_queue_req(amp_request_t *req) {
	amp_lock(&request_queue_lock);
	list_add_tail(&req->req_list, &request_queue); //not understand,why
	amp_unlock(&request_queue_lock);
	amp_sem_up(&request_queue_sem);
	return 1;
}

int test_alloc_pages(void * msg, amp_u32_t *num, amp_kiov_t **kiov) //the aiming of this
{
	amp_kiov_t *iovp = NULL; //amp_kiov_t: data buffer
	test_msg_t * testmsgp = NULL;
	int page_num, i;
	char * bufp = NULL;

	testmsgp = (test_msg_t *) msg;
	page_num = testmsgp->page_num;
	iovp = (amp_kiov_t *) malloc(sizeof(amp_kiov_t) * page_num);
	memset(iovp, 0, sizeof(amp_kiov_t) * page_num);

	for (i = 0; i < page_num; i++) {
		bufp = (char*) malloc(PAGE_SIZE);
		if (!bufp) {
			printf("bufp alloc error\n");
			return 1;
		}
		iovp[i].ak_addr = bufp;
		iovp[i].ak_len = PAGE_SIZE;
		iovp[i].ak_offset = 0;
		iovp[i].ak_flag = 0;
	}

	/*iovp->ak_addr = (char *) malloc(4096);
	iovp->ak_len = 4096;
	iovp->ak_offset = 0;
	iovp->ak_flag = 0;*/
	*num = page_num;
	*kiov = iovp;
	return 0;
}

void test_free_pages(amp_u32_t num, amp_kiov_t **kiov) {
	int i = 0;
	amp_kiov_t *iov = *kiov;
	for (i = 0; i < num; i++)
		free(iov[i].ak_addr);
	free(iov);
	return;

}

void* send_work(void* arg) {
	int err = 0;
	amp_request_t *req = NULL; //请求块
	amp_message_t *reqmsgp = NULL; //消息块
	test_msg_t *msgp = NULL; //消息
	amp_kiov_t * kiov = NULL; //数据块
	char * bufpp[TOTAL_PAGE];

	int size;
	int i, j;

	//创建数据块
	kiov = (amp_kiov_t *) malloc(sizeof(amp_kiov_t) * TOTAL_PAGE);
	if (!kiov) {
		printf("[main] kiov alloc memory failed\n");
		return NULL;
	}
	memset(kiov, 0, sizeof(amp_kiov_t) * TOTAL_PAGE);
	for (i = 0; i < TOTAL_PAGE; i++) {
		bufpp[i] = (char *) malloc(PAGE_SIZE);
		if (!bufpp[i]) {
			for (j = 0; j < i; j++) {
				free(bufpp[j]);
			}
			return NULL;
		}
	}
	for (j = 0; j < TOTAL_PAGE; j++) {
		char tmpch;
		tmpch = 'a' + j % 26;

		memset(bufpp[j], tmpch, PAGE_SIZE);
		bufpp[j][PAGE_SIZE - 1] = '\0';
		kiov[j].ak_addr = bufpp[j];
		kiov[j].ak_len = PAGE_SIZE;
		kiov[j].ak_offset = 0;
		kiov[j].ak_flag = 0;
	}

	i = 0;
	while (1) {
		err = __amp_alloc_request(&req);	//3、创建请求块
		if (err < 0) {
			printf("alloc request error, err:%d\n", err);
			continue;
		}

		size = AMP_MESSAGE_HEADER_LEN + sizeof(test_msg_t);
		reqmsgp = (amp_message_t *) malloc(size);	//4、创建消息块
		if (!reqmsgp) {
			printf("alloc for req msg error, err:%d\n", err);
			continue;
		}

		memset(reqmsgp, 0, size);	//置为0
		msgp = (test_msg_t *) ((char *) reqmsgp + AMP_MESSAGE_HEADER_LEN);
	    sprintf(msgp->msg, "this is No.%d client:%d", CLIENT_ID2, i);
		//msgp->len = strlen(msgp->msg) + 1;
		msgp->len = sizeof(test_msg_t) + AMP_MESSAGE_HEADER_LEN;
		msgp->type = 8;
		msgp->seqno = i;
		msgp->page_num = TOTAL_PAGE;

		req->req_msg = reqmsgp;
		req->req_msglen = size;
		req->req_iov = kiov;
		req->req_niov = TOTAL_PAGE;

		req->req_need_ack = 1;	//设置回复
		req->req_resent = 1;
		req->req_type = AMP_REQUEST | AMP_DATA;

		err = amp_send_sync(this_ctxt, req, SERVER, SERVER_ID1, 1);	//5、发送消息
		printf("============CLIENT_SEND_WORK: send message %ld-%d, err: %d\n", pthread_self(), i, err);
		if (err < 0) {
			printf("send error, err:%d\n", err);
			continue;
		}

		msgp = (test_msg_t *) ((char *) req->req_reply + AMP_MESSAGE_HEADER_LEN);
//		printf("Received message %d :%s\n", i, msgp->msg);

		i++;
		amp_free(req->req_msg, req->req_msglen);
		amp_free(req->req_reply, req->req_replylen);
		__amp_free_request(req);
	}

	if (kiov)
		free(kiov);
	for (i = 0; i < TOTAL_PAGE; i++)
		free(bufpp[i]);
	return NULL;
}

void *listen_work(void *arg) {
	int err = 0;

	amp_request_t *req = NULL; //请求块
	test_msg_t *msgp = NULL; //消息
	amp_kiov_t * kiov;
	int j;
	while (1) {
		//printf("[main] before down semaphore\n");
		amp_sem_down(&request_queue_sem);
		//printf("[main] after down semaphore\n");
		amp_lock(&request_queue_lock);
		if (list_empty(&request_queue)) {
			amp_unlock(&request_queue_lock);
			continue;
		}
		req = list_entry(request_queue.next, amp_request_t, req_list); //3、获取请求
		list_del_init(&req->req_list);
		amp_unlock(&request_queue_lock);

		msgp = (test_msg_t *) ((char *) req->req_msg + AMP_MESSAGE_HEADER_LEN);
//		printf("[main]type:%d, len:%d, msg:%s\n", msgp->type, msgp->len,
//				msgp->msg);

		//设置返回消息
		sprintf(msgp->msg, "Client: we reply to server:%d, seqno:%d\n",
				req->req_remote_id, msgp->seqno);
		printf("++++++++++++++CLIENT_LISTEN_WORK: receivd message %ld-%d\n", pthread_self(), msgp->seqno);
		req->req_reply = req->req_msg;
		req->req_replylen = req->req_msglen;
		req->req_type = AMP_REPLY | AMP_MSG;
		req->req_need_ack = 0;
		req->req_resent = 1;

		err = amp_send_sync(this_ctxt, req, req->req_remote_type,
				req->req_remote_id, 1); //发送回复消息
		if (err < 0)
			printf("Send to client error, err:%d\n", err);
		
		kiov =  req->req_iov;
		for(j = 0; j < req->req_niov; j++)
			free(kiov[j].ak_addr);
		free(kiov);

		amp_free(req->req_reply, req->req_replylen);
		__amp_free_request(req);
		continue;
	}
}

int main(int argc, char *argv[]) {
	int err = 0;
	int addr; //IP，数值
	struct in_addr naddr; //IP，类

	pthread_t send_thread[SEND_THREAD];
	pthread_t listern_thread[LISTEN_THREAD];

	int size;
	int i, j;

	//err = inet_aton("127.0.0.1", &naddr); //检查IP正确性，并转为数字
	//err = inet_aton("10.18.130.143", &naddr); //检查IP正确性，并转为数字
	//err = inet_aton("192.168.1.159", &naddr); //检查IP正确性，并转为数字
	err = inet_aton("10.18.130.143", &naddr); //检查IP正确性，并转为数字
	if (!err) {
		printf("[main] wrong ip address\n");
		exit(1);
	}
	addr = htonl(naddr.s_addr);

	amp_lock_init(&request_queue_lock);
	amp_sem_init_locked(&request_queue_sem);
    this_ctxt = amp_sys_init(CLIENT, CLIENT_ID2);	//1、初始化系统
	if (!this_ctxt) {
		printf("[main] sys init error\n");
		exit(1);
	}
	printf("amp init complete...\n");
	err = amp_create_connection(this_ctxt, 
					CLIENT, 
				    CLIENT_ID2, 
					INADDR_ANY, 
					CLIENT_PORT, 
					AMP_CONN_TYPE_TCP, 
					AMP_CONN_DIRECTION_LISTEN, 
					test_queue_req,
					test_alloc_pages,
					test_free_pages);
	if(err < 0){
		printf("[main] create connection error, err:%d\n", err);
		amp_sys_finalize(this_ctxt);
		exit(1);
	}
	for(i=0;i<SOCKET_NUM;i++){
		err = amp_create_connection(this_ctxt,
		SERVER,
		SERVER_ID1, addr,
		SERVER_PORT, AMP_CONN_TYPE_TCP,
		AMP_CONN_DIRECTION_CONNECT, test_queue_req, test_alloc_pages,
				test_free_pages);	//2、创建链接
		if (err < 0) {
			printf("[main] connect to server error, err:%d\n", err);
			amp_sys_finalize(this_ctxt);
			exit(1);
		}
	}
	printf("connect success....\n");
	for (i = 0; i < LISTEN_THREAD; i++) {
		err = pthread_create(&listern_thread[i], NULL, listen_work, NULL);
		if (err != 0) {
			printf("send pthread %d create falied!\n", i);
			exit(1);
		}
	}
#if 1
	//创建进程
	for (i = 0; i < SEND_THREAD; i++) {

		err = pthread_create(&send_thread[i], NULL, send_work, NULL);
		if (err != 0) {
			printf("send pthread %d create falied!\n", i);
			exit(1);
		}
	}
#endif
	while (1)
		;
	amp_sys_finalize(this_ctxt);	//6、结束系统

	printf("Finished!\n");

	return 0;
}
