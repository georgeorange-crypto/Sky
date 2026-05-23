/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_help.h 
 */

#ifndef __MDS_HELP_H
#define __MDS_HELP_H

extern pthread_mutex_t forward_request_lock;

#define SKYFS_M_FILL_ACK_HEAD(mds_ackp, ack_type, ack_error)    \
    do {                                        \
        (mds_ackp)->type = (ack_type | 0x80000000); \
        (mds_ackp)->fs_id = 0;              \
        (mds_ackp)->error = (ack_error); \
        (mds_ackp)->magic = SKYFS_MSG_MAGIC;             \
        (mds_ackp)->fromid = mds_this_id;    \
        (mds_ackp)->fromType  = SKYFS_MDS;   \
        (mds_ackp)->size = SKYFS_MSGHEAD_SIZE;     \
    } while(0)


#endif

extern skyfs_s32_t 
__skyfs_MS_set_bit(skyfs_u8_t *addr,
    skyfs_u32_t local, 
    skyfs_u32_t value);

extern skyfs_s32_t 
__skyfs_MS_is_set(skyfs_u8_t *addr, skyfs_u32_t size);

extern skyfs_s32_t
__skyfs_MS_test_bit(skyfs_u8_t *addr, skyfs_u32_t local);

extern skyfs_s32_t
__skyfs_MS_init_req(amp_request_t **req, 
				skyfs_msg_t **msg, 
				skyfs_u32_t msg_type,
				skyfs_u32_t ack_flag,
				skyfs_u32_t req_type,
				skyfs_u32_t	msgsize);
extern skyfs_s32_t	
__skyfs_MS_init_reply(amp_request_t **req, 
				skyfs_msg_t **msgp, 
				skyfs_u32_t req_type,
				skyfs_u32_t	req_niov,
				amp_kiov_t *req_iov,
				skyfs_u32_t	size);

extern skyfs_s32_t 
__skyfs_MS_forward_request(amp_request_t *req,
				skyfs_u32_t	com_type,
				skyfs_u32_t	id);


extern skyfs_u32_t __skyfs_MS_get_mdsid(void);

/*This is end of file*/
