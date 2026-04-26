// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include "cobject.h"
#include "qperrors.h"

void CObject_Constructor(struct c_object *param_1, struct c_object *param_2, u32 param_3)
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
void CObjectMgr_RemoveObjects(struct c_object_mgr *mgr)
{
    struct c_object_entry *entry;
    struct c_object_entry *next;

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

void CObject_Destructor(struct c_object *obj)
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

void CObjectMgr_Destructor(struct c_object_mgr *mgr)
{
    if (!mgr)
        return;

    /* Remove all objects */
    CObjectMgr_RemoveObjects(mgr);

    /* Destroy base object */
    CObject_Destructor(&mgr->m_Object);
}

void CObject_Lock(struct c_object *param_1)
{
    /* TODO: implement from Ghidra if needed */
}

void CObject_Unlock(struct c_object *param_1)
{
    /* TODO: implement from Ghidra if needed */
}
u32 CObjectMgr_AddObject(struct c_object *mgr, void *obj)
{
static u32 h = 0x1000; return h++;
    /* TODO: implement from Ghidra if needed */
}
void *CObjectMgr_RemoveObject(struct c_object_mgr *mgr, u32 handle)
{
    struct c_object_entry *entry;
    struct c_object_entry *prev;
    struct c_object_entry *to_free;
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
