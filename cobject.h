/* SPDX-License-Identifier: GPL-2.0 */
#ifndef COBJECT_H
#define COBJECT_H

#include "include/abi/cobject.h"
#include "include/abi/cobjectmgr.h"
#include "include/abi/cppobject.h"

void CObject_Constructor(struct CObject *param_1, struct CObject *param_2, u32 param_3);
void CObject_Destructor(struct CObject *param_1);
void CObjectMgr_Destructor(struct CObjectMgr *mgr);
void CObject_Unlock(struct CObject *param_1);
void CObject_Lock(struct CObject *param_1);
u32 CObjectMgr_AddObject(struct CObject *mgr, void *obj);
void *CObjectMgr_RemoveObject(struct CObjectMgr *mgr, u32 handle);
u32 CppObject_WhoAmI(struct CObject *this);
int CObject_IsInitialized(struct CObject *obj);
/* In sync.h - add prototype */
void CppObject_enterCritical(struct CppObject *this);
void CppObject_leaveCritical(struct CppObject *this);
void *CObjectMgr_GetObjectByHandle(struct CObjectMgr *this, u32 handle);

#endif /* COBJECT_H */
