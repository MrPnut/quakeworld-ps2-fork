#ifndef __MODEL_H__
#define __MODEL_H__

#include "bspfile.h"
#include "gl_model.h"

typedef qmodel_t model_t;
typedef qmodel_t brushmodel_t;

#define ENT_EF_BRIGHTFIELD (1 << 0)
#define ENT_EF_MUZZLEFLASH (1 << 1)
#define ENT_EF_BRIGHTLIGHT (1 << 2)
#define ENT_EF_DIMLIGHT    (1 << 3)
#define ENT_EF_FLAG1       (1 << 4)
#define ENT_EF_FLAG2       (1 << 5)
#define ENT_EF_BLUE        (1 << 6)
#define ENT_EF_RED         (1 << 7)

#endif
