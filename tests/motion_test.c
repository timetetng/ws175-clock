#include <assert.h>
#include <math.h>
#include "../main/motion.h"
int main(void) {
    motion_t m = {.target=100};
    for (int i=0;i<240;i++) motion_step(&m, 1.f/120, .39f, .85f);
    assert(m.x == 100 && m.v == 0);
    assert(motion_rubber(100, 466)>0 && motion_rubber(100,466)<100);
    assert(fabsf(motion_rubber(-100,466)+motion_rubber(100,466))<.001f);
    m=(motion_t){.target=100};
    int overshot=0;
    for(int i=0;i<240;i++){motion_step(&m,1.f/60,.25f,.45f);if(m.x>100)overshot=1;}
    assert(overshot && m.x==100);
    return 0;
}
