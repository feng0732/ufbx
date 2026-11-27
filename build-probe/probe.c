#include "ufbx.h"
#include <stdio.h>

static void probe(const char *path)
{
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(path, NULL, &error);
    printf("== %s\n", path);
    if (!scene) { printf("  LOAD FAILED: %s\n", error.description.data); return; }
    printf("  version=%u\n", scene->metadata.version);
    for (size_t m = 0; m < scene->materials.count; m++) {
        ufbx_material *mat = scene->materials.data[m];
        printf("  material %s: textures=%zu\n", mat->name.data, mat->textures.count);
        for (size_t t = 0; t < mat->textures.count; t++) {
            ufbx_texture *tex = mat->textures.data[t].texture;
            printf("    prop=%s type=%d layers=%zu file_textures=%zu\n",
                mat->textures.data[t].material_prop.data, (int)tex->type, tex->layers.count, tex->file_textures.count);
            for (size_t l = 0; l < tex->layers.count; l++) {
                ufbx_texture_layer *L = &tex->layers.data[l];
                printf("      layer[%zu] blend=%d alpha=%.4f tex=%s type=%d\n",
                    l, (int)L->blend_mode, (double)L->alpha,
                    L->texture->relative_filename.data, (int)L->texture->type);
            }
        }
    }
    ufbx_free_scene(scene);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) probe(argv[i]);
    return 0;
}
