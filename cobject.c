// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "cobject.h"
#include "qperrors.h"
#include "sync.h"
#include "qposm.h"

void CObject_Constructor(struct CObject *param_1, struct CObject *param_2, u32 param_3)
{
    param_1->m_pParent = param_2;
    param_1->m_dwObjectAttributes = param_3;
    param_1->Init = NULL;
    param_1->Done = NULL;
    param_1->m_fInitialized = 0;
    param_1->m_semCriticalSection = NULL;

    /* Match Windows: if attr & 1, initialize spinlock */
    if ((param_3 & 1) == 0) {
        if (param_3 & 2) {
            /* Semaphore path - skip for now */
        }
    } else {
        spin_lock_init(&param_1->m_spinlock);  /* ← Windows: KeInitializeSpinLock */
    }

    param_1->m_irql = 0;
}
void CObjectMgr_RemoveObjects(struct CObjectMgr *mgr)
{
    struct CObjectEntry *entry;
    struct CObjectEntry *next;

    if (!mgr)
        return;

    entry = mgr->m_pHead;
    while (entry) {
        /* Save next pointer before freeing */
        next = entry->pNext;

        /* Call callback if defined and object exists */
        if (mgr->m_pFuncCallback && entry->pObject) {
            ((_EQPErrors (*)(void *))mgr->m_pFuncCallback)(entry->pObject);
        }

        /* Free the entry */
        kfree(entry);

        /* Move to next */
        entry = next;
    }

    /* Reset manager state */
    mgr->m_dwObjectNb = 0;
    mgr->m_pHead = NULL;
}

void CObject_Destructor(struct CObject *obj)
{
    if (!obj)
        return;

    /* Check if using semaphore mode (bit 0 == 0) */
    if ((obj->m_dwObjectAttributes & 1) == 0) {
        /* Check if semaphore exists (bit 1 set) and pointer valid */
        if ((obj->m_dwObjectAttributes & 2) && obj->m_semCriticalSection) {
            /* Destroy and free semaphore */
            mutex_destroy((struct mutex *)obj->m_semCriticalSection);
            kfree(obj->m_semCriticalSection);
            obj->m_semCriticalSection = NULL;
        }
    } else {
        /* Spinlock mode: zero the spinlock */
        /* Linux spinlock_t is a struct; use memset instead of assignment */
        memset(&obj->m_spinlock, 0, sizeof(obj->m_spinlock));
    }
}

void CObjectMgr_Destructor(struct CObjectMgr *mgr)
{
    if (!mgr)
        return;

    /* Remove all objects */
    CObjectMgr_RemoveObjects(mgr);

    /* Destroy base object */
    CObject_Destructor(&mgr->m_Object);
}

void CObject_Lock(struct CObject *param_1)
{
    /* TODO: implement from Ghidra if needed */
}

void CObject_Unlock(struct CObject *param_1)
{
    /* TODO: implement from Ghidra if needed */
}
u32 CObjectMgr_AddObject(struct CObject *mgr, void *obj)
{
static u32 h = 0x1000; return h++;
    /* TODO: implement from Ghidra if needed */
}
void *CObjectMgr_RemoveObject(struct CObjectMgr *mgr, u32 handle)
{
    struct CObjectEntry *entry;
    struct CObjectEntry *prev;
    struct CObjectEntry *to_free;
    void *object;

    /* Lock the manager using base CObject lock */
    CObject_Lock(&mgr->m_Object);

    entry = mgr->m_pHead;
    to_free = NULL;
    object = NULL;

    if (entry) {
        /* Check head entry */
        if (entry->hObject == handle) {
            object = entry->pObject;
            mgr->m_pHead = entry->pNext;
            mgr->m_dwObjectNb--;
            to_free = entry;
        } else {
            /* Search rest of list */
            prev = entry;
            entry = entry->pNext;
            while (entry) {
                if (entry->hObject == handle) {
                    object = entry->pObject;
                    prev->pNext = entry->pNext;
                    mgr->m_dwObjectNb--;
                    to_free = entry;
                    break;
                }
                prev = entry;
                entry = entry->pNext;
            }
        }
    }

    /* Unlock */
    CObject_Unlock(&mgr->m_Object);

    /* Free the entry structure (not the object itself) */
    if (to_free) {
        kfree(to_free);
    }

    return object;
}

u32 CppObject_WhoAmI(struct CObject *this)
{
    /* TODO: m_dwWhoAmI not yet defined in struct */
    return 0;
    // return this->m_dwWhoAmI;
}

int CObject_IsInitialized(struct CObject *obj)
{
    return obj->m_fInitialized;
}
EXPORT_SYMBOL_GPL(CObject_IsInitialized);

/* In sync.c - add implementation */
void CppObject_enterCritical(struct CppObject *this)
{
    if (!this)
        return;

    if ((this->m_dwObjectAttributes & 1) != 0) {
        /* Use spinlock */
        this->m_irql = KeAcquireSpinLockRaiseToDpc(&this->m_spinlock);
    } else if ((this->m_dwObjectAttributes & 2) != 0) {
        /* Use mutex */
        KeWaitForSingleObject(&this->m_mutex, 0, 0, 0, 0);
    }
}
EXPORT_SYMBOL_GPL(CppObject_enterCritical);

void CppObject_leaveCritical(struct CppObject *this)
{
    if (!this)
        return;

    if ((this->m_dwObjectAttributes & 1) != 0) {
        /* Release spinlock */
        KeReleaseSpinLock(&this->m_spinlock, this->m_irql);
    } else if ((this->m_dwObjectAttributes & 2) != 0) {
        /* Release mutex */
        KeReleaseMutex(&this->m_mutex, 0);
    }
}

/* In cobject.c */

void *CObjectMgr_GetObjectByHandle(struct CObjectMgr *this, u32 handle)
{
    void *result = NULL;
    struct CObjectEntry *entry;
    u32 i;

    if (!this)
        return NULL;

    /* Enter critical section */
    if ((this->m_Object.m_dwObjectAttributes & 1) != 0) {
        this->m_Object.m_irql = KeAcquireSpinLockRaiseToDpc(&this->m_Object.m_spinlock);
    } else if ((this->m_Object.m_dwObjectAttributes & 2) != 0) {
        if (this->m_Object.m_semCriticalSection)
            QPOSMWaitSem(this->m_Object.m_semCriticalSection, -1);
    }

    /* Walk the linked list looking for matching handle */
    entry = this->m_pHead;
    for (i = 0; i < this->m_dwObjectNb; i++) {
        if (entry->hObject == handle) {
            result = entry->pObject;
        }
        entry = entry->pNext;
    }

    /* Leave critical section */
    if ((this->m_Object.m_dwObjectAttributes & 1) != 0) {
        KeReleaseSpinLock(&this->m_Object.m_spinlock, this->m_Object.m_irql);
    } else if ((this->m_Object.m_dwObjectAttributes & 2) != 0) {
        if (this->m_Object.m_semCriticalSection)
            QPOSMSignalSem(this->m_Object.m_semCriticalSection);
    }

    return result;
}
