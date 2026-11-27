#include "ufbx.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    ufbx_load_opts opts = {0}; opts.retain_dom = true;
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], &opts, &error);
    if (!scene) { printf("FAIL %s\n", error.description.data); return 1; }
    for (size_t i = 0; i < scene->textures.count; i++) {
        ufbx_texture *tex = scene->textures.data[i];
        if (tex->type != UFBX_TEXTURE_LAYERED) continue;
        printf("LAYERED %s layers=%zu\n", tex->name.data, tex->layers.count);
        for (size_t l = 0; l < tex->layers.count; l++)
            printf("  layer[%zu] blend=%d alpha=%.3f tex=%s\n", l,
                (int)tex->layers.data[l].blend_mode, (double)tex->layers.data[l].alpha,
                tex->layers.data[l].texture->name.data);
        if (tex->element.dom_node) {
            for (size_t c = 0; c < tex->element.dom_node->children.count; c++) {
                ufbx_dom_node *ch = tex->element.dom_node->children.data[c];
                printf("  DOM node '%s' is_array=%d size=%zu\n",
                    ch->name.data, (int)ufbx_dom_is_array(ch), ufbx_dom_array_size(ch));
                for (size_t v = 0; v < ch->values.count; v++) {
                    ufbx_dom_value *val = &ch->values.data[v];
                    printf("    value type=%d blob=%zu\n", (int)val->type, val->value_blob.size);
                    if (val->type == UFBX_DOM_VALUE_ARRAY_I32) {
                        int32_t *p = (int32_t*)val->value_blob.data;
                        size_t n = val->value_blob.size / 4;
                        for (size_t k = 0; k < n && k < 20; k++) printf("      i32[%zu]=%d\n", k, p[k]);
                    } else if (val->type == UFBX_DOM_VALUE_ARRAY_F64 || val->type == UFBX_DOM_VALUE_ARRAY_F32) {
                        size_t es = val->type == UFBX_DOM_VALUE_ARRAY_F64 ? 8 : 4;
                        size_t n = val->value_blob.size / es;
                        for (size_t k = 0; k < n && k < 20; k++)
                            printf("      f[%zu]=%.3f\n", k, es == 8 ? (double)((double*)val->value_blob.data)[k]
                                                                     : (double)((float*)val->value_blob.data)[k]);
                    }
                }
            }
        }
    }
    ufbx_free_scene(scene);
    return 0;
}
