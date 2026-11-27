#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim_stack*st=ufbx_find_anim_stack(s,"Spin");if(!st)st=ufbx_find_anim_stack(s,"Skeleton|Spin");
    ufbx_anim*an=st->anim;ufbx_node*node=ufbx_find_node(s,"Bottom");
    double t0=an->time_begin,t1=an->time_end;
    for(int N=100;N<=800;N*=2){
        double sp=0;ufbx_quat p=ufbx_evaluate_transform(an,node,t0).rotation;
        for(int i=1;i<=N;i++){double t=t0+(t1-t0)*i/N;ufbx_quat q=ufbx_evaluate_transform(an,node,t).rotation;sp+=qang(p,q);p=q;}
        printf("N=%d source=%.5f (%.3f deg)\n",N,sp,sp*180/M_PI);
    }
    ufbx_bake_opts o={0};ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_quat_list keys=ufbx_find_baked_node(b,node)->rotation_keys;
    double bp=0;for(size_t k=1;k<keys.count;k++){double dot=ufbx_quat_dot(keys.data[k-1].value,keys.data[k].value);bp+=2*acos(fmin(fabs(dot),1.0));}
    printf("baked key path=%.5f (%.3f deg) over %zu keys\n",bp,bp*180/M_PI,keys.count);
    return 0;}
