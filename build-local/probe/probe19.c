#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);if(!s)return 1;
    ufbx_anim*an=s->anim;if(!an)return 0;
    double t0=an->time_begin,t1=an->time_end;int N=120;
    double gpath=0,ghold=0;const char*gn="";
    for(size_t n=0;n<s->nodes.count;n++){ufbx_node*nd=s->nodes.data[n];
        double path=0,hold=0,cur=0;ufbx_quat prev=ufbx_evaluate_transform(an,nd,t0).rotation;double pt=t0;
        for(int k=1;k<=N;k++){double t=t0+(t1-t0)*k/N;ufbx_quat q=ufbx_evaluate_transform(an,nd,t).rotation;
            double a=qang(prev,q);path+=a;double rate=a/(t-pt);
            if(rate<1e-3){cur+=(t-pt);if(cur>hold)hold=cur;}else cur=0;prev=q;pt=t;}
        if(path>gpath){gpath=path;ghold=hold;gn=nd->name.data;}
    }
    if(gpath>1.57 && ghold>0.03) printf("%s: node '%s' path=%.1f hold=%.4f\n",v[1],gn,gpath*180/M_PI,ghold);
    return 0;}
