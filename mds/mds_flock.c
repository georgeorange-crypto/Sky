
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <linux/fs.h>
#include <fcntl.h>


#include "skyfs_sys.h"
#include "skyfs_list.h"
#include "skyfs_const.h"
#include "skyfs_types.h"
#include "skyfs_fs.h"

//////////////////
#include "amp.h"
#include "skyfs_msg.h"
#include "skyfs_debug.h"
#include "skyfs_hash.h"
#include "mds_fs.h"
#include "mds_flock.h"

static int  IS_LOCK_POSIX(skyfs_M_flock_t * file_lock)
{
	return ((file_lock->l_flags & 0x03) == SKYFS_FL_POSIX); 
}

static int  IS_LOCK_FLOCK(skyfs_M_flock_t * file_lock)
{
	return ((file_lock->l_flags & 0x03) == SKYFS_FL_FLOCK); 
}
static int posix_same_owner(skyfs_M_flock_t * file_lock1, skyfs_M_flock_t * file_lock2)
{
	// just use pid lock_owner and  clt_id as flock_owner, do not use pid
	return (file_lock1->clt_id == file_lock2->clt_id 
				&& file_lock1->lock_owner == file_lock2->lock_owner);
}

static int flock_same_owner(skyfs_M_flock_t * file_lock1, skyfs_M_flock_t * file_lock2)
{
	// just use lock_owner  and clt_id as flock_owner, do not use pid
	return (file_lock1->clt_id == file_lock2->clt_id 
				&& file_lock1->lock_owner == file_lock2->lock_owner);
}


static void locks_free_lock(skyfs_M_flock_t *fl)
{
	free(fl);
}

static skyfs_M_flock_t * locks_alloc_lock(void)
{
	skyfs_M_flock_t *fl = (skyfs_M_flock_t *)calloc(1,sizeof( skyfs_M_flock_t));
	if(fl){
		//locks_init_lock_heads(fl);
		INIT_LIST_HEAD(&fl->flock_list);
		INIT_LIST_HEAD(&fl->posix_lock_list);

	}
	return fl;
}

static void __locks_copy_lock(skyfs_M_flock_t *new, skyfs_M_flock_t *fl)
{
	new->lock_owner = fl->lock_owner;
	new->l_pid = fl->l_pid;
	new->clt_id = fl->clt_id;
	//new->l_fd = l->fl_fd;
	//new->l_caller_type = fl->fl_caller_type;
	new->l_flags = fl->l_flags;
	new->l_type = fl->l_type;
	new->l_start = fl->l_start;
	new->l_end = fl->l_end;
}


static int locks_conflict(skyfs_M_flock_t *new, skyfs_M_flock_t *fl)
{
	if(fl->l_type == F_WRLCK)
		return 1;
	if(new->l_type == F_WRLCK)
		return 1;
	return 0;

}

/* Check if two locks overlap each other.
 */
static  int locks_overlap(skyfs_M_flock_t *fl1, skyfs_M_flock_t *fl2)
{               
	return ((fl1->l_end >= fl2->l_start) &&
			(fl2->l_end >= fl1->l_start));
} 


static int posix_locks_conflict(skyfs_M_flock_t *caller_fl, skyfs_M_flock_t * sys_fl)
{
	if (!IS_LOCK_POSIX(sys_fl) || posix_same_owner(caller_fl, sys_fl)) {
		//klog(DEBUG,"same lock owner is %p", caller_fl->fl_owner);
		return (0);
	}

	/* Check whether they overlap */
	if (!locks_overlap(caller_fl, sys_fl)) {
		//if(locks_align && extend_locks_conflict(caller_fl, sys_fl))		
		//	return 1;
		return 0;
	}

	return (locks_conflict(caller_fl, sys_fl));

}

/* Insert file lock fl into an inode's lock list at the PREV of the  position indicated
 * by pos. At the same time add the lock to the global file lock list.
 * Must be called with the i_lock held!
 */
static void locks_insert_lock(struct list_head *pos, skyfs_M_flock_t  *fl)
{
	struct list_head * new_list = NULL;
	if(IS_LOCK_POSIX(fl))
		new_list = &fl->posix_lock_list;
	if(IS_LOCK_FLOCK(fl))
		new_list = &fl->flock_list;
	if(new_list){
		list_add_tail(new_list, pos);
	}else{
		printf("SKYFS insert lock failed , NULL list of fl\n");
	}

	
}

/* Delete file lock fl from an mmeta's lock list, then free it
 * Acorrding to the new_fl->l_type
 * Must be called with the i_lock held!
 */


static void locks_delete_lock(skyfs_M_flock_t *fl, skyfs_M_flock_t  *new_fl)
{
	struct list_head * new_list = NULL;
	SKYFS_ERROR("SKYFS delete lock  , fl start is %lu , end is %lu\n", fl->l_start, fl->l_end);
	if(IS_LOCK_POSIX(new_fl))
		new_list = &fl->posix_lock_list;
	if(IS_LOCK_FLOCK(new_fl))
		new_list = &fl->flock_list;
	if(new_list){
		list_del_init(new_list);
		locks_free_lock(fl);
	}else{
		SKYFS_ERROR("SKYFS delete lock failed , NULL list of fl\n");
	}

	
}


// Note the function below is called with the mmeta lock held.
int __skyfs_flock_lock_file(skyfs_M_mmeta_t * mmeta, skyfs_M_flock_t *request, skyfs_M_flock_t *conflock)
{
	// TODO : need to implement this function
	skyfs_M_flock_t *fl;
	skyfs_M_flock_t *new_fl = NULL;
	struct list_head *   head = NULL;
	struct list_head *   before = NULL;
	struct list_head *   next = NULL;
	int error = 0;
	int found = 0;
	int i = 0;

	skyfs_M_cmeta_t * cmeta = mmeta->cmetap;

	if (!(request->l_flags & SKYFS_FL_ACCESS) &&
	    request->l_type != F_UNLCK ) {
		new_fl = locks_alloc_lock(); // TODO , implement this function
		if(!new_fl)
			return ENOENT;
	}
	
	head = &mmeta->flock_head;
	if (request->l_flags & SKYFS_FL_ACCESS)
		goto find_conflict;
	
	// find the same_ownered flock and replace it.
	SKYFS_ERROR("flock same owner loop , head %p , prev %p next %p\n", head, head->prev, head->next);
	list_for_each_safe(before, next,head){
		i++;
		if(i<10)
			SKYFS_ERROR("flock same owner IN loop , head %p , prev %p next %p : %d\n", head, head->prev, head->next, i);
		fl = list_entry(before, skyfs_M_flock_t, flock_list);
		if(!IS_LOCK_FLOCK(fl))
			continue;
		if(! flock_same_owner(request, fl))
			continue;  
		if(request->l_type == fl->l_type) // request and fl are sll same, just return success 
			goto out;
		found = 1;
		locks_delete_lock(fl, request);
		break;
		 	
	}// end list_for each
	if (request->l_type == F_UNLCK) {
		if ((request->l_flags & SKYFS_FL_EXISTS) && !found)
			error = ENOENT;
		goto out;
	}
			
	// check if here are conflict flocks
find_conflict:
	SKYFS_ERROR("flock conflict loop , head %p , prev %p next %p\n");
	i = 0;
	list_for_each_safe(before, next,head){
		i++;
		if(i<10)
			SKYFS_ERROR("flock conflict IN loop , head %p , prev %p next %p : %d\n", head, head->prev, head->next, i);
		fl = list_entry(before, skyfs_M_flock_t, flock_list);
		if(!IS_LOCK_FLOCK(fl))
			continue;
		if(flock_same_owner(request, fl))
			continue;  
		if(!locks_conflict(request, fl)) // just use locks_conflict for flock_conflict
			continue;
		error =	EAGAIN;
		found = 1;
		if(conflock)
			__locks_copy_lock(conflock, fl);
		goto out;
		 	
	}// end list_for each
	if (request->l_flags & SKYFS_FL_ACCESS )
			goto out;

	SKYFS_ERROR("flock insert , head %p , prev %p next %p\n");
	__locks_copy_lock(new_fl, request);
	locks_insert_lock(before, new_fl);
	new_fl = NULL;
	error = 0;

out:
	if(new_fl)
		locks_free_lock(new_fl);
	if(request->l_type != F_UNLCK)
		SKYFS_ERROR_1("skyfs flock lock file ino %llx,  return %d\n", error, cmeta->ino);
	return error;
}


// Note the function below is called with the mmeta lock held.
int __skyfs_posix_lock_file(skyfs_M_mmeta_t * mmeta, skyfs_M_flock_t *request, skyfs_M_flock_t *conflock)
{
	skyfs_M_flock_t *fl;
	skyfs_M_flock_t rfl;
	skyfs_M_flock_t *new_fl = NULL;
	skyfs_M_flock_t *new_fl2 = NULL;
	skyfs_M_flock_t  *left = NULL;
	skyfs_M_flock_t  *right = NULL;
	struct list_head *   head = NULL;
	struct list_head *   before = NULL;
	struct list_head *   next = NULL;
	int error;
	int added = 0;
	int unlock_all = 0;
	
	skyfs_M_cmeta_t * cmeta = mmeta->cmetap;

	__locks_copy_lock(&rfl, request);
	/*
	 * We may need two file_lock structures for this operation,
	 * so we get them in advance to avoid races.
	 *
	 * In some cases we can be sure, that no new locks will be needed
	 */
	if (!(request->l_flags & SKYFS_FL_ACCESS) &&
	    (request->l_type != F_UNLCK ||
	     request->l_start != 0 || request->l_end != OFFSET_MAX)) {
		new_fl = locks_alloc_lock(); // TODO , implement this function
		new_fl2 = locks_alloc_lock();
	}
	if(request->l_start == 0 && request->l_end == OFFSET_MAX && request->l_type == F_UNLCK ){
		unlock_all = 1;
		SKYFS_ERROR_1("SKYFS_MS unlock all  %llu, %llu\n", request->l_end, OFFSET_MAX );
	}


	head = &mmeta->posix_lock_head;
	if (request->l_type != F_UNLCK) {
		SKYFS_ERROR("start !UNLOCK loop for posix lock \n");
		list_for_each(before, head){
			fl = list_entry(before, skyfs_M_flock_t, posix_lock_list);
			if (!IS_LOCK_POSIX(fl))
				continue;
			if (!posix_locks_conflict(request, fl)) // TODO: implement this function
				continue;
			if (conflock)
				__locks_copy_lock(conflock, fl);   // TODO : implement this function
			error = EAGAIN; // if lock wait, should be EAGAIN
			//if (!(request->l_flags & SKYFS_FL_SLEEP))
			if (request->l_flags & SKYFS_FL_ACCESS){
				SKYFS_ERROR("getlk failed, copy conflock , lstart %d, old_lstart %d\n", conflock->l_start, fl->l_start);
			}
			goto out;
			
		}// end list_for_each
	} // end if(request->fl_type) 
	
	error = 0;
	// no conflict, so we can return success if only do getlk
	if (request->l_flags & SKYFS_FL_ACCESS)
		goto out;
 

	// come here means no posix_locks_conflict, new start insert request to the mmeta posix_lock_list

	// use safe iterate, because maybe remove
	SKYFS_ERROR("start no conflict  loop for posix lock \n");
	list_for_each_safe(before, next,head){
		fl = list_entry(before, skyfs_M_flock_t, posix_lock_list);
		// skip the different owner, because these ones have been verified no conflict by above loop
		if(! posix_same_owner(request, fl))
			continue;  
		if(request->l_type == fl->l_type){
			// same lock type
			/* In all comparisons of start vs end, use
			 * "start - 1" rather than "end + 1". If end
			 * is OFFSET_MAX, end + 1 will become negative.
			 */
			if (fl->l_end < request->l_start - 1)
				goto next_lock;
			/* If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->l_start - 1 > request->l_end)
				break;

			/* If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->l_start > request->l_start)
				fl->l_start = request->l_start;
			else
				request->l_start = fl->l_start;
			if (fl->l_end < request->l_end)
				fl->l_end = request->l_end;
			else
				request->l_end = fl->l_end;
			if (added) {
				// TODO: mayl implement the function below
				//locks_delete_lock(mmeta, before, request);
				locks_delete_lock(fl, request);
				//if(fl->fl_reuse == 1) 
				//	goto next_lock;
				continue;
			}
			request = fl;
			added = 1;

		}else{ 

			if(unlock_all){
				SKYFS_ERROR_1("unlock all , fl_start %llu, fl_end %llu, fl_type %lu\n",
						fl->l_start, fl->l_end, fl->l_type);
			}
			// different lock type, more complex !
			if (fl->l_end < request->l_start)
				goto next_lock;
			if (fl->l_start > request->l_end)
				break;
			if (request->l_type == F_UNLCK)
				added = 1;
			if (fl->l_start < request->l_start)
				left = fl;
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->l_end > request->l_end) {
				right = fl;
				break;
			}
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->l_end > request->l_end) {
				right = fl;
				break;
			}
			if (fl->l_start >= request->l_start) {
				/* The new lock completely replaces an old
				 * one (This may happen several times).
				 */
				
				if (added) {
					locks_delete_lock(fl, request);
					//if(fl->fl_reuse == 1) goto next_lock;
					continue;
				}
				/* Replace the old lock with the new one.
				 * Wake up anybody waiting for the old one,
				 * as the change in lock type might satisfy
				 * their needs.
				 */
				//locks_wake_up_blocks(fl);
				fl->l_start = request->l_start;
				fl->l_end = request->l_end;
				fl->l_type = request->l_type;
				/* enfs do not have private */
				//locks_release_private(fl);
				//locks_copy_private(fl, request);
				request = fl;
				added = 1;
			}
			


			
		}// end else (different lock type)
next_lock:
		continue;
			
	} // end list_for_each

	SKYFS_ERROR("start remove /insert lock for posix lock \n");
	/*
	 * The above code only modifies existing locks in case of merging or
	 * replacing. If new lock(s) need to be inserted all modifications are
	 * done below this, so it's safe yet to bail out.
	 */
	error = ENOLCK; /* "no lock" */
	// no lock error because alloc lock failed
	if (right && left == right && !new_fl2)
		goto out;

	error = 0;
	if (!added) {
		if (request->l_type == F_UNLCK) {
			if (request->l_flags & SKYFS_FL_EXISTS)
				error = ENOENT;
			goto out;
		}

		if (!new_fl) {
			error = ENOLCK;
			goto out;
		}
		__locks_copy_lock(new_fl, request);
		locks_insert_lock(before, new_fl);
		new_fl = NULL;
	}
	if (right) {
		if (left == right) {
			/* The new lock breaks the old one in two pieces,
			 * so we have to use the second new lock.
			 */
			left = new_fl2;
			new_fl2 = NULL;
			__locks_copy_lock(left, right);
			locks_insert_lock(before, left);
		}
		right->l_start = request->l_end + 1;
		//locks_wake_up_blocks(right);
	}
	if (left) {
		left->l_end = request->l_start - 1;
		//locks_wake_up_blocks(left);
	}
out:
	//pthread_spin_unlock(&file->i_lock);
	/*
	 * Free any unused locks.
	 */
	if (new_fl)
		locks_free_lock(new_fl);
	if (new_fl2)
		locks_free_lock(new_fl2);
	
	if (request->l_type != F_UNLCK) {
		SKYFS_ERROR_1("receive a file posix lock request ino %llx, clt_id %d, pid %d, l_owner %llx, type %x, flags %x , return %d, start %llu , end %llu \n",
				cmeta->ino, rfl.clt_id, rfl.l_pid, rfl.lock_owner, rfl.l_type, rfl.l_flags , error, request->l_start, request->l_end);
	}else{
		SKYFS_ERROR_1("receive a file posix unlock request ino %llx, clt_id %d, pid %d, l_owner %llx, type %x, flags %x , return %d , start %llu end %llu, max %llu\n",
				cmeta->ino, rfl.clt_id, rfl.l_pid, rfl.lock_owner, rfl.l_type, rfl.l_flags , error, request->l_start, request->l_end, OFFSET_MAX);

	}
	return error;

}


void convert_flock_arg_to_fl(skyfs_m_flock_args_t * req_arg, skyfs_M_flock_t * fl)
{

	uint32_t flock_class = (req_arg->fl_type)>>16; // high 16-bit identyfy FLOCK or POSIX_LOCK
	fl->clt_id = req_arg->clt_id;
	fl->l_pid = req_arg->pid;
	fl->lock_owner = req_arg->lock_owner;
	fl->l_start = req_arg->start;
	fl->l_len = req_arg->len;
	fl->l_end = req_arg->len+req_arg->start-1;
	if(fl->l_len == 0){
		SKYFS_ERROR("fl_len is 0 , set L_end to OFFSET_MAX\n");
		fl->l_end = OFFSET_MAX;
	}
	fl->l_type = (req_arg->fl_type) & 0xffff; // F_RDLCK, F_WRLCK, F_UNLCK, get form the low-16bits
	fl->l_flags = 0;
	if((req_arg->fl_type & 0xffff) == F_UNLCK)
		fl->l_flags |= SKYFS_FL_EXISTS;
	if((req_arg->op_type & 0xffff) == F_GETLK)
		fl->l_flags |= SKYFS_FL_ACCESS;
	if((req_arg->op_type & 0xffff) == F_SETLKW)
		fl->l_flags |= SKYFS_FL_SLEEP;
	if(flock_class == 0x01)
		fl->l_flags |= SKYFS_FL_POSIX;
	if(flock_class == 0x02)
		fl->l_flags |= SKYFS_FL_FLOCK;
	
	SKYFS_ERROR("get client %d file_lock , pid %d, op_type %x, fl_type %x\n",
				req_arg->clt_id, req_arg->pid, req_arg->op_type, req_arg->fl_type);
	SKYFS_ERROR("receive a file lock request clt_id %d, pid %d, l_owner %llx, type %x, flags %x   \n",
				fl->clt_id, fl->l_pid, fl->lock_owner, fl->l_type,fl->l_flags );

	

	// TODO: convert req cmd to fl flags 
	
}

int __skyfs_MS_lock_file(skyfs_M_mmeta_t * mmeta, skyfs_M_flock_t * request,  skyfs_M_flock_t * conflock)
{
	int rc = 0;
	if(IS_LOCK_POSIX(request)){
		SKYFS_ERROR("call _posix_lock_file\n");
		rc =   __skyfs_posix_lock_file(mmeta, request, conflock); 
	}else if (IS_LOCK_FLOCK(request)){
		SKYFS_ERROR("call _flock_lock_file\n");
		//return  __skyfs_posix_lock_file(mmeta, request, conflock); 
		 rc =   __skyfs_flock_lock_file(mmeta, request, conflock);
	}else{
		SKYFS_ERROR("invalid FILE_LOCK  TYPE %x\n", request->l_type);
		rc =  EINVAL;
	}
		SKYFS_ERROR_1("skyfs FILE_LOCK  TYPE %x, ret %d\n", request->l_type, rc);
	return rc;
	

}
