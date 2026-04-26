/* SPDX-License-Identifier: GPL-2.0 */
#ifndef COBJECT_H
#define COBJECT_H

#include "structs.h"

void CObject_Constructor(struct c_object *param_1, struct c_object *param_2, u32 param_3);
void CObject_Destructor(struct c_object *param_1);
void CObjectMgr_Destructor(struct c_object_mgr *mgr);
void CObject_Unlock(struct c_object *param_1);
void CObject_Lock(struct c_object *param_1);
u32 CObjectMgr_AddObject(struct c_object *mgr, void *obj);
void *CObjectMgr_RemoveObject(struct c_object_mgr *mgr, u32 handle);


#endif /* COBJECT_H */
