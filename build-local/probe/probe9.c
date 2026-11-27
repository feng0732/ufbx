#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>
static double qang(ufbx_quat a, ufbx_quat b){double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0)d=-d;if(d>1)d=1;return 2*acos(d);}
int main(int argc,char**argv){
    ufbx_error error;
    ufbx_scene*scene=ufbx_load_file(argv[1],NULL,&error);
    ufbx_node*node=ufbx_find_node(scene,argv[2]);
    ufbx_anim*anim=scene->anim;
    double t0=anim->time_begin,t1=anim->time_end;
    // hold: source is constant from f18 (0.75) to end (0.7917)
    double h0=0.75,h1=t1;
    ufbx_quat qh=ufbx_evaluate_transform(anim,node,h0).rotation;
    double holdvar=0;
    for(int i=0;i<=20;i++){double t=h0+(h1-h0)*i/20;ufbx_quat q=ufbx_evaluate_transform(anim,node,t).rotation;holdvar=fmax(holdvar,qang(qh,q));}
    printf("source hold variation 0.75..end: %.6f deg\n",holdvar*180/M_PI);
    // bake default, check baked playback across hold region
    ufbx_bake_opts o={0};
    ufbx_baked_anim*bake=ufbx_bake_anim(scene,anim,&o,&error);
    ufbx_baked_node*bn=ufbx_find_baked_node(bake,node);
    double bakehold=0;
    for(int i=0;i<=20;i++){double t=h0+(h1-h0)*i/20;ufbx_quat q=ufbx_evaluate_baked_quat(bn->rotation_keys,t);bakehold=fmax(bakehold,qang(qh,q));}
    printf("baked hold variation 0.75..end: %.6f deg, keys in hold=%zu\n",bakehold*180/M_PI,
        (size_t)0);
    // overall source path
    double path=0;int N=200;ufbx_quat prev=ufbx_evaluate_transform(anim,node,t0).rotation;
    for(int i=1;i<=N;i++){double t=t0+(t1-t0)*i/N;ufbx_quat q=ufbx_evaluate_transform(anim,node,t).rotation;path+=qang(prev,q);prev=q;}
    printf("source dense path=%.2f deg\n",path*180/M_PI);
    // stored-key path
    double kp=0;for(size_t k=1;k<bn->rotation_keys.count;k++){double dot=ufbx_quat_dot(bn->rotation_keys.data[k-1].value,bn->rotation_keys.data[k].value);kp+=2*acos(fmin(fabs(dot),1.0));}
    printf("baked stored-key path=%.2f deg over %zu keys\n",kp*180/M_PI,bn->rotation_keys.count);
    ufbx_free_baked_anim(bake);ufbx_free_scene(scene);return 0;
}
