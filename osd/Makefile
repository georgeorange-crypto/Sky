LDDINC := $(shell pwd)
INC_CLI := $(LDDINC)
INC_PUB  := $(LDDINC)/../include
INC_COM  := $(LDDINC)/../amp-rdma/include
INC_MDS  := $(LDDINC)/../mds
EXTRA_CFLAGS	+= -I$(INC_CLI) -I$(INC_PUB) -I$(INC_COM) -I$(INC_MDS) -D__AMP_RDMA__
EXTRA_CFLAGS	+= -O2 -g -Wall 
#EXTRA_CFLAGS	+= -DSEND_P
#EXTRA_CFLAGS	+= -DCREATE_P
#EXTRA_CFLAGS	+= -DREMOVE_P
#EXTRA_CFLAGS	+= -DLOOKUP_P
#EXTRA_CFLAGS	+= -DREAD_P
#EXTRA_CFLAGS	+= -DHASH_P
#EXTRA_CFLAGS	+= -DPROFILING
EXTRA_CFLAGS	+= -D_GNU_SOURCE -D_AMP_RDMA
#EXTRA_CFLAGS	+= -D__SKYFS_DEBUG__
#EXTRA_CFLAGS	+= -D__MPS_PROF__
#EXTRA_CFLAGS	+= -DMPS_PROFILE
#EXTRA_CFLAGS	+= -DMPS_PREFETCH

LDPATH_AMP = ${INC_COM}/../lib/
LIBNAME_AMP = amp-rdma
LDFLAGS_AMP = -L${LDPATH_AMP} -l${LIBNAME_AMP} -libverbs -lrdmacm -D__AMP_RDMA__


LDPATH_INTVAL_T = ./
LIBNAME_INTVAL_T = IntervalTree 
LDFLAGS_INTVAL_T = -L${LDPATH_INTVAL_T} -l${LIBNAME_INTVAL_T}
LDFLAGS =  -fopenmp -march=native -lm -lpthread -lzstd -lz -lInterval_tree   -L. -Wl,-rpath,. -lmapping_cpu_uint16 -fopenmp -lgomp -lpans -lSZ3c
#LDFLAGS = -lm -lpthread  -lzstd
#libInterval_tree.so

#CC = /opt/gcc-11/bin/gcc
CC = cc
CP = cp
RM = rm -f
AR = ar rvs
MV = mv -f

EXE_FILES = osd 

OBJS =	osd.o	\
	osd_init.o	\
	osd_thread.o	\
	util_compress.o \
	osd_dop.o	\
	osd_mop.o	\
	osd_rw.o	\
	osd_meta.o	\
	osd_daemon.o	\
	osd_layout.o	\
	osd_replica.o	\
	osd_help.o	\
	osd_profile.o \
	osd_dir_op.o \
	osd_ito.o \
	osd_dl.o \
	osd_loadb.o \
	osd_cache.o

#LIB = libcom.a
.c.o:
	${CC}  ${EXTRA_CFLAGS}  -c $*.c

osd: ${OBJS}

	#./make_libmapping.sh
	#./make_interval_dynamic.sh
	${CC}  -O2 -g -o $@ ${OBJS} ${LDFLAGS} ${LDFLAGS_AMP} 
	cp osd /cluster/skyfs/bin/skyfs_osd
	cp osd /cluster/skyfs/bin/osd
	cp osd ../bin
#	cp osd /home/xingjing/bin
#	g++ -fPIC -shared interval_tree_so.cpp -o libInterval_tree.so
#	cp libInterval_tree.so /usr/lib
#       cp libInterval_tree.so /usr/lib64

clean:
	${RM} *.o core ~* ${EXE_FILES} 

