#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_node*node=ufbx_find_node(s,"Bottom");
    for(size_t i=0;i<s->anim_stacks.count;i++){
        ufbx_anim_stack*st=s->anim_stacks.data[i];ufbx_anim*an=st->anim;
        int N=200;double t0=an->time_begin,t1=an->time_end;
        double path=0,hold=0,cur=0;ufbx_quat prev=ufbx_evaluate_transform(an,node,t0).rotation;double pt=t0;
        for(int k=1;k<=N;k++){double t=t0+(t1-t0)*k/N;ufbx_quat q=ufbx_evaluate_transform(an,node,t).rotation;
            double a=qang(prev,q);path+=a;double rate=a/(t-pt);
            if(rate<1e-3){if(t-(t-cur?0:0)>=0)cur+=(t-pt);if(cur>hold)hold=cur;}else cur=0;
            prev=q;pt=t;}
        printf("'%s' path=%6.1f deg longest_hold=%.4fs\n",st->element.name.data,path*180/M_PI,hold);
    }
    return 0;}
