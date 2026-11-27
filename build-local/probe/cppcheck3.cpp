#include "../../ufbx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define ufbxt_assert(c) do { if(!(c)){printf("fail %s:%d %s\n",__FILE__,__LINE__,#c);} } while(0)
#define ufbxt_hintf(...) do{}while(0)
#define ufbxt_arraycount(a) (sizeof(a)/sizeof(*(a)))
#define UFBXT_BAKE_QUAT_PI 3.14159265358979323846
typedef struct ufbx_scene ufbx_scene;
#include "helpers.inc"
int main(){ return 0; }
