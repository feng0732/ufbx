# FBX 分层材质在 ufbx 便捷贴图视图中的语义保真分析

- 分析对象：本仓库 `ufbx.h` / `ufbx.c`（当前 `task/155` 分支，基线提交 `5955c5c`）
- 素材依据：`data/maya_texture_layers_{6100,7500}_ascii.fbx`、`data/maya_texture_blend_modes_{6100,7500}_ascii.fbx`
- 验证方式：源码走读 + 用当前源码编译的独立探针（`build-probe/probe*.c`）实际加载上述四个文件 + 仓库测试套件（`test/runner.c`，658/658 通过）
- 结论一句话：**在 ≥7000 版 FBX 上，`ufbx_texture.layers[]` 完整保留了有序层、每层混合方式与权重，但没有任何合成实现；只消费 `material->fbx/pbr.*.texture` 这类便捷贴图视图无法还原分层外观。在 <7000 旧版上，便捷视图甚至不引用分层对象，需要回到连接关系或 DOM 才能保持来源关系。**

---

## 1. 数据模型：库提供了哪些"贴图视图"

### 1.1 便捷贴图视图（每属性一个指针）

- `ufbx_material.fbx`（`ufbx_material_fbx_maps`，19 个 `ufbx_material_map`，如 `diffuse_color`、`normal_map`）和 `ufbx_material.pbr`（`ufbx_material_pbr_maps`，50+ 个 map）。每个 `ufbx_material_map` 只含**一个** `texture` 指针（`ufbx.h:2303`），外加常量值、`texture_enabled`、`feature_disabled`。
- 该指针由 `ufbxi_fetch_mapping_maps()` 调用 `ufbx_find_prop_texture_len()` 填充（`ufbx.c:20078-20084`、`31422-31431`）。后者在按属性名排序的 `material->textures[]` 上做二分查找，**一个属性名只返回第一个匹配的贴图**。
- `material->textures[]`（`ufbx_material_texture_list`，按 `material_prop` 排序，`ufbx.h:2670`）是属性→贴图的完整连接列表，但同样只是"哪个属性连了哪个 `ufbx_texture`"，不含层内混合语义。
- 若该属性连接的是分层纹理，便捷指针指向的是 `type == UFBX_TEXTURE_LAYERED` 的**容器对象**，混合语义必须继续下钻到 `texture->layers[]` 才能拿到。

### 1.2 分层视图（完整语义所在）

`ufbx_texture`（`ufbx.h:2890-2959`）在 `type == UFBX_TEXTURE_LAYERED` 时提供：

```c
// ufbx.h:2935-2936
// LAYERED: Inner texture layers, ordered from _bottom_ to _top_
ufbx_texture_layer_list layers;
```

每一层（`ufbx.h:2749-2753`）：

```c
typedef struct ufbx_texture_layer {
    ufbx_texture *texture;      // 内层纹理，永不 NULL
    ufbx_blend_mode blend_mode; // 该层合成到背景的方程
    ufbx_real alpha;            // 该层混合权重
} ufbx_texture_layer;
```

另有 `ufbx_texture.file_textures[]`：该纹理（递归）引用到的去重后的**文件纹理扁平列表**（`ufbx.h:2943-2945`），只回答"用到哪些图片文件"，**不保留层序、混合方式和权重**。

### 1.3 混合方式枚举

`ufbx_blend_mode`（`ufbx.h:2700-2734`）共 31 个枚举值（`UFBX_BLEND_TRANSLUCENT=0` … `UFBX_BLEND_OVERLAY=30`，已用编译探针确认），头注释只给出数学方程（如 `OVER = src*src_alpha + dst*(1-src_alpha)`），**库内没有任何一处实现这些方程**（全库无 composite/blend 像素运算代码，`grep` 可证）。这些枚举是给调用方的着色器/合成器准备的描述数据。

---

## 2. 材质连接如何成为有次序的纹理层（导入期，≥7000 版）

以 `data/maya_texture_layers_7500_ascii.fbx` 为实证。FBX 中的原始结构：

```
LayeredTexture: 2109835829776, "layeredTexture1" {
    BlendModes: 2,4,1          // 节点级数组（int32）
    Alphas: 0.75,0.5,1         // 节点级数组（double）
}
C: "OP", 2109835829776, <lambert2>, "DiffuseColor"   ; 分层容器 → 材质属性
C: "OO", 2111522587648, 2109835829776   ; file3 → 容器
C: "OO", 2111522588128, 2109835829776   ; file2 → 容器
C: "OO", 2111522582848, 2109835829776   ; file1 → 容器
```

每个内层 `Texture` 自身还带属性 `Texture alpha`（如 0.75、0.5）和 `CurrentTextureBlendMode`（如 2、4）。

### 2.1 层的成员与次序：完全来自 OO 连接

1. 解析期 `ufbxi_read_layered_texture()`（`ufbx.c:14575-14602`）只把节点级 `Alphas`/`BlendModes` 两个数组存进内部 `ufbxi_texture_extra`（`ufbx.c:6338-6345`），此时 `layers` 为空。
2. 后处理期对每个 `UFBX_TEXTURE_LAYERED` 调 `ufbxi_fetch_texture_layers()`（`ufbx.c:19240-19261`、调用点 `22495-22496`）：遍历 `ufbxi_find_dst_connections(layered, NULL)`，凡 `src` 是纹理就生成一个 `ufbx_texture_layer`。
3. 连接在 `ufbxi_resolve_connections()` 中经**稳定排序**（`ufbxi_cmp_connection_less`，`ufbx.c:18637-18652`），排序键是元素指针地址、再按属性名字典序。因此 `layers[]` 的次序 = **文件中 OO 连接的出现次序**（稳定排序保持同键元素的原始顺序），头文件据此承诺"bottom → top"（`ufbx.h:2935`）。
4. 探针实测 7500 素材：`layers[0..2]` 依次为 file3、file2、file1，与文件中三条 OO 连接顺序一致。

> 含义：层序不是来自 `Alphas`/`BlendModes` 数组的位置，而是来自连接流。两个节点级数组是**按同一连接次序并行索引**的补丁表（见 2.3）。

### 2.2 每层权重与混合方式的初始值：一个名义属性 + 固定缺省

`ufbxi_fetch_texture_layers()` 对每个内层纹理读取（`ufbx.c:19249-19250`）：

```c
layer.alpha      = ufbxi_find_real (&texture->props, "Texture alpha", 1.0);
layer.blend_mode = ufbxi_find_enum (&texture->props, "BlendMode",
                                    UFBX_BLEND_REPLACE /*缺省=5*/,
                                    UFBX_BLEND_OVERLAY /*上限=30*/);
```

**但对 Maya 导出的 FBX，这两个内层属性查找在实际数据上都不成立**，经插桩实测（在 `ufbxi_fetch_texture_layers` 内打印查找结果，四个素材全部验证）：

- Maya 在内层纹理上写的属性名是 **`CurrentTextureBlendMode`**（7500/6100 素材皆然），不是代码查找的 `BlendMode`；而 `BlendMode` 作为属性名只出现在 3ds Max 等其他对象的模板里，`FbxFileTexture`/`FbxLayeredTexture` 模板（7500 素材 169-210 行）都不含它。因此 `ufbxi_find_enum(... "BlendMode" ...)` 恒返回缺省，**所有层的 `blend_mode` 初始都是 `UFBX_BLEND_REPLACE(5)`**。
- 权重路径相反，`Texture alpha` 是真实属性名，能查到（如 file3=0.75、file2=0.5），查不到时缺省 `1.0`（如 file1 无该属性即得 1.0）。
- `ufbxi_find_enum()` 的越界保护：属性值超出 `[0,30]` 时回退缺省值（`ufbx.c:11547-11560`）。
- 即：对 Maya 素材，内层 `CurrentTextureBlendMode` 数值（0/4/1 等）在结构化分层结果中**不直接生效**，真正决定混合方式的是下一节的节点级 `BlendModes` 数组。`CurrentTextureBlendMode` 仍保留在 `texture->props` 里，调用方可自行读回。

### 2.3 节点级数组的覆盖：Maya 素材混合方式的实际唯一来源

回到 `ufbx.c:22497-22507`：

```c
for (i ... min(extra->num_alphas, layers.count))
    texture->layers.data[i].alpha = extra->alphas[i];
for (i ... min(extra->num_blend_modes, layers.count)) {
    int32_t mode = extra->blend_modes[i];
    if (mode >= 0 && mode < UFBX_BLEND_OVERLAY)   // UFBX_BLEND_OVERLAY == 30，仅接受 0..29
        texture->layers.data[i].blend_mode = mode;
}
```

- `Alphas` 无条件按索引覆盖每层 `alpha`；长度不足时短的一侧生效（`min`），剩余层保留 2.2 的属性值/缺省值 1.0（合成素材实测：把 `Alphas` 截成 2 项后，第 3 层保持 1.0）。
- `BlendModes` 仅当值在 `[0,29]` 时覆盖初始的 5；**值为 30（`UFBX_BLEND_OVERLAY`）或越界时被拒绝，该层保留缺省 `REPLACE(5)`**。合成素材实测：把数组首项改为 30 后，该层最终为 5。
- 实证（`maya_texture_blend_modes_7500_ascii.fbx`，14 层全部连同一纹理，插桩确认 14 层初始值全为 5）：数组 `BlendModes: 5,29,5,27,...,4,5` 逐字映射为 `UFBX_BLEND_*`，其中 29（`LUMINOSITY`，Maya "Illuminate"）在 `[0,29]` 内故正常生效；Maya 不支持的模式（"CPV Modulate/Desaturate/Out/In"，原始值同为 5）落成 `UFBX_BLEND_REPLACE(5)`。测试 `test/test_material.h:1316-1329` 对 14 个结果逐一断言。
- 同素材 `Alphas` 为 `1,1,...,0.2,0.1`，探针实测层 alpha 从顶层 1.0 递减到底层 0.1，测试 `test_material.h:1304-1308` 以逆序方式断言，证明**数组索引与连接次序一一对齐**。
- 对 `maya_texture_layers_7500_ascii.fbx`：无内层 `BlendMode` 属性可命中，最终 `blend` 2/4/1 全部来自节点级 `BlendModes: 2,4,1`（插桩确认初始全 5、覆盖后为 2/4/1）；`alpha` 0.75/0.5/1 则与内层 `Texture alpha` 属性值恰好相同，两路数值一致但**生效的是数组覆盖路径**。

### 2.4 每属性便捷指针如何接到分层容器

- 材质的 `textures[]` 由 `ufbxi_fetch_textures()` 从材质元素的入站 OP 连接生成（`ufbx.c:19150-19172`），7500 素材中 `DiffuseColor` 属性直接连到 LayeredTexture，故 `material->fbx.diffuse_color.texture` 指向该分层容器（探针实测 `type=1, layers=3`）。
- 同一容器还可被 `pbr.base_color` 等 PBR 映射复用（`ufbxi_fetch_maps`，`ufbx.c:20123` 起）；指针相同，语义不复制。

---

## 3. 场景求值（`ufbx_evaluate_scene`）之后语义来自何处

`ufbx_evaluate_scene()` 是**深拷贝 + 动画求值**，不做任何材质合成。对纹理的处理仅为指针搬迁（`ufbx.c:26265-26289`）：

```c
ufbx_texture_layer *layers = ufbxi_push(... texture->layers.count);
for (i ...) { layers[i] = texture->layers.data[i];
              layers[i].texture = translate(...); }
texture->layers.data = layers;
```

探针实测：对 7500 素材求值后，新场景中分层纹理仍为 `type=LAYERED`、3 层、`blend/alpha` 与原场景逐字节一致。因此：

- **导入期确定的层序、`blend_mode`、`alpha` 在求值后原样保留**，没有第二条产生路径，也不会被动画重算（这些字段不参与 `ufbx_evaluate_prop` 的动画通道；`ufbx_evaluate_prop` 只作用于元素 `props`）。
- 若内层纹理的 `Texture alpha` / `CurrentTextureBlendMode` 属性本身被动画化，库**不会**把动画结果烘焙进 `layer.alpha/blend_mode`——调用方需自行用 `ufbx_evaluate_prop()` 在对应内层 `ufbx_texture->props` 上求值。仓库现有素材没有这种动画，属于**证据未覆盖**的情形（见第 7.3 节）。

---

## 4. 旧版（<7000）FBX 的结构性信息损失

这是便捷视图最严重的盲区，由探针在两个 6100 素材上实测确认，且与库内 `// TODO: Recover layered textures from <7000.....`（`test/test_material.h:1217,1292`）互相印证。

### 4.1 旧版的连接拓扑不同

`maya_texture_layers_6100_ascii.fbx` 中（文件 833-847 行）：

- **没有任何连接把 LayeredTexture 挂到 Material**。材质 `lambert2` 只挂到 mesh；
- 三个文件纹理同时有两条出边：`fileN → LayeredTexture`（OO）**和** `fileN → Model::pCube1`（OO，旧版 mesh 级纹理关联）；
- 混合信息写在 mesh 的 `LayerElementTexture { BlendMode: "Normal"/"Over"/"Add"; TextureAlpha: 1; TextureId: k }` 里（文件 304-348 行）。

### 4.2 库实际产生的结果（探针实测）

1. LayeredTexture **仍被解析为 `UFBX_TEXTURE_LAYERED` 且 `layers.count==3`**（连接关系在场景里完整保留，可从 `scene->textures[]` 按类型遍历找到，探针 3 可见其 3 条入站连接）。
2. 但 `material->textures[]` 由两条独立路径填充：`ufbxi_fetch_textures()` 的材质入站连接（此处无分层容器）+ 旧版 mesh `LayerElementTexture` 补丁（`ufbx.c:13671-13702`、`22367-22469`）。补丁路径**只提取 `TextureId` 和属性名**（`LayerElementTexture` 被映射为属性名 `"Diffuse"`），**字符串 `BlendMode` 与 `TextureAlpha` 字段被直接忽略**（`13685-13700` 的代码只读 mapping 与 TextureId）。
3. 于是 `material->fbx.diffuse_color.texture` 指向**第一个文件纹理**（探针实测三个并列 `Diffuse` 条目，`ufbx_find_prop_texture_len` 二分只取首个），分层对象对便捷视图不可达；`map.texture->layers.count == 0`。
4. 旧版 LayeredTexture 节点没有 `Alphas` 数组（两个 6100 素材均无，`grep` 可证），其 `layers[].alpha` 全部来自内层纹理的 `Texture alpha` 属性（探针实测 1.0/0.5/1.0）；`blend_mode` 初始同样因属性名不匹配（内层写 `CurrentTextureBlendMode`，代码查 `BlendMode`）全部落为缺省 5，再由节点级 `BlendModes: 5,4,1` 数组覆盖为 5/4/1（插桩确认）。该分层对象游离于材质之外，这些值不会经便捷视图暴露。
5. 旧版 mesh 上字符串形式的 `"Normal"/"Over"/"Add"` 混合语义在结构化 API 中**完全丢失**，仅当 `retain_dom=true` 时能从 DOM 文本节点读回。

---

## 5. 只消费便捷贴图视图是否足以还原源材质外观

**不足以。** 分档给出可证明的外观等价条件。

### 5.1 外观等价的充分条件（有源码 + 素材依据）

当且仅当以下条件**全部**成立时，只用 `map.texture`（再取其 `file_textures[0]` 或 `content`）才能得到与源材质一致的外观：

1. 文件版本 ≥7000（7500 素材实证；<7000 时按第 4 节便捷视图根本不引用分层对象）。
2. 该属性连接的纹理 `type == UFBX_TEXTURE_FILE`，或虽是分层容器但 `layers.count == 1` 且该层 `blend_mode == UFBX_BLEND_TRANSLUCENT(0)` 且 `alpha == 1.0`（单层、全权重、仅 alpha 透传时合成结果等价于直接采样）。
3. 不存在需要按 `blend_mode` 方程参与的多像素运算——库不提供任何合成实现，多层时调用方必须自行按 `ufbx.h:2700-2734` 的方程在着色器/CPU 合成器中自底向上（`layers[0]` 为底）迭代：以当前累计结果为 `dst`、当前层采样为 `src`、`layer.alpha` 为权重应用 `layer.blend_mode`。
4. 每层使用的 UV 集与 UV 变换取自**内层 `ufbx_texture` 自身**的 `uv_set` / `uv_transform` / `wrap_u/v`（这些字段挂在内层纹理上，`ufbx.c:22477-22482` 逐纹理设置；7500 素材三个层分别引用 map1/map2/map3 即为实证）。分层容器本身不携带统一 UV 语义，取容器级字段是错误的。
5. `map.texture_enabled == true` 且 `map.feature_disabled == false`（`ufbx.h:2302-2315`）；否则源材质中该连接本就不参与着色。
6. 常量值与贴图的乘染关系按 `ufbx_material_map` 注释处理（`ufbx.h:2290-2292`：常量与贴图并存时多数着色模型做乘法 tint）。

### 5.2 便捷视图明确丢失或无法表达的语义

| 语义 | 便捷视图（`map.texture` / `file_textures`） | 完整分层视图（`layers[]`） | 证据 |
|---|---|---|---|
| 层的存在与数量 | 丢失（只看到容器指针；`file_textures` 被去重，14 层引用同一纹理时只剩 1 个） | 保留 | `test_material.h:1310-1314`；`ufbx.c:20941-20967` |
| 底→顶次序 | 丢失 | 保留（连接稳定排序） | `ufbx.c:19240-19261,18637-18652` |
| 每层混合方程 | 丢失 | `blend_mode`，31 枚举 | `ufbx.h:2700-2734` |
| 每层权重 | 丢失 | `alpha`，内层 `Texture alpha` 属性 + 节点 `Alphas` 数组覆盖 | `ufbx.c:19249,22498-22500` |
| 每层独立 UV 集/变换 | 若只看容器则丢失 | 在内层纹理上 | `ufbx.c:22477-22482`；7500 素材 map1/2/3 |
| 不支持的 Maya 模式 | 无损表达渠道，但被归一化为 REPLACE | 同左（归一化发生在导入期：数组里 Maya 私有的模式号恰为 5，两视图都拿不回原始模式名，仅 DOM 可查） | `test_material.h:1316-1329` |
| 数组值 30（OVERLAY 边界） | 同上 | 被 `< 30` 守卫拒绝，该层回退为缺省 REPLACE(5) | `ufbx.c:22503`；合成素材实测 |
| <7000 mesh 级字符串 BlendMode/TextureAlpha | 完全丢失 | 分层对象与材质断链；仅 DOM 保留原文 | 本报告 4.2；`ufbx.c:13685-13700` |

`file_textures[]` 的去重逻辑（`ufbx.c:20880-20967`，依赖图 DFS + `ufbxi_deduplicate_textures`）进一步证明它只是资源清单：14 层同一纹理时 `file_textures.count == 1`，3 层不同纹理时为 3 且恰好与各层相同（两个测试均有断言）。

---

## 6. 信息不足时仍能保持来源关系的接入方案

按"优先结构化 API，不足再退 DOM/连接"的层级设计。所有路径在当前库版本均可实现，API 均为公开接口。

### 方案 A（推荐，≥7000 的主路径）：以分层容器为一等输入

```c
ufbx_material_map *map = &material->fbx.diffuse_color;   // 或 pbr.base_color 等
if (!map->texture || !map->texture_enabled) { /* 按常量值着色 */ }
else if (map->texture->type == UFBX_TEXTURE_LAYERED) {
    ufbx_texture *lt = map->texture;
    // layers[0] 是底层，自底向上合成；alpha/blend_mode 已在导入期合并好
    for (size_t i = 0; i < lt->layers.count; i++) {
        ufbx_texture_layer *L = &lt->layers.data[i];
        // 1) 用 L->texture 自己的 uv_set/uv_transform/wrap_u/v 采样
        // 2) 按 L->blend_mode 方程、以 L->alpha 为权重合成到累计颜色
    }
} else { /* UFBX_TEXTURE_FILE 直接采样 */ }
```

要点：
- 合成方程照抄 `ufbx.h:2700-2734` 注释；`DISSOLVE` 含随机抖动、`OVERLAY` 等需按 W3C compositing 规范实现，库不代劳。
- 遇到不认识的枚举值（前向兼容）建议退化为 `UFBX_BLEND_REPLACE` 并保留告警，与库的缺省策略一致。
- 嵌套分层（层的 `texture->type` 仍是 LAYERED）可递归处理；代码中 `ufbxi_fetch_texture_layers` 与 `file_textures` 依赖图（`ufbx.c:20915-20923,20992-20995`）都按任意深度设计，仓库无嵌套素材但**源码支持递归**。
- `ufbx_evaluate_scene()` 之后同样适用，字段原样保留（第 3 节）。

### 方案 B（旧版 <7000 补救）：连接关系 + DOM 双轨

1. **不要只信 `material->textures[]`**。遍历 `scene->textures[]`，对每个 `UFBX_TEXTURE_LAYERED` 检查 `element.connections_src`（它作为 src 指向谁）或反向从 mesh/材质可达性建立关联；旧版素材里分层对象通过共享的内层文件纹理与 mesh 上的 `LayerElementTexture.TextureId` 间接对应，可据此把游离的分层对象重新关联回材质属性。
2. 以 `retain_dom=true` 加载（`ufbx_load_opts.retain_dom`，`ufbx.h:4886`），用公开 DOM API 读回结构化路径丢失的字段：
   - `ufbx_dom_find(element->dom_node, "BlendModes")` + `ufbx_dom_as_int32_list()`（实测返回 `[5,4,1]`）；
   - `ufbx_dom_find(..., "Alphas")` + `ufbx_dom_as_real_list()`（7500 实测 `[0.75,0.5,1]`；6100 无此节点）；
   - mesh DOM 中的 `LayerElementTexture/BlendMode` 字符串与 `TextureAlpha`（结构化 API 不解析，见 4.2），需自行维护 `"Normal"/"Over"/"Add"` 到 `ufbx_blend_mode` 的映射表。
3. 关联键建议用 `ufbx_texture.element_id` / 文件名，而不是名字串（旧版名字允许重复，`blend_modes` 素材即 14 次连接同一对象）。

### 方案 C（最低保底，任何版本）：保留来源而不承诺外观

当合成能力缺失或遇到 shader 纹理（`UFBX_TEXTURE_SHADER`，3ds Max 节点图，`ufbx.h:2814-2852`）等无法解释的对象时：

1. 持久化 `material->textures[]` 的 `(material_prop, texture->element_id)` 对、分层容器的 `layers[](texture->element_id, blend_mode, alpha)` 与全部连接（`scene->connections[]`），保证**来源关系无损**、可往返；
2. 运行期降级为 `file_textures[]` 资源清单做单层近似，并在资产元数据中标记"分层未合成"；
3. 这也是 picort 示例的现状：`examples/picort/picort.cpp:84-112` 的 `setup_texture()` 只读 `map.texture->content`，对 LAYERED 容器会读不到任何图片内容（容器 `content` 为空），属于已知的最简降级，不应被当作外观等价实现。

---

## 7. 证据分级声明

### 7.1 导入器提供的数据（源码 + 素材双重证实）

- 层成员/次序来自 OO 连接的稳定排序；`alpha` 先取内层纹理 `Texture alpha` 属性（缺省 1.0），再被节点级 `Alphas` 数组按连接次序覆盖；`blend_mode` 的内层查找键 `BlendMode` 与 Maya 实际属性名 `CurrentTextureBlendMode` 不匹配，故 Maya 素材初始恒为缺省 `REPLACE(5)`，最终值实际全部来自节点级 `BlendModes` 数组（仅接受 0..29，30 被拒绝）。出处：`ufbx.c:14575-14602,18637-18652,19240-19261,22494-22508`，插桩与合成素材实测。
- ≥7000 时便捷视图的 `map.texture` 指向分层容器本身；`file_textures` 是递归去重的文件清单。出处：`ufbx.c:20078-20084,20880-20967,31422-31431`，探针与 `test_material.h:1214-1330` 实测。
- 求值场景深拷贝保留全部层字段，无合成、无重算。出处：`ufbx.c:26265-26289`，探针实测。
- <7000 时 LayeredTexture 与材质无连接、mesh 补丁路径丢弃字符串 BlendMode/TextureAlpha、便捷视图回退为首个文件纹理。出处：`ufbx.c:13671-13702,22367-22469`，6100 素材连接表与探针实测。

### 7.2 调用方负责的合成语义（库只给描述，不给实现）

- 31 种 `ufbx_blend_mode` 的像素方程、`alpha` 权重如何参与方程、底→顶迭代顺序、跨层 UV 集/变换的分别采样、常量值乘染、`texture_enabled/feature_disabled` 的开关解释。库内无任何 compositing 代码，全部由接入方实现；头文件只提供方程注释（`ufbx.h:2696-2734,2290-2315`）。

### 7.3 现有证据尚未支持的结论（不应写死进实现）

- **动画化的层权重/混合方式**：仓库无 `Texture alpha`/`CurrentTextureBlendMode` 被动画曲线驱动的素材；`layer.alpha/blend_mode` 是否需要随 `ufbx_evaluate_scene` 时间点变化，源码没有对应通道，理论上需调用方自行 `ufbx_evaluate_prop`，但无素材佐证。
- **Maya 之外导出器的分层写法**：`data/` 中含 LayeredTexture 的只有 4 个 Maya 素材；Blender、3ds Max 等导出器是否产生 LayeredTexture、数组与连接次序是否同样对齐，仓库无证据。
- **嵌套分层纹理**：源码按递归设计，但无测试素材，行为未被测试锁定。
- **`BlendModes` 出现值 30（OVERLAY）时的行为**：代码的守卫是 `mode < UFBX_BLEND_OVERLAY(30)`（`ufbx.c:22503`），合成素材已实测值 30 被拒绝、该层回退为缺省 REPLACE(5)；但真实导出器是否会写出 30、其作者意图是否等价于 OVERLAY，仓库素材无覆盖。
- **内层 `BlendMode` 属性路径的实际效果**：该查找是为非 Maya 导出器保留的（3ds Max 模板中确有 `BlendMode` 属性名），但仓库里没有任何分层纹理素材来自这类导出器，因此"内层属性直接决定 `blend_mode`"这条路径在当前素材上从未被触发，仅有源码依据、无素材依据。
- **旧版字符串 BlendMode 与 ufbx 枚举的精确映射表**：库未提供，方案 B 中的映射表属于接入方自定义，仓库不背书。

---

## 附：复现方式

```sh
# 仓库测试（含 maya_texture_layers / maya_texture_blend_modes 两个分层专项）
cc -I. -Itest test/runner.c ufbx.c -lm -lpthread -o /tmp/ufbx-test
/tmp/ufbx-test -d data -t maya_texture_layers        # OK
/tmp/ufbx-test -d data -t maya_texture_blend_modes   # OK

# 本报告使用的探针（独立小程序，只读不写素材）
cc -I. build-probe/probe.c  ufbx.c -lm -o build-probe/probe
./build-probe/probe data/maya_texture_layers_*.fbx \
                     data/maya_texture_blend_modes_*.fbx
```
