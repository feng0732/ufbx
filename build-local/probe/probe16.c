#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
static void analyze(ufbx_scene*s,ufbx_anim*an,const char*tag,const char*node_name){
    ufbx_node*node=ufbx_find_node(s,node_name);
    ufbx_error e;ufbx_bake_opts o={0};
    ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_node*bn=ufbx_find_baked_node(b,node);
    printf("[%s] time %.4f..%.4f keys=%zu\n",tag,an->time_begin,an->time_end,bn->rotation_keys.count);
    double mk=0,mm=0;int N=400;
    for(int i=0;i<=N;i++){double t=an->time_begin+(an->time_end-an->time_begin)*i/N;
        ufbx_quat sr=ufbx_evaluate_transform(an,node,t).rotation,bk=ufbx_evaluate_baked_quat(bn->rotation_keys,t);
        double er=qang(sr,bk),near=1e9;
        for(size_t k=0;k<bn->rotation_keys.count;k++){double d=fabs(bn->rotation_keys.data[k].time-t);if(d<near)near=d;}
        if(near<1e-7)mk=fmax(mk,er);else mm=fmax(mm,er);}
    printf("  keyerr=%.2e miderr=%.2e deg\n",mk*180/M_PI,mm*180/M_PI);
    int neg=0;for(size_t k=1;k<bn->rotation_keys.count;k++)if(ufbx_quat_dot(bn->rotation_keys.data[k-1].value,bn->rotation_keys.data[k].value)<0)neg++;
    printf("  neg dots=%d\n",neg);
    ufbx_free_baked_anim(b);
}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim_stack*spin=ufbx_find_anim_stack(s,"Spin");if(!spin)spin=ufbx_find_anim_stack(s,"Skeleton|Spin");
    analyze(s,spin->anim,"Spin","Bottom");
    analyze(s,s->anim,"default","Bottom");
    return 0;}
