# 实机测试与校准清单

> 本工程未在实机验证。下列步骤按顺序做，任何一步异常都先回到 `mode=observe` 收集证据。
> 文中 `<dataDir>` 指模组数据目录，启动日志里的 `Config file: ...` 会打印准确路径。

## 0. 静态检查（无需游戏）

```powershell
# 1) Java 参考实现 + 资源包重新生成（应输出 Java lightmap reference OK）
python tools\gen_reference.py

# 2) 资源包 JSON 语法
Get-ChildItem -Recurse resource_pack -Filter *.json |
  ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json | Out-Null; "OK $($_.Name)" }

# 3) C++ 回归测试（需要编译环境）
xmake -b java-lighting-tests; xmake run java-lighting-tests
```

期望：`All JavaLightmap checks passed.`（day/night 校验和分别是
`0x225C4263708D2897` / `0x23FC6AB5296F0BD6`）。

`tools/gen_reference.py` 会同时校验 Java 参考数值（白天 `#FCFCFC`、夜晚天空光 `#50507D`、
gamma 0/0.5/1 的阴影灰度），失败会明确列出哪一项不符。

## 1. 启动检查（传统图形 + observe）

1. 图形模式设为 **传统 / Classic**（不要开 Vibrant Visuals）；
2. 启动客户端，打开日志，确认：
   - `JavaLighting loaded: mode=observe`
   - `Lightmap hook installed in OBSERVE mode`
   - 无 `Failed to hook BaseLightTextureImageBuilder::buildImage`
3. 若出现 hook 失败：说明 26.51.9 客户端的符号/版本不匹配，模组会降级为无操作；
   记录客户端完整版本号 + LeviLamina 版本并提 issue。

## 2. 布局确认（关键）

进入一个白天世界，站在户外，让画面渲染 ≥ 3 秒，然后检查：

```
<dataDir>/
├─ config.json
├─ java_lightmap_reference.csv          # Java 参考（行 sky，列 block）
└─ lightmap_dump/
   ├─ bedrock_lightmap.csv              # 基岩版实际生成的贴图 + 输入参数
   └─ bedrock_lightmap.ppm              # 同一数据的图片（可用任意看图软件/转 PNG）
```

判断规则：

| 观察 | 结论 | 配置修改 |
| --- | --- | --- |
| dump 的 CSV 表头 width/height 不是 16×16 | 基岩版贴图尺寸与假设不同（仍支持任意方形，按 texel 比例映射） | 记录尺寸提 issue |
| ppm 中"亮→暗"的方向与参考图差 90° | 方块光/天空光轴互换 | `"blockAxis": "y"` |
| ppm 上下相反 | 行方向相反 | `"flipY": true` |
| dump 里 gamma 不在 0..1 / skyDarken 为 0 或负数 | 基岩版字段语义与 Java 不同 | 先记录，可能需 `invertSkyDarken` |
| 白天 dump 整体像 Java 的夜晚 | skyDarken 语义相反 | `"invertSkyDarken": true` |

## 3. 切换到 java 模式

```json
{ "lightmap": { "mode": "java" } }
```

重进世界（或切换维度触发贴图重建），检查：

- [ ] 白天户外：亮部接近 Java 的"奶白"（略低于纯白），阴影不过曝；
- [ ] 室内火把：暖橙色光晕、暖色到冷色的过渡比原版更明显；
- [ ] 洞穴（无光源）：接近纯黑但保留微弱细节（约 `#191919` 级别）；
- [ ] 夜晚：天空光偏蓝，夜景比原版更冷、更暗；
- [ ] 下界/末地：无异常（这两个维度 ambientLight 不同，属已知偏差，见 LIMITATIONS）；
- [ ] 生物/实体：**不会变化**（见 LIMITATIONS：实体不走光照贴图）；
- [ ] 画面无闪烁/花屏/法线错误。

任一项异常：改回 `observe`，把 `lightmap_dump/` 与 `config.json` 打包提 issue。

## 4. 资源包路线检查（Vibrant Visuals）

1. 图形模式切换到 **Vibrant Visuals**；
2. 全局资源激活 JavaLighting 资源包（顺序放在其他光影包之上）；
3. 检查：
   - [ ] 方块阴影边缘是方块状硬边（`blocky_shadows`），不是柔和模糊；
   - [ ] 火把/灯笼没有动态点光高光与彩色光（`static_light` + 统一暖色）；
   - [ ] 材质没有金属反光（PBR 粗糙度 255）；
   - [ ] 夜晚月光偏冷；洞穴环境光很低；
   - [ ] 内容日志（设置 → 创作者 → 内容日志）无 `local_lighting` 相关报错。

若想更接近"柔和 AO"观感，把 `shadows/global.json` 的 `shadow_style` 改为 `soft_shadows`
（`tools/java_look.json` 里 `shadowStyle` 同步改，之后重跑生成脚本）。

## 5. 兼容性回归

- [ ] 切换维度（主世界 ↔ 下界 ↔ 末地）不崩溃；
- [ ] 重载资源包 / 切换图形模式不崩溃；
- [ ] 与常见光影/材质包叠加无冲突（资源包只覆盖 `lighting/shadows/local_lighting/pbr`）；
- [ ] 多人服务器：客户端模组只在本地渲染层生效，不改变服务端逻辑；
- [ ] 重进世界后光照正常。

## 6. 提交 issue 时请附

1. 客户端完整版本（如 `1.26.51.01`）、LeviLamina 版本（`26.51.9`）；
2. `<dataDir>/config.json`；
3. `<dataDir>/lightmap_dump/` 全部文件；
4. 日志中 `JavaLighting` 相关行（含 hook 结果）；
5. 图形模式（传统 / Vibrant Visuals）与是否安装其他光影包。
