/*
 *  Copyright (c) 2004  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_const.h 
 */
 
/*
 *  Tail queue list usually be used to describe 
 *  the "busy" or "inuse" list structure. 
 *  To help some object's need lasting some time to do some things
 *  with this list unit, we provide the following MACROs.
 */

#ifndef __SKYFS_TAILQ_H__
#define __SKYFS_TAILQ_H__

#ifndef         NIL
#   define      NIL         (-1)
#endif

/*
 * Tail queue definitions.
 */

#define SKYFS_TAILQ_HEAD(name, type) \
    struct  name {      \
        int         tqh_first; /* index of the first element. */   \
        int         tqh_last;  /* index of last next element. */   \
        int         nr_tqElm;  /* number of elements in the tail queue. */  \
        type        *tq_basep; /* base pointer of the tail queue. */    \
    }

#define SKYFS_TAILQ_HEAD_INITIALIZER(head)					\
	    { NIL, NIL, 0, NULL}

#define SKYFS_TAILQ_ENTRY(type) \
    struct type {    \
        int         tqe_next;   /* index of next element. */  \
        int         tqe_prev;   /* index of previous next element. */ \
    } 

/*
 * Tail queue functions.
 */
#define SKYFS_TAILQ_EMPTY(head) ((head)->tqh_first == NIL)

#define SKYFS_TAILQ_FOREACH(var, head, field)            \
	for (var = SKYFS_TAILQ_FIRST(head); \
         var != NIL; \
         var = SKYFS_TAILQ_NEXT(head, var, field))

#define SKYFS_TAILQ_FOREACH_REVERSE(var, head, field)		\
    for ((var) = SKYFS_TAILQ_LAST((head));			\
         (var) != NIL;							\
         (var) = SKYFS_TAILQ_PREV(head, (var), field))

#define SKYFS_ELM(head, index) \
    ((head)->tq_basep + (index))

#define SKYFS_TAILQ_ELM(head, index) \
    ((head)->tq_basep + (index))

#define SKYFS_BASEP(head) \
    ((head)->tq_basep)

#define SKYFS_TAILQ_SET_FIRST(head, index)   \
    do {    \
        (head)->tqh_first   =   (index);    \
    } while (0);    

#define SKYFS_TAILQ_SET_LAST(head, index)   \
    do {    \
        (head)->tqh_last   =   (index);    \
    } while (0);    

#define SKYFS_TAILQ_SET_NEXT(head, field, prev, next)   \
    do {    \
        SKYFS_ELM((head), (prev))->field.tqe_next   =   (next);    \
    } while (0);    

#define SKYFS_TAILQ_SET_PREV(head, field, prev, next)   \
    do {    \
        SKYFS_ELM((head), (next))->field.tqe_prev   =   (prev);    \
    } while (0);    

#define	SKYFS_TAILQ_FIRST(head)          ((head)->tqh_first)
#define	SKYFS_TAILQ_LAST(head)           ((head)->tqh_last)

#define	SKYFS_TAILQ_NEXT(head, elm, field)     \
    (SKYFS_ELM((head), (elm))->field.tqe_next)

#define SKYFS_TAILQ_PREV(head, elm, field)     \
    (SKYFS_ELM((head),(elm))->field.tqe_prev)

#define SKYFS_TAILQ_NEXT_ELM(head, elm, field)	\
	(SKYFS_ELM((head), SKYFS_TAILQ_NEXT(head, elm, field)))	

#define SKYFS_TAILQ_PREV_ELM(head, elm, field)	\
	(SKYFS_ELM((head), SKYFS_TAILQ_PREV(head, elm, field)))	


#define	SKYFS_TAILQ_INIT(head, base, nr_elm)   \
    do {                                    \
	    (head)->tqh_first = NIL;			\
	    (head)->tqh_last = NIL;				\
        (head)->tq_basep = (base);          \
        (head)->nr_tqElm = (nr_elm);        \
    } while (0);

#define SKYFS_TAILQ_ENTRY_INIT(head, elm, field) \
    do {                                 \
        SKYFS_ELM(head, elm)->field.tqe_next = NIL; \
        SKYFS_ELM(head, elm)->field.tqe_prev = NIL; \
    } while (0)

#define SKYFS_TAILQ_ELM_INIT(elm, field)   \
    do {                                  \
        elm->field.tqe_next = NIL;        \
        elm->field.tqe_prev = NIL;        \
    } while (0)                           \

/*
 * Before operataion:
 * +--------+
 * |  head  |-----> [first]     NIL <-  head -> ...
 * +--------+ 
 *      |
 *      +---------> [last]      ...  <- tail -> NIL     
 *
 * After operation:
 * +--------+
 * |  head  |-----> [new first]  NIL <- head ----+
 * +--------+                           / \      |
 *      |                                |      \ /
 *      |                                +----  old head -> ...
 *      |
 *      +---------> [last]      ...  <- tail -> NIL     
 */

#define SKYFS_TAILQ_INSERT_HEAD(head, elm, field)    \
    do {			\
	    if ((head)->tqh_first != NIL)	{ \
		    SKYFS_ELM(head, (elm))->field.tqe_next =	(head)->tqh_first;	\
		    SKYFS_ELM(head, (head)->tqh_first)->field.tqe_prev =	(elm);	\
        }   \
	    else	{\
		    (head)->tqh_last = (elm);	\
            SKYFS_ELM((head), (elm))->field.tqe_next = NIL;  \
        }   \
        \
	    (head)->tqh_first = (elm);					\
	    SKYFS_ELM(head, (elm))->field.tqe_prev = NIL;	\
    } while (0);

/*
 * Before operataion:
 * +--------+
 * |  head  |-----> [first]     NIL <-  head -> ...
 * +--------+ 
 *      |
 *      +---------> [last]      ...  <- tail -> NIL     
 *
 * After operation:
 * +--------+
 * |  head  |-----> [last]     ... <- old tail --+
 * +--------+                           / \      |
 *      |                                |      \ /
 *      |                                +----  new tail -> NIL
 *      |
 *      +---------> [first]      NIL  <- head -> ...     
 */


#define SKYFS_TAILQ_INSERT_TAIL(head, elm, field) \
    do {	\
        if ((head)->tqh_last != NIL)	{ \
		    SKYFS_ELM(head, (elm))->field.tqe_prev =	(head)->tqh_last;	\
		    SKYFS_ELM(head, (head)->tqh_last)->field.tqe_next =	(elm);	\
        }   \
	    else	{\
		    (head)->tqh_first = (elm);	\
            SKYFS_ELM((head), (elm))->field.tqe_prev = NIL;  \
        }   \
        \
	    (head)->tqh_last = (elm);					\
	    SKYFS_ELM(head, (elm))->field.tqe_next = NIL;	\
    } while (0);

/*
 * Before operataion:
 *
 *  ... <-- the prev (listelm)--+
 *                / \           |
 *                 |           \ /
 *                 +----  the next --> ...
 *
 * After operation:
 *
 *  ... <-- the prev (listelm) --+
 *           / \                 |
 *            |                 \ /
 *            +----  the insert one (elm) ----+
 *                          / \               |
 *                           |               \ /
 *                           +--------  the next --> ...
 */
#define SKYFS_TAILQ_INSERT_AFTER(head, listelm, elm, field)   \
    do {		\
        if (SKYFS_TAILQ_LAST(head) != (listelm))  {   \
            \
            if (SKYFS_TAILQ_FIRST(head) != (listelm)) {  \
                \
                SKYFS_ELM((head), \
                     SKYFS_TAILQ_NEXT((head), \
                                     (listelm), field))->field.tqe_prev \
                =  (elm);  \
            }   \
            else    \
                SKYFS_TAILQ_SET_LAST(head, (elm));   \
            \
            SKYFS_ELM((head), (listelm))->field.tqe_next = (elm);    \
            SKYFS_ELM((head), (elm))->field.tqe_prev = (listelm);    \
            SKYFS_ELM((head), (elm))->field.tqe_next = \
                            SKYFS_TAILQ_NEXT((head), (listelm), field);    \
        }   \
        else    \
            SKYFS_TAILQ_INSERT_TAIL(head, elm, field);   \
    } while (0);

/*
 * Before operataion:
 *
 *  ... <-- the prev -----------+
 *                / \           |
 *                 |           \ /
 *                 +----  the next (listelm) --> ...
 *
 * After operation:
 *
 *  ... <-- the prev ------------+
 *           / \                 |
 *            |                 \ /
 *            +----  the insert one (elm) ----+
 *                          / \               |
 *                           |               \ /
 *                           +--------  the next (listelm) --> ...
 */

#define SKYFS_TAILQ_INSERT_BEFORE(head, listelm, elm, field)    \
    do {			\
        if (SKYFS_TAILQ_FIRST(head) != (listelm))  {   \
            \
            if (SKYFS_TAILQ_LAST(head) != (listelm)) {  \
                \
                SKYFS_ELM((head), \
                     SKYFS_TAILQ_PREV((head), \
                                     (listelm), field))->field.tqe_next \
                =  (elm);  \
            }   \
            else    \
                SKYFS_TAILQ_SET_FIRST(head, (elm));   \
            \
            SKYFS_ELM((head), (listelm))->field.tqe_prev = (elm);    \
            SKYFS_ELM((head), (elm))->field.tqe_next = (listelm);    \
            SKYFS_ELM((head), (elm))->field.tqe_prev = \
                            SKYFS_TAILQ_PREV((head), (listelm), field);    \
        }   \
        else    \
            SKYFS_TAILQ_INSERT_HEAD(head, elm, field);   \
    } while (0);

/*
 * Before operataion:
 *
 *  ... <-- the prev  -----------+
 *           / \                 |
 *            |                 \ /
 *            +----  the one to be delete(elm) ----+
 *                          / \                    |
 *                           |                    \ /
 *                           +---------------   the next --> ...
 *
 * After operation:
 *
 *  ... <-- the prev -----------+
 *                / \           |
 *                 |           \ /
 *                 +----  the next --> ...
 */

#define SKYFS_TAILQ_REMOVE(head, elm, field)  \
    do {		\
        if ((elm) != SKYFS_TAILQ_FIRST((head))   \
            && (elm) != SKYFS_TAILQ_LAST((head)))   {    \
            SKYFS_ELM((head), SKYFS_TAILQ_PREV((head), (elm), field))->field.tqe_next  =  \
                 SKYFS_ELM((head), (elm))->field.tqe_next;   \
            SKYFS_ELM((head), SKYFS_TAILQ_NEXT((head), (elm), field))->field.tqe_prev  =  \
                 SKYFS_ELM((head), (elm))->field.tqe_prev;   \
        }   \
        else {  \
            if ((elm) != SKYFS_TAILQ_FIRST((head)))  {   \
                SKYFS_ELM((head), SKYFS_TAILQ_PREV((head), (elm), field))->field.tqe_next = NIL;   \
                SKYFS_TAILQ_SET_LAST((head), SKYFS_TAILQ_PREV((head), (elm), field));  \
            }  \
            else    {   \
                if (SKYFS_TAILQ_FIRST(head) != SKYFS_TAILQ_LAST(head)) {  \
                        SKYFS_ELM((head), SKYFS_TAILQ_NEXT((head), (elm), field))->field.tqe_prev = NIL;   \
                        SKYFS_TAILQ_SET_FIRST((head), SKYFS_TAILQ_NEXT((head), (elm), field));  \
                }   \
                else    {   \
                    (head)->tqh_first = (head)->tqh_last = NIL; \
                }   \
            }   \
        } \
        SKYFS_TAILQ_ENTRY_INIT(head, elm, field); \
    } while (0);

#define SKYFS_TAILQ_REMOVE_HEAD(head, field)  \
	do {	\
		int elm;	\
		\
		elm = SKYFS_TAILQ_FIRST(head);	\
		SKYFS_TAILQ_REMOVE(head, elm, field);	\
        SKYFS_TAILQ_ENTRY_INIT(head, elm, field);   \
		\
	} while (0);

#define SKYFS_TAILQ_LOOKUP(head, elm, field, if_found)	\
	do {	\
        if(SKYFS_ELM((head),(elm))->field.tqe_next != NIL   \
           || SKYFS_ELM((head),(elm))->field.tqe_prev != NIL  \
           || (SKYFS_TAILQ_FIRST(head) == elm))   \
            if_found = 1;  \
        else              \
            if_found = 0; \
    } while (0);
#endif
