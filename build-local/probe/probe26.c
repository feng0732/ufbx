#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);if(!s)return 1;
    ufbx_bake_opts o={0};ufbx_baked_anim*b=ufbx_bake_anim(s,s->anim,&o,&e);
    if(!b)return 1;
    int total=0;
    for(size_t n=0;n<b->nodes.count;n++){
        ufbx_baked_quat_list keys=b->nodes.data[n].rotation_keys;
        for(size_t k=1;k<keys.count;k++)if(ufbx_quat_dot(keys.data[k-1].value,keys.data[k].value)<0){total++;break;}
    }
    if(total)printf("%s: %d nodes with neg dots\n",v[1],total);
    ufbx_free_baked_anim(b);return 0;}
