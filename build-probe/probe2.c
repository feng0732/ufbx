#include "ufbx.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    ufbx_load_opts opts = {0};
    opts.retain_dom = true;
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], &opts, &error);
    if (!scene) { printf("LOAD FAILED: %s\n", error.description.data); return 1; }
    ufbx_material *mat = scene->materials.data[0];
    printf("fbx.diffuse_color.texture = %p\n", (void*)mat->fbx.diffuse_color.texture);
    for (size_t t = 0; t < mat->textures.count; t++)
        printf("  textures[%zu] prop=%s tex=%p\n", t,
            mat->textures.data[t].material_prop.data, (void*)mat->textures.data[t].texture);

    /* evaluate scene and compare layers */
    ufbx_scene *ev = ufbx_evaluate_scene(scene, NULL, 0.0, NULL, &error);
    if (!ev) { printf("EVAL FAILED: %s\n", error.description.data); return 1; }
    ufbx_material *emat = ev->materials.data[0];
    ufbx_texture *et = emat->fbx.diffuse_color.texture;
    printf("evaluated diffuse texture type=%d layers=%zu\n", (int)et->type, et->layers.count);
    for (size_t i = 0; i < et->layers.count; i++)
        printf("  eval layer[%zu] blend=%d alpha=%.3f\n", i,
            (int)et->layers.data[i].blend_mode, (double)et->layers.data[i].alpha);

    /* raw DOM props on layered texture */
    for (size_t i = 0; i < scene->textures.count; i++) {
        ufbx_texture *tex = scene->textures.data[i];
        if (tex->type != UFBX_TEXTURE_LAYERED) continue;
        printf("layered texture %s props=%zu dom? %d\n", tex->name.data, tex->props.props.count, (int)(tex->element.dom_node != NULL));
        for (size_t p = 0; p < tex->props.props.count; p++)
            printf("  prop %s type=%d\n", tex->props.props.data[p].name.data, (int)tex->props.props.data[p].type);
        if (tex->element.dom_node) {
            ufbx_dom_node *n = tex->element.dom_node;
            for (size_t c = 0; c < n->children.count; c++)
                printf("  dom child: %s\n", n->children.data[c]->name.data);
        }
    }
    ufbx_free_scene(ev);
    ufbx_free_scene(scene);
    return 0;
}
