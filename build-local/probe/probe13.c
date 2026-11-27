#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;
    ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_node*node=ufbx_find_node(s,"Bottom");
    for(size_t i=0;i<s->anim_stacks.count;i++){
        ufbx_anim_stack*st=s->anim_stacks.data[i];
        ufbx_anim*an=st->anim;
        double path=0;int N=200;
        ufbx_quat prev=ufbx_evaluate_transform(an,node,an->time_begin).rotation;
        for(int k=1;k<=N;k++){double t=an->time_begin+(an->time_end-an->time_begin)*k/N;
            ufbx_quat q=ufbx_evaluate_transform(an,node,t).rotation;path+=qang(prev,q);prev=q;}
        printf("stack '%s' %.4f..%.4f Bottom path=%.1f deg\n",st->element.name.data,
            an->time_begin,an->time_end,path*180/M_PI);
    }
    return 0;}
