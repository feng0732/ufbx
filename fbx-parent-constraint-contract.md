# FBX Parent Constraint：源节点权重、绑定偏移与求值场景契约

目标读者：资产预览器接入方。本文回答两个问题：

1. 约束描述（`ufbx_constraint`）、源节点动画、被约束节点最终世界姿态三者之间，**哪些联系已经由 ufbx 建立**，**哪些必须由接入方自己完成**。
2. 当层级变换与场景坐标换算（轴/单位/左右手系）共同生效时，保留原绑定意图所需的**组合关系**是什么；仅消费 `ufbx_evaluate_scene()` 会遗漏什么。

文中“当前行为”均给出 `ufbx.c` / `ufbx.h` 行号；“建议方案”明确标注为**未实现**。

---

## 0. 结论速览

| 联系 | 当前是否成立 | 由谁负责 |
|---|---|---|
| 约束元素 → 被约束节点 `constraint->node` | **成立**（解析时建立） | ufbx |
| 约束元素 → 有序、带权重的源节点列表 `constraint->targets[]` | **成立**（含 6x00/7x00 连接方向兼容） | ufbx |
| 每个源节点的 `weight`（标量） | **成立**，但只在本节点求值时刻有效 | ufbx |
| 每个源节点的绑定偏移 `target->transform`（T/R/S） | **成立**，7x00 从 `<Name>.Offset T/R/S` 读取，6x00 无此属性→零偏移 | ufbx |
| 约束整体权重 / 启停 / 影响轴 | **成立**，但存在属性名不对称与模板缺失（见 §4） | ufbx（有已知缺口） |
| 源节点动画 → 被约束节点世界姿态 | **不成立**。约束从不参与任何姿态求解 | **接入方** |
| 绑定偏移 + 层级 + 世界矩阵的合成顺序 | **不成立**（无代码）。本文给出经验证的关系式 | **接入方** |
| 场景坐标换算对偏移的影响 | **未处理**。偏移与来源单位/手系无关地保留 | **接入方**（见 §6） |
| `ufbx_evaluate_scene()` 输出中的约束世界结果 | **不存在**，只重新求值了约束自身的属性 | 接入方 |

一句话：**ufbx 交付的是“约束描述的数据”，不是“约束的结果”。** 预览器如果要保留多源父约束的绑定关系，必须自己实现 §5 的求解，并自行补偿 §6 的坐标换算。

---

## 1. 约束描述从哪里来

### 1.1 元素读取（只读类型名）

`ufbxi_read_constraint()`（`ufbx.c:14817`）只做两件事：读取 `Type` 字符串，再查表映射为枚举（`ufbx.c:14808` 的类型表）：

```
{ UFBX_CONSTRAINT_PARENT, "Parent-Child" },
```

其余全部交给连接和属性阶段。注意 `ufbx.c:14835` 处的 `// TODO: There's some extremely cursed all-caps data in characters`，说明此处仍是保守实现。

### 1.2 源节点与目标节点的绑定（连接表）

`ufbx.c:22570-22590` 是“节点 ↔ 约束”关系的唯一建立点。关键设计是**同时扫描 `connections_src` 和 `connections_dst`**：

```c
// Find property connections in _both_ src and dst connections as they are inconsistent
// in pre-7000 files. For example "Constrained Object" is a "PO" connection in 6100.
```

属性名到角色的映射在 `ufbx.c:20230-20241`：

| FBX 属性名 | 结果 |
|---|---|
| `Constrained Object` / `Constrained object (Child)` | `constraint->node`（被约束节点） |
| `First Joint` | `constraint->node`（IK） |
| `Source` / `Source (Parent)` | **新增一个** `ufbx_constraint_target` |
| `Aim At Object` / `Pole Vector Object` | 新增一个 target（Aim / IK pole） |
| `Effector` / `End Joint` | `ik_effector` / `ik_end_node` |
| `World Up Object` | `aim_up_node` |

`ufbxi_add_constraint_prop()`（`ufbx.c:20243`）在创建 target 时写入初值（`ufbx.c:20255-20257`）：

```c
target->node = node;
target->weight = 1.0f;
target->transform = ufbx_identity_transform;
```

**目标顺序** = 属性连接在（去重后的）连接表中的稳定顺序。实测 `maya_double_parent_7500_ascii.fbx` 的 targets 顺序为 `ParentA, ParentB`，与文件中两条 `Source (Parent)` 的出现顺序一致。**报告/保存时必须保留 `targets[]` 的顺序**，因为混合权重时顺序影响四元数符号对齐（见 §5.2）。

### 1.3 权重与偏移（属性求值阶段）

`ufbxi_update_constraint()`（`ufbx.c:23419`）在**每次** `ufbxi_update_scene()` 末尾被调用（`ufbx.c:23865-23867`）：

```c
constraint->weight = ufbxi_find_real(props, ufbxi_Weight, 100.0) / 100.0;

ufbxi_for_list(ufbx_constraint_target, target, constraint->targets) {
    ufbx_real weight_scale = 100.0;
    if (constraint_type == UFBX_CONSTRAINT_SINGLE_CHAIN_IK) weight_scale = 1.0;

    parts[0] = node->name;
    parts[1] = ".Weight";
    prop = ufbx_find_prop_concat(props, parts, 2);
    target->weight = (prop ? prop->value_real : weight_scale) / weight_scale;

    if (constraint_type == UFBX_CONSTRAINT_PARENT) {
        // ".Offset T" → translation
        // ".Offset R" → ufbx_euler_to_quat(r, UFBX_ROTATION_ORDER_XYZ)
        // ".Offset S" → scale（默认 (1,1,1)）
    }
}
```

要点（全部已在样本上实测）：

- **源节点权重的键是 `"<targetNode->name>.Weight"`**，按节点名拼接（`ufbx.c:23439-23442`）。若两个 target 指向**同名**节点，两者会读到同一个属性；若节点在求值后被重命名，键名失效→回落到默认 100（除以 100 得 1.0）。
- **权重除数不统一**：普通约束除以 100（FBX 里的 `100` = 1.0），`SINGLE_CHAIN_IK` 除以 1（`ufbx.c:23431-23435` 的注释 “IK weights seem to be not scaled 100x?”）。实测 `Pole.Weight = 1` 的 IK 约束得到 `weight == 1.0`。
- **约束整体权重**来自约束元素自身的 `Weight`（`ufbx.c:23426`），`targets[].weight` **不含**整体权重。两者是**相乘**关系，但 ufbx 不替你做乘法。
- **偏移的 T/R/S 都读 `vec3`**，其中 `Offset R` 是**欧拉角（度，XYZ 顺序）**，不是四元数（`ufbx.c:23456`）。`Offset S` 在样本中没有实例（`grep "Offset S" data/*.fbx` 为空），默认 `(1,1,1)`。
- **`transform_offset`（约束级）与 `target->transform`（每源级）是两个不同的偏移**：前者来自约束元素自己的 `Translation/Rotation/RotationOffset/Scaling`（`ufbx.c:22941-22955`），后者来自 `<Name>.Offset T/R/S`。父约束语义使用后者；`transform_offset` 目前未在任何求解代码中被消费。
- 偏移读取发生在 **props 求值之后**，因此 `Offset T/R/S`、`.Weight` 支持动画曲线：`ufbx_evaluate_scene()` 会用 `ufbx_evaluate_props_flags()` 重建元素属性（`ufbx.c:26406`），随后 `ufbxi_update_scene()` 重算 `targets[].weight/transform`（`ufbx.c:26411`）。实测对 `ParentA.Weight` / `ParentA.Offset T` 施加 prop override 后，被求值场景里的这两个值都正确变化，而源场景保持不变。

### 1.4 6x00 与 7x00 的偏移差异

- 7x00：偏移在 `Properties70` 里的独立属性，形如 `P: "ParentA.Offset T", "Translation", "", "A", …`。能正确读入。
- 6x00：`maya_constraint_zoo_6100_ascii.fbx` 里父约束虽带 `Property: "Parent.Offset R", "Translation", "A",0,0,90`，但 ufbx 读出的 `targets[0].transform` 是**单位变换**（实测 `offset.t=(0,0,0)`、`offset.r=(0,0,0,1)`），同时该文件另有一个未被读取的旧式字段 `Offset: 0,0,90,0,0,0,0,0,0`（`data/maya_constraint_zoo_6100_ascii.fbx:1759`）。**结论：6x00 的每源偏移不可依赖**；好在该样本的重建结果仍与静止姿态一致（因为 `Parent.Offset T/R` 对该文件实际为零），但这只是巧合，接入方不能据此假设 6x00 偏移可用。

---

## 2. 已经成立的联系（可依赖的契约）

以下联系在加载完成后**直接成立**，接入方可以按此信任：

1. `constraint->node` 非空时一定是同场景的 `ufbx_node`；`targets[i].node` 亦同。校验代码在 `test/check_scene.h:1193-1202`，其中 `ufbxt_check_element_ptr`（`test/check_scene.h:129`）只校验非空指针，**允许 `constraint->node == NULL`**。
2. `targets[]` 的每个元素都至少带 `node`、`weight >= 0`（解析初值 1.0，之后由属性覆盖）、`transform`（T/R/S）。
3. `targets[].weight` 与 `targets[].transform` 在以下时机**同步刷新**：初次加载（`ufbx.c:25356`）、`ufbx_evaluate_scene()`（`ufbx.c:26411`）。所以你拿到的约束描述总是与同一 scene 对象的“当前时刻”一致。
4. 属性求值链路是完整的：约束元素的动画曲线/prop override 在 `ufbx_evaluate_scene()` 中会进入 `constraint->props`，再流入 `targets[]`。**约束自身的 `.Weight`/`.Offset T` 可以动画化并被正确求值**。
5. 与约束相关的指针在求值场景中**已重映射**到求值场景内的节点（`ufbx.c:26315-26330`），不会留下指向源场景的悬垂指针。

源码依据：`ufbx.h:3354-3394`（结构定义）、`ufbx.c:22570-22590`（绑定）、`ufbx.c:23419-23490`（求值）、`ufbx.c:26315-26330`（求值场景重映射）。

---

## 3. 由接入方负责的联系（当前不成立）

### 3.1 约束从不参与姿态求解

`ufbx_update_scene()`（`ufbx.c:23809`）先对**所有节点**调用 `ufbxi_update_node()`（`ufbx.c:23811-23813`），节点变换完全由自身属性 + 父链决定（`ufbx.c:22958-23040`）；约束更新在其后（`ufbx.c:23865`），且 `ufbxi_update_constraint()` 只写 `constraint` 自身字段。全文检索 `constraint->node->` / `target->node->` 的写入为零。

实测（`maya_double_parent_7500_ascii.fbx`）：把 `ParentA` 的 `Lcl Translation` 覆盖为 `(100,0,0)` 再 `ufbx_evaluate_scene()`：

```
eval: targetA world=(100.0000,0.0000,0.0000) child world=(0.0000,0.0000,0.0000) child local=(0.0000,0.0000,0.0000)
```

**源节点动了，被约束节点完全不动。** 这正是“仅消费求值场景会遗漏”的核心。

### 3.2 动画烘焙也不覆盖约束

`ufbx_bake_anim()` 只对 `ufbx_anim_layer` 里出现的属性做烘焙（`ufbx.c:27621-27635`），节点走 `ufbxi_bake_node`，其他元素走通用 `ufbxi_bake_anim_prop`（`ufbx.c:27556-27590`）。约束没有烘焙路径上的特殊处理。按代码路径推断，若约束属性本身有动画曲线，它会作为普通元素属性出现在 `baked->elements[]`（`ufbx.h:4565-4570` 的 `ufbx_baked_element`）；但这一点**未在现有样本中触发**：实测对约束加 prop override 后烘焙，`baked->elements.count == 0`，因为样本里没有约束动画曲线，override 不足以让约束进入烘焙枚举。**不要把烘焙结果当作约束已求解的证据。**

### 3.3 没有反向索引

`ufbx_node` 没有 `constraints` 字段（`ufbx.h:844-995` 中不存在）。要从节点找到约束，只能：

- 遍历 `scene->constraints`（`ufbx.h:4004`），比对 `constraint->node`；或
- 读 `node->element.connections_src/dst` 并手工匹配 `"Constrained object (Child)"` 等属性名（实测 `Node` 的 `connections_src` 里确实有 `dst='Node_parentConstraint1' dst_prop='Constrained object (Child)'`）。

### 3.4 一个被约束节点可有多个父约束

样本 `maya_double_parent_7500_ascii.fbx` 是**一个约束带两个源**；而“多个父约束作用于同一节点”在 FBX 中同样合法，ufbx 对此**不做任何合并/排序**，`scene->constraints` 的顺序是读取顺序。接入方需要自行决定多个约束的叠放顺序（建议按 `constraint->weight * active` 顺序逐层施加，并在每次施加后重算 `local_transform`）。

---

## 4. 约束描述中的已知缺陷（会直接影响预览器）

这些是**当前行为**，不是建议；接入方要么绕开，要么在加载后自行修补。

### 4.1 `AffectScale*` 属性名不匹配

`ufbxi_update_constraint()` 读取 `"AffectScale"`（`ufbx.c:23478`），而 Maya 导出的是 `AffectScalingX/Y/Z`（见 `data/maya_double_parent_7500_ascii.fbx:234-236`、`data/maya_constraint_zoo_6100_ascii.fbx:1752-1754`）。实测：

```
6100 父约束 base T=(111) R=(111) S=(000)
  override AffectScalingX=1 -> T=(111) R=(111) S=(000)   // 无效
  override AffectScaleX=1   -> T=(111) R=(111) S=(100)   // 只有这个键生效
```

后果：所有 Maya 文件中**父约束的 `constrain_scale` 恒为默认 `false`**。若场景里确实有 `AffectScaling*` 打开的情况，缩放通道会被漏掉。

### 4.2 7x00 的属性模板不生效

`ufbxi_find_template()`（`ufbx.c:12191`）要求 `tmpl->sub_type == sub_type`，但约束对象的 sub_type 是 `"Parent-Child"`，而模板名是 `"FbxConstraintParent"`（去 `Fbx` 前缀后为 `"ConstraintParent"`），两者不相等，因此**父约束拿不到模板默认值**。实测 `maya_double_parent_7500_ascii.fbx` 的约束 `props.defaults == NULL`。后果：

- `AffectTranslation*`/`AffectRotation*` 在 7x00 文件里**只存在于模板中**，约束对象自身并不写这些属性 → ufbx 用硬编码默认 `true`（`ufbx.c:23476-23478`），恰好与 Maya 默认一致，所以大多数情况无感；但**如果文件里真的关掉了某个轴**，7x00 下会读不到。
- 同理 `Active` 默认取 `1`（`ufbx.c:23461`），`Weight` 默认取 `100`（`ufbx.c:23426`）。

对比：6100 文件把 `AffectTranslationX/Y/Z`、`AffectRotationX/Y/Z`、`Weight`、`Active` 全部写成对象自身的 legacy 属性，所以 6100 能正确读到（实测 `AffectTranslationX=1`、`AffectScalingX=0`）。**即同一语义在 6x00/7x00 下的可读性不同。**

### 4.3 IK pole vector 与 pole type 的字段错位

`ufbx.c:23489`：

```c
constraint->ik_pole_vector = ufbx_find_vec3(props, "PoleVectorType", ufbx_zero_vec3);
```

`PoleVectorType` 是 `enum`（实测值 `1`），真正的向量在 `PoleVector`。因此 `ik_pole_vector` 会被读成 `(1,0,0)`（标量写进 `.x`，其余补零）；`PoleVector` 的值被丢弃。实测 `IKHandle: ik_pole_vector=(1,0,0)`，而原件 `PoleVector=(-2.58156,3.65612,-5.05499)`。

另外 `ufbx_constraint_ik_pole_type` 枚举在 `ufbx.h:3345-3352` 有定义，但 `ufbx_constraint` 结构（`ufbx.h:3354-3394`）**没有对应字段**，所以 pole type 完全暴露不出来。预览器若要还原 IK pole，必须直接读 `constraint->props` 里的 `"PoleVector"`。

### 4.4 `Pole Vector Object` 连接被塞进 `targets`

`ufbx.c:20237` 把 `"Pole Vector Object"` 归类为 `UFBXI_CONSTRAINT_PROP_TARGET`。头文件注释（`ufbx.h:3369`、`ufbx.h:3390`）已说明 IK 的 `targets` 是 pole vector，但这也意味着 **IK 的 targets 不能按“平移/旋转权重源”来解释**。

---

## 5. 层级变换 + 绑定偏移的组合关系（保留原绑定意图）

这是接入方必须自己实现的部分。以下关系式**全部已在三个样本上用 C 探针数值验证**（误差 ≤ 1e-13，见 §5.4 的实测表）。

### 5.1 单个源节点的“带偏移世界姿态”

父约束的绑定意图是：**被约束节点应当出现在“源节点世界姿态”叠加一个固定局部偏移 `Off` 后的位置**。在 ufbx 的节点变换约定下（`ufbx.h:844-940`，`ufbx.c:22839-22909` 的 `ufbxi_get_transform`），这个组合是：

```
W_target = T_t · R_t · S_t              // 源节点 node_to_world 分解出的 TRS
Off      = { off_t, off_r, off_s }      // targets[i].transform

W_desired.translation = T_t + R_t · ( S_t ⊙ off_t )        // 注意 S_t 逐分量
W_desired.rotation    = R_t · off_r
W_desired.scale       = S_t ⊙ off_s
```

**关键点：`off_t` 必须乘上源节点的世界缩放 `S_t`**，因为 ufbx 的变换是 `T·R·S` 且在 S 之前作用于局部空间。忽略 `S_t` 会产生 1.2 量级的位移误差（在 `ParentA` 这类非均匀缩放的源上非常明显：`S_t = (1.330, 0.760, 1.0)`）。

**反例（错误做法）**：直接 `ufbx_matrix_mul(&target->node_to_world, &ufbx_transform_to_matrix(&target_transform))`。这在存在非均匀缩放时会产生**剪切（shear）**，因为 `S_t · R_off` 不是可对角化的 TRS。实测矩阵差：

```
target ParentA: |TRS 公式 − (M_t · M_off)| 的 3x3 部分 = 0.2210
target ParentB:                                           0.0565
```

即：**矩阵直乘 ≠ TRS 复合**。若预览器内部用矩阵，必须按上式先算 TRS，再 `ufbx_transform_to_matrix`。

### 5.2 多源混合

`targets[]` 的语义是“加权平均的相对权重”（`ufbx.h:3326` 注释：“does not always sum to 1”）。约束整体权重 `constraint->weight` 是另一层乘子。建议的混合：

```
w_i = targets[i].weight * constraint->weight
Wnum = Σ w_i
若 Wnum ≤ 0：不施加该约束（保持原 local_transform）

translation = Σ (w_i / Wnum) · W_desired_i.translation
scale       = Σ (w_i / Wnum) · W_desired_i.scale

rotation：先把每个 W_desired_i.rotation 对齐到同一半球（与第一个非零权重的四元数点积 < 0 则整体取反），
          再按 (w_i / Wnum) 加权求和并归一化。
```

**顺序敏感**：半球对齐的参考四元数取 `targets[0]`，所以 `targets[]` 顺序变化会翻转混合结果。这就是 §1.2 强调“保留 targets 顺序”的原因。

对 `maya_double_parent_7500_ascii.fbx`（两个源权重均 1，静止姿态下两者解出的世界姿态都恰为单位变换）实测混合误差：`pos=8.87e-15, rot=5.52e-16`。

### 5.3 落回被约束节点的 local

预览器最终需要的是 `node->local_transform`。给定父节点世界矩阵 `P`：

```
M_desired_world = transform_to_matrix(W_desired_blended)
M_local         = P⁻¹ · M_desired_world
L               = matrix_to_transform(M_local)

最终 local：
  translation.c = constrain_translation[c] ? L.translation.c : 原 local.translation.c
  rotation      = 全三轴都开启时取 L.rotation，否则保留原值（见 §4.1 的轴掩码缺陷）
  scale.c       = constrain_scale[c]      ? L.scale.c      : 原 local.scale.c
```

如果被约束节点没有父节点（`parent == NULL`，例如 root 的直接子节点被约束），`P = identity`。

**通道掩码 `constrain_*[3]` 必须被尊重**：只开启旋转的父约束不能改动 translation；否则预览器会在源动画时出现“被约束节点被平移吸走”的抖动。

实测（`probe45`，用上述参考实现重建静止姿态）：

```
data/maya_double_parent_7500_ascii.fbx  Node_parentConstraint1     dT=8.87e-15 dR=5.52e-16
data/maya_constraint_zoo_6100_ascii.fbx joint1_parentConstraint1   dT=0        dR=0
data/maya_constraint_zoo_7500_ascii.fbx joint1_parentConstraint1   dT=0        dR=0
```

### 5.4 不满足 §5.1 时会发生什么（可观察姿态判据）

如果接入方漏掉 `S_t`、或改用矩阵直乘、或漏掉通道掩码，都会在**静止姿态**下就被检出。以下判据可直接写进预览器的自检：

**判据 A（静止姿态回代）**：加载后、不做任何动画求值时，用 §5.1–5.3 由 `targets[]` 反算 `local_transform`，与文件实际 `local_transform` 比较。对上表三个样本应满足 `|ΔT| ≤ 1e-6` 且四元数差（取半球最小）`≤ 1e-6`。这是最强、最便宜的一致性检查——它不依赖任何参考渲染。

**判据 B（动画跟随）**：对源节点施加已知平移 `Δ`，被约束节点世界位置的移动量应为 `Δ`（当 `constrain_translation` 全开且 `S_t` 为单位时）。样本实测：施加 `Δ=(100,0,0)` 后当前实现移动 `0`——判据 B **失败**，证明约束未被求解。

**判据 C（权重单调性）**：两个源分别向相反方向移动固定距离时，被约束节点位置应在两者连线上按 `weight` 比例插值；把某源 `weight` 从 1 改到 0，其影响应线性消失。当前 ufbx 不提供这个行为，属于接入方实现后的验收项。

**判据 D（非均匀缩放敏感度）**：在源节点缩放为 `(1.33, 0.76, 1.0)` 的场景里，使用矩阵直乘会在静止姿态产生 ≥ 0.05 的 3x3 偏差（§5.1 实测 0.221 / 0.0565）。若你的实现通过了判据 A 但在这类文件上失败，几乎可以确定是矩阵直乘问题。

### 5.5 当前 ufbx 的行为 vs 建议方案

| 项 | 当前行为 | 建议（未实现） |
|---|---|---|
| 偏移乘法 | 仅存 `target->transform`，不参与任何计算 | 按 §5.1 计算 `T + R·(S⊙off_t)`、`R·off_r`、`S⊙off_s` |
| 多源混合 | 不混合 | §5.2 加权平均 + 半球对齐 |
| 通道掩码 | 只解析出 `constrain_*[]` | §5.3 逐通道取用 |
| 落回 local | 不写回 | `P⁻¹ · W_desired` → `matrix_to_transform` |
| 迭代/依赖顺序 | 无 | 约束之间有依赖时需按 DAG 迭代（本仓库样本无此情况，但 Maya 允许） |
| 静止姿态校验 | 无 | 判据 A 应作为加载期断言/日志 |

**注意**：不要在 `ufbx_evaluate_scene()` 返回的 scene 上原地写 `local_transform`——节点矩阵不会因此重建，必须同时重算 `node_to_parent` / `node_to_world`（或直接用 §5.3 的结果自己维护世界矩阵）。`evaluate_scene` 的 `ufbxi_update_scene(&ec->scene, false, …)`（`ufbx.c:26411`）会在下次覆盖你的写入。

---

## 6. 场景坐标换算与层级共同生效时的保留关系

> 本节区分**已实测**与**未实现**：ufbx 当前**完全不处理**约束偏移的坐标换算；下面给出接入方应采用的补偿规则，并给出实测验证。

### 6.1 ufbx 在换算时改动了什么

`ufbxi_transform_to_axes()`（`ufbx.c:24954-24989`）与 `ufbxi_update_adjust_transforms()`（`ufbx.c:23679-23806`）只作用于**节点**：

- `TRANSFORM_ROOT`：把轴矩阵乘进 root 的 `local_transform`（`ufbx.c:24977-24987`）。
- `ADJUST_TRANSFORMS`：给 `node_depth ≤ 1` 的节点写 `adjust_pre_rotation` / `adjust_pre_scale`（`ufbx.c:23758-23764`）；这些值会被 `ufbxi_get_transform()`（`ufbx.c:22890-22900`）并进 `local_transform`。
- `MODIFY_GEOMETRY`：除上述外，还通过 `adjust_translation_scale` 缩放平移（`ufbx.c:23765-23772`、`ufbx.c:22892-22895`），并把 `geometry_scale` 应用到网格顶点（`ufbx.c:21198`、`ufbx.c:21223`）。
- 左右手系翻转：当轴矩阵行列式为负且指定了 `handedness_conversion_axis`，对每个非 root 节点设置 `adjust_mirror_axis`（`ufbx.c:24959-24973`），`ufbxi_get_transform()` 会镜像平移与旋转（`ufbx.c:22898-22901`，镜像函数见 `ufbx.c:22748-22761`）。

**约束元素不在上述任何循环里**：`scene->constraints` 未被 `ufbxi_transform_to_axes` / `ufbxi_update_adjust_transforms` 访问。实测（`probe54`）：同一文件在 `LH z-up / ADJUST / mirror=Z` 下加载后，`targets[0].transform` 与 `transform_offset` 与原始文件**逐位相同**，`aim_vector` / `aim_up_vector` 也未镜像（`probe52`）。

### 6.2 后果

- 对**纯旋转轴换算**（`RH y-up → RH z-up`）与**单位换算**，偏移数值不必改变即可复用：实测在 `TRANSFORM_ROOT` / `ADJUST_TRANSFORMS` / `MODIFY_GEOMETRY(m 单位)` 下，`raw` 公式（`k=1`）的误差均为 1e-14 量级。原因：偏移是**在世界坐标系中、作用于源节点局部空间**的量，源节点世界矩阵本身已被换算好，偏移只需在源局部空间表达——而纯旋转/单位换算不改变“局部空间中的方向/相对长度”。
- 对**几何缩放**（`MODIFY_GEOMETRY`，`metadata.geometry_scale ≠ 1`）：**必须**把偏移平移乘以 `geometry_scale`。实测 `target_unit_meters=1.0`（`geometry_scale=0.01`）下，`k=1` 误差 `5.27`，`k=geometry_scale` 误差 `1.53e-16`。`probe26` 的 `posGcomp` 列即此结论。
- 对**左右手系翻转**：必须按 `metadata.mirror_axis`（或等价的根缩放负轴）镜像偏移的平移与旋转。实测 `LH y-up`（`root_scale = -1`，镜像落在 x）下：不镜像旋转误差 `1.29`；按 x 轴镜像后旋转误差回落到 `1.1e-15`。该样本位置误差在两个版本下都是 `1.1e-14` 量级——只说明这个样本的平移方向恰与镜像轴正交，**不能**据此认为平移无需镜像。`LH z-up / mirror=Z` 下按 z 轴镜像后 pos/rot 均回落到 `1.5e-14` / `1.1e-15`，这一组才同时验证了平移与旋转的镜像必要性。
- 对**仅有根缩放负值、但 `metadata.mirror_axis == 0`** 的老式行为（`LH y-up` 默认 `handedness_conversion_axis = NONE` 时）：镜像信息只体现在 `root_node->local_transform.scale` 的负号上，`metadata.mirror_axis` 为 0。接入方必须从 root 的局部缩放里推断镜像轴。

### 6.3 建议的统一补偿（未实现）

```
mirror_axis = metadata.mirror_axis
若为 NONE 且 metadata.root_scale < 0：
    取 root_node->local_transform.scale 中为负的分量对应轴（x=1,y=2,z=3）
k = metadata.geometry_scale        // 带符号；MODIFY_GEOMETRY 下可 < 0

off_t' = mirror_translation(targets[i].transform.translation, mirror_axis) * k
off_r' = mirror_rotation  (targets[i].transform.rotation,    mirror_axis)
```

镜像函数需与 ufbx 同构（`ufbx.c:22748-22761`）：平移取 `v[axis-1] = -v[axis-1]`；四元数取 `q[axis%3]` 与 `q[(axis+1)%3]` 变号。

验证结果（`probe48` / `probe49`，`pos` 为逐源最大位置误差、`rot` 为四元数半球最小差）：

| 换算模式 | 不补偿 pos/rot | 按 §6.3 补偿 pos/rot |
|---|---|---|
| 默认（y-up, cm） | 1.5e-14 / 1.2e-15 | 同左 |
| RH z-up × {ROOT, ADJUST, MODIFY} | ≤1.4e-14 / ≤1.1e-15 | 同左 |
| LH y-up ROOT / ADJUST | 1.1e-14 / 1.29 | 1.1e-14 / **1.1e-15** |
| LH y-up MODIFY（`gs=-1`） | 10.6 / 1.15e-15 | 1.2e-14 / **1.2e-15** |
| unit → m, ROOT / MODIFY | 1.5e-16 / 5.27 | 1.5e-16 / **1.5e-16** |
| LH z-up ROOT, mirror=Z | 1.19 / 1.19 | 1.5e-14 / **1.1e-15** |

注意：`LH y-up ROOT` 的镜像轴来自 `root_scale < 0` 而非 `metadata.mirror_axis`（后者为 0），这是 §6.3 分支 2 的必要性来源。

### 6.4 只消费求值场景会遗漏什么

`ufbx_evaluate_scene()` 的价值与边界，逐条对照：

**它会做的**：
- 重新求值每个元素的属性（`ufbx.c:26406`），包括约束的 `.Weight` / `.Offset *`；
- 重算所有节点的 `local_transform` / `node_to_world`（`ufbx.c:26411` → `ufbx.c:23811`）；
- 把约束指针重映射到新场景（`ufbx.c:26315-26330`）；
- **完整继承源场景的坐标换算结果**：实测 `LH z-up / ADJUST` 下对 scene 做 `evaluate_scene(NULL anim, t=0)`，所有节点 `node_to_world` 与源场景逐位相同，`adjust_*` 字段也被复制（`probe51`）。

**它不会做的**：
- **不会让被约束节点跟随源**（§3.1 实测：源移到 x=100，被约束节点纹丝不动）；
- **不会把偏移换算到目标坐标系**（§6.1 实测：镜像后偏移仍是原值）；
- **不会求解 IK / Aim / Position / Rotation / Scale 约束**——本文只讨论 Parent，但同一结论适用（`ufbxi_update_constraint` 对所有类型都只做字段填充）；
- **不会写入 `constraint->node->local_transform`**，因此不存在“先 evaluate 再读世界矩阵就能拿到解”的捷径；
- **不会在求值场景里新造约束**。`ec->scene = ec->src_scene`（`ufbx.c:26115`），约束列表只是**浅拷贝 + 指针重映射**，元素数量与源场景一致。

**因此接入方的正确姿势**：

1. 用 `ufbx_load_file()` 拿到原始 scene（决定坐标换算选项在这里生效，且 `metadata.*` 记录了换算方式）。
2. 需要每个时刻：`ufbx_evaluate_scene()` 求源节点动画 → 拿 `targets[]` 的当前权重/偏移 → 按 §5 求解 → 按 §6.3 补偿 → 自己写 `local_transform`，并**自行重算**受影响节点的 `node_to_world`（evaluate 场景的矩阵不会被你的写入自动更新）。
3. 不要依赖 `evaluate_scene` 输出的约束结果——它不存在。

---

## 7. 支持边界（源码依据）

| 能力 | 状态 | 依据 |
|---|---|---|
| 解析约束类型（6 种） | 支持 | `ufbx.c:14808`、`ufbx.c:14817` |
| 源节点列表 + 顺序 | 支持，顺序 = 连接顺序 | `ufbx.c:22570-22590` |
| 6x00/7x00 连接方向兼容 | 支持（双扫） | `ufbx.c:22575-22584` |
| 每源权重（100 基准 / IK 1 基准） | 支持 | `ufbx.c:23426-23442` |
| 每源绑定偏移（仅 7x00） | 支持 | `ufbx.c:23444-23458` |
| 偏移/权重动画化 | 支持（属性链路） | `ufbx.c:26406-26411` |
| `AffectTranslation*` / `AffectRotation*`（7x00 模板） | **部分**：模板不生效，只有对象自身属性可读 | `ufbx.c:12191`，实测 `props.defaults == NULL` |
| `AffectScaling*` | **不支持**（键名写成 `AffectScale`） | `ufbx.c:23478`，实测 |
| `transform_offset`（约束级） | 解析但不消费 | `ufbx.c:23424` |
| IK pole vector | **错位**（读 `PoleVectorType`） | `ufbx.c:23489`，实测 |
| IK pole type | 枚举存在但无字段 | `ufbx.h:3345-3352` vs `ufbx.h:3354-3394` |
| 约束求解（任何类型） | **不支持** | 全文无写入 `constraint->node` 的代码；`ufbx.c:23865` 仅更新字段 |
| 约束与坐标换算的联动 | **不支持** | `ufbx.c:24954`、`ufbx.c:23679` 均不遍历 constraints |
| 约束的烘焙 | 无特殊处理（仅普通属性） | `ufbx.c:27556-27590`、`ufbx.c:27621-27635` |
| 节点 → 约束反向索引 | 无字段，需遍历 | `ufbx.h:844-995` 无 `constraints` |
| 约束指针的求值场景重映射 | 支持 | `ufbx.c:26315-26330` |
| 测试覆盖 | 仅字段断言（权重、类型、掩码、target 名），无姿态/求解断言 | `test/test_constraints.h:18-99` |

---

## 8. 给预览器的落地清单

1. **加载时**：`ufbx_load_file()` 后用一次 `ufbxi_update_scene` 已完成的 `targets[]`；记录 `metadata.space_conversion` / `root_scale` / `geometry_scale` / `mirror_axis` / `handedness_conversion_axis`。
2. **修补 §4 的缺陷**：若文件里有 `AffectScalingX/Y/Z`，自行读 `constraint->props` 覆盖 `constrain_scale[]`；若要 IK pole，自行读 `"PoleVector"`。
3. **每帧**：`ufbx_evaluate_scene()` → 对每个 `constraint->type == UFBX_CONSTRAINT_PARENT` 的约束，按 §5 求 `W_desired`，按 §6.3 补偿偏移，按 §5.3 落回 `local_transform`，然后**自底向上重算** `node_to_world`（节点顺序已是父先子后，`ufbx.c:18591-18600` 的 `node_depth` 排序可保证一次线性遍历）。
4. **自检**：把 §5.4 判据 A 作为加载期断言（静止姿态回代），判据 B/C 作为回归测试。
5. **不要**在 `evaluate_scene` 的节点上留下自己写的 `local_transform` 而不重算矩阵；也**不要**假设 `evaluate_scene` 会替你解约束。

---

## 附录 A：实测样本与命令

样本（`data/`）：

- `maya_double_parent_7500_ascii.fbx` — 单约束、双源、非均匀缩放源、非零 `Offset T/R`。是验证 §5.1 的关键样本。
- `maya_constraint_zoo_7500_ascii.fbx` / `maya_constraint_zoo_6100_ascii.fbx` — 覆盖 6 种约束类型；父约束的 `Offset R` 为 `(0,0,90)`，但重建后的静止姿态与 `local_transform` 重合（因源 `Parent` 世界变换恰为单位旋转，90° 偏移被源自身旋转抵消）。
- `maya_constraint_multi_7500_ascii.fbx` — 双源 Position 约束，用于验证 `targets[].weight` 读取（`test/test_constraints.h:88-110`）。

复现探针位于 `.tmp-probe/`（未纳入版本控制），核心为 `probe45`（参考求解）、`probe48/49`（坐标补偿表）、`probe51`（evaluate 保留换算）、`probe44`（约束不参与求值）。这些探针只读 `ufbx.h` + 编译 `ufbx.c`，不修改库源码。

## 附录 B：术语对照

| 本文用语 | 代码字段 |
|---|---|
| 被约束节点 | `constraint->node` |
| 源节点 | `constraint->targets[i].node` |
| 每源权重 | `constraint->targets[i].weight` |
| 每源绑定偏移 | `constraint->targets[i].transform`（T/R/S） |
| 约束整体权重 | `constraint->weight` |
| 约束级偏移（未消费） | `constraint->transform_offset` |
| 影响轴掩码 | `constraint->constrain_translation/rotation/scale[3]` |
| 场景坐标换算记录 | `scene->metadata.space_conversion/root_scale/geometry_scale/mirror_axis/handedness_conversion_axis` |
