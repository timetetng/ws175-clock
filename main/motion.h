/* 动效算法改编自 chhongzh/lilt anim，MIT，许可见 LICENSE.lilt。 */
#pragma once
#include <math.h>
typedef struct { float x, v, target; } motion_t;
static inline void motion_step(motion_t *m, float dt, float response, float damping)
{
    if (dt <= 0) return;
    if (dt > .05f) dt = .05f;
    float w = 6.2831855f / response, x = m->x - m->target;
    int n = (int)(dt * 240) + 1;
    float h = dt / n;
    for (int i=0; i<n; i++) {
        m->v += (-w*w*x - 2*damping*w*m->v)*h;
        x += m->v*h;
    }
    if (fabsf(x)<.05f && fabsf(m->v)<.5f) { m->x=m->target; m->v=0; }
    else m->x=m->target+x;
}
static inline float motion_rubber(float x, float dim)
{
    if (dim<=0) return 0;
    float y=(1-1/(fabsf(x)*.55f/dim+1))*dim;
    return x<0 ? -y:y;
}
