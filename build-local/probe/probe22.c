#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
static void run(ufbx_scene*s,ufbx_node*n,ufbx_bake_opts*op,const char*l){
    ufbx_error e;ufbx_anim*an=s->anim;
    ufbx_baked_anim*b=ufbx_bake_anim(s,an,op,&e);ufbx_baked_node*bn=ufbx_find_baked_node(b,n);
    double t0=an->time_begin,t1=an->time_end,mk=0,mm=0;int N=400;
    for(int i=0;i<=N;i++){double t=t0+(t1-t0)*i/N;
        ufbx_quat sr=ufbx_evaluate_transform(an,n,t).rotation,bk=ufbx_evaluate_baked_quat(bn->rotation_keys,t);
        double er=qang(sr,bk),near=1e9;
        for(size_t k=0;k<bn->rotation_keys.count;k++){double d=fabs(bn->rotation_keys.data[k].time-t);if(d<near)near=d;}
        if(near<1e-7)mk=fmax(mk,er);else mm=fmax(mm,er);}
    int neg=0;for(size_t k=1;k<bn->rotation_keys.count;k++)if(ufbx_quat_dot(bn->rotation_keys.data[k-1].value,bn->rotation_keys.data[k].value)<0)neg++;
    printf("%-16s keys=%2zu neg=%d keyerr=%.1e miderr=%.4f deg\n",l,bn->rotation_keys.count,neg,mk*180/M_PI,mm*180/M_PI);
    ufbx_free_baked_anim(b);
}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);ufbx_node*n=ufbx_find_node(s,"pCube2");
    ufbx_anim*an=s->anim;double t0=an->time_begin,t1=an->time_end;
    // hold detection
    int N=200;double hold=0,cur=0;ufbx_quat prev=ufbx_evaluate_transform(an,n,t0).rotation;double pt=t0;
    for(int k=1;k<=N;k++){double t=t0+(t1-t0)*k/N;ufbx_quat q=ufbx_evaluate_transform(an,n,t).rotation;
        double a=qang(prev,q),rate=a/(t-pt);
        if(rate<1e-3){cur+=(t-pt);if(cur>hold)hold=cur;}else cur=0;prev=q;pt=t;}
    printf("longest hold=%.4fs anim dur=%.4f\n",hold,t1-t0);
    double path=0;prev=ufbx_evaluate_transform(an,n,t0).rotation;
    for(int k=1;k<=N;k++){double t=t0+(t1-t0)*k/N;ufbx_quat q=ufbx_evaluate_transform(an,n,t).rotation;path+=qang(prev,q);prev=q;}
    printf("source path=%.1f deg\n",path*180/M_PI);
    ufbx_bake_opts a={0};run(s,n,&a,"default");
    ufbx_bake_opts b={0};b.resample_rate=60;run(s,n,&b,"rate60");
    ufbx_bake_opts g={0};g.resample_rate=12;run(s,n,&g,"rate12");
    ufbx_bake_opts d={0};d.key_reduction_enabled=1;d.key_reduction_rotation=1;run(s,n,&d,"reduce_rot");
    ufbx_bake_opts f={0};f.key_reduction_enabled=1;run(s,n,&f,"reduce_norot");
    return 0;}
