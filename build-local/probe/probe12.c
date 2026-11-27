#include "../../ufbx.h"
#include <stdio.h>
int main(int c,char**v){
    ufbx_error e;
    ufbx_scene*s=ufbx_load_file(v[1],NULL,&e);
    ufbx_anim*an=s->anim;
    printf("nodes=%zu time=%.4f..%.4f stacks=%zu\n",s->nodes.count,an->time_begin,an->time_end,s->anim_stacks.count);
    for(size_t i=0;i<s->anim_stacks.count;i++){
        ufbx_anim_stack*st=s->anim_stacks.data[i];
        printf(" stack '%s' time=%.4f..%.4f anim=%p\n",st->element.name.data,st->time_begin,st->time_end,(void*)st->anim);
    }
    for(size_t n=0;n<s->nodes.count;n++){
        ufbx_node*nd=s->nodes.data[n];
        if(!strcmp(nd->name.data,"Bottom")){
            ufbx_bake_opts o={0};
            ufbx_baked_anim*b=ufbx_bake_anim(s,an,&o,&e);
            ufbx_baked_node*bn=ufbx_find_baked_node(b,nd);
            printf("Bottom baked rot keys=%zu times:",bn?bn->rotation_keys.count:0);
            if(bn)for(size_t k=0;k<bn->rotation_keys.count && k<6;k++)printf(" %.4f",bn->rotation_keys.data[k].time);
            printf("\n");
            ufbx_free_baked_anim(b);
        }
    }
    return 0;}
