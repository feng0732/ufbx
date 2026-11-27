#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a, ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
static void run(ufbx_scene*scene,ufbx_node*node,ufbx_bake_opts*op,const char*l){
    ufbx_error e;ufbx_anim*an=scene->anim;
    ufbx_baked_anim*b=ufbx_bake_anim(scene,an,op,&e);ufbx_baked_node*bn=ufbx_find_baked_node(b,node);
    double t0=an->time_begin,t1=an->time_end,mk=0,mm=0;int N=400;
    for(int i=0;i<=N;i++){double t=t0+(t1-t0)*i/N;
        ufbx_quat s=ufbx_evaluate_transform(an,node,t).rotation,bk=ufbx_evaluate_baked_quat(bn->rotation_keys,t);
        double er=qang(s,bk),near=1e9;
        for(size_t k=0;k<bn->rotation_keys.count;k++){double d=fabs(bn->rotation_keys.data[k].time-t);if(d<near)near=d;}
        if(near<1e-7)mk=fmax(mk,er);else mm=fmax(mm,er);}
    printf("%-22s keys=%3zu keyerr=%.2e deg miderr=%.4f deg\n",l,bn->rotation_keys.count,mk*180/M_PI,mm*180/M_PI);
    ufbx_free_baked_anim(b);
}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);ufbx_node*n=ufbx_find_node(s,v[2]);
    ufbx_bake_opts a={0};run(s,n,&a,"default");
    ufbx_bake_opts b={0};b.resample_rate=120;run(s,n,&b,"rate120");
    ufbx_bake_opts g={0};g.resample_rate=10;run(s,n,&g,"rate10");
    ufbx_bake_opts d={0};d.resample_rate=24;d.key_reduction_enabled=1;d.key_reduction_rotation=1;run(s,n,&d,"reduce_rot");
    ufbx_bake_opts f={0};f.resample_rate=24;f.key_reduction_enabled=1;run(s,n,&f,"reduce_norot");
    ufbx_free_scene(s);return 0;}
