#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>

static double quat_angle_between(ufbx_quat a, ufbx_quat b)
{
    double d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (d < 0) d = -d;
    if (d > 1.0) d = 1.0;
    return 2.0 * acos(d);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], NULL, &error);
    if (!scene) { printf("LOAD FAIL\n"); return 1; }

    printf("file=%s nodes=%zu anim=%p\n", argv[1], scene->nodes.count, (void*)scene->anim);
    if (!scene->anim) return 0;

    double t0 = scene->anim->time_begin;
    double t1 = scene->anim->time_end;
    printf("time: %.4f .. %.4f (dur %.4f)\n", t0, t1, t1-t0);

    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        ufbx_node *node = scene->nodes.data[ni];
        // Sample source rotation densely
        enum { N = 48 };
        ufbx_quat src[N+1];
        double max_step = 0;
        for (int i = 0; i <= N; i++) {
            double t = t0 + (t1-t0) * (double)i / N;
            ufbx_transform tr = ufbx_evaluate_transform(scene->anim, node, t);
            src[i] = tr.rotation;
            if (i > 0) {
                double a = quat_angle_between(src[i-1], src[i]);
                if (a > max_step) max_step = a;
            }
        }
        // Total accumulated path length (using shortest-arc each step)
        double path = 0;
        for (int i = 1; i <= N; i++) path += quat_angle_between(src[i-1], src[i]);

        ufbx_vec3 e0 = ufbx_quat_to_euler(src[0], UFBX_ROTATION_ORDER_XYZ);
        ufbx_vec3 eN = ufbx_quat_to_euler(src[N], UFBX_ROTATION_ORDER_XYZ);
        double chord = quat_angle_between(src[0], src[N]);
        printf("node[%zu] '%s': path=%.1f deg chord=%.1f maxstep=%.1f euler0=(%.1f,%.1f,%.1f) eulerN=(%.1f,%.1f,%.1f)\n",
            ni, node->name.data, path*180.0/M_PI, chord*180.0/M_PI, max_step*180.0/M_PI,
            e0.x, e0.y, e0.z, eN.x, eN.y, eN.z);

        // Bake with defaults and inspect quat keys
        ufbx_bake_opts opts = { 0 };
        ufbx_baked_anim *bake = ufbx_bake_anim(scene, scene->anim, &opts, &error);
        if (!bake) { printf("  BAKE FAIL\n"); continue; }
        ufbx_baked_node *bn = ufbx_find_baked_node(bake, node);
        if (!bn) { printf("  no baked node\n"); ufbx_free_baked_anim(bake); continue; }
        printf("  baked rot keys=%zu constant=%d\n", bn->rotation_keys.count, bn->constant_rotation);
        int num_neg = 0;
        for (size_t k = 1; k < bn->rotation_keys.count; k++) {
            double dot = ufbx_quat_dot(bn->rotation_keys.data[k-1].value, bn->rotation_keys.data[k].value);
            if (dot < 0) num_neg++;
        }
        printf("  negative-dot adjacent pairs: %d\n", num_neg);
        ufbx_free_baked_anim(bake);
    }
    ufbx_free_scene(scene);
    return 0;
}
