#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    printf("file default anim time=%.4f..%.4f stacks=%zu\n",s->anim->time_begin,s->anim->time_end,s->anim_stacks.count);
    for(size_t n=0;n<s->nodes.count;n++){
        ufbx_node*nd=s->nodes.data[n];
        double path=0;int N=120;ufbx_quat prev=ufbx_evaluate_transform(s->anim,nd,s->anim->time_begin).rotation;
        for(int k=1;k<=N;k++){double t=s->anim->time_begin+(s->anim->time_end-s->anim->time_begin)*k/N;
            ufbx_quat q=ufbx_evaluate_transform(s->anim,nd,t).rotation;path+=qang(prev,q);prev=q;}
        if(path>0.1)printf(" node '%s' path=%.1f\n",nd->name.data,path*180/M_PI);
    }
    return 0;}
