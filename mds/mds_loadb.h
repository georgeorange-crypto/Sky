/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
/*
 * $Id: mds_loadb.h 
 */

#ifndef __MDS_LOADB_H
#define __MDS_LOADB_H

extern void __skyfs_MS_start_balance(amp_request_t *req);

extern void __skyfs_MS_balance_load(amp_request_t *req);

skyfs_s32_t __skyfs_MS_choose_kindone();

#endif
/*This is end of mds_loadb.h*/
