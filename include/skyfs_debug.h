/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
#ifndef __SKYFS_DEBUG_H__
#define __SKYFS_DEBUG_H__

/*
 * $Id: skyfs_debug.h 
 */


/*
 * skyfs_log_file_len declares the extern varible of a log file's length,
 * which must be defined in each module.
 */
#ifndef SKYFS_LOG_FILE_PATH
#define SKYFS_LOG_FILE_PATH "/var/log/"
#endif

#ifdef __KERNEL__

#define SKYFS_ERROR(format, a...)                                       \
do {                                                            \
           printk("ERROR in (%s, %d): ", __FILE__, __LINE__);    \
    printk(format, ## a); \
} while (0) 

#ifndef __SKYFS_DEBUG__

#define  SKYFS_DEBUG(format, a...) 
#define  SKYFS_MSG(format, a...) 
#define  SKYFS_ENTER(format, a...) 
#define  SKYFS_LEAVE(format, a...) 

#else

#define SKYFS_DEBUG(format, a...)                                \
        do {                                                            \
            printk("(%s, %d): ",  __FILE__, __LINE__);                  \
             printk(format, ## a);                                       \
        } while (0) 

#define SKYFS_MSG(format, a...)                                  \
         do {                                                            \
             printk (format, ## a);                                       \
         } while (0) 

#define SKYFS_ENTER(format, a...)                                  \
         do {                                                            \
             printk (format, ## a);                                       \
         } while (0)

#define SKYFS_LEAVE(format, a...)                                  \
         do {                                                            \
             printk (format, ## a);                                       \
         } while (0)


#endif

#endif

#ifndef __KERNEL__

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
extern int errno;
#define SKYFS_MAX_LOG_FILE_LEN    (50000000)
extern  int skyfs_log_file_len;
extern  pthread_mutex_t skyfs_debug_lock;
/*
#define SKYFS_ERROR(format, a...)                                       \
         do { \
         time_t mytime ;  \
         char *tmp_time_buf = NULL;  \
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         mytime = time (NULL); \
         tmp_time_buf = ctime(&mytime); \
         tmp_time_buf[strlen(tmp_time_buf) - 1] = '\0'; \
         fprintf(stderr, "[%s]: ", tmp_time_buf); \
         fprintf (stderr, format, ## a); \
         fflush(stderr);                        \
         } while (0) 
*/
#ifdef __SKYFS_ADM__
#define SKYFS_ERROR_1(format, a...)                                       \
         do { \
         struct timeval thistime;\
	 FILE * fp = NULL;\
         unsigned long long prsent_us;\
	 fp = fopen("/var/log/skyfs_adm_client.log", "a+"); \
	 if(fp == NULL){ \
         fprintf (stderr,"can not open log file \n"); \
		 break; \
	 }\
          gettimeofday(&thistime, NULL);\
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         prsent_us = thistime.tv_sec * 1000000 + thistime.tv_usec; \
         fprintf(fp, "%llu ", prsent_us); \
         fprintf (fp, format, ## a); \
         fflush(fp);\
	 fclose(fp);	 \
         } while (0) 

#else
#define SKYFS_ERROR_1(format, a...)                                       \
         do { \
         struct timeval thistime;\
         unsigned long long prsent_us;\
          gettimeofday(&thistime, NULL);\
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         prsent_us = thistime.tv_sec * 1000000 + thistime.tv_usec; \
         fprintf(stderr, "%llu ", prsent_us); \
         fprintf (stderr, format, ## a); \
         fflush(stderr);                        \
         } while (0) 

#endif

#define SKYFS_ERROR(format, a...)                                       \
         do { \
                               \
         } while (0) 




/*
    do {                                                       \
         if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {         \
             ftruncate (STDERR_FILENO, 0);                          \
             lseek (STDERR_FILENO, 0, SEEK_SET);                    \
             skyfs_log_file_len = 0;                                \
         } else                                                     \
             skyfs_log_file_len ++;                                 \
			 fprintf (stderr, format, ## a);                        \
			 fflush(stderr);                                        \
         } while (0)

 
 */

#ifndef __SKYFS_DEBUG__

#define SKYFS_MSG(format, a...)   
#define SKYFS_ENTER(format, a...)
#define SKYFS_LEAVE(format, a...)
#define SKYFS_DEBUG(format, a...)                                   \
         do {                                                       \
                                               \
         } while (0)


#else

#define SKYFS_DEBUG(format, a...)                                \
         do {              \
         time_t mytime; \
         char *tmp_time_buf = NULL; \
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         mytime = time (NULL); \
         tmp_time_buf = ctime(&mytime); \
         tmp_time_buf[strlen(tmp_time_buf) - 1] = '\0'; \
         fprintf(stderr, "[%s][%lu]: ", tmp_time_buf, pthread_self()); \
             fprintf(stderr, "(%s, %d): ",  __FILE__, __LINE__);         \
             fprintf(stderr, format, ## a);                              \
             fflush(stderr);                    \
         } while (0) 

#define SKYFS_MSG(format, a...)                                  \
         do {                                                           \
         time_t mytime ;  \
         char *tmp_time_buf = NULL;  \
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         mytime = time (NULL); \
         tmp_time_buf = ctime(&mytime); \
         tmp_time_buf[strlen(tmp_time_buf) - 1] = '\0'; \
         pthread_mutex_lock(&skyfs_debug_lock);                            \
         fprintf(stderr, "[%s][%lu]: ", tmp_time_buf, pthread_self()); \
             fprintf (stderr, format, ## a);                            \
             fflush(stderr);                    \
         pthread_mutex_unlock(&skyfs_debug_lock);                            \
         } while (0)

#define SKYFS_ENTER(format, a...)                                  \
         do {                                                           \
         time_t mytime ;  \
         char *tmp_time_buf = NULL;  \
         if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
             ftruncate (STDERR_FILENO, 0);                          \
             lseek (STDERR_FILENO, 0, SEEK_SET);                    \
             skyfs_log_file_len = 0;                                 \
         } else                                                     \
             skyfs_log_file_len ++;                                  \
         mytime = time (NULL); \
         tmp_time_buf = ctime(&mytime); \
         tmp_time_buf[strlen(tmp_time_buf) - 1] = '\0'; \
         fprintf(stderr, "[%s][%lu]: ", tmp_time_buf, pthread_self()); \
         fprintf (stderr, format, ## a);                            \
         fflush(stderr);                    \
         } while (0)

#define SKYFS_LEAVE(format, a...)                                  \
         do {                                                           \
         time_t mytime ;  \
         char *tmp_time_buf = NULL;  \
             if (skyfs_log_file_len > SKYFS_MAX_LOG_FILE_LEN) {           \
                 ftruncate (STDERR_FILENO, 0);                          \
                 lseek (STDERR_FILENO, 0, SEEK_SET);                    \
                 skyfs_log_file_len = 0;                                 \
             } else                                                     \
                 skyfs_log_file_len ++;                                  \
         mytime = time (NULL); \
         tmp_time_buf = ctime(&mytime); \
         tmp_time_buf[strlen(tmp_time_buf) - 1] = '\0'; \
         fprintf(stderr, "[%s][%lu]: ", tmp_time_buf, pthread_self()); \
             fprintf (stderr, format, ## a);                            \
             fflush(stderr);                    \
         } while (0)


#endif

#endif

#endif

