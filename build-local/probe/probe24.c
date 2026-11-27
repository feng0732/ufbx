#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim*an;
    if(c>3){ufbx_anim_stack*st=ufbx_find_anim_stack(s,v[3]);if(!st)st=ufbx_find_anim_stack(s,"Skeleton|Spin");an=st->anim;}else an=s->anim;
    ufbx_node*node=ufbx_find_node(s,v[2]);
    ufbx_bake_opts o={0};ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_quat_list keys=ufbx_find_baked_node(b,node)->rotation_keys;
    int neg=0;for(size_t k=1;k<keys.count;k++)if(ufbx_quat_dot(keys.data[k-1].value,keys.data[k].value)<0)neg++;
    printf("%s keys=%zu neg=%d\n",v[1],keys.count,neg);
    return 0;}
