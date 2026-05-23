#ifndef __HEADER_H_
#define __HEADER_H_
#include "amp.h"

#define PAGE_SIZE (4096)
#define TOTAL_PAGE (256)
#define SEND_THREAD (32)
#define LISTEN_THREAD (32)
#define SOCKET_NUM (128)


#define  SERVER_PORT 4446
#define  CLIENT_PORT 4445

#define SERVER  1
#define CLIENT  2


#define SERVER_ID1   1
#define SERVER_ID2   2
#define SERVER_ID3   3
#define SERVER_ID4   4


#define CLIENT_ID1   1
#define CLIENT_ID2   2
#define CLIENT_ID3   3
#define CLIENT_ID4   4

#define CLIENT1_ID   1
#define CLIENT2_ID   2
#define CLIENT3_ID   3
#define CLIENT4_ID   4
#define CLIENT5_ID   5
#define CLIENT6_ID   6
#define CLIENT7_ID   7
#define CLIENT8_ID   8

#define SERVER1_ID   1
#define SERVER2_ID   2
#define SERVER3_ID   3
#define SERVER4_ID   4
#define SERVER5_ID   5
#define SERVER6_ID   6
#define SERVER7_ID   7
#define SERVER8_ID   8

#define READ_DATA   (118)
int server_id = SERVER_ID1;

struct __test_msg {
	int type;
	int seqno;
	int len;
	int page_num;
	char msg[512];
};
typedef struct __test_msg  test_msg_t;


#endif
