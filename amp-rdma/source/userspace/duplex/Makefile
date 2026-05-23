
CC = cc
CP = cp
RM = rm -f
MV = mv -f
TOP_DIR = ../../../
INC_AMP = ${TOP_DIR}/include/
INC_THIS = ./
CFLAGS = -I${INC_AMP} -I${INC_THIS}

#LDPATH_AMP = ${TOP_DIR}/lib/
LDPATH_AMP = ./

LIBNAME_AMP = amp
LDFLAGS_AMP = -L${LDPATH_AMP} -l${LIBNAME_AMP}
LDFLAGS = -lm -lpthread -lrt

EXE_FILES=server client
all: ${EXE_FILES} 

SERVER_OBJ=server.o
CLIENT_OBJ=client.o

	
.c.o:
	${CC} ${CFLAGS} ${EXTRA_CFLAGS} -g  -c $*.c

server: server.o
	${CC}  -O2 -g -o $@ ${SERVER_OBJ} ${LDFLAGS} ${LDFLAGS_AMP} 

client: client.o
	${CC}  -O2 -g -o $@ ${CLIENT_OBJ} ${LDFLAGS} ${LDFLAGS_AMP} 



clean:
	${RM} *.o core ~* *.cpp ${EXE_FILES}
