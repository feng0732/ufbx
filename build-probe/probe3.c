#include "ufbx.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    ufbx_load_opts opts = {0};
    opts.retain_dom = true;
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(argv[1], &opts, &error);
    if (!scene) { printf("LOAD FAILED: %s\n", error.description.data); return 1; }
    for (size_t i = 0; i < scene->textures.count; i++) {
        ufbx_texture *tex = scene->textures.data[i];
        printf("texture[%zu] %s type=%d layers=%zu\n", i, tex->name.data, (int)tex->type, tex->layers.count);
        printf("  conns_dst: %zu conns_src: %zu\n", tex->element.connections_dst.count, tex->element.connections_src.count);
        for (size_t c = 0; c < tex->element.connections_dst.count; c++) {
            ufbx_connection *cn = &tex->element.connections_dst.data[c];
            printf("    dst <- src=%s (type=%d) prop='%s'\n",
                cn->src->name.data, (int)cn->src->type, cn->dst_prop.data);
        }
        for (size_t c = 0; c < tex->element.connections_src.count; c++) {
            ufbx_connection *cn = &tex->element.connections_src.data[c];
            printf("    src -> dst=%s (type=%d) prop='%s'\n",
                cn->dst->name.data, (int)cn->dst->type, cn->dst_prop.data);
        }
        if (tex->element.dom_node) {
            ufbx_dom_node *n = tex->element.dom_node;
            for (size_t c = 0; c < n->children.count; c++) {
                ufbx_dom_node *ch = n->children.data[c];
                printf("  DOM child '%s' values=%zu\n", ch->name.data, ch->values.count);
                for (size_t v = 0; v < ch->values.count; v++)
                    printf("     value[%zu] type=%d\n", v, (int)ch->values.data[v].type);
            }
        }
    }
    ufbx_free_scene(scene);
    return 0;
}
