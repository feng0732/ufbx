# FBX 灯光辐射度量学评估报告（ufbx 接入物理渲染器）

本报告基于当前工作区 ufbx 源码（`ufbx.h` / `ufbx.c`，提交基线 `5955c5c`）的实际实现，评估 FBX 灯光在接入物理渲染器时的**光照强度解释**问题。所有结论均给出可定位的源码证据；源码未覆盖的部分单列为「证据缺口」。

---

## 0. 结论摘要（TL;DR）

1. ufbx 把 FBX 原始属性 `"Intensity"`（DCC 约定尺度，典型值 100）**无条件除以 100** 后暴露为 `ufbx_light.intensity`，`"Color"` 原样透传，运行时颜色辐照度因子为 `color * intensity`（`ufbx.h:1433-1437`，`ufbx.c:23047-23065`）。该 0.01 系数**没有 quirk 开关**，源码注释本身对其跨导出器的正确性存疑。
2. ufbx **不解释任何物理单位、不实现任何衰减公式、不做颜色空间转换**。`decay` 只是枚举（none/linear/quadratic/cubic），聚光锥角是**度数**，面积光连尺寸都没有进入类型化结构体（`ufbx.h:1391` 留有 `TODO: Units?`）。
3. **静态导入与 `ufbx_evaluate_scene()` 动画求值走的是同一个 `ufbxi_update_light()`**，动画曲线作用在原始属性 `"Intensity"`（×100 尺度）上，再经同一处 `/100` 收敛到 `light->intensity`。因此两条路径**可以且应当使用同一套换算**，这是由代码结构保证的，不是约定。
4. 物理一致性的关键负担在渲染器侧：若启用了单位缩放（`target_unit_meters`），几何距离被缩放而灯光强度不缩放，渲染器**必须按衰减阶数补偿强度因子**（二次衰减时为 `ratio²`），否则同一文件在不同单位设置下渲染亮度不同。

---

## 1. 源灯光参数 → 运行时灯光描述的数值关系

### 1.1 类型化字段的完整映射表

`ufbxi_update_light()`（`ufbx.c:23047-23065`）是唯一的灯光派生值计算入口，静态加载与动画求值都调用它（见第 2 节）。映射关系如下：

| 运行时字段（`ufbx_light`，`ufbx.h:1424-1451`） | 源 FBX 属性 | 数值关系 | 源码位置 |
|---|---|---|---|
| `intensity` | `"Intensity"`（Number，模板默认 100） | **`intensity = value_real / 100.0`**；属性缺失时按 `100.0` 代入 → 得 `1.0` | `ufbx.c:23052` |
| `color` | `"Color"`（ColorRGB，模板默认 1,1,1） | **原样透传**，无 sRGB/线性转换、无强度合并 | `ufbx.c:23054` |
| `type` | `"LightType"`（enum 0..4） | 整数直转 `ufbx_light_type`，越界回退默认 0（point） | `ufbx.c:23055`；枚举见 `ufbx.h:1381-1398` |
| `decay` | `"DecayType"`（enum 0..3） | 整数直转 `ufbx_light_decay`，越界回退 `NONE` | `ufbx.c:23056`；枚举见 `ufbx.h:1403-1410` |
| `area_shape` | `"AreaLightShape"`（enum 0..1） | 整数直转，越界回退 0（rectangle） | `ufbx.c:23057` |
| `inner_angle` | `"HotSpot"`，再被 `"InnerAngle"` 覆盖 | 原值透传，**单位为度（°），不转弧度**；默认 0 | `ufbx.c:23058-23059` |
| `outer_angle` | `"Cone_angle"`（旧名）→ `"ConeAngle"` → `"OuterAngle"` 依次覆盖 | 原值透传，**单位为度**；默认 0（FBX 模板默认 45） | `ufbx.c:23060-23062` |
| `local_direction` | 非属性，硬编码 | 本地 `-Y`（`{0,-1,0}`）；设置 `target_light_axes` 时做轴旋转 | `ufbx.c:23726-23731`、`23697-23711` |
| `cast_light` / `cast_shadows` | `"CastLight"` / `"CastShadows"` | 布尔直转，默认 1 / 0 | `ufbx.c:23063-23064` |

`LightType` 枚举顺序（`ufbx.h:1381-1398`）：0 = point，1 = directional，2 = spot，3 = area，4 = volume。测试数据 `motionbuilder_lights_7700_ascii.fbx:663` 中聚光灯 `LightType=2` 与该顺序一致。

### 1.2 强度的 0.01 系数：可复算的数值关系

设 FBX 文件内原始属性值为 `I_fbx`，则：

```
ufbx_light.intensity = I_fbx / 100
运行时灯光辐射权重  L = color * intensity      // ufbx.h:1433 注释建议的用法
                    = color * (I_fbx / 100)
```

可复算的实测样本（原始值 → 派生值，均有测试断言锁定）：

| 文件 / 灯光 | 原始 `"Intensity"` | `light->intensity` | 证据 |
|---|---|---|---|
| `motionbuilder_lights` PointLinear | 50 | 0.5 | 数据 `motionbuilder_lights_7700_ascii.fbx:635`；断言 `test/test_lights.h:28` |
| 同文件 PointQuadratic | 150 | 1.5 | 数据 `:645`；断言 `test_lights.h:39` |
| 同文件 PointCubic | 200 | 2.0 | 数据 `:655`；断言 `test_lights.h:50` |
| `blender_279_default` Lamp | 100.0 | 1.0 | 原始值断言 `test/test_mesh.h:26-28` |
| `maya_anim_light` 帧 0 | 307.2289 | 3.072 | 数据 `maya_anim_light_6100_ascii.fbx:139`；断言 `test/test_animation.h:891,915` |

**注意两个尺度陷阱：**

- **写入侧不对称**：测试用生成器 `ufbxw_light_set_intensity()` 直接写入原始尺度（默认 100，`test/casegen/ufbx_write.c:5950,6813`，头文件注释 `ufbx_write.h:1125-1127` 明确「Usually multiplied by 100.0」）。即 ufbx 读时 `/100`、写时不换算，往返处理需要使用方自己记住尺度。
- **逐属性求值 API 不做换算**：`ufbx_evaluate_prop(anim, light, "Intensity", t)` 返回的是**原始 ×100 尺度**（测试中 override 给 10.0，直接断言取回 10.0，`test_animation.h:1003-1005`）；而经 `ufbx_evaluate_scene()` 得到的 `light->intensity` 是 `/100` 后的 0.1（`test_animation.h:927,977`）。渲染器若混用这两条 API，会差 100 倍。

源码对该系数的自我怀疑记录在 `ufbx.c:23049-23051`：

> "FBX seems to store intensities 100x of what's specified in at least Maya and Blender, should there be a quirks mode to not do this for specific exporters. Does the FBX SDK do this transparently as well?"

即：**该 100 倍关系是从 Maya/Blender 导出样本归纳的经验值，不是 FBX 规范条款，且没有按导出器区分的 quirk**。`ufbx_load_opts.disable_quirks`（`ufbx.h:4738`）也不涉及它——此除法无条件执行。

### 1.3 场景单位对亮度解释的限制

**单位信息如何流动（`ufbx.c:23906-23915`、`24991-25018`）：**

```
UnitScaleFactor (cm/unit, FBX 默认 1)
  → settings.unit_meters = UnitScaleFactor * 0.01      // 默认 0.01 m（厘米）
target_unit_meters 给定时:
  ratio = unit_meters / target_meters                  // ufbx.c:24996
  uc->unit_scale = ratio                               // ufbx.c:25000
  仅作用于根节点缩放（TRANSFORM_ROOT 模式）或几何/调整变换
```

关键限制：

1. **单位缩放只改几何/变换，从不触碰 `light->intensity`、`color`、角度**。全文检索 `ufbxi_update_light` 与单位代码无任何耦合。
2. `ufbx_light` 中**没有任何字段携带物理单位语义**（无流明/坎德拉/瓦特级标记）。`intensity` 是无量纲乘数。
3. 衰减枚举只给阶数，不含距离参考尺度：
   - `UFBX_LIGHT_DECAY_NONE` → 1；`LINEAR` → 1/d；`QUADRATIC` → 1/d²（头文件标注 "physically accurate"，`ufbx.h:1406`）；`CUBIC` → 1/d³。
   - FBX 模板中的 `"DecayStart"`、`"EnableNearAttenuation"`、`"NearAttenuationStart/End"`、`"EnableFarAttenuation"`、`"FarAttenuationStart/End"`（完整清单见 `misc/gen_default_props.py:107-115`）**均未进入 `ufbx_light`**，只能通过 `light->props` 自行读取。这意味着近/远截断衰减在类型化接口里是丢失的。
4. 距离 `d` 的物理意义依赖 `settings.unit_meters`（`ufbx.h:3909-3912`），但强度没有配套的单位归一化——**"1.0 强度"在厘米场景和米场景下对应的物理量纲不同**，ufbx 不解决这个歧义。

### 1.4 灯光类型对亮度解释的限制

| 类型 | ufbx 已提供 | 物理渲染器仍缺的信息 |
|---|---|---|
| point | 位置（节点世界变换）、颜色、强度、decay | 衰减公式本体、半径/尺寸（`"Radius"` 非标准 FBX 属性，官方示例只能从 raw props 碰运气读取，`examples/picort/picort.cpp:276-277`）、DecayStart/截断 |
| directional | `local_direction`（默认 -Y）+ 节点旋转可求世界方向 | 方向光的辐射度量应解释为**辐照度（irradiance, W/m²）**而非点源强度；ufbx 不做区分，`intensity` 语义对两类光完全同构 |
| spot | 内/外锥角（度）、方向、decay | 锥角间的过渡函数（FBX 未定义统一 penumbra 曲线）、锥内能量归一化（立体角积分）均无约定 |
| area | 仅 `area_shape`（rectangle/sphere） | **尺寸字段完全缺失**：头文件 `ufbx.h:1391` 直接挂着 `// TODO: Units?`；没有宽/高/半径，无法计算面积，而面光物理强度必须按面积归一化 |
| volume | 枚举值 | `ufbx.h:1394` 标注 `// TODO: How does this work`，无可用于渲染的参数 |

官方路径追踪示例 picort 印证了类型信息被丢弃的程度：它把**所有非方向光一律按点光 + 固定二次衰减**处理（`picort.cpp:279-289`），着色时 `att = max(|Δ|², 0.01)` 硬编码 1/d²（`picort.cpp:609-610`），**完全忽略 `light->decay`、`type`（spot/area）与锥角**。这不是 ufbx 的 bug，而是 ufbx 把这部分解释权完全留给了接入方的直接证据。

---

## 2. 静态导入与动画求值能否共用同一套光照换算

### 2.1 代码路径对比：同一收敛点

**静态加载路径：**

```
ufbx 加载 → 属性入 light->props（原始 ×100 尺度）
         → ufbxi_update_scene(initial=true)           // ufbx.c:23809
            → ufbxi_update_light(light)               // ufbx.c:23815-23817
               → intensity = props["Intensity"] / 100 // ufbx.c:23052
```

**动画求值路径（`ufbx_evaluate_scene()`，`ufbx.c:26113` 起）：**

```
1. 逐元素 memcpy 静态场景的 ufbx_light（含旧的派生值）     // ufbx.c:26154-26159
2. 对含动画属性的元素重新求值属性表：
   elem->props = ufbx_evaluate_props_flags(...)           // ufbx.c:26406
   （动画曲线绑定在原始 "Intensity" 上，输出仍是 ×100 尺度；
     新 props.defaults 回指静态场景属性表，ufbx.c:26407）
3. ufbxi_update_scene(initial=false, ...)                 // ufbx.c:26411
   → ufbxi_update_light(light)  // 同一个函数，同一条 /100 // ufbx.c:23815-23817
```

**判定：可以共用，且是结构性保证。** 两条路径的灯光派生值由同一个函数、同一段 `/100` 代码产生；动画系统只替换输入属性，不改变换算关系。测试 `maya_anim_light`（`test_animation.h:887-919`）在 8 个帧时刻反复 `ufbx_evaluate_scene()` 并断言 `light->intensity` 始终等于「曲线原始值 /100」，验证了该收敛行为。

可动画属性还包括 `"Color"`、`"InnerAngle"`、`"OuterAngle"`、`"DecayType"` 等（FBX 模板中带 `"A"` 标志者，`gen_default_props.py:103-108`），它们都经由同一机制：曲线 → raw prop → `ufbxi_update_light` 派生字段，因此颜色/锥角/衰减类型的动画与强度动画同构，换算规则逐帧不变。

### 2.2 距离衰减与最终照明一致的成立条件

设渲染器采用 FBX 衰减阶数 `n`（none: n=0，linear: n=1，quadratic: n=2，cubic: n=3），在世界单位 `u`（米/单位）下，表面处辐照度的通用形式为：

```
E(d) = C · I · d^(−n)，   其中 C = color，I = light->intensity，d 以世界单位计
```

**条件 A — 同一文件、同一单位设置下，静态帧与动画帧完全一致。**
动画只改变 `I(t)`（以及可能的 `C(t)`、`type/decay`、节点变换），公式形式不变。只要渲染器每帧从求值后场景读取 `light->intensity/decay/type` 并重算，静态与动态无需两套代码。此条件由 §2.1 的代码结构保证。

**条件 B — 跨单位设置（`target_unit_meters` 改变）保持物理一致，需要渲染器补偿。**
设单位缩放比 `ratio = unit_meters / target_meters`（`ufbx.c:24996`）。几何距离在新场景中变为 `d′ = ratio·d`，而强度字段不变 `I′ = I`。若渲染器直接套用同一公式：

```
E′(d′) = C·I·(ratio·d)^(−n) = ratio^(−n) · E(d)
```

要使 `E′ = E`，渲染器必须把强度替换为补偿值：

```
I_phys = I · ratio^n
```

- n=2（物理二次衰减，点光/聚光）：**补偿因子 `ratio²`**。例：厘米场景（0.01 m）归一到米（1.0 m），`ratio=0.01`，`I_phys = I·10⁻⁴`；距离同时放大 100 倍，两项抵消。
- n=0（方向光与 constant 点光）：`ratio⁰ = 1`，**无需补偿**——这与方向光按辐照度解释、不参与距离衰减的物理直觉一致。
- n=1、n=3：分别补偿 `ratio¹`、`ratio³`；cubic 本身非物理，仅为复刻 DCC 观感。

ufbx 不提供该补偿（`uc->unit_scale` 只进入根变换，`ufbx.c:23685-23687, 25002-25015`），但渲染器具备全部所需输入：`scene->settings.unit_meters` / `original_unit_meters`（`ufbx.h:3909-3931`）与 `metadata.root_scale`（`ufbx.h:3840` 附近）。

**条件 C — 衰减公式的归一化口径必须固定。** FBX/ufbx 只给阶数，未规定 `d` 的归一化参考距离，也未规定 `DecayStart` 如何参与（该属性 ufbx 不解析）。渲染器需自行决定：是按裸 `1/dⁿ`（picort 的做法，且加了 `max(·, 0.01)` 防奇异，`picort.cpp:609`），还是在 `d < DecayStart` 时钳制。选用 raw props 读取 `DecayStart`/远近衰减时，静态与动画路径仍共用同一套读取逻辑，但这些属性若被动画化，只能通过 `ufbx_evaluate_prop()`（原始尺度 API）获取。

**条件 D — 聚光/面积光的能量守恒口径。** 聚光锥角动画改变立体角覆盖；若渲染器按立体角归一化锥内能量，则强度解释随锥角变化，需要把 `inner/outer_angle`（度，需自行 `rad = deg·π/180`）纳入统一公式。面积光因 ufbx 不提供尺寸（§1.4），不存在可复算的面积归一化，属于硬缺口。

---

## 3. 责任划分：源码已实现 vs 渲染器仍需承担

### 3.1 ufbx 源码已经实现并担保的处理

- 原始 `"Intensity"` → `light->intensity` 的固定 `/100` 换算（含缺省 100 → 1.0 的回退），静态与动画同一入口（`ufbx.c:23052`）。
- `"Color"` 原样透传、`LightType/DecayType/AreaLightShape` 枚举映射与越界回退（`ufbx.c:23054-23057`，`ufbxi_find_enum` 越界逻辑 `ufbx.c:11547-11560`）。
- 聚光锥角多属性名兼容读取（HotSpot/InnerAngle、Cone_angle/ConeAngle/OuterAngle），度数透传（`ufbx.c:23058-23062`）。
- 灯光本地朝向 `-Y` 与 `target_light_axes` 轴旋转（`ufbx.c:23697-23731`）。
- 单位元数据披露（`unit_meters`）与可选几何单位缩放（`ufbxi_scale_units`），但**仅限变换域**。
- 动画属性求值、prop override、场景深拷贝后重跑全部派生值（`ufbx.c:26385-26411`），保证求值场景与静态场景同构。
- 全部原始属性仍可经 `ufbx_light.props` / `ufbx_evaluate_prop()` 访问（含 ufbx 未类型化的 DecayStart、远近衰减、BarnDoor 等）。

### 3.2 渲染器必须自行承担的物理量转换

1. **量纲赋予**：把无量纲 `color·intensity` 解释为发光强度（点光，cd）、辐照度（方向光，W/m²）或光通量/面积（面光，lm/m²），FBX 不携带任何物理单位标记。
2. **衰减公式实现**：按 `decay` 枚举实现 d⁻ⁿ（n=0..3），包含参考距离口径、`d→0` 奇异值处理；若需 `DecayStart` 与近/远截断，须自行从 raw props 读取并实现。
3. **单位补偿（条件 B）**：启用 `target_unit_meters` 时按 `ratioⁿ` 修正强度，二次衰减为 `ratio²`；方向光 n=0 免补偿。
4. **角度单位换算**：`inner/outer_angle` 度 → 弧度，以及聚光 penumbra 过渡曲线与锥内能量归一化。
5. **面积光尺寸**：ufbx 无尺寸字段，需从 DCC 自定义 props 自行约定读取，否则无法物理归一化。
6. **颜色空间**：ufbx 不做 sRGB→linear（`color` 直传），渲染器须按其色彩管线自行保证线性空间着色。
7. **API 尺度纪律**：经 `ufbx_evaluate_scene()` 读 `light->intensity`（/100 尺度）；经 `ufbx_evaluate_prop(..., "Intensity")` 读原始值时须自行 `/100`，二者不可混用。
8. **非物理枚举的处置策略**：cubic 衰减、volume 类型在物理渲染器中无对应量纲，需要明确的降级/拒绝策略。

### 3.3 可复算的亮度关系汇总

```
# 输入
I_fbx = light->props["Intensity"].value_real        # 原始尺度，默认 100
C     = light->color                                # 线性 RGB 透传
n     = (int)light->decay                           # 0..3
u     = scene.settings.unit_meters                  # m / world-unit
ratio = u / target_unit_meters（未做单位转换时 = 1）
d_w   = 世界单位下的灯到表面距离

# ufbx 输出（静态/动画同式）
I     = I_fbx / 100

# 渲染器侧物理一致的辐照度（条件 B 补偿后）
E(d)  = C · (I · ratio^n) · d_w^(−n)

# 若改用米制距离 d_m = u · d_w，代入可化简为与单位设置无关的形式：
E     = C · I · u^n · d_m^(−n)                      # ratio=1 时的等价写法
# 二次衰减点光：E = C·I·u²/d_m²，厘米场景 u=0.01
```

方向性光（n=0）：`E = C·I`，与距离、单位均无关，仅取决于颜色管线与量纲映射。

---

## 4. 证据缺口（源码无法回答的问题）

1. **100 倍强度系数的规范地位未知**。`ufbx.c:23049-23051` 注释自陈仅基于 Maya/Blender 样本归纳，未确认 FBX SDK 是否透明换算，也无按导出器（3ds Max、MotionBuilder、其他）区分的 quirk。MotionBuilder 样本（50/150/200 等"整数×100"形态）与 Maya 样本（307.2289…）符合同一尺度，但样本集有限；遇到不遵循该约定的导出文件时，`/100` 会产生系统性偏差，且 ufbx 无开关可关闭。
2. **衰减公式的权威定义缺失**。仓库内没有任何代码或文档定义 FBX `DecayType` 的归一化参考距离、`DecayStart` 的数学形式，以及近/远 attenuation 区间的曲线形状；这些属性虽保留在 raw props 中，但其精确定义需外部 FBX SDK/DCC 文档佐证，本仓库无法闭环。
3. **面积光尺寸无来源**。`ufbx.h:1391` 的 `TODO: Units?` 表明面积光单位与尺寸在 ufbx 层面未定义；不同 DCC（Maya 面积光、3ds Max 区域光）存放尺寸的自定义属性名未在仓库中建立映射，渲染器无法凭 ufbx 类型化接口复算面光亮度。
4. **volume 灯光语义空白**（`ufbx.h:1394` `TODO: How does this work`），无任何可渲染参数。
5. **聚光 penumbra 插值曲线无约定**。仅给出 inner/outer 两个角度，两角度之间的衰减函数（线性、smoothstep 或 DCC 专用曲线）在源码与测试中均无依据。
6. **颜色空间归属未声明**。仓库未对灯光 `"Color"` 标注 sRGB 或 linear，本报告按"原样透传、渲染器自负其责"陈述，但具体 FBX 导出器写入何种空间缺少仓库内证据。
7. **动画化的非类型化属性（如 DecayStart、FarAttenuation*）** 可经 `ufbx_evaluate_prop()` 取原始值，但其动画曲线的默认值/模板合成行为在灯光场景下没有专门测试覆盖（现有灯光动画测试仅覆盖 Intensity 与 Color，`test_animation.h:887-919`）。

---

## 附：关键源码索引

| 主题 | 位置 |
|---|---|
| `ufbx_light` 结构体与字段注释 | `ufbx.h:1424-1451` |
| 灯光类型/衰减/面积形状枚举 | `ufbx.h:1381-1421` |
| 强度 /100 与全部灯光派生值 | `ufbx.c:23047-23065`（核心行 23052） |
| 单位元数据计算 | `ufbx.c:23906-23915` |
| 单位缩放比与根变换应用 | `ufbx.c:24991-25018` |
| 灯光朝向与 target_light_axes | `ufbx.c:23691-23731` |
| 静态/动画统一更新入口 | `ufbx.c:23809-23817` |
| 动画求值：属性重算 → update_scene | `ufbx.c:26385-26411` |
| 灯光原始属性模板（含未类型化项） | `misc/gen_default_props.py:99-123` |
| 官方渲染器对灯光的实际消费方式（忽略 decay/spot） | `examples/picort/picort.cpp:270-293, 609-610` |
| 强度换算测试（静态） | `test/test_lights.h`、`test/test_mesh.h:26-28` |
| 强度换算测试（动画，/100 与原始值双尺度） | `test/test_animation.h:887-1018` |
| 写入侧尺度（×100 约定） | `test/casegen/ufbx_write.h:1125-1127`、`ufbx_write.c:5950` |
