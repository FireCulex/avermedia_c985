/* project.h */
#ifndef PROJECT_H
#define PROJECT_H

#include "structs.h"

int project_c985_init(struct c985_poc *d);
void project_c985_cleanup(struct c985_poc *d);
void ProjectC985_selectInputSource(struct c985_poc *d,
                                   enum project_input_control param_1);

#endif /* PROJECT_H */
