/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * rx_kmutex.h - mutex and condition variable macros for kernel environment.
 *
 * Linux implementation.
 * This are noops until such time as the kernel no longer has a global lock.
 */
#ifndef RX_KMUTEX_H_
#define RX_KMUTEX_H_

#include "../rx/rx_kernel.h"	/* for osi_Panic() */

/* AFS_GLOBAL_RXLOCK_KERNEL is defined so that the busy tq code paths are
 * used. The thread can sleep when sending packets.
 */
#define	AFS_GLOBAL_RXLOCK_KERNEL 1


#ifdef CONFIG_SMP
#define RX_ENABLE_LOCKS 1

#ifndef _LINUX_CODA_FS_I
#define _LINUX_CODA_FS_I
struct coda_inode_info {};
#endif
#include "linux/wait.h"
#include "linux/sched.h"

typedef struct afs_kmutex {
    struct semaphore sem;
    int owner;
} afs_kmutex_t;

#ifndef set_current_state
#define set_current_state(X) current->state=X
#endif

#if defined(AFS_LINUX24_ENV)
typedef wait_queue_head_t afs_kcondvar_t;
#else
typedef struct wait_queue *afs_kcondvar_t;
#endif

static inline int MUTEX_ISMINE(afs_kmutex_t *l)
{
    return l->owner == current->pid;
}


static inline void afs_mutex_init(afs_kmutex_t *l)
{
#if defined(AFS_LINUX24_ENV)
    init_MUTEX(&l->sem);
#else
    l->sem = MUTEX;
#endif
    l->owner = 0;
}
#define MUTEX_INIT(a,b,c,d) afs_mutex_init(a)

#define MUTEX_DESTROY(a)

static inline void MUTEX_ENTER(afs_kmutex_t *l)
{
    down(&l->sem);
    if (l->owner)
	osi_Panic("mutex_enter: 0x%x held by %d", l, l->owner);
    l->owner = current->pid;
}
							      
/* And how to do a good tryenter? */
static inline int MUTEX_TRYENTER(afs_kmutex_t *l)
{
    if (down_trylock(&l->sem))
        return 0;
    l->owner = current->pid;
    return 1;
}

static inline void MUTEX_EXIT(afs_kmutex_t *l)
{
    if (l->owner != current->pid)
	osi_Panic("mutex_exit: 0x%x held by %d",
		  l, l->owner);
    l->owner = 0;
    up(&l->sem);
}

#if defined(AFS_LINUX24_ENV)
#define CV_INIT(cv,b,c,d) init_waitqueue_head((wait_queue_head_t *)(cv))
#else
#define CV_INIT(cv,b,c,d) init_waitqueue((struct wait_queue**)(cv))
#endif
#define CV_DESTROY(cv)

/* CV_WAIT and CV_TIMEDWAIT sleep until the specified event occurs, or, in the
 * case of CV_TIMEDWAIT, until the specified timeout occurs.
 * - NOTE: that on Linux, there are circumstances in which TASK_INTERRUPTIBLE
 *   can wake up, even if all signals are blocked
 * - TODO: handle signals correctly by passing an indication back to the
 *   caller that the wait has been interrupted and the stack should be cleaned
 *   up preparatory to signal delivery
 */
static inline int CV_WAIT(afs_kcondvar_t *cv, afs_kmutex_t *l)
{
    int isAFSGlocked = ISAFS_GLOCK(); 
    sigset_t saved_set;
#ifdef DECLARE_WAITQUEUE
    DECLARE_WAITQUEUE(wait, current);
#else
    struct wait_queue wait = { current, NULL };
#endif
 
    add_wait_queue(cv, &wait);
    set_current_state(TASK_INTERRUPTIBLE);

    if (isAFSGlocked) AFS_GUNLOCK();
    MUTEX_EXIT(l);

    spin_lock_irq(&current->sigmask_lock);
    saved_set = current->blocked;
    sigfillset(&current->blocked);
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);

    schedule();
    remove_wait_queue(cv, &wait);

    spin_lock_irq(&current->sigmask_lock);
    current->blocked = saved_set;
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);

    if (isAFSGlocked) AFS_GLOCK();
    MUTEX_ENTER(l);

    return 0;
}

static inline int CV_TIMEDWAIT(afs_kcondvar_t *cv, afs_kmutex_t *l, int waittime)
{
    int isAFSGlocked = ISAFS_GLOCK();
    long t = waittime * HZ / 1000;
    sigset_t saved_set;
#ifdef DECLARE_WAITQUEUE
    DECLARE_WAITQUEUE(wait, current);
#else
    struct wait_queue wait = { current, NULL };
#endif
 
    add_wait_queue(cv, &wait);
    set_current_state(TASK_INTERRUPTIBLE);

    if (isAFSGlocked) AFS_GUNLOCK();
    MUTEX_EXIT(l);
    
    spin_lock_irq(&current->sigmask_lock);
    saved_set = current->blocked;
    sigfillset(&current->blocked);
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);

    t = schedule_timeout(t);
    remove_wait_queue(cv, &wait);
    
    spin_lock_irq(&current->sigmask_lock);
    current->blocked = saved_set;
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);

    if (isAFSGlocked) AFS_GLOCK();
    MUTEX_ENTER(l);

    return 0;
}

#if defined(AFS_LINUX24_ENV)
#define CV_SIGNAL(cv) wake_up((wait_queue_head_t *)cv)
#define CV_BROADCAST(cv) wake_up((wait_queue_head_t *)cv)
#else
#define CV_SIGNAL(cv) wake_up((struct wait_queue**)cv)
#define CV_BROADCAST(cv) wake_up((struct wait_queue**)cv)
#endif

#else

#define MUTEX_ISMINE(a)
#define osirx_AssertMine(addr, msg)

#define MUTEX_DESTROY(a)
#define MUTEX_ENTER(a)
#define MUTEX_TRYENTER(a) 1
#define MUTEX_EXIT(a)  
#define MUTEX_INIT(a,b,c,d) 
#define CV_INIT(a,b,c,d)
#define CV_DESTROY(a)
#endif

/* Since we're using the RX listener daemon, we don't need to hold off
 * interrupts.
 */
#define SPLVAR
#define NETPRI
#define USERPRI

#endif /* RX_KMUTEX_H_ */
