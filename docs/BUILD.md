# 构建与安装

## 0. 没有本地 C++ 环境？两条捷径

### 路线 A：GitHub Actions（推荐，零本地环境）

工程自带 `.github/workflows/build.yml`，推到 GitHub 后由 GitHub 的 Windows 机器编译：

1. 新建一个 GitHub 仓库（public/private 都行）；
2. 把本工程（或 `java-lighting-src.zip` 解压后的内容）推到仓库根目录，确保
   `.github/workflows/build.yml`、`xmake.lua`、`src/` 在根目录；
3. 打开仓库 **Actions → build → Run workflow**（push 也会自动触发）；
4. 等 5–15 分钟，在该次 run 页面底部 **Artifacts** 下载
   `<仓库名>-<sha>-client-windows-x64.zip`；
5. 解压出来就是 `java-lighting/` 目录（含 `java-lighting.dll`），直接按第 3 节安装。

> GitHub 的 Windows runner 自带 VS2022 + LLVM，不需要你装任何东西；唯一要求是能访问 GitHub。

### 路线 B：本机一键脚本

已经装了或允许装软件（需要管理员权限 + 能访问 GitHub）：

```powershell
# 在 java-lighting/ 目录，普通 PowerShell 也行，脚本会提示 UAC
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1
```

脚本会：检查/安装 xmake → VS2022 Build Tools（VC++ 工作负载）→ LLVM 22 → 运行
`xmake ... --target_type=client` → 跑回归测试 → 把产物整理到 **`dist/java-lighting/`**，
并打印安装路径。依赖已齐时可加 `-SkipPrereq` 跳过安装。

## 1. 环境

| 依赖 | 版本 / 说明 |
| --- | --- |
| Windows | 10/11 x64 |
| Visual Studio 2022 | 勾选 "Desktop development with C++" |
| LLVM | **22**（模板 CI 使用 `ZhongRuoyu/setup-llvm@v0`，本地装 `LLVM 22` 并让 `clang-cl` 在 PATH） |
| xmake | 最新版（`xmake --version`） |
| LeviLamina | **26.51.\***，由 xmake 包管理器自动拉取（`levimc-repo`） |
| Minecraft Bedrock | **1.26.51.01** 客户端（带 LeviLamina client 版） |

> LeviLamina 客户端模组的构建参数必须是 `--target_type=client`；服务端模组不能加载本模组（它钩客户端渲染）。

## 2. 编译（手动）

```powershell
# 在 java-lighting/ 目录
xmake repo -u
xmake f -a x64 -m release -p windows -v --target_type=client -y
xmake -v -y
```

首次会下载 `levilamina 26.51.*` 与 `levibuildscript` 包，需要能访问 GitHub；
网络不便时按 xmake 文档配置镜像/代理。

产物：`bin/` 下由 `@levibuildscript/modpacker` 打包出的 `java-lighting/` 目录。

### 只跑回归测试（不需要游戏）

```powershell
xmake -b java-lighting-tests
xmake run java-lighting-tests
```

期望输出 `All JavaLightmap checks passed.`；若修改了 Java 光照算法，
用 `tools/gen_reference.py` 重新生成 `tests/expected_lightmap.json` 并同步测试里的校验和。

### CI

`.github/workflows/build.yml`：windows-latest + LLVM 22 + xmake，构建 client 模组并运行回归测试，
产物以 artifact 上传。把工程推到 GitHub 即可用。

## 3. 安装到客户端

### 3.0 前置：装好 LeviLauncher + lip + LeviLamina

1. 装 [Microsoft Visual C++ Redistributable x64](https://aka.ms/vc14/vc_redist.x64.exe)；
2. 装 [LeviLauncher](https://docs.levimc.org/LeviLauncher/download)，在它的 **设置** 里安装 **lip**
   （LeviLauncher 装 LeviLamina 要用它）；
3. 在 LeviLauncher 里安装一个**LeviLamina 支持的客户端版本**（1.26.51.01），安装时勾选 **Install LeviLamina**；
   已经装过的版本，可以在实例设置的 **Loader** 页补装/升级 LeviLamina；
4. 启动一次，确认实例可以进游戏，且 LeviLauncher 的 **Mods** 卡片正常显示。

> 客户端模组由 **LeviLauncher** 管理，`lip` 只管服务端目录。**不要**把 lip 装到
> `C:\XboxGames` 下的官方安装目录，官方 Store 版目录受保护、结构也不同。

### 3.1 原生模组

1. 找到你的**客户端实例根目录**（包含 `minecraftWindows.exe` 与 `mods/` 的那一层；
   LeviLauncher 里可以"打开版本文件夹"直接到这一层）；
2. 把构建产物 `java-lighting/` 整个目录放进去：

   ```
   <实例根目录>/mods/java-lighting/
       java-lighting.dll
       ...
   ```

3. 或者不用手动复制：在 LeviLauncher 的 **Mods** 页用 **Import**，选择
   `java-lighting.dll`（或整个目录打成的 zip）；
4. 用 LeviLauncher 启动该实例，在 LeviLauncher 的 **Mods** 卡片里应能看到 `java-lighting` 已启用。

启动后看日志：应出现 `JavaLighting loaded: mode=observe ...` 与
`Lightmap hook installed in OBSERVE mode`，以及 `Config file: <路径>`。

### 3.2 资源包（Vibrant Visuals 路线）

1. 把 `resource_pack/` 目录复制/改名为：

   ```
   <实例根目录>/resource_packs/JavaLighting/
       manifest.json
       pack_icon.png
       lighting/global.json
       shadows/global.json
       local_lighting/local_lighting.json
       pbr/global.json
   ```

   （也可以在 LeviLauncher 的 **Content Management** 里用资源包导入/拖放。）

2. 游戏内 **设置 → 全局资源** 激活 "JavaLighting — Java Edition look"；
3. **视频 → 图形模式** 选择 **Vibrant Visuals**（资源包带 `pbr` capability、`min_engine_version 1.21.120`）。

### 3.3 卸载

删除 `mods/java-lighting/`、`resource_packs/JavaLighting/` 即可，不写入存档，无残留。

## 4. 发布（可选）

`tooth.json` 已按 client variant 填写，发布时：

- 改 `tooth` 为你自己的仓库地址、填 `info.avatar_url`；
- 把 release 里的 zip 拆成两个放置项：模组目录放 `mods/java-lighting/`，资源包放 `resource_packs/JavaLighting/`
  （`placements` 支持多个条目，`src` 指向 zip 内目录）；
- 版本号与 `tooth.json` 的 `version` 保持一致。

> lip 新版格式是 `manifest.json`（format 4）；模板/本工程仍用 `tooth.json`（format 3，lip 兼容）。
> 需要迁移时 `lip migrate` 处理。
