/*
 *  Copyright (c) 2009  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: client_help.h 
 */

#ifndef __CLIENT_HELP_H
#define __CLIENT_HELP_H


int __skyfs_C_get_clientid(void);

skyfs_s32_t __skyfs_C_test_bit(skyfs_u8_t *addr, skyfs_u32_t local);

skyfs_s32_t    
__skyfs_C_init_reply(amp_request_t **req, 
                skyfs_msg_t **msgp, 
                skyfs_u32_t req_type,
                skyfs_u32_t req_niov,
                amp_kiov_t 	*req_iov,
				skyfs_u32_t size);

#endif
/*This is end of client_help.h*/
