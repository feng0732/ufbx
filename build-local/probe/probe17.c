#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a,ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int c,char**v){
    ufbx_error e;ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim_stack*st=ufbx_find_anim_stack(s,"Spin");if(!st)st=ufbx_find_anim_stack(s,"Skeleton|Spin");
    ufbx_anim*an=st->anim;ufbx_node*node=ufbx_find_node(s,"Bottom");
    ufbx_bake_opts o={0};
    ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
    ufbx_baked_quat_list keys=ufbx_find_baked_node(b,node)->rotation_keys;
    size_t fx=keys.count/2;
    double mt=0.5*(keys.data[fx-1].time+keys.data[fx].time);
    ufbx_quat src=ufbx_evaluate_transform(an,node,mt).rotation;
    ufbx_quat good=ufbx_evaluate_baked_quat(keys,mt);
    printf("before flip mid err=%.6f deg\n",qang(src,good)*180/M_PI);
    keys.data[fx].value.x=-keys.data[fx].value.x;
    keys.data[fx].value.y=-keys.data[fx].value.y;
    keys.data[fx].value.z=-keys.data[fx].value.z;
    keys.data[fx].value.w=-keys.data[fx].value.w;
    ufbx_quat bad=ufbx_evaluate_baked_quat(keys,mt);
    printf("after  flip mid err=%.6f deg\n",qang(src,bad)*180/M_PI);
    // signed vs abs path
    double sp=0,ap=0;
    for(size_t k=1;k<keys.count;k++){
        double dot=ufbx_quat_dot(keys.data[k-1].value,keys.data[k].value);
        sp+=acos(fmin(fmax(dot,-1.0),1.0));
        ap+=acos(fmin(fmax(fabs(dot),-1.0),1.0));
    }
    printf("signed acos-path=%.4f abs acos-path=%.4f\n",sp,ap);
    return 0;}
