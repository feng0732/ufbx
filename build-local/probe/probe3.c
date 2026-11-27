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

int main(int argc, char **argv)
{
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], NULL, &error);
    if (!scene) { printf("LOAD FAIL\n"); return 1; }
    ufbx_node *node = scene->nodes.data[scene->nodes.count - 1];
    ufbx_anim *anim = scene->anim;
    double t0 = anim->time_begin, t1 = anim->time_end;

    ufbx_bake_opts opts = { 0 };
    ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
    ufbx_baked_node *bn = ufbx_find_baked_node(bake, node);
    printf("bake playback: %.4f..%.4f keytime %.4f..%.4f rotkeys=%zu\n",
        bake->playback_time_begin, bake->playback_time_end,
        bake->key_time_min, bake->key_time_max, bn->rotation_keys.count);

    // Compare source vs baked playback over dense samples incl. mid times
    double max_key_err = 0, max_mid_err = 0;
    int N = 200;
    for (int i = 0; i <= N; i++) {
        double t = t0 + (t1 - t0) * (double)i / N;
        ufbx_quat src = ufbx_evaluate_transform(anim, node, t).rotation;
        ufbx_quat bk = ufbx_evaluate_baked_quat(bn->rotation_keys, t);
        double e = qang(src, bk);
        // decide key vs mid: distance to nearest baked key time
        double nearest = 1e9;
        for (size_t k = 0; k < bn->rotation_keys.count; k++) {
            double d = fabs(bn->rotation_keys.data[k].time - t);
            if (d < nearest) nearest = d;
        }
        if (nearest < 1e-6) max_key_err = fmax(max_key_err, e);
        else max_mid_err = fmax(max_mid_err, e);
    }
    printf("max angular err: key=%.5f rad (%.4f deg) mid=%.5f rad (%.4f deg)\n",
        max_key_err, max_key_err*180/M_PI, max_mid_err, max_mid_err*180/M_PI);

    // Sign coherence of stored keys
    int neg = 0;
    double worst_dot = 1;
    for (size_t k = 1; k < bn->rotation_keys.count; k++) {
        double dot = ufbx_quat_dot(bn->rotation_keys.data[k-1].value, bn->rotation_keys.data[k].value);
        if (dot < 0) neg++;
        if (dot < worst_dot) worst_dot = dot;
    }
    printf("stored-key negative dots=%d worst_dot=%.4f\n", neg, worst_dot);

    // Unwrapped path via stored keys (coherent signs)
    double path = 0;
    for (size_t k = 1; k < bn->rotation_keys.count; k++) {
        double dot = ufbx_quat_dot(bn->rotation_keys.data[k-1].value, bn->rotation_keys.data[k].value);
        double cc = fmin(fabs(dot), 1.0);
        path += 2.0 * acos(cc);
    }
    printf("stored-key path=%.1f deg\n", path*180/M_PI);
    ufbx_free_baked_anim(bake);
    ufbx_free_scene(scene);
    return 0;
}
