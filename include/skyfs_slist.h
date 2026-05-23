/*
 *  Copyright (c) 2008  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_const.h 
 */

/*
 *  Single list usually be used to describe 
 *  the "free" list structure. When request a "idle" list unit
 *  from it or return a list unit into the free list, we provide
 *  the following MACROs.
 */

#ifndef __SKYFS_SLIST_H__
#define __SKYFS_SLIST_H__

#ifndef     NIL
#   define  NIL (-1)
#endif


/*
 * Singly-linked List definitions.
 */
#define SKYFS_SLIST_HEAD(name, type)						\
    struct name {								\
	int      slh_first;	/* first element */			\
    type     *sl_basep;  /* base pointer of single-list. */  \
    int      nr_elm;     /* number of the elements in single-list. */    \
    }

#define SKYFS_SLIST_HEAD_INITIALIZER(head)					\
	{ NIL }
 
#define SKYFS_SLIST_ENTRY(type)						\
    struct type {								\
	    int sle_next;	/* next element */			\
    }
 
/*
 * Singly-linked List functions.
 */
#define	SKYFS_SLIST_EMPTY(head)	((head)->slh_first == NIL)

#define	SKYFS_SLIST_FIRST(head)	((head)->slh_first)

#define SKYFS_SLIST_NR_ELM(head) ((head)->nr_elm)

#define SKYFS_SLIST_ELM_BAD(head, elm)    \
        ((elm) >= 0 && (elm) < SKYFS_SLIST_NR_ELM(head))

#define SKYFS_SLIST_ELM(head, index)  \
    ((head)->sl_basep + index)

#define SKYFS_SLIST_BASEP(head)   \
    ((head)->sl_basep)

#define SKYFS_SLIST_FOREACH(var, head, field)					\
	for((var) = (head)->slh_first; \
        (var) != NIL; \
        (var) = SKYFS_SLIST_ELM(head, (var))->field.sle_next)

#ifdef __KERNEL__
# define SKYFS_SLIST_INIT(head, type, field, nr_e, basep)   \
    do {		\
        \
        int i;  \
        \
        (head)->slh_first = 0;					\
        if ((basep) != NULL) { \
            ((head)->sl_basep) = basep; \
        }           \
        else { \
        (head)->sl_basep =(type *)kmalloc(sizeof(type)*(nr_e),GFP_KERNEL); \
        }  \
        ((head)->nr_elm) = (nr_e);  \
        if ((head)->sl_basep == NULL)  \
		*(int *) 0 = 0;		\
        for (i = 0; i < (nr_e) - 1; i ++)   {   \
            (SKYFS_SLIST_ELM(head, i))->field.sle_next = i + 1; \
            (SKYFS_SLIST_ELM(head, i))->index = i; \
        }   \
        (SKYFS_SLIST_ELM(head, (nr_e) - 1))->field.sle_next =  NIL; \
        (SKYFS_SLIST_ELM(head, (nr_e) - 1))->index = (nr_e) - 1; \
        \
    } while (0);
#else
# define SKYFS_SLIST_INIT(head, type, field, nr_e, basep)   \
    do {		\
        \
        int i;  \
        \
        (head)->slh_first = 0;					\
        if ((basep) != NULL) { \
            ((head)->sl_basep) = basep; \
        }           \
        else { \
            (head)->sl_basep = (type *)malloc(sizeof(type) * (nr_e));    \
        } \
        ((head)->nr_elm) = (nr_e);  \
        if ((head)->sl_basep == NULL)   \
		*(int *) 0 = 0;       \
        for (i = 0; i < (nr_e) - 1; i ++)   {   \
            (SKYFS_SLIST_ELM(head, i))->field.sle_next = i + 1; \
            (SKYFS_SLIST_ELM(head, i))->index = i; \
        }   \
        (SKYFS_SLIST_ELM(head, (nr_e) - 1))->field.sle_next =  NIL; \
        (SKYFS_SLIST_ELM(head, (nr_e) - 1))->index = (nr_e) - 1; \
        \
    } while (0);
#endif

#define SKYFS_SLIST_INSERT_AFTER(head, slistelm, elm, field) \
    do {			\
        if (!SKYFS_SLIST_ELM_BAD(head, slistelm))    \
		*(int *) 0 = 0;			\
        if (!SKYFS_SLIST_ELM_BAD(head, elm))    \
		*(int *) 0 = 0;			\
        SKYFS_SLIST_ELM(head, elm)->field.sle_next =          \
            SKYFS_SLIST_ELM(head, slistelm)->field.sle_next;  \
        SKYFS_SLIST_ELM(head, slistelm)->field.sle_next =  (slistelm); \
    } while (0);

#define SKYFS_SLIST_INSERT_HEAD(head, elm, field) \
    do {			\
        if (!SKYFS_SLIST_ELM_BAD(head, elm))    \
		*(int *) 0 = 0;			\
        SKYFS_SLIST_ELM(head, elm)->field.sle_next =          \
                (head)->slh_first;  \
	    (head)->slh_first = (elm);					\
    } while (0);

#define SKYFS_SLIST_NEXT(head, elm, field)	\
        (SKYFS_SLIST_ELM(head, (elm))->field.sle_next)

#define SKYFS_SLIST_REMOVE_HEAD(head, field) \
    do {				\
	    (head)->slh_first = \
            SKYFS_SLIST_ELM(head, (head)->slh_first)->field.sle_next;		\
    } while (0)

#define SKYFS_SLIST_REMOVE(head, elm, type, field) \
    do {			\
	    if ((head)->slh_first == (elm)) {				\
		    SKYFS_SLIST_REMOVE_HEAD((head), field);			\
	    }								\
	    else {								\
		    type *curelm = SKYFS_SLIST_ELM(head, (head)->slh_first);\
		    while( curelm->field.sle_next != (elm) )		\
			    curelm = SKYFS_SLIST_ELM(head, curelm->field.sle_next);	\
		    curelm->field.sle_next =				\
		        SKYFS_SLIST_ELM(head, curelm->field.sle_next)->field.sle_next;\
	    }	\
    } while (0)

#endif
