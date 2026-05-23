/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_sys.h 
 */
 
#ifndef __SKYFS_SYS_H
#define __SKYFS_SYS_H

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
//#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/swap.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/smp_lock.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/genhd.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#else
#include <stdio.h>
#include <stdlib.h>
#define __USE_GNU 1
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <ctype.h>

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <aio.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <asm/ioctls.h>

#include <sys/mman.h>
#include <sys/statvfs.h>
//#include <asm/error.h>

#endif

#endif
/* end of skyfs_sys.h*/
