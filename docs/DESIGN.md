# 原理与设计

## 1. 为什么是"双路线"

基岩版（RenderDragon 之后）有两条完全不同的光栅化路径，Java 版的观感也来自两套不同的机制：

| 图形模式 | 光照来源 | 可干预层 | 本工程对应 |
| --- | --- | --- | --- |
| 传统图形（Classic） | CPU 生成的 16×16 光照贴图（`BaseLightTextureImageBuilder`），方块顶点带 `v_lightmapUV` 采样；实体使用假法线的固定环境光 | 只能原生钩子改光照贴图 | 原生模组 `mode=java` |
| Vibrant Visuals（延迟渲染） | 真实光照（方向光/点光/环境光/天空光 + 阴影贴图），由资源包 JSON 驱动 | 资源包 `lighting/`、`shadows/`、`local_lighting/`、`pbr/` | `resource_pack/` |

调研结论（出处见 [research/bedrock-rendering.md](research/bedrock-rendering.md)）：

- 基岩版光照贴图是**CPU 侧程序化生成**的（不是贴图文件），传统资源包无法覆盖，也没有 `textures/lightmap`；
- 因此"传统模式想改光"只能原生钩子；
- Vibrant Visuals 提供官方数据驱动 JSON，是"延迟模式改光"的正规入口。

两条路线各自生效，可同时安装。

## 2. Java 版光照贴图移植

Java 版 `LightTexture.updateLightTexture`（1.21.2+ 位于 `lightmap.fsh`）逐像素计算
`color(skyLight, blockLight)`，写入 16×16 纹理（X=方块光，Y=天空光，线性过滤）。
完整推导与源码引用见 [research/java-lighting.md](research/java-lighting.md)。核心：

```
skyFactor   = skyDarken*0.95 + 0.05
blockFactor = 1.5（+ 火焰闪烁）
q = brightness(skyLight) * skyFactor
r = brightness(blockLight) * blockFactor
brightness(i) = (i/15)/(4-3*(i/15)) + ambientLight*(1-...)

c = (r, r*((r*0.6+0.4)*0.6+0.4), r*(r*r*0.6+0.4))     // 暖色方块光
skyColor = (0.65*skyDarken+0.35, 同, 1.0)             // 白天中性、夜晚偏蓝
c += skyColor * q
c = mix(c, 0.75, 0.04)
c = mix(c, c*(0.7,0.6,0.6), darkenWorld)
夜视：c = mix(c, c/max(c), nightVision)
c = clamp(c)
c = mix(c, notGamma(c), gamma)                        // gamma 默认 0.5
c = mix(c, 0.75, 0.04)
c = clamp(c)
```

`src/lighting/JavaLightmap.cpp` 是它的 C++ 版本；`tools/gen_reference.py` 是独立的 Python 版本，
两者用同一组数值（`tests/expected_lightmap.json`、FNV-1a 校验和）互相校验，CI 会跑 C++ 回归测试。

## 3. 原生钩子为什么选 `buildImage`

LeviLamina 26.51 客户端头文件给出了完整的类声明（`src-client/mc/client/renderer/ptexture/`）：

```cpp
class BaseLightTextureImageBuilder {
public:
    virtual bool buildImage(BaseLightData const&, mce::Image*, uint imageLength,
                            float a, float ambientBoost, bool clampToMinimum);
    MCNAPI bool $buildImage(...);   // 非虚 thunk，虚调用经由 vtable 进入这里
};
```

- `BaseLightData` 恰好携带 Java 所需的全部输入：`mGamma`、`mSkyDarken`、`mDarkenWorldAmount`、
  `mNightvisionScale/mNightvisionActive`、`mSkyFlashTime`、`mDimensionType`；
- 钩住 **非虚 thunk `$buildImage`**（LeviLamina 对虚函数的 `$` 写法）即可覆盖所有维度、所有重建时机；
- 之后直接改写 `mce::Image` 的像素（`mWidth/mHeight/mImageBytes`，格式 `RGBA8Unorm` 等）。

实现见 `src/lighting/LightmapHook.cpp`。

### 安全设计

1. **默认 `observe`**：先只安装钩子、导出基岩版光照贴图，不修改任何像素；
2. 像素写入前校验：格式（RGBA8/RGB8）、`mImageBytes.size()`、宽高；
3. 轴向/朝向/`skyDarken` 方向都可配置，避免猜错后画面失真；
4. 钩子安装失败（版本不匹配）只记日志，不影响游戏；`mode=off` 完全不安装；
5. 单次 LUT 计算 = 256 次浮点运算，重建时才执行，开销可忽略。

## 4. Vibrant Visuals 预设的数值依据

Java 版没有 PBR、点光源、动态彩色光；方块光永远走同一条暖色曲线，天空光白天中性、夜晚偏蓝，
环境光很低（洞穴接近纯暗但仍留 ~0.1 亮度），阴影是方块对齐的硬边投影 + 平滑 AO。
因此资源包做如下映射（`tools/java_look.json`）：

| JSON | 取值 | 理由 |
| --- | --- | --- |
| `shadows/global.json` | `"blocky_shadows"`, `texel_size: 16` | Java 阴影按方块面投影、边缘硬；块状量化最接近 |
| `lighting/global.json` → sun | 文档默认照度曲线 + `#FFF7EC` | 保留官方昼夜曲线，略暖 |
| → moon | `0.27` lx + `#C6D4FF` | Java 夜间天空光偏蓝，用冷色月光近似 |
| → ambient | `0.1` lx, `#FFFFFF` | Java 光照贴图最低约 `#191919`（非纯黑） |
| → sky.intensity | `0.8` | Java 阴影对比更强（天空间接光略低） |
| `local_lighting/local_lighting.json` | 发光方块 `static_light`，颜色 `#FFD18C` | Java 只有烘焙的暖色光，没有点光源/彩色光/动态高光；`#FFD18C` 取自 Java 暖色曲线 r≈0.5 处 |
| `pbr/global.json` | MERS `[0,0,255,0]` | Java 无 PBR：金属度 0、粗糙度最大、去除反射高光 |

> 这些是**近似映射**：VV 用物理单位（lux）与色调映射，Java 是 16 级查表；
> 二者无法等价，只能做到"观感接近"。参数都在 `tools/java_look.json` 里可调，改完重新运行生成脚本即可。

## 5. 数据流

```
游戏启动 → LeviLamina 加载 mods/java-lighting
        → JavaLighting::load()  读取 <dataDir>/config.json
                                导出 java_lightmap_reference.csv
        → JavaLighting::enable() 按 mode 安装 buildImage 钩子

进世界（传统图形）:
  Bedrock 重建光照贴图 → BaseLightTextureImageBuilder::$buildImage
                       → [钩子] origin() 生成原图
                       → mode=java ? 用 Java LUT 覆写像素 : 导出 dump

进世界（Vibrant Visuals）:
  RenderDragon 读取 resource_packs/JavaLighting/ 下的 lighting/shadows/local_lighting/pbr JSON
```
