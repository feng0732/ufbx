#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;
    ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim*an=s->anim;
    printf("time_begin=%.17g time_end=%.17g 19/24=%.17g diff=%.3e\n",
        an->time_begin,an->time_end,19.0/24.0,fabs(an->time_end-19.0/24.0));
    ufbx_node*node=ufbx_find_node(s,"Bottom");
    ufbx_bake_opts o={0};
    ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_node*bn=ufbx_find_baked_node(b,node);
    printf("rot keys=%zu\n",bn->rotation_keys.count);
    return 0;}
