/*
 *  Copyright (c) 2004  NCIC
 *  National Research Center of Intelligent Computing Systems
 *  All rights reserved.
 *
 *  Written by Xing Jing
 */
 
 /*
 * $Id: skyfs_staticlist.h 
 */
 
#ifndef __SKYFS_LIST_H__
#define __SKYFS_LIST_H__

/*
 * List definitions.
 */
#define SKYFS_LIST_HEAD(name, type)				\
    struct name {								\
	    int  lh_first;	/* first element */			\
        type *lh_basep; /* base pointer of the list. */ \
    }

#define SKYFS_LIST_HEAD_INITIALIZER(head)					\
	{ NIL }

#define SKYFS_LIST_ENTRY(type)						\
    struct type {								\
	    int     le_next;	/* next element */			\
	    int     le_prev;	/* prev element */	\
    }

/*
 * List functions.
 */

#define SKYFS_LIST_ELM(head, index)   ((head)->lh_basep + (index))

#define SKYFS_LIST_BASEP(head)       ((head)->lh_basep)

#define SKYFS_LIST_ELM_NEXT(head, field, index)  \
        ((head)->lh_basep + ((head)->lh_basep + (index))->field.le_next)

#define SKYFS_LIST_ELM_PREV(head, field, index)  \
        ((head)->lh_basep + ((head)->lh_basep + (index))->field.le_prev)

#define	SKYFS_LIST_EMPTY(head) ((head)->lh_first == NIL)

#define SKYFS_LIST_FIRST(head)	((head)->lh_first)

#define SKYFS_LIST_FOREACH(var, head, field)					\
	for((var) = (head)->lh_first; (var); (var) = (var)->field.le_next)

#define	SKYFS_LIST_INIT(head, base) \
    do {						\
	    (head)->lh_first = NIL;		\
        (head)->lh_basep = (base);  \
    } while (0)

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

#define SKYFS_LIST_INSERT_AFTER(head, listelm, elm, field)   \
    do {		\
        SKYFS_LIST_ELM(head, elm)->field.le_next  =   \
            SKYFS_LIST_ELM(head, listelm)->field.le_next; \
        \
        if (SKYFS_LIST_ELM(head, elm)->field.le_next != NIL)  \
            SKYFS_LIST_ELM_NEXT(head, field, listelm)->field.le_prev = (elm); \
        \
        SKYFS_LIST_ELM(head, listelm)->field.le_next = (elm)  \
        SKYFS_LIST_ELM(head, elm)->field.le_prev = (listelm);    \
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

#define SKYFS_LIST_INSERT_BEFORE(head, listelm, elm, field)    \
    do {			\
        SKYFS_LIST_ELM(head, elm)->field.le_prev = \
            SKYFS_LIST_ELM(head, listelm)->field.le_prev; \
        SKYFS_LIST_ELM(head, elm)->field.le_next = (listelm); \
        \
        if (SKYFS_LIST_FIRST(head) != (listelm)) \
            SKYFS_LIST_ELM_PREV(head, field, listelm)->field.le_next = (elm); \
        else\
            SKYFS_LIST_FIRST(head) = (elm);   \
        \
        SKYFS_LIST_ELM(head, listelm)->field.le_prev = (elm); \
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
 * |  head  |-----> [new first]  NIL <- head ----+
 * +--------+                           / \      |
 *      |                                |      \ /
 *      |                                +----  old head -> ...
 *      |
 *      +---------> [last]      ...  <- tail -> NIL     
 */

#define SKYFS_LIST_INSERT_HEAD(head, elm, field)    \
    do {			\
        SKYFS_LIST_ELM(head, elm)->field.le_next = SKYFS_LIST_FIRST(head);  \
        if ( !SKYFS_LIST_EMPTY(head) )   \
            SKYFS_LIST_ELM(head, SKYFS_LIST_FIRST(head))->field.le_prev = (elm);    \
        SKYFS_LIST_FIRST(head) = (elm);   \
        SKYFS_LIST_ELM(head, elm)->field.le_prev = NIL;  \
        \
    } while (0);

#define SKYFS_LIST_NEXT(head, elm, field)	    \
        (SKYFS_LIST_ELM(head, (elm))->field.le_next)

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
#define SKYFS_LIST_REMOVE(head, elm, field)     \
    do {					\
        if ( SKYFS_LIST_ELM(head, elm)->field.le_next != NIL ) \
            SKYFS_LIST_ELM_NEXT(head, field, elm)->field.le_prev = \
                        SKYFS_LIST_ELM(head, elm)->field.le_prev; \
        \
        if ( SKYFS_LIST_FIRST(head) != (elm) )    \
            SKYFS_LIST_ELM_PREV(head, field, elm)->field.le_next = \
                SKYFS_LIST_ELM(head, elm)->field.le_next; \
        else    \
            SKYFS_LIST_FIRST(head) = SKYFS_LIST_NEXT(head, elm, field);  \
        \
    } while (0)

#endif
