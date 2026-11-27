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
    ufbx_node *node = scene->nodes.data[scene->nodes.count - 1];
    ufbx_anim *anim = scene->anim;

    ufbx_bake_opts opts = { 0 };
    ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
    ufbx_baked_node *bn = ufbx_find_baked_node(bake, node);

    printf("first 12 baked rot keys:\n");
    for (size_t k = 0; k < bn->rotation_keys.count && k < 12; k++) {
        ufbx_baked_quat *kf = &bn->rotation_keys.data[k];
        ufbx_vec3 e = ufbx_quat_to_euler(kf->value, UFBX_ROTATION_ORDER_XYZ);
        printf("  t=%.4f euler=(%.2f,%.2f,%.2f) flags=0x%x\n", kf->time, e.x, e.y, e.z, kf->flags);
    }
    printf("last 4 baked rot keys:\n");
    for (size_t k = bn->rotation_keys.count - 4; k < bn->rotation_keys.count; k++) {
        ufbx_baked_quat *kf = &bn->rotation_keys.data[k];
        ufbx_vec3 e = ufbx_quat_to_euler(kf->value, UFBX_ROTATION_ORDER_XYZ);
        printf("  t=%.4f euler=(%.2f,%.2f,%.2f) flags=0x%x\n", kf->time, e.x, e.y, e.z, kf->flags);
    }

    // Sample source vs bake at fine intervals, print worst 8
    double t0 = anim->time_begin, t1 = anim->time_end;
    double worst[8] = {0}; double worst_t[8] = {0};
    int N = 400;
    for (int i = 0; i <= N; i++) {
        double t = t0 + (t1 - t0) * (double)i / N;
        ufbx_quat src = ufbx_evaluate_transform(anim, node, t).rotation;
        ufbx_quat bk = ufbx_evaluate_baked_quat(bn->rotation_keys, t);
        double e = qang(src, bk);
        for (int j = 0; j < 8; j++) {
            if (e > worst[j]) {
                for (int m = 7; m > j; m--) { worst[m] = worst[m-1]; worst_t[m] = worst_t[m-1]; }
                worst[j] = e; worst_t[j] = t;
                break;
            }
        }
    }
    printf("worst sample errors:\n");
    for (int j = 0; j < 8; j++) printf("  t=%.4f err=%.3f deg\n", worst_t[j], worst_t[j] == 0 && worst[j] == 0 ? 0 : worst[j]*180/M_PI);
    ufbx_free_baked_anim(bake);
    ufbx_free_scene(scene);
    return 0;
}
