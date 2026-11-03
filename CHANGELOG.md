# Changelog

## 0.1.0

首个版本，目标：Minecraft Bedrock 1.26.51.01 + LeviLamina 26.51.9 客户端。

### 新增

- **Java 光照贴图 C++ 移植**（`src/lighting/JavaLightmap.*`）
  - 按 Java Edition 1.21.4 `lightmap.fsh` 重写：暖色方块光曲线、白天中性/夜晚偏蓝的天空光、
    `notGamma` 亮度提升、Boss 变暗、夜视；
  - 纯函数、无游戏依赖，可主机端回归测试。
- **原生光照贴图钩子**（`src/lighting/LightmapHook.*`）
  - 拦截 `BaseLightTextureImageBuilder::$buildImage`，读取 `BaseLightData` 实时参数生成 Java LUT；
  - `observe` 模式导出基岩版光照贴图（CSV + PPM），`java` 模式覆写像素；
  - 格式/尺寸/缓冲校验，失败静默降级。
- **Vibrant Visuals 资源包**（`resource_pack/`，由 `tools/gen_reference.py` 生成）
  - `shadows/global.json`：`blocky_shadows` + `texel_size 16`（Java 式硬边阴影）；
  - `lighting/global.json`：太阳/月亮照度与颜色、低环境光、`sky.intensity 0.8`；
  - `local_lighting/local_lighting.json`：发光方块统一暖色 `static_light`（Java 无点光源/彩色光）；
  - `pbr/global.json`：粗糙度最大、金属度 0，去掉 Java 版没有的 PBR 高光。
- **工具与验证**
  - `tools/gen_reference.py`：Java 光照参考实现 + 资源包生成 + 文档图 + 回归数据，内含数值断言；
  - `tests/test_lightmap.cpp`：C++ 回归测试，CI 中构建并运行；
  - `.github/workflows/build.yml`：LLVM 22 + xmake 客户端模组构建。
- **文档**：README、原理、构建、实机测试校准、已知边界，以及三份带出处的调研报告。

### 已知限制

- 未实机验证（详见 `docs/LIMITATIONS.md`）；
- 实体光照/阴影不在本次范围内；
- Vibrant Visuals 与原生光照贴图路线互斥生效。
