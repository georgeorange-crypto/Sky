#include "enfs2state.h"
#include "enfs2locks.h"
#include "enfs2xdr.h"
#include "enfscommon.h"
#include "rbtree.h"
#include <syslog.h>
#include <glib/gslice.h>
#include <string.h>
#include "enfsd.h"
#include <fcntl.h>
#include "hashtable.h"
#include "compiler.h"

#define MAX_DEADLK_ITERATIONS 10
#define IS_POSIX(fl)    (fl->fl_flags & ENFS_FL_POSIX)
//#define IS_FLOCK(fl)    (fl->fl_flags & ENFS_FL_FLOCK)
#define IS_LEASE(fl)    (fl->fl_flags & (ENFS_FL_LEASE|ENFS_FL_DELEG))
#define for_each_lock(file, lockp) \
	        for (lockp = &file->i_flock; *lockp != NULL; lockp = &(*lockp)->fl_next)

#define BLOCKED_HASH_BITS       7
static DEFINE_HASHTABLE(blocked_hash, BLOCKED_HASH_BITS);
pthread_mutex_t blocked_lock_lock = PTHREAD_MUTEX_INITIALIZER;

struct list_head reuse_file_head = LIST_HEAD_INIT(reuse_file_head);
pthread_mutex_t reuse_file_lock = PTHREAD_MUTEX_INITIALIZER;
//static struct list_head *file_lock_list = NULL;

pthread_mutex_t file_lock_lglock = PTHREAD_MUTEX_INITIALIZER;

static void locks_init_lock_heads(struct enfs_file_lock *fl)
{
	INIT_HLIST_NODE(&fl->fl_link);
	INIT_LIST_HEAD(&fl->fl_block);
//	init_waitqueue_head(&fl->fl_wait);
}

void locks_init_lock(struct enfs_file_lock *fl)
{
	memset(fl, 0, sizeof(struct enfs_file_lock));
	locks_init_lock_heads(fl);
}

struct enfs_file_lock * locks_alloc_lock(void)
{
	struct enfs_file_lock *fl = g_slice_new0(struct enfs_file_lock);
	if(fl)
		locks_init_lock_heads(fl);
	return fl;
}

/* Check if two locks overlap each other.
 */
static inline int locks_overlap(struct enfs_file_lock *fl1, struct enfs_file_lock *fl2)
{               
	return ((fl1->fl_end >= fl2->fl_start) &&
			(fl2->fl_end >= fl1->fl_start));
} 

/*
 * Check whether two locks have the same owner.
 */                     
static int posix_same_owner(struct enfs_file_lock *fl1, struct enfs_file_lock *fl2)
{                               
	return fl1->fl_owner == fl2->fl_owner;
}  


/* Must be called with the i_lock held! */
//	static inline void
//locks_insert_global_locks(struct enfs_file_lock *fl)
//{
//	pthread_mutex_lock(&file_lock_lglock);
//	hlist_add_head(&fl->fl_link, file_lock_list);
//	pthread_mutex_unlock(&file_lock_lglock);
//}

/* Must be called with the i_lock held! */
//	static inline void
//locks_delete_global_locks(struct enfs_file_lock *fl)
//{
//	/*
//	 * Avoid taking lock if already unhashed. This is safe since this check
//	 * is done while holding the i_lock, and new insertions into the list
//	 * also require that it be held.
//	 */
//	if (hlist_unhashed(&fl->fl_link))
//		return;
//	pthread_mutex_lock(&file_lock_lglock);
//	hlist_del_init(&fl->fl_link);
//	pthread_mutex_unlock(&file_lock_lglock);
//}


static unsigned long
	posix_owner_key(struct enfs_file_lock *fl)
{                               
	return (unsigned long)fl->fl_owner;
} 

static inline void
locks_insert_global_blocked(struct enfs_file_lock *waiter)
{
	hash_add(blocked_hash, &waiter->fl_link, posix_owner_key(waiter));
}

static inline void
locks_delete_global_blocked(struct enfs_file_lock *waiter)
{
	hash_del(&waiter->fl_link);
}

/* Remove waiter from blocker's block list.
 * When blocker ends up pointing to itself then the list is empty.
 *
 * Must be called with blocked_lock_lock held.
 */
static void __locks_delete_block(struct enfs_file_lock *waiter)
{
	locks_delete_global_blocked(waiter);
	list_del_init(&waiter->fl_block);
	waiter->fl_next = NULL;
}

static void __attribute__((unused)) locks_delete_block(struct enfs_file_lock *waiter)
{
	pthread_mutex_lock(&blocked_lock_lock);
	__locks_delete_block(waiter);
	pthread_mutex_unlock(&blocked_lock_lock);
}

void locks_copy_lock(struct enfs_file_lock *new, struct enfs_file_lock *fl)
{
	new->fl_owner = fl->fl_owner;
	new->fl_pid = fl->fl_pid;
	new->fl_fd = fl->fl_fd;
	new->fl_caller_type = fl->fl_caller_type;
	new->fl_flags = fl->fl_flags;
	new->fl_type = fl->fl_type;
	new->fl_start = fl->fl_start;
	new->fl_end = fl->fl_end;
}


/* Insert waiter into blocker's block list.
 * We use a circular list so that processes can be easily woken up in
 * the order they blocked. The documentation doesn't require this but
 * it seems like the reasonable thing to do.
 *
 * Must be called with both the i_lock and blocked_lock_lock held. The fl_block
 * list itself is protected by the file_lock_list, but by ensuring that the
 * i_lock is also held on insertions we can avoid taking the blocked_lock_lock
 * in some cases when we see that the fl_block list is empty.
 */
static void __locks_insert_block(struct enfs_file_lock *blocker,
					struct enfs_file_lock *waiter)
{
	klog(DEBUG, "!list_empty(&waiter->fl_block");
	waiter->fl_next = blocker;
	list_add_tail(&waiter->fl_block, &blocker->fl_block);
	if (IS_POSIX(blocker))
		locks_insert_global_blocked(waiter);
}

/* Must be called with i_lock held. */
static void __attribute__((unused)) locks_insert_block(struct enfs_file_lock *blocker,
					struct enfs_file_lock *waiter)
{
	pthread_mutex_lock(&blocked_lock_lock);
	__locks_insert_block(blocker, waiter);
	pthread_mutex_unlock(&blocked_lock_lock);
}

/*
 * Wake up processes blocked waiting for blocker.
 *
 * Must be called with the inode->i_lock held!
 */
static void locks_wake_up_blocks(struct enfs_file_lock *blocker)
{
	/*
	 * Avoid taking global lock if list is empty. This is safe since new
	 * blocked requests are only added to the list under the i_lock, and
	 * the i_lock is always held here. Note that removal from the fl_block
	 * list does not require the i_lock, so we must recheck list_empty()
	 * after acquiring the blocked_lock_lock.
	 */
	if (list_empty(&blocker->fl_block))
		return;

	pthread_mutex_lock(&blocked_lock_lock);
	while (!list_empty(&blocker->fl_block)) {
		struct enfs_file_lock *waiter;

		waiter = list_first_entry(&blocker->fl_block,
				struct enfs_file_lock, fl_block);
		__locks_delete_block(waiter);
		/* Futhur implement
		if (waiter->fl_lmops && waiter->fl_lmops->lm_notify)
			waiter->fl_lmops->lm_notify(waiter);
		else
			wake_up(&waiter->fl_wait);
		*/
	}
	pthread_mutex_unlock(&blocked_lock_lock);
}

/* Insert file lock fl into an inode's lock list at the position indicated
 * by pos. At the same time add the lock to the global file lock list.
 *
 * Must be called with the i_lock held!
 */
static void locks_insert_lock(struct enfs_file_lock **pos, struct enfs_file_lock *fl)
{
	//fl->fl_nspid = get_pid(task_tgid(current));

	/* insert into file's list */
	fl->fl_next = *pos;
	*pos = fl;

	//locks_insert_global_locks(fl);
}
void locks_free_lock(struct enfs_file_lock *fl)
{
	g_slice_free(struct enfs_file_lock, fl);
}

/*
 * Delete a lock and then free it.
 * Wake up processes that are blocked waiting for this lock,
 * notify the FS that the lock has been cleared and
 * finally free the lock.
 *
 * Must be called with the i_lock held!
 */
static void locks_delete_lock(struct enfs2_file *file, struct enfs_file_lock **thisfl_p, struct enfs_file_lock *request)
{
	struct enfs_file_lock *fl = *thisfl_p;
	klog(DEBUG,"Here that we delete lock ");

	if((fl->fl_caller_type == 0) || 
		(fl->fl_caller_type == FL_CNAS && request->fl_caller_type == FL_CNAS)){
		*thisfl_p = fl->fl_next;
		fl->fl_next = NULL;
		/*
		   if (fl->fl_nspid) {
		   put_pid(fl->fl_nspid);
		   fl->fl_nspid = NULL;
		   }
		   */
		locks_wake_up_blocks(fl);
		locks_free_lock(fl);
	} else {
		get_enfs2_file(file);
		struct enfs2_lockowner *lo;
		lo = alloc_stateowner(&fl->fl_owner->lo_owner.so_owner,fl->fl_owner->lo_owner.so_client,sizeof(*lo));
		fl->fl_owner = lo;
		fl->fl_reuse = 1;
		clock_gettime(CLOCK_REALTIME,&fl->fl_reuse_time);
		pthread_mutex_lock(&reuse_file_lock);
		if(!file->fi_in_reuse) {
			list_add(&file->fi_reuse_file_node, &reuse_file_head);
			file->fi_in_reuse = 1;
		}
		list_add(&fl->fl_reuse_lock_node, &file->fi_reuse_lock_head);
		pthread_mutex_unlock(&reuse_file_lock);
	}
}


/* Determine if lock sys_fl blocks lock caller_fl. Common functionality
 * checks for shared/exclusive status of overlapping locks.
 * */
static int locks_conflict(struct enfs_file_lock *caller_fl, struct enfs_file_lock *sys_fl)
{
	if (sys_fl->fl_type == F_WRLCK)
		return 1;
	if (caller_fl->fl_type == F_WRLCK)
		return 1;
	return 0;
}

/* Determine if 4k-alignment lock fl1 blocks 4k-alignment lock fl2.*/                     
static int extend_locks_conflict(struct enfs_file_lock *fl1, struct enfs_file_lock *fl2)
{
	struct enfs2_clientid *clid_fl1, *clid_fl2;
	uint64_t fl1_start_bnr, fl1_end_bnr, fl2_start_bnr, fl2_end_bnr;

	clid_fl1 = &fl1->fl_owner->lo_owner.so_client->cl_clientid;	
	clid_fl2 = &fl2->fl_owner->lo_owner.so_client->cl_clientid;	
  	if((clid_fl1->cl_mdsverf == clid_fl2->cl_mdsverf) && 
			(clid_fl1->cl_shclid == clid_fl2->cl_shclid))
		return 0;
	
	fl1_start_bnr = fl1->fl_start >> EXFS_BLKSIZE_BITS;
	fl1_end_bnr = fl1->fl_end >> EXFS_BLKSIZE_BITS;
	fl2_start_bnr = fl2->fl_start >> EXFS_BLKSIZE_BITS;
	fl2_end_bnr = fl2->fl_end >> EXFS_BLKSIZE_BITS;
	if((fl2_end_bnr < fl1_start_bnr) || (fl1_end_bnr < fl2_start_bnr))
		return 0;
	
	return (locks_conflict(fl1, fl2));
}


/* Determine if lock sys_fl blocks lock caller_fl. POSIX specific
 * checking before calling the locks_conflict().
 * */                     
static int posix_locks_conflict(struct enfs_file_lock *caller_fl, struct enfs_file_lock *sys_fl)
{                               
	/* POSIX locks owned by the same process do not conflict with
	 * each other.          
	 * */             
	if (!IS_POSIX(sys_fl) || posix_same_owner(caller_fl, sys_fl)) {
		klog(DEBUG,"same lock owner is %p", caller_fl->fl_owner);
		return (0);
	}

	/* Check whether they overlap */
	if (!locks_overlap(caller_fl, sys_fl)) {
		if(locks_align && extend_locks_conflict(caller_fl, sys_fl))		
			return 1;
		return 0;
	}

	return (locks_conflict(caller_fl, sys_fl));
}   

static int posix_locks_conflict_win(struct enfs_file_lock *caller_fl, struct enfs_file_lock *sys_fl)
{                               
	if (!IS_POSIX(sys_fl))
		return 0;

	/* Check whether they overlap */
	if (!locks_overlap(caller_fl, sys_fl)) {
		if(locks_align && extend_locks_conflict(caller_fl, sys_fl))		
			return 1;
		return 0;
	}

	/* windows allow a read lock on top of a write lock if they have the same owner */
	if (posix_same_owner(caller_fl, sys_fl) &&
		caller_fl->fl_type == F_RDLCK &&
		sys_fl->fl_type == F_WRLCK) 
		return 0;

	return locks_conflict(caller_fl, sys_fl);
}   

/*
 * Initialize a new lock from an existing file_lock structure.
 * */
void __locks_copy_lock(struct enfs_file_lock *new, const struct enfs_file_lock *fl)
{
	new->fl_owner = fl->fl_owner;
	new->fl_pid = fl->fl_pid;
	new->fl_flags = fl->fl_flags;
	new->fl_type = fl->fl_type;
	new->fl_start = fl->fl_start;
	new->fl_end = fl->fl_end;
} 

struct enfs2_file *enfs2_get_file(struct enfs2_lockowner *lo)
{
	struct enfs2_ol_stateid *lst;
	
	if (!lo)
		return NULL;
	
	lst = list_first_entry(&lo->lo_owner.so_stateids, 
			struct enfs2_ol_stateid, st_perstateowner);
	/*lst = list_first_entry_or_null(&lo->lo_owner.so_stateids, 
			struct enfs2_ol_stateid, st_perstateowner);
	*/
	return lst->st_file;
}

void
enfs2_test_lock(struct enfs_file_lock *fl, struct enfs2_file *fp)
{
	struct enfs_file_lock *cfl;
	struct enfs_file_lock **before;
	struct enfs2_file *file = NULL;

	file = enfs2_get_file(fl->fl_owner);
	if (!file) {
	//fl->fl_type = F_UNLCK;
	//	return;
		file = fp;
	}

	pthread_spin_lock(&file->i_lock);
	
	for_each_lock(file, before) {
		cfl = *before;
	/*
	for (cfl = file_inode(filp)->i_flock; cfl; cfl = cfl->fl_next) {
	*/
		if (!IS_POSIX(cfl))
			continue;
		if (posix_locks_conflict(fl, cfl))
			break;
	}       
	if (*before) {      
		__locks_copy_lock(fl, cfl);

		//if (cfl->fl_nspid)
		//	fl->fl_pid = pid_vnr(cfl->fl_nspid);
	} else  
		fl->fl_type = F_UNLCK;
	pthread_spin_unlock(&file->i_lock);
	return; 
} 

void
enfs2_test_lock_win(struct enfs_file_lock *fl, struct enfs2_file *fp)
{
	struct enfs_file_lock *cfl;
	struct enfs_file_lock **before;
	struct enfs2_file *file = NULL;

	file = enfs2_get_file(fl->fl_owner);
	if (!file) {
		//fl->fl_type = F_UNLCK;
		//return;
		file = fp;
	}

	pthread_spin_lock(&file->i_lock);
	
	for_each_lock(file, before) {
		cfl = *before;
		if (!IS_POSIX(cfl))
			continue;
		if (posix_locks_conflict_win(fl, cfl))
			break;
	}       
	if (*before) {      
		__locks_copy_lock(fl, cfl);
	} else { 
		fl->fl_type = F_UNLCK;
	}
	pthread_spin_unlock(&file->i_lock);
	return; 
} 
/* Find a lock that the owner of the given block_fl is blocking on. */
static struct enfs_file_lock *what_owner_is_waiting_for(struct enfs_file_lock *block_fl)
{
	struct enfs_file_lock *fl;

	hash_for_each_possible(blocked_hash, fl, fl_link, posix_owner_key(block_fl)) {
		if (posix_same_owner(fl, block_fl))
			return fl->fl_next;
	}               
	return NULL;    
}  

/* Must be called with the blocked_lock_lock held! */
static int posix_locks_deadlock(struct enfs_file_lock *caller_fl,
		struct enfs_file_lock *block_fl) 
{                       
	int i = 0;      

	while ((block_fl = what_owner_is_waiting_for(block_fl))) {
		if (i++ > MAX_DEADLK_ITERATIONS)
			return 0;
		if (posix_same_owner(caller_fl, block_fl))
			return 1;
	}               
	return 0;       
} 

#if 0
static int flock_locks_conflict(struct enfs_file_lock *caller_fl, struct enfs_file_lock *sys_fl)
{
	/* FLOCK locks referring to the same filp do not conflict with
 * 	 * each other.
 * 	 	 */
	if (!IS_FLOCK(sys_fl) || (caller_fl->fl_owner == sys_fl->fl_owner))
		return (0);
	if ((caller_fl->fl_type & LOCK_MAND) || (sys_fl->fl_type & LOCK_MAND))
		return 0;

	return (locks_conflict(caller_fl, sys_fl));
}

int enfs2_flock_file(struct enfs2_file *file, struct enfs_file_lock *request, struct enfs_file_lock *conflock)
{
	struct enfs_file_lock *new_fl = NULL;
	struct enfs_file_lock **before;
	int error = 0;
	int found = 0;

	if (!(request->fl_flags & ENFS_FL_ACCESS) && (request->fl_type != F_UNLCK)) {
		new_fl = locks_alloc_lock();
		if (!new_fl)
			return ENFSERR_NOENT;
	}

	pthread_spin_lock(&file->i_lock);
	if (request->fl_flags & ENFS_FL_ACCESS)
		goto find_conflict;

	for_each_lock(file, before) {
		struct enfs_file_lock *fl = *before;
		if (IS_POSIX(fl))
			break;
		if (IS_LEASE(fl))
			continue;
		if (request->fl_owner != fl->fl_owner)
			continue;
		if (request->fl_type == fl->fl_type)
			goto out;
		found = 1;
		locks_delete_lock(before);
		break;
	}

	if (request->fl_type == F_UNLCK) {
		if ((request->fl_flags & ENFS_FL_EXISTS) && !found)
			error = ENFSERR_NOENT;
		goto out;
	}

	/*
 * 	 * If a higher-priority process was blocked on the old file lock,
 * 	 	 * give it the opportunity to lock the file.
 * 	 	 	 */
	/*FIXME distribute filesystem do not have a priority client
 *	so we dont do wait again*/
	/*
	if (found) {
		unlock_flocks();
		cond_resched();
		lock_flocks();
	}
	*/
find_conflict:
	for_each_lock(file, before) {
		struct enfs_file_lock *fl = *before;
		if (IS_POSIX(fl))
			break;
		if (IS_LEASE(fl))
			continue;
		if (!flock_locks_conflict(request, fl))
			continue;
		error = ENFSERR_LOCKED;
		memcpy(conflock, fl, sizeof(struct enfs_file_lock));
		if (!(request->fl_flags & ENFS_FL_SLEEP))
			goto out;
		/*fix me no person to wake up this queue*/
		//locks_insert_block(fl, request);
		goto out;
	}
	if (request->fl_flags & ENFS_FL_ACCESS)
		goto out;
	locks_copy_lock(new_fl, request);
	locks_insert_lock(before, new_fl);
	new_fl = NULL;
	error = 0;

out:
	pthread_spin_unlock(&file->i_lock);
	if (new_fl)
		locks_free_lock(new_fl);
	return error;	
}
#endif

int enfs2_lock_file_reuse(struct enfs2_file *file, struct enfs_file_lock *request, struct enfs_file_lock *conflock)
{
	struct enfs_file_lock *fl;
	struct enfs_file_lock **before;
	int ret = 0;
	int err = 0;

	pthread_spin_lock(&file->i_lock);

	for_each_lock(file, before) {
		fl = *before;
		if (!IS_POSIX(fl))
			continue;
		if (request->fl_type == fl->fl_type
			&& fl->fl_start == request->fl_start
			&& fl->fl_end == request->fl_end
			&& fl->fl_caller_type == FL_CNAS) {
			/* fl_reuse == 1: lock after ganesha crash, locku is done
 			 * fl_reuse == 0: lock after nas ip change, no locku
 			 *  */
			if(fl->fl_reuse == 1) { 
				fl->fl_reuse == 0;
				put_enfs2_file(file);
				if(fl->fl_owner) enfs2_free_lockowner(fl->fl_owner);
				memset(&fl->fl_reuse_time, 0, sizeof(struct timespec));;

				pthread_mutex_lock(&reuse_file_lock);
				list_del(&fl->fl_reuse_lock_node);
				if(list_empty(&file->fi_reuse_lock_head)) {
					list_del(&file->fi_reuse_file_node);
					file->fi_in_reuse = 0;
				}
				pthread_mutex_unlock(&reuse_file_lock);

			}
			fl->fl_owner = request->fl_owner;
			ret = 0;
			pthread_spin_unlock(&file->i_lock);
			goto out;
		}
	}

	pthread_spin_unlock(&file->i_lock);
	ret = enfs2_lock_file(file, request, conflock);

out:
	return ret;	

}

void dump_reuse_lock_info(FILE *fp)
{
	struct enfs2_file *filep;
	int filenum = 1;
	int i = 1;
	struct enfs_file_lock *lock = NULL;
	fprintf(fp, "============= Dump reuse lock info =============\n");
	pthread_mutex_lock(&reuse_file_lock);
	list_for_each_entry(filep, &reuse_file_head, fi_reuse_file_node){
		fprintf(fp, " File %d\n", filenum++);
		i = 1;
		list_for_each_entry(lock, &filep->fi_reuse_lock_head, fl_reuse_lock_node){
			fprintf(fp, " --%d: File owner is %p", i++, lock->fl_owner);
			fprintf(fp, "  Start %lu, End %lu Type is %d", lock->fl_start, lock->fl_end, lock->fl_type);
			fprintf(fp, "  caller %d reuse %lu time %ld.%ld\n", lock->fl_caller_type, lock->fl_reuse, 
					lock->fl_reuse_time.tv_sec, lock->fl_reuse_time.tv_nsec);
		}
	}
	pthread_mutex_unlock(&reuse_file_lock);
	fprintf(fp, "============= Dump reuse lock End =============\n");

}

void clean_reuse_lock(void)
{
	struct enfs2_file *filep = NULL;
	struct enfs2_file *filep_next = NULL;
	struct enfs_file_lock *fl = NULL;
	struct enfs_file_lock *fl_next = NULL;
	struct timespec curr = {};
	struct timespec standard_lease = {30,0};// must less then client lease time. see enfsd2_state_init()
	int do_clean = 0;
	struct enfs_file_lock **before;

	clock_gettime(CLOCK_REALTIME,&curr);
	enfs_timespecsub(&curr, &standard_lease, &curr);
	pthread_mutex_lock(&reuse_file_lock);
	list_for_each_entry_safe(filep, filep_next, &reuse_file_head, fi_reuse_file_node){
		list_for_each_entry_safe(fl, fl_next, &filep->fi_reuse_lock_head, fl_reuse_lock_node){
				if((enfs_timespeccmp(&curr,&fl->fl_reuse_time, <))) continue;
				do_clean = 1;
				if(fl->fl_owner) enfs2_free_lockowner(fl->fl_owner);
				/* del lock in reuse lock list */
				list_del(&fl->fl_reuse_lock_node);
				if(list_empty(&filep->fi_reuse_lock_head)) {
					/* del file in reuse file list */
					list_del(&filep->fi_reuse_file_node);
					filep->fi_in_reuse = 0;
				}
		}
		/* enfs2_laundromat() not clean lock in file, so we must clean lock in filep->i_flock(use for lock/lockt/locku),
		 * and only clean invalid reuse lock.*/
		if(do_clean == 1) {
			/* normal need filep->i_lock, 
			 * when save or reuse lock, file->i_lock ---> reuse_file_lock
			 * here we will reuse_file_lock ---> file->i_lock, may deadlock, so dont lock filep->i_lock,
			 * as clean thread use enfsd2_lock_state(), here will not make a mistake for using filep->i_flock.
			 * if enfsd2_lock_state() is invalid, must lock filep->i_lock here,
			 * and modify to reuse_file_lock ---> file->i_lock when save or reuse lock*/
			before = &filep->i_flock;
			while ((fl = *before)) {
				if(fl->fl_reuse == 0) goto next;
				if((enfs_timespeccmp(&curr,&fl->fl_reuse_time, <))) goto next;
				fl->fl_reuse == 0;
				memset(&fl->fl_reuse_time, 0, sizeof(struct timespec));;
				/* del reuse lock in file->i_flock */
				*before = fl->fl_next;
				fl->fl_next = NULL;
				locks_wake_up_blocks(fl);
				locks_free_lock(fl);
				put_enfs2_file(filep);
				continue;
next:
				before = &fl->fl_next;
			}
			do_clean = 0;
		}
	}
	pthread_mutex_unlock(&reuse_file_lock);
		
}

int enfs2_lock_file(struct enfs2_file *file, struct enfs_file_lock *request, struct enfs_file_lock *conflock)
{
	struct enfs_file_lock *fl;
	struct enfs_file_lock *new_fl = NULL;
	struct enfs_file_lock *new_fl2 = NULL;
	struct enfs_file_lock *left = NULL;
	struct enfs_file_lock *right = NULL;
	struct enfs_file_lock **before;
	int error;
	int added = 0;
	//struct enfs2_file *file = NULL;

	/*
	 * We may need two file_lock structures for this operation,
	 * so we get them in advance to avoid races.
	 *
	 * In some cases we can be sure, that no new locks will be needed
	 */
	if (!(request->fl_flags & ENFS_FL_ACCESS) &&
	    (request->fl_type != F_UNLCK ||
	     request->fl_start != 0 || request->fl_end != OFFSET_MAX)) {
		new_fl = locks_alloc_lock();
		new_fl2 = locks_alloc_lock();
	}

	pthread_spin_lock(&file->i_lock);
	/*
	 * New lock request. Walk all POSIX locks and look for conflicts. If
	 * there are any, either return error or put the request on the
	 * blocker's list of waiters and the global blocked_hash.
	 */
	if (request->fl_type != F_UNLCK) {
		for_each_lock(file, before) {
			fl = *before;
			if (!IS_POSIX(fl))
				continue;
			if (!posix_locks_conflict(request, fl))
				continue;
			if (conflock)
				__locks_copy_lock(conflock, fl);
			error = ENFSERR_LOCKED;
			if (!(request->fl_flags & ENFS_FL_SLEEP))
				goto out;
			/*
			 * Deadlock detection and insertion into the blocked
			 * locks list must be done while holding the same lock!
			 */
			error = ENFSERR_DEADLOCK;
			pthread_mutex_lock(&blocked_lock_lock);
			if (likely(!posix_locks_deadlock(request, fl))) {
				error = ENFSERR_FILE_LOCK_DEFERRED;
				__locks_insert_block(fl, request);
			}
			pthread_mutex_unlock(&blocked_lock_lock);
			goto out;
  		}
  	}

	/* If we're just looking for a conflict, we're done. */
	error = 0;
	if (request->fl_flags & ENFS_FL_ACCESS)
		goto out;

	/*
	 * Find the first old lock with the same owner as the new lock.
	 */
	
	before = &file->i_flock;

	/* First skip locks owned by other processes.  */
	while ((fl = *before) && (!IS_POSIX(fl) ||
				  !posix_same_owner(request, fl))) {
		before = &fl->fl_next;
	}

	/* Process locks with this owner. */
	while ((fl = *before) && posix_same_owner(request, fl)) {
		/* Detect adjacent or overlapping regions (if same lock type)
		 */
		if (request->fl_type == fl->fl_type) {
			/* In all comparisons of start vs end, use
			 * "start - 1" rather than "end + 1". If end
			 * is OFFSET_MAX, end + 1 will become negative.
			 */
			if (fl->fl_end < request->fl_start - 1)
				goto next_lock;
			/* If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start - 1 > request->fl_end)
				break;

			/* If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->fl_start > request->fl_start)
				fl->fl_start = request->fl_start;
			else
				request->fl_start = fl->fl_start;
			if (fl->fl_end < request->fl_end)
				fl->fl_end = request->fl_end;
			else
				request->fl_end = fl->fl_end;
			if (added) {
				locks_delete_lock(file, before, request);
				if(fl->fl_reuse == 1) goto next_lock;
				continue;
			}
			request = fl;
			added = 1;
		}
		else {
			/* Processing for different lock types is a bit
			 * more complex.
			 */
			if (fl->fl_end < request->fl_start)
				goto next_lock;
			if (fl->fl_start > request->fl_end)
				break;
			if (request->fl_type == F_UNLCK)
				added = 1;
			if (fl->fl_start < request->fl_start)
				left = fl;
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->fl_end > request->fl_end) {
				right = fl;
				break;
			}
			if (fl->fl_start >= request->fl_start) {
				/* The new lock completely replaces an old
				 * one (This may happen several times).
				 */
				if (added) {
					locks_delete_lock(file, before, request);
					if(fl->fl_reuse == 1) goto next_lock;
					continue;
				}
				/* Replace the old lock with the new one.
				 * Wake up anybody waiting for the old one,
				 * as the change in lock type might satisfy
				 * their needs.
				 */
				locks_wake_up_blocks(fl);
				fl->fl_start = request->fl_start;
				fl->fl_end = request->fl_end;
				fl->fl_type = request->fl_type;
				/* enfs do not have private */
				//locks_release_private(fl);
				//locks_copy_private(fl, request);
				request = fl;
				added = 1;
			}
		}
		/* Go on to next lock.
		 */
	next_lock:
		before = &fl->fl_next;
	}

	/*
	 * The above code only modifies existing locks in case of merging or
	 * replacing. If new lock(s) need to be inserted all modifications are
	 * done below this, so it's safe yet to bail out.
	 */
	error = ENFSERR_NOLCK; /* "no luck" */
	if (right && left == right && !new_fl2)
		goto out;

	error = 0;
	if (!added) {
		if (request->fl_type == F_UNLCK) {
			if (request->fl_flags & ENFS_FL_EXISTS)
				error = ENFSERR_NOENT;
			goto out;
		}

		if (!new_fl) {
			error = ENFSERR_NOLCK;
			goto out;
		}
		locks_copy_lock(new_fl, request);
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
			locks_copy_lock(left, right);
			locks_insert_lock(before, left);
		}
		right->fl_start = request->fl_end + 1;
		locks_wake_up_blocks(right);
	}
	if (left) {
		left->fl_end = request->fl_start - 1;
		locks_wake_up_blocks(left);
	}
 out:
	pthread_spin_unlock(&file->i_lock);
	/*
	 * Free any unused locks.
	 */
	if (new_fl)
		locks_free_lock(new_fl);
	if (new_fl2)
		locks_free_lock(new_fl2);
	return error;
}

int enfs2_lock_file_win(struct enfs2_file *file, struct enfs_file_lock *request, struct enfs_file_lock *conflock)
{
	struct enfs_file_lock *fl;
	struct enfs_file_lock *new_fl = NULL;
	struct enfs_file_lock **before;
	struct enfs_file_lock **tmp_unlck;
	int error = 0;

	pthread_spin_lock(&file->i_lock);

	/*
	 * New lock request. Walk all POSIX locks and look for conflicts. If
	 * there are any, either return error or put the request on the
	 * blocker's list of waiters and the global blocked_hash.
	 */
	if (request->fl_type != F_UNLCK) {
		for_each_lock(file, before) {
			fl = *before;
			if (!IS_POSIX(fl))
				continue;
			if (!posix_locks_conflict_win(request, fl))
				continue;
			if (conflock)
				__locks_copy_lock(conflock, fl);
			error = ENFSERR_LOCKED;
			/* never sleep */
			goto out;
  		}
  	}

	/* If we're just looking for a conflict, we're done. */
	if (request->fl_flags & ENFS_FL_ACCESS)
		goto out;

	/*
	 * windows semantic don`t deal with lock merge/upgrade/downgrade,
	 * so insert the lock directly
	 */
	if (request->fl_type != F_UNLCK) {
		new_fl = locks_alloc_lock();
		if (!new_fl) {
			error = ENFSERR_NOLCK;
			goto out;
		}
		locks_copy_lock(new_fl, request);
		locks_insert_lock(&file->i_flock, new_fl);
		goto out;
	}

	/*
	 * start to deal with unlock,
	 * windows semantic have to find out an exactly match lock range
	 */
	before = NULL;
	for_each_lock(file, tmp_unlck) {
		fl = *tmp_unlck;
		if (!IS_POSIX(fl))
			continue;
		if (!posix_same_owner(request, fl))
			continue;
		if (request->fl_start == fl->fl_start &&
			request->fl_end == fl->fl_end) {
			// find fisrt lock to unlock in Windows semantic
			before = tmp_unlck;
			if (fl->fl_type == F_WRLCK)    
				break;
		}
	}

	if (before == NULL) {
		error = ENFSERR_LOCK_RANGE;
		goto out;
	}

	/* real unlock */
	locks_delete_lock(file, before, request);

	/*
	 * NOTE:
	 * close unlock will be done in locks_remove_posix
	 * through enfs2_lock_file
	 */
out:
	pthread_spin_unlock(&file->i_lock);
	return error;
}

int from_same_client(struct enfs_stateid *stateid, struct enfs_file_lock *fl)
{
	struct enfs2_clientid cl_id_lk, cl_id_st;
	cl_id_lk = fl->fl_owner->lo_owner.so_client->cl_clientid;
	/*FIXED ME: Because client not change to network word order, so enfsd no need change*/
//	decode_clntid_from_stateid(&cl_id_st, stateid->stid_other);
	memcpy(&cl_id_st, stateid->stid_other, 16);

	klog(DEBUG, "Lock client shclid and mdsverf are \"%llu %llu\" "
			" layout client shclid and mdsverf are \"%llu %llu\"",
			cl_id_lk.cl_shclid, cl_id_lk.cl_mdsverf, cl_id_st.cl_shclid, cl_id_st.cl_mdsverf);

	return (cl_id_lk.cl_shclid == cl_id_st.cl_shclid &&
			cl_id_lk.cl_mdsverf == cl_id_st.cl_mdsverf);
}

int check_layout_lock_conflict(u_int64_t off, u_int64_t len, int mode, struct enfsd_wcc *wcc, struct enfs_stateid *stateid)
{
	struct enfs2_file *fp = NULL;
	struct enfs_file_lock **before;
	struct enfs_file_lock *fl = NULL;
	int err = 0;
	fp = find_file(wcc);
	if (!fp) {      
		klog(DEBUG, "Did not find file %p", wcc);
		err = 0;
		return err;
	}

	pthread_spin_lock(&fp->i_lock);
	for_each_lock(fp, before) {
		fl = *before;
		if (!IS_POSIX(fl))
			continue;
		if (from_same_client(stateid, fl))
			continue;
		if (mode == BW_LAYOUT_IOMODE_RDONLY && fl->fl_type == F_RDLCK)
			continue;
		if(off + len >= fl->fl_start && fl->fl_end >= off) {
			err = ENFSERR_LOCKED;
			pthread_spin_unlock(&fp->i_lock);
			put_enfs2_file(fp);
			return err;
		}
	}

	pthread_spin_unlock(&fp->i_lock);
	put_enfs2_file(fp);
	return err;
}

static void locks_delete_lock_without_wakeup(struct enfs_file_lock **thisfl_p)
{
	struct enfs_file_lock *fl = *thisfl_p;

	klog(DEBUG, "delete lock start %d end %d type %d flags %d",
		fl->fl_start, fl->fl_end, fl->fl_type,	
		fl->fl_flags);
	//locks_delete_global_locks(fl);

	*thisfl_p = fl->fl_next;
	fl->fl_next = NULL;
/*
	if (fl->fl_nspid) {
		put_pid(fl->fl_nspid);
		fl->fl_nspid = NULL;
	}
*/
	locks_free_lock(fl);
}
int enfs2_unlock_file_by_type(struct enfs2_file *file, struct enfs_file_lock *request)
{
	int status = 0;
	struct enfs_file_lock **before = NULL;
	struct enfs_file_lock *fl = NULL;

	pthread_spin_lock(&file->i_lock);
	before = &file->i_flock;	

	while ((fl = *before) && (!IS_POSIX(fl) ||
				  !posix_same_owner(request, fl))) {
		before = &fl->fl_next;
	}
	
	while((fl = *before) && posix_same_owner(request, fl)){
		if(request->fl_type == fl->fl_type){
			locks_delete_lock_without_wakeup(before);
			continue;
		} else if(request->fl_type == WR_STATE){
			/*write delegation need remove all type locks 
 			* inclue read lock
 			* */
			locks_delete_lock_without_wakeup(before);
			continue;
		} else
			before = &fl->fl_next;
	}
	pthread_spin_unlock(&file->i_lock);
	return status;
}

