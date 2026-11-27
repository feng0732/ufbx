#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);if(!s){printf("FAIL\n");return 1;}
    ufbx_anim*an=s->anim;ufbx_node*node=ufbx_find_node(s,v[2]);
    double t0=an->time_begin,t1=an->time_end;
    printf("'%s' time %.4f..%.4f\n",v[1],t0,t1);
    
    ufbx_bake_opts o={0};ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_node*bn=ufbx_find_baked_node(b,node);
    printf("baked rot keys=%zu constant=%d\n",bn->rotation_keys.count,bn->constant_rotation);
    int neg=0;for(size_t k=1;k<bn->rotation_keys.count;k++)if(ufbx_quat_dot(bn->rotation_keys.data[k-1].value,bn->rotation_keys.data[k].value)<0)neg++;
    printf("neg dots=%d\n",neg);
    double mk=0,mm=0;int N=400;
    for(int i=0;i<=N;i++){double t=t0+(t1-t0)*i/N;
        ufbx_quat sr=ufbx_evaluate_transform(an,node,t).rotation,bk=ufbx_evaluate_baked_quat(bn->rotation_keys,t);
        double er=qang(sr,bk),near=1e9;
        for(size_t k=0;k<bn->rotation_keys.count;k++){double d=fabs(bn->rotation_keys.data[k].time-t);if(d<near)near=d;}
        if(near<1e-7)mk=fmax(mk,er);else mm=fmax(mm,er);}
    printf("keyerr=%.2e miderr=%.4f deg\n",mk*180/M_PI,mm*180/M_PI);
    ufbx_free_baked_anim(b);return 0;}
