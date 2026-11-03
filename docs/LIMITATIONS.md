# 已知边界与风险

## 1. 最大的两个未知（务必先读）

1. **未实机验证**：本工程在无游戏二进制、无编译器（MSVC/xmake 缺失）的离线环境完成。
   - C++ 侧只做了静态审阅，编译交给 CI；
   - `BaseLightTextureImageBuilder` 的类声明来自 LeviLamina 26.51 的 Bedrock 头文件，
     但 `$buildImage` 是否真的导出、`mce::Image` 的实际格式/尺寸、光照贴图 UV 轴向
     都需要按 [TESTING.md](TESTING.md) 的 `observe` 流程在实机确认。
2. **钩子失败是"静默降级"**：安装失败只会写日志，画面保持原版；
   成功安装但像素布局判断错误时，画面可能偏色 —— 因此默认 `mode=observe`。

## 2. 光照贴图钩子的局限

- 只影响**传统图形模式**；Vibrant Visuals 下光照由延迟管线计算，不读这张贴图
  （此时是资源包路线生效）。
- 只影响**方块光照**。基岩版的实体使用固定的假环境光（代码中 `AMBIENT = 0.45` 一类硬编码），
  不采样光照贴图，所以**生物明暗不会随本模组变化** —— 这是"基岩版实体看起来比 Java 平"的
  主要原因之一，需要另一套实体渲染钩子才能解决（未来工作）。
- 实体阴影：基岩版 RenderDragon 没有 `textures/misc/shadow.png` 这类可替换的阴影贴图
  （调研已确认），Java 的"方块投影 + 光照/高度衰减 alpha"阴影无法用资源包复刻；
  传统模式下本模组也不改变实体阴影形状。资源包路线通过 `shadows/global.json` 改善的是
  **方块投射阴影**的风格。
- 下界/末地的 `ambientLight` 与 Java 的维度参数不同（Overworld 0、Nether 0.1、End 0），
  本实现按 Overworld 语义计算；在下界可能出现与 Java 的细微差异。
- 水下视觉、熔岩雾、Boss 变暗等 Java 特性只实现了 `darkenWorld` 与夜视两项；
  水下滤镜（`getWaterVision`）未移植。

## 3. Vibrant Visuals 路线的局限

- 只在 VV 图形模式下生效；VV 本身对设备有要求（延迟渲染 + 阴影级联）。
- VV 使用物理量（lux）与色调映射，Java 是 16 级查表，**不可能逐像素等价**，
  只能做观感近似；具体参数在 `tools/java_look.json`，可自行调。
- 把发光方块全部设为 `static_light` 是为了贴近 Java（Java 没有点光源），
  代价是失去 VV 的动态点光/高光效果；如果你更想保留 VV 的点光，删掉
  `local_lighting/local_lighting.json` 即可。
- 资源包的 `format_version`（`lighting/global.json` 用 1.26.0、`shadows/global.json` 用 1.21.80、
  `local_lighting` 用 1.21.120）依赖 1.26.x 的解析器；低版本客户端会忽略或报内容错误。
- `manifest.json` 的 `min_engine_version` 为 `[1,21,120]`，低于该版本的客户端不会加载本包。

## 4. 兼容性 / 合规

- 客户端模组只影响本地渲染；不修改服务端逻辑、不写存档、不联网。
- 与其它修改光照贴图/光影的模组可能冲突（都钩 `buildImage` 时以加载顺序为准）；
  与光影资源包叠加时以资源包优先级决定最终效果。
- 本项目不含 Mojang 代码或资源；Java 算法为按公开反编译源码的重写实现，
  文档引用代码仅用于说明。请遵守 Minecraft EULA 与 LeviLamina 的许可。

## 5. 性能

- 光照贴图重建时才计算 LUT（256 次浮点运算 + 最多 256 像素写入），开销可忽略；
- `observe` 模式默认只在首次重建时导出 dump（`dumpEveryFrame=false` 时不会持续写盘）；
- 资源包方案的开销来自 VV 本身，与本模组无关。

## 6. 未来工作（按收益排序）

1. 实机确认 `buildImage` 的贴图尺寸/格式/UV 轴向后，把 `observe` 默认改为更智能的自动探测；
2. 实体渲染钩子：`ActorRenderDispatcher` / `BaseActorRenderContext` 路径，替换硬编码的
   `calculateLightIntensity` 假光照，让生物受光照贴图影响；
3. Java 风格实体阴影：用世界轴对齐的四边形 + 光照/高度衰减 alpha 绘制（Java 的
   `EntityRenderDispatcher.renderShadow` 逻辑），替代基岩版碰撞盒阴影；
4. 水下视觉、Boss 黑暗、末地闪光等剩余 Java 光照分支。
