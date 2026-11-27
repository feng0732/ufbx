#include "../../ufbx.h"
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv)
{
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], NULL, &error);
    if (!scene) { printf("LOAD FAIL\n"); return 1; }
    ufbx_anim *anim = scene->anim;
    printf("time_begin=%.4f time_end=%.4f num layers=%zu\n", anim->time_begin, anim->time_end, anim->layers.count);

    for (size_t li = 0; li < anim->layers.count; li++) {
        ufbx_anim_layer *layer = anim->layers.data[li];
        printf("layer %zu '%s' props=%zu\n", li, layer->element.name.data, layer->anim_props.count);
        for (size_t pi = 0; pi < layer->anim_props.count; pi++) {
            ufbx_anim_prop *prop = &layer->anim_props.data[pi];
            ufbx_anim_value *val = prop->anim_value;
            printf("  prop '%s'\n", prop->prop_name.data);
            for (size_t ci = 0; ci < 3; ci++) {
                ufbx_anim_curve *curve = val->curves[ci];
                if (!curve) continue;
                printf("    dim%zu keys=%zu pre=%d post=%d: ", ci, curve->keyframes.count,
                    curve->pre_extrapolation.mode, curve->post_extrapolation.mode);
                size_t show = curve->keyframes.count < 16 ? curve->keyframes.count : 16;
                for (size_t k = 0; k < show; k++) {
                    ufbx_keyframe kf = curve->keyframes.data[k];
                    printf("[%.3f=%.1f i%d]", kf.time, kf.value, (int)kf.interpolation);
                }
                if (curve->keyframes.count > 16) printf(" ...");
                printf("\n");
            }
        }
    }
    ufbx_free_scene(scene);
    return 0;
}
