#include "../../ufbx.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], NULL, &error);
    ufbx_node *node = ufbx_find_node(scene, argv[2]);
    ufbx_anim_layer *layer = scene->anim->layers.data[0];
    for (size_t pi = 0; pi < layer->anim_props.count; pi++) {
        ufbx_anim_prop *p = &layer->anim_props.data[pi];
        if (p->element != (ufbx_element*)node) continue;
        if (strcmp(p->prop_name.data, "Lcl Rotation")) continue;
        ufbx_anim_curve *cv = p->anim_value->curves[0];
        printf("rot X all %zu keys:\n", cv->keyframes.count);
        for (size_t k = 0; k < cv->keyframes.count; k++)
            printf("  f%5.2f t=%7.4f v=%8.3f interp=%d\n", cv->keyframes.data[k].time*24.0,
                cv->keyframes.data[k].time, cv->keyframes.data[k].value, cv->keyframes.data[k].interpolation);
    }
    ufbx_free_scene(scene);
    return 0;
}
