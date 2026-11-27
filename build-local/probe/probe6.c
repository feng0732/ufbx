#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>

static double qang(ufbx_quat a, ufbx_quat b)
{
    double d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (d < 0) d = -d;
    if (d > 1.0) d = 1.0;
    return 2.0 * acos(d);
}

static void run(ufbx_scene *scene, ufbx_node *node, ufbx_bake_opts *opts, const char *label)
{
    ufbx_error error;
    ufbx_anim *anim = scene->anim;
    ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, opts, &error);
    if (!bake) { printf("%s: BAKE FAIL\n", label); return; }
    ufbx_baked_node *bn = ufbx_find_baked_node(bake, node);
    double t0 = anim->time_begin, t1 = anim->time_end;

    int neg = 0;
    double worst_dot = 1.0;
    for (size_t k = 1; k < bn->rotation_keys.count; k++) {
        double dot = ufbx_quat_dot(bn->rotation_keys.data[k-1].value, bn->rotation_keys.data[k].value);
        if (dot < 0) neg++;
        if (dot < worst_dot) worst_dot = dot;
    }

    double max_key = 0, max_mid = 0;
    int N = 300;
    for (int i = 0; i <= N; i++) {
        double t = t0 + (t1-t0) * (double)i / N;
        ufbx_quat src = ufbx_evaluate_transform(anim, node, t).rotation;
        ufbx_quat bk = ufbx_evaluate_baked_quat(bn->rotation_keys, t);
        double e = qang(src, bk);
        double nearest = 1e9;
        for (size_t k = 0; k < bn->rotation_keys.count; k++) {
            double d = fabs(bn->rotation_keys.data[k].time - t);
            if (d < nearest) nearest = d;
        }
        if (nearest < 1e-7) max_key = fmax(max_key, e); else max_mid = fmax(max_mid, e);
    }
    printf("%-34s keys=%3zu neg=%d worstdot=%.4f keyerr=%.5f miderr=%.5f rad (%.4f deg)\n",
        label, bn->rotation_keys.count, neg, worst_dot, max_key, max_mid, max_mid*180/M_PI);
    ufbx_free_baked_anim(bake);
}

int main(int argc, char **argv)
{
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], NULL, &error);
    if (!scene) { printf("LOAD FAIL\n"); return 1; }
    ufbx_node *node = ufbx_find_node(scene, argc > 2 ? argv[2] : "Bottom");
    if (!node && argc <= 2) node = scene->nodes.data[scene->nodes.count - 1];
    printf("node='%s'\n", node ? node->name.data : "(null)");

    ufbx_bake_opts o1 = { 0 };
    run(scene, node, &o1, "default30");
    ufbx_bake_opts o2 = { 0 }; o2.resample_rate = 60.0;
    run(scene, node, &o2, "rate60");
    ufbx_bake_opts o3 = { 0 }; o3.resample_rate = 15.0;
    run(scene, node, &o3, "rate15");
    ufbx_bake_opts o4 = { 0 }; o4.resample_rate = 24.0; o4.key_reduction_enabled = true; o4.key_reduction_rotation = true;
    run(scene, node, &o4, "rate24_reduce_rot");
    ufbx_bake_opts o5 = { 0 }; o5.resample_rate = 24.0; o4.key_reduction_enabled = true; o5.key_reduction_enabled = true;
    run(scene, node, &o5, "rate24_reduce_norot");
    ufbx_bake_opts o6 = { 0 }; o6.no_resample_rotation = true;
    run(scene, node, &o6, "no_resample_rotation");
    ufbx_bake_opts o7 = { 0 }; o7.trim_start_time = true;
    run(scene, node, &o7, "trim_start");
    ufbx_free_scene(scene);
    return 0;
}
