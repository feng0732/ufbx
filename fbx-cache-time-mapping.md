# FBX 场景时间 → 外部几何缓存通道的映射，以及缺帧时顶点的来源

基于 ufbx（本仓库 `ufbx.h` / `ufbx.c`）的实际实现与 `data/` 下真实缓存样本分析。
所有结论均标注代码位置或数据文件，区分「可直接证实」与「需要额外证据」的部分。

---

## 0. 数据流总览

FBX 场景中的外部几何缓存由三层对象构成：

| 层 | FBX/文件元素 | ufbx 类型 | 数据来源 |
|---|---|---|---|
| 场景绑定 | `Deformer: …, "VertexCacheDeformer"`（含 `ChannelName` 属性） | `ufbx_cache_deformer` | FBX 文件内 |
| 文件引用 | `Cache: …`（含 `CacheFileName` / `CacheAbsoluteFileName` / `CacheFileType`） | `ufbx_cache_file` | FBX 文件内 |
| 外部数据 | `.xml` + `.mc/.mcx`（Maya）或 `.pc2`（3ds Max Point Cache 2） | `ufbx_geometry_cache` → `ufbx_cache_channel` → `ufbx_cache_frame` | 外部文件，仅 `load_external_files` 时加载 |

连接关系（ASCII FBX 中可见）：`Cache --OO--> VertexCacheDeformer --OO--> Geometry`。
`data/maya_cache_sine_7500_ascii.fbx:579-618, 681-691` 显示一个网格可挂多个
deformer（位置通道 + 法线通道各一个），多个 deformer 可指向同一个 `Cache` 对象。

加载流水线（`ufbx.c`）：

1. `ufbxi_load_external_files()`（24886）去重后调用 `ufbxi_load_external_cache()`（24821）；
2. `ufbxi_cache_load_imp()`（24644）先读根文件（按 magic 分发 PC2 / MC / XML，24435-24441），
   若是 XML 再由 `ufbxi_cache_load_frame_files()`（24464）按调度表逐帧打开 `.mc/.mcx`；
3. 全部采样块归并为扁平的 `cache.frames`，排序后由 `ufbxi_cache_setup_channels()`（24579）
   按通道名切分成 `cache.channels[]`，每通道的帧按时间升序排列；
4. 求值时 `ufbxi_evaluate_skinning()`（25063）按场景时间调用
   `ufbx_sample_geometry_cache_vec3()`（32953）在通道帧之间查找/插值，
   数据本身在 `ufbx_read_geometry_cache_real()`（32704）中按 `data_offset` 现读，并不常驻内存。

---

## 1. 三类证据各自能证实的「顶点位置 ↔ 时间」关系

### 1.1 XML 元数据（`.xml`，仅 Maya/MC 系列存在）

解析代码：`ufbxi_cache_load_xml_imp()`（`ufbx.c:24312-24401`）。它只读取：

- `cacheType`：`OneFile`（单一 `.mc/.mcx`）或 `OneFilePerFrame`（每帧一个文件）；
- `cacheTimePerFrame TimePerFrame`：全局每帧 tick 数，缺省 250（24317）；
- 每个 `channelN` 的 `ChannelName`、`ChannelInterpretation`、
  `SamplingRate`、`StartTime`、`EndTime`（24385-24394），存入 `ufbxi_cache_tmp_channel`；
- `extra` 文本（来源软件等）。

**能证实的：**

- 通道的**名义**采样计划：速率（tick/采样）、起止 tick、语义名（positions/normals/…）。
  例：`data/maya_cache_sine_7500_ascii_fpc/pCubeShape1.xml:11-12` 声明两个通道，
  `SamplingRate=250, StartTime=250, EndTime=7500`，即 24fps 下 1s 时长、每帧一个采样。
- 多通道频率不一致时，**各通道声明的速率可以不同**。调度循环（24505-24521）
  对每通道独立维护 `current_time`，步长取 `chan->sample_rate ? chan->sample_rate : xml_ticks_per_frame`，
  每轮取所有通道下一个待试时间的最小值合并推进——XML 层面没有统一重采样。

**不能证实的：**

- **任何顶点坐标**。XML 不含坐标，只含文件名规则与时间表。
- **实际存在哪些采样块**。`ufbxi_cache_tmp_channel` 只是调度游标；
  真正的 `ufbx_cache_channel` 是在 `ufbxi_cache_setup_channels()` 中
  **按实际读到的帧**分组建立的（24584-24597）。XML 里声明了但磁盘上没有对应文件的通道，
  最终不会出现在 `cache.channels[]` 中（见 §3.2）。
- 注意 `<time Range=...>` 标签**根本未被解析**（代码只找 `cacheType/cacheTimePerFrame/Channels/extra`），
  它不构成任何时间证据。

### 1.2 实际采样块（`.mc/.mcx` 二进制块、`.pc2` 数据段）

这是唯一同时携带「时间戳 + 顶点字节」的证据，时间来源随格式不同：

**MC/Maya 缓存**（`ufbxi_cache_load_mc()`，24166-24247）：

- 每个数据块（`FVCA/DVCA/FBCA/DBCA/DBLA` 标签）生成一个 `ufbx_cache_frame`；
- 帧时间 = 块出现时的 `TIME`/`STIM` 整数 tick × `1/6000`（24229），单位秒。
  FBX/Maya 的时间整数一律是 6000 tick/秒，与帧率无关；
- 块还记录 `data_offset`（字节偏移）、`data_count`（点数）、元素大小、
  大端编码（24232-24236）和通道名（最近的 `CHNM`）。

实测 `data/caches/sine_mcmf_undersample/`（OneFilePerFrame，每文件含两个 `MYCH` 通道块）：

```
cacheFrame1.mc  STIM=250  → 0.041667s   cacheFrame11.mc STIM=2750 → 0.458333s
cacheFrame3.mc  STIM=750  → 0.125000s   …（步长 500 tick = 1/12s，与 XML SamplingRate=500 一致）
cacheFrame29.mc STIM=7250 → 1.208333s
```

即：**帧的权威时间是二进制块内的 STIM/TIME，不是 XML 推算或文件名里的 Frame 号。**
文件名（`Frame%u[Tick%u].mcx`，24525-24531）只是用来定位文件；打开后时间以块内标签为准。

**PC2 缓存**（`ufbxi_cache_load_pc2()`，24249-24296）：

- 32 字节头部：`num_points`、`start_frame`(f32)、`frames_per_sample`(f32)、`num_samples`(u32)；
- 第 i 个采样的时间 = `(start_frame + i * frames_per_sample) / frames_per_second`（24281-24283）。
  这里的 FPS 来自 `ufbx_geometry_cache_opts.frames_per_second`，场景加载时取
  `scene.settings.frames_per_second`（24828），独立调用 API 缺省 30（24752）；
- 数据为紧密排列的小端 float vec3，每点 12 字节，偏移按序累加（24287-24292）。

实测 `data/max_cache_box_7500_binary_fpc/max_cache_box.pc2`：
770 点 × 11 采样，start=40、步长 1 帧；30fps 下时间为 1.3333s … 1.6667s，
且文件实际大小与 `32 + 770×12×11` 精确相等，证实偏移表覆盖了全部真实数据。

**采样块能证实的：**

- 某一具体秒时间点上，某通道确实存在 N 个点的坐标块，以及它在文件中的字节位置；
- 通道真实频率（相邻块时间差）与 XML 声明是否相符；
- 多通道情况下每个通道各自的实际帧集合（块按 `CHNM` 归属通道）。

### 1.3 静态网格（FBX `Geometry` 的 `Vertices`）

- `mesh->vertices` 直接来自 FBX Geometry 节点的 `Vertices:` 数组
  （赋值点 `ufbx.c:13468` 与 `16204`，零顶点补丁 16692-16699）。
- 它**没有时间维度**：是绑定姿态/建模姿态下的一份位置，不随 `time` 变化。
- 它能证实的是「缓存不可用时顶点长什么样」的兜底形状，以及拓扑（点数、面索引），
  后者还用来核对缓存块点数是否与网格匹配（求值时要求 `num_read == num_vertices`，25098）。
- `data/maya_cache_sine_7500_ascii.fbx:285-286` 的 `Vertices: *108`（36 顶点 ×3）
  与测试中缓存每帧 36 点的断言（`test/test_cache.h:25`）互相印证了这种点数契约。

### 1.4 三者关系小结

| 问题 | XML 元数据 | 采样块 | 静态网格 |
|---|---|---|---|
| 通道名义速率/起止时间/语义 | ✅ 权威 | — | — |
| 某秒时间点是否真有坐标数据 | ❌ 仅计划 | ✅ 权威（STIM/TIME 或 PC2 推算） | ❌ |
| 顶点坐标的具体数值 | ❌ | ✅ 唯一来源 | ✅ 仅静态姿态一份 |
| 顶点数/拓扑契约 | — | `data_count` | `num_vertices`，两者需相等才采纳缓存 |
| 帧在文件中的字节位置 | ❌ | ✅ `data_offset` | — |

---

## 2. 场景时间如何映射到不同采样频率的通道

### 2.1 时间的两条入口

- 加载期一次性求值：`ufbxi_evaluate_skinning(..., time=0.0, ...)`（25371-25372）；
- `ufbx_evaluate_scene()` 按调用者给定的 `ec->time`（秒）求值（26417-26418）。

两个入口都把同一个 `time` 传给每个缓存 deformer，**ufbx 不做任何帧率换算**：
MC 侧用 6000 tick/秒写入秒，PC2 侧在加载时已用场景 FPS 折成秒，因此采样时各方都在「秒」域比较。

### 2.2 通道内的查找规则（`ufbxi_sample_geometry_cache_real()`，32869-32935）

帧数组按时间升序，算法为二分 + 线性扫描定位首个 `frame.time >= time` 的帧，然后：

1. **时间早于首帧**：直接返回首帧（钳制，不外推）；
2. **时间晚于末帧**：直接返回末帧（钳制，32933-32935）；
3. 与相邻任一时间差小于 `eps = 1e-8` 秒：吸附到该帧，不插值（32912-32917）；
4. 落在两帧之间：线性插值，权重 `t=(time-prev.time)/(next.time-prev.time)`，
   先以 `1-t` 权重读 prev，再以 `t` 累加读 next（32919-32930）。

插值是逐点线性混合，两次读取都真正打开外部文件按 offset 取数
（`ufbxi_read_geometry_cache_real()`，32757 打开失败即返回 0）。

### 2.3 频率不一致的多通道如何并存

- 每个 `ufbx_cache_channel` 拥有**独立的帧时间数组**，采样函数一次只接收一个 channel，
  因此 250/125/75/500 tick 等不同速率的通道互不干扰，各自做各自的二分+插值；
- 一个网格上的多个 deformer 在 `ufbxi_evaluate_skinning()` 中按
  `interpretation` 分流（25096-25115）：位置类（`VERTEX_POSITION` 或泛化 `POINTS`）
  写 `skinned_position`，法线类写 `skinned_normal`；同类只采纳第一个成功者
  （`cached_position` / `cached_normals` 标志位）；
- 解释名匹配是宽松 glob（24573-24577）：`positions?`/`points?`/`normals?`；
  PC2 通道一律视为顶点位置（24606-24607）；匹配不上则为 `INTERPRETATION_UNKNOWN`，
  求值时被跳过。
- 仓库的四档频率样本正好覆盖全部组合（XML 声明 vs 实测）：

| 样本 | 类型 | XML 速率 | 实测节奏 | 测试容差 |
|---|---|---|---|---|
| `sine_mxsf_regular` | OneFile/mcx | 250 tick | 与帧率相同 | 0.008 |
| `sine_mcsd_oversample` | OneFile/mc | 125 tick（2 倍过采样） | 单文件多块 | 0.003 |
| `sine_mxmd_oversample` | PerFrame/mcx | 75 tick（含 Tick 文件名） | `Frame10Tick75` 等 | 0.003 |
| `sine_mcmf_undersample` | PerFrame/mc | 500 tick（2 倍欠采样） | 仅奇数 Frame 文件 | 0.04 |

测试（`test/test_cache.h:69-81`）以 24fps 的 1/10 帧步长在整条时间轴上采样并与正弦解析解比较：
过采样误差最小（0.003），欠采样因大量时间点落在两帧之间走线性插值、误差最大（0.04）——
这实证了「不同频率通道在秒域独立插值、场景时间不需要与任何通道的采样时刻对齐」。

---

## 3. 缺帧 / 外部文件不可用时顶点来自何处

### 3.1 采样块层面的「缺帧」

**OneFilePerFrame 的容错调度**（24500-24543）：

- 按合并后的最小时间拼出候选文件名尝试打开；`ufbxi_cache_try_open_file()`（24446）
  打开失败时 `*p_found=false` 但**不报错、不中断**；
- 找不到文件的通道累加 `consecutive_fails`，连续失败超过 10 次才放弃该通道（24506）；
- 因此缺失的帧**根本不产生 `ufbx_cache_frame`**，通道帧数组里没有空洞，只有时间间隔变大。

后续采样遇到这种间隔时行为完全确定：落在间隔内的场景时间走 §2.2 的线性插值
（用最近的两个**存在**的帧），超出首尾则钳制到端点帧。不会返回错误，也不会回退静态网格——
前提是该通道至少成功加载过一个块。`sine_mcmf_undersample` 只有 15 个文件
（Frame1,3,5,…,29，缺全部偶数帧）仍通过全部采样断言，就是这条路径的证据。

**读取期失败**（文件在采样时才被打开，32757）：若 offset 跳转失败或文件此刻不可读，
`ufbxi_read_geometry_cache_real()` 返回 0；插值两次读取中若返回数量不足，
`ufbxi_evaluate_skinning()` 发现 `num_read != num_vertices`（25098），
则 `cached_position` 保持 false，落入下面的静态回退。

### 3.2 整个外部文件不可用

分两种情形（`ufbxi_load_external_cache()`，24851-24865）：

1. 默认：先试相对路径，再试 `CacheAbsoluteFileName`（24840-24845）；都找不到则
   `UFBX_ERROR_EXTERNAL_FILE_NOT_FOUND`，**整个场景加载失败**。
   实证：`data/synthetic_missing_cache_fail_7500_ascii.fbx` 引用不存在的
   `missing_cache.xml`，测试断言加载错误正是该类型且 info 为文件名
   （`test/test_cache.h:179-185`）。
2. 设置 `ignore_missing_external_files`：发出 `UFBX_WARNING_MISSING_EXTERNAL_FILE`
   警告后继续（24853-24855），此时 `cache_file.external_cache` 与
   `deformer.external_channel` 均为 NULL（24932 的 NULL 检查会跳过补丁）。

### 3.3 求值时的静态回退链（25090-25137）

对每个网格，位置结果 `result_pos` 的产生顺序：

1. 若 `load_caches` 为真且存在 `external_channel != NULL` 的位置类通道，
   尝试缓存采样；**恰好读满 `num_vertices` 个点**才置 `cached_position=true`，
   并标记 `skinned_is_local=true`（缓存数据是几何局部空间，25099-25100）；
2. 否则 `cached_position==false`，执行：
   `memcpy(result_pos, mesh->vertices.data, ...)`（25120）——**顶点首先来自 FBX 静态网格**；
3. 在静态副本上叠加混合变形（blend shape）偏移（25122-25124）；
4. 若有蒙皮，再逐顶点做骨骼矩阵变换（25127-25135），并置 `skinned_is_local=false`
   （结果已是世界空间）。

即「缓存缺帧时顶点来自何处」的准确答案分两级：

- **个别帧缺失、通道尚存**：顶点仍 100% 来自缓存——由相邻存在的帧线性插值
  （或端点钳制）产生，静态网格不参与；
- **通道整体缺失/不可读/点数不匹配/未开启 `evaluate_caches`**：
  顶点来自 `mesh->vertices` 静态数组，再照常走 blend shape + 蒙皮管线，
  与该网格从未挂过缓存时的结果完全一致。

法线同理：缓存法线通道失败时，由 `ufbxi_compute_normals()` 从（可能已被缓存位置填充的）
位置重新生成（25141-25168），代码中 `// TODO: Is this right at all?`（25103）也表明
缓存法线的语义本身在实现里仍属未完全确定的部分。

### 3.4 通道名对不上的特殊处理

补丁 deformer→channel 时（24936-24948）：若缓存只有一个通道，**忽略通道名直接绑定**
（注释说明是 Marvelous→Maya 导出名称不匹配的兼容 hack）；多通道时按名二分查找，
找不到则 `external_channel` 留 NULL，该 deformer 在求值时被静默跳过 → 静态回退。

---

## 4. 可判定的结论 vs 需要额外证据的结论

### 4.1 仅凭代码 + 文件即可判定

1. **时间映射机制是确定的**：MC 时间 = 块内 tick/6000；PC2 时间 =
   `(start_frame + i·frames_per_sample)/scene_fps`；场景时间以秒为单位在各通道
   独立二分查找，帧间线性插值、区间外钳制、1e-8 秒内吸附。
2. **不同采样频率的通道不需要、也不会被统一重采样**；每通道独立帧表，
   多通道按 interpretation 分流到位置/法线，同类首个成功者生效。
3. **XML 只是调度表与语义标签**：不提供坐标，不保证采样块真实存在；
   未被采样块证实的 XML 通道不会进入 `cache.channels[]`；`<time Range>` 被解析器忽略。
4. **缺帧的两种确定性后果**：块级缺失 → 间隔变大，插值/钳制兜底，顶点仍源自缓存；
   文件级缺失 → 默认加载失败（`EXTERNAL_FILE_NOT_FOUND`），忽略模式下降级为
   静态 `mesh->vertices` + 变形/蒙皮管线。
5. **采纳缓存有严格点数契约**：读满 `num_vertices` 才采纳，否则整体回退，
   不会出现「半缓存半静态」的混合顶点数组。
6. 坐标读取还会应用缓存级 `scale_factor`（场景几何缩放，24836-24837）与
   `mirror_axis`（坐标轴转换），这些变换参数记录在每个 frame 上（24627-24630），
   可由加载选项 `ignore_transform` 关闭。

### 4.2 需要额外证据才能成立的结论

1. **XML 声明速率与实际块速率是否一致**：必须解析二进制块的 STIM/TIME（或 PC2 头部）
   才能核对。XML 的 `SamplingRate` 仅驱动「尝试打开哪些文件名」，若文件内时间标签
   与计划不符，最终帧时间以二进制为准——二者漂移的后果无法只从 XML 判断。
2. **某个具体场景时间点上输出顶点的正确性**：需要同时具备 (a) 可读的外部文件、
   (b) 与网格相等的点数、(c) 参考几何。本仓库靠导出的 OBJ 帧
   （如 `maya_cache_sine_12.obj`，经 `ufbxt_check_frame()` 在
   `evaluate_caches=true` 下比对，`test/test_transform.h:26-67`）提供这种证据；
   没有参考帧时，只能判定「数据来自哪个帧/插值权重」，不能判定数值对错。
3. **欠采样/过采样对视觉质量的影响**：测试误差容差（0.003 vs 0.04）只对该正弦
   测试几何成立；其它内容的误差上界需要新的参考数据。
4. **OneFilePerFrame 模式超过 10 次连续缺帧后的通道截断行为**（24506）：
   阈值 10 是实现策略而非格式规范，对任意真实生产文件的触发情况需要实际文件验证；
   被截断通道的剩余时间区间将静默改由端点钳制处理。
5. **缓存法线通道的语义正确性**：源码 TODO（25103）明示存疑；法线是否应随位置
   插值方式相同、空间变换是否正确，需要带法线参考数据的专门测试。
6. **`INTERPRETATION_UNKNOWN` 通道的用途**：ufbx 只保留 `interpretation_name` 原文
   （`sine_xml_parse` 测试验证了未知解释被保留而非猜测，`test_cache.h:130-134`），
   其业务含义需要文件生产方（Maya/Marvelous/3ds Max）的格式文档或导出器证据。
7. **外部文件被篡改/截断时的数值行为**：解析器对截断报 "Truncated file"（24054）、
   未知 MC 标签报 "Unknown tag"（24217），但深度嵌套 XML 等畸形输入只保证返回错误
   （`cache_xml_depth` 测试，`test_cache.h:140-151`），不保证任何部分数据可用；
   安全性结论需要针对具体文件的模糊测试结果（`data/cache_fuzz/` 是该类证据的语料）。

---

## 附：关键代码索引

| 主题 | 位置 |
|---|---|
| 缓存数据结构（frame/channel/cache/deformer/file） | `ufbx.h:2170-2283` |
| MC 块解析与 tick→秒 | `ufbx.c:24166-24247`（24229 为 `/6000`） |
| PC2 头部解析与 FPS→秒 | `ufbx.c:24249-24296`（24281-24283） |
| XML 元数据解析 | `ufbx.c:24312-24401` |
| Per-frame 调度与 10 次失败截断 | `ufbx.c:24464-24547` |
| 通道分组与解释名匹配 | `ufbx.c:24573-24641` |
| 外部文件加载/缺失处理 | `ufbx.c:24821-24952` |
| 求值入口与静态回退 | `ufbx.c:25063-25177`（25120 为 memcpy 静态顶点） |
| 通道采样（二分/吸附/插值/钳制） | `ufbx.c:32869-32935` |
| 数据块实际读取（offset/缩放/镜像） | `ufbx.c:32704-32867` |
