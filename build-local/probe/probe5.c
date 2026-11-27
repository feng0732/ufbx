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
    ufbx_anim *anim = scene->anim;
    double t0 = anim->time_begin, t1 = anim->time_end;
    enum { N = 96 };

    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        ufbx_node *node = scene->nodes.data[ni];
        ufbx_quat q[N+1];
        double path = 0, maxstep = 0;
        int hold_samples = 0;
        for (int i = 0; i <= N; i++) {
            double t = t0 + (t1-t0) * (double)i / N;
            q[i] = ufbx_evaluate_transform(anim, node, t).rotation;
            if (i > 0) {
                double a = qang(q[i-1], q[i]);
                path += a;
                if (a > maxstep) maxstep = a;
                if (a < 1e-4) hold_samples++;
            }
        }
        if (path < 0.05) continue;
        // longest run of near-constant samples
        int longest = 0, cur = 0;
        for (int i = 1; i <= N; i++) {
            double a = qang(q[i-1], q[i]);
            if (a < 1e-4) { cur++; if (cur > longest) longest = cur; }
            else cur = 0;
        }
        double hold_time = (double)longest / N * (t1 - t0);
        printf("node '%s': path=%.1f deg maxstep=%.1f longest_hold=%.3fs (%.1f%% of anim)\n",
            node->name.data, path*180/M_PI, maxstep*180/M_PI, hold_time, 100.0*longest/N);
    }
    ufbx_free_scene(scene);
    return 0;
}
