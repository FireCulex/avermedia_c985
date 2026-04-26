/* qperrors.h */
#ifndef QPERRORS_H
#define QPERRORS_H

#include <linux/types.h>

/* QP Error Codes - matches Windows QPERR_* values */
#define QPERR_SUCCESS       0
#define QPERR_PENDING       1
#define QPERR_CTRL_VERSION  245
#define QPERR_AGAIN         246
#define QPERR_NOTFOUND      247
#define QPERR_CANCELLED     248
#define QPERR_INVALID       249
#define QPERR_TIMEOUT       250
#define QPERR_NOTSUPP       251
#define QPERR_NOTIMPL       252
#define QPERR_PARMS         253
#define QPERR_NOMEM         254
#define QPERR_FAIL          255

/* Return type for codec functions */
typedef u8 _EQPErrors;

/* Helper macros */
#define QPERR_IS_ERROR(err)   ((err) != QPERR_SUCCESS && (err) != QPERR_PENDING)
#define QPERR_IS_SUCCESS(err) ((err) == QPERR_SUCCESS)

#endif /* QPERRORS_H */
