#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);ufbx_node*n=ufbx_find_node(s,v[2]);
    ufbx_anim*an=s->anim;double t0=an->time_begin,t1=an->time_end;int N=400;
    double min_moving=1e9,max_still=0;ufbx_quat prev=ufbx_evaluate_transform(an,n,t0).rotation;double pt=t0;
    for(int i=1;i<=N;i++){double t=t0+(t1-t0)*i/N;ufbx_quat q=ufbx_evaluate_transform(an,n,t).rotation;
        double rate=qang(prev,q)/(t-pt);
        if(rate<1e-3)max_still=fmax(max_still,rate);else min_moving=fmin(min_moving,rate);
        prev=q;pt=t;}
    printf("max still rate=%.2e rad/s, min moving rate=%.2e rad/s (threshold 1e-3)\n",max_still,min_moving);
    return 0;}
