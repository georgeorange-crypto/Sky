#ifndef __MDS_FLOCK_H
#define __MDS_FLOCK_H

extern void convert_flock_arg_to_fl(skyfs_m_flock_args_t * req_arg, skyfs_M_flock_t * fl);
extern int __skyfs_MS_lock_file(skyfs_M_mmeta_t * mmeta, skyfs_M_flock_t * request,  skyfs_M_flock_t * conflock);

#endif 
/*This is end of file*/

