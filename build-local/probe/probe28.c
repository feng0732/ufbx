#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim_stack*st=ufbx_find_anim_stack(s,"Spin");if(!st)st=ufbx_find_anim_stack(s,"Skeleton|Spin");
    ufbx_anim*an=st->anim;ufbx_node*n=ufbx_find_node(s,"Bottom");
    ufbx_bake_opts a={0};a.resample_rate=24;
    ufbx_baked_anim*ba=ufbx_bake_anim(s,an,&a,&e);
    ufbx_bake_opts b={0};b.resample_rate=24;b.key_reduction_enabled=1;b.key_reduction_rotation=1;
    ufbx_baked_anim*bb=ufbx_bake_anim(s,an,&b,&e);
    ufbx_bake_opts g={0};g.resample_rate=24;g.key_reduction_enabled=1;
    ufbx_baked_anim*bg=ufbx_bake_anim(s,an,&g,&e);
    printf("plain24=%zu reduce_rot=%zu reduce_norot=%zu\n",
        ufbx_find_baked_node(ba,n)->rotation_keys.count,
        ufbx_find_baked_node(bb,n)->rotation_keys.count,
        ufbx_find_baked_node(bg,n)->rotation_keys.count);
    return 0;}
