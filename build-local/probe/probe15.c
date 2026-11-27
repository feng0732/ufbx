#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim_stack*spin=ufbx_find_anim_stack(s,"Spin");
    if(!spin) spin=ufbx_find_anim_stack(s,"Skeleton|Spin");
    ufbx_anim*an=spin->anim;
    ufbx_node*node=ufbx_find_node(s,"Bottom");
    printf("spin time=%.6f..%.6f\n",an->time_begin,an->time_end);
    // dense angular velocity to find holds
    int N=96;double prev_t=an->time_begin;ufbx_quat pq=ufbx_evaluate_transform(an,node,prev_t).rotation;
    double hold_start=-1;
    for(int i=1;i<=N;i++){double t=an->time_begin+(an->time_end-an->time_begin)*i/N;
        ufbx_quat q=ufbx_evaluate_transform(an,node,t).rotation;
        double rate=qang(pq,q)/(t-prev_t)*180/M_PI;
        if(rate<1.0){if(hold_start<0)hold_start=prev_t;}
        else{if(hold_start>=0){printf("hold %.4f..%.4f (%.4fs)\n",hold_start,prev_t,prev_t-hold_start);hold_start=-1;}}
        pq=q;prev_t=t;}
    if(hold_start>=0)printf("tail hold %.4f..%.4f\n",hold_start,an->time_end);
    return 0;}
