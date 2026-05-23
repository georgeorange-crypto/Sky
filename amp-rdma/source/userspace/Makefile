
CC = cc
RM = rm -f
AR = ar rvs
CP = cp
MV = mv -f
RANLIB = ranlib
CFLAGS = -I ../../include/ -I/usr/include/ -g -Wall -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE 
LDFLAGS = -lm -lpthread -libverbs -lrdmacm 
LIBPATH = ../../lib/libamp.a
LIB_RDMA_PATH = ../../lib/libamp-rdma.a
LIB_RDMA_PATH_1 = ../../libamp-rdma.a

LDDINC := $(shell pwd)
INC_PUB  := $(LDDINC)/../../../include
EXTRA_CFLAGS	+= -I$(INC_PUB)

#EXTRA_CFLAGS	+= -D__AMP_LISTEN_SELECT
#EXTRA_CFLAGS	+= -D__AMP_LISTEN_POLL
EXTRA_CFLAGS	+= -D__AMP_LISTEN_EPOLL

#EXTRA_CFLAGS    += -D__AMP_CONNS_DUPLEX


#amp_select_conn socket_id pooling
EXTRA_CFLAGS    += -D__AMP_SOCKET_POOL

#if nor not use memory to identify resend message
#EXTRA_CFLAGS    += -D__AMP_RECONFIRM_MSG

EXTRA_CFLAGS    += -D__TOKEN1__

EXTRA_CFLAGS    += -D__AMP_RDMA__
#EXTRA_CFLAGS    += -D__CPU_QOS_FOR_LISTEN__
#EXTRA_CFLAGS    += -D__FOR_ROCE_V2__
#EXTRA_CFLAGS    += -D__AMP_ROCE__


EXTRA_CFLAGS    += -D__AMP_RDMA_EVENT__

# if or not start amp_netmorn_thread
#EXTRA_CFLAGS    +=-D__AMP_ICMP_NETMORN

#AMP_MAX_THREAD_NUM define the num thread used for send and receive respectively
EXTRA_CFLAGS 	+= -DAMP_MAX_THREAD_NUM=1

OBJS = amp_interface.o amp_conn.o amp_utcp.o amp_protos.o amp_request.o amp_uthread.o amp_help.o 

LIB = libamp.a

.c.o:
	${CC} ${CFLAGS} ${EXTRA_CFLAGS} ${LDFLAGS}  -c $*.c


lib: ${OBJS}
	${AR} ${LIB} ${OBJS}
	${RANLIB} ${LIB}
	${CP} ${LIB} ${LIB_RDMA_PATH}
	${CP} ${LIB} ${LIB_RDMA_PATH_1}
	${MV} ${LIB} ${LIBPATH}

clean:
	${RM} *.o core ~* *.cpp libamp.a
