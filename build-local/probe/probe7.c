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
    ufbx_node *node = ufbx_find_node(scene, argv[2]);
    ufbx_anim *anim = scene->anim;
    double t0 = anim->time_begin, t1 = anim->time_end;
    printf("anim %.4f..%.4f node='%s'\n", t0, t1, node->name.data);

    // Dense samples: report angular velocity per step (deg per second)
    int N = 96;
    double prev_t = t0;
    ufbx_quat prev_q = ufbx_evaluate_transform(anim, node, t0).rotation;
    int hold_runs = 0;
    double hold_begin = 0;
    for (int i = 1; i <= N; i++) {
        double t = t0 + (t1-t0) * (double)i / N;
        ufbx_quat q = ufbx_evaluate_transform(anim, node, t).rotation;
        double a = qang(prev_q, q);
        double rate = a / (t - prev_t) * 180.0 / M_PI;
        if (rate < 1.0) {
            if (hold_runs == 0 || hold_begin < 0) { hold_begin = prev_t; hold_runs++; }
        } else {
            if (hold_begin >= 0) {
                printf("hold-ish interval: %.4f .. %.4f (%.3fs), next rate=%.1f deg/s\n",
                    hold_begin, prev_t, prev_t - hold_begin, rate);
                hold_begin = -1;
            }
        }
        prev_q = q; prev_t = t;
    }
    if (hold_begin >= 0) printf("hold-ish tail: %.4f .. %.4f\n", hold_begin, t1);

    // Find source curve keyframes affecting this node
    ufbx_anim_layer *layer = anim->layers.data[0];
    for (size_t pi = 0; pi < layer->anim_props.count; pi++) {
        ufbx_anim_prop *p = &layer->anim_props.data[pi];
        if (p->element != (ufbx_element*)node) continue;
        printf("prop '%s':\n", p->prop_name.data);
        for (int c = 0; c < 3; c++) {
            ufbx_anim_curve *cv = p->anim_value->curves[c];
            if (!cv) continue;
            printf("  dim%d %zu keys:", c, cv->keyframes.count);
            size_t n = cv->keyframes.count < 10 ? cv->keyframes.count : 10;
            for (size_t k = 0; k < n; k++) printf(" [%.4f=%.2f i%d]", cv->keyframes.data[k].time, cv->keyframes.data[k].value, cv->keyframes.data[k].interpolation);
            if (cv->keyframes.count > 10) printf(" ...");
            printf("\n");
        }
    }
    ufbx_free_scene(scene);
    return 0;
}
