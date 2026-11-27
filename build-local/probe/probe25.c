#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);if(!s){printf("loadfail\n");return 1;}
    ufbx_anim*an=s->anim;
    if(c>3){ufbx_anim_stack*st=ufbx_find_anim_stack(s,v[3]);if(!st){char buf[128];snprintf(buf,sizeof(buf),"Skeleton|%s",v[3]);st=ufbx_find_anim_stack(s,buf);}if(!st){printf("nostack\n");return 1;}an=st->anim;}
    ufbx_node*node=ufbx_find_node(s,v[2]);if(!node){printf("nonode\n");return 1;}
    ufbx_bake_opts o={0};ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_node*bn=ufbx_find_baked_node(b,node);
    if(!bn){printf("nobake\n");return 1;}
    ufbx_baked_quat_list keys=bn->rotation_keys;
    int neg=0;for(size_t k=1;k<keys.count;k++)if(ufbx_quat_dot(keys.data[k-1].value,keys.data[k].value)<0)neg++;
    printf("%-40s keys=%2zu neg=%d\n",v[1],keys.count,neg);
    return 0;}
