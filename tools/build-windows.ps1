<#
.SYNOPSIS
    一键构建 JavaLighting 客户端模组（Windows x64, LeviLamina 26.51.x）。

.DESCRIPTION
    按顺序检查并准备：winget -> VS2022 Build Tools (VC++ 工作负载) -> LLVM 22 -> xmake，
    然后执行 xmake 构建，最后把产物整理到 dist/java-light/ 便于直接复制进客户端 mods/ 目录。

    用法（普通 PowerShell 窗口）：
        powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1

    常用参数：
        -SkipPrereq   跳过依赖安装（依赖已齐时）
        -NoCopy       只构建，不整理 dist/

    注意：
      * 安装 VS Build Tools / LLVM 需要管理员权限（脚本会提示 UAC）。
      * 全程需要能访问 GitHub；国内网络可先设置代理：$env:HTTP_PROXY / HTTPS_PROXY。
      * 如果公司/学校机器不允许装软件，请改用 GitHub Actions（见 docs/BUILD.md）。
#>

[CmdletBinding()]
param(
    [switch]$SkipPrereq,
    [switch]$NoCopy
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ToolsDir   = Join-Path $RepoRoot '.build-tools'
$XmakeDir   = Join-Path $ToolsDir 'xmake'
$XmakeVer   = 'v3.1.2'
$XmakeZip   = "https://github.com/xmake-io/xmake/releases/download/$XmakeVer/xmake-$XmakeVer.win64.zip"
$LlvmVer    = '22.1.8'

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Write-Warn2($msg){ Write-Host "  [!!] $msg" -ForegroundColor Yellow }

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Winget {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw '找不到 winget（应用安装程序）。请手动安装依赖，或改用 GitHub Actions 构建（docs/BUILD.md）。'
    }
}

function Refresh-PathFromRegistry {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $parts   = @($machine, $user, $env:Path) | Where-Object { $_ }
    $env:Path = $parts -join ';'
}

# --------------------------------------------------------------------------- #
Write-Step '1/5 检查 xmake'
# --------------------------------------------------------------------------- #
$xmakeExe = $null
$cmd = Get-Command xmake -ErrorAction SilentlyContinue
if ($cmd) {
    $xmakeExe = $cmd.Source
    Write-Ok "已安装：$(& $xmakeExe --version 2>&1 | Select-Object -First 1)"
}

if (-not $xmakeExe) {
    if ($SkipPrereq) { throw 'xmake 未安装，但指定了 -SkipPrereq。' }
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $zipPath = Join-Path $ToolsDir "xmake-$XmakeVer.win64.zip"
    Write-Host "  下载 xmake $XmakeVer ..."
    Invoke-WebRequest -Uri $XmakeZip -OutFile $zipPath -UseBasicParsing
    Expand-Archive -Path $zipPath -DestinationPath $ToolsDir -Force
    # 官方 zip 解压后是 xmake/xmake.exe（多套一层目录）
    $candidate = Get-ChildItem $ToolsDir -Recurse -Filter 'xmake.exe' |
                 Select-Object -First 1
    if (-not $candidate) { throw 'xmake 解压后找不到 xmake.exe。' }
    $xmakeExe = $candidate.FullName
    $env:Path = "$(Split-Path $xmakeExe -Parent);$env:Path"
    Write-Ok "xmake 已就绪：$xmakeExe"
}

# --------------------------------------------------------------------------- #
Write-Step '2/5 检查 Visual C++ 工具链（clang-cl 需要 VS 的 Windows SDK）'
# --------------------------------------------------------------------------- #
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                         -property installationPath 2>$null
}
if ($vsPath) {
    Write-Ok "找到 Visual Studio：$vsPath"
} else {
    if ($SkipPrereq) { throw '未找到 VS C++ 工具链，但指定了 -SkipPrereq。' }
    Write-Warn2 '未找到 VS C++ 工具链，尝试用 winget 安装 VS2022 Build Tools（需要管理员权限，体积较大）'
    Assert-Winget
    if (-not (Test-Admin)) { Write-Warn2 '当前不是管理员，安装会失败；请用管理员 PowerShell 重新运行本脚本。' }
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget `
        --accept-source-agreements --accept-package-agreements `
        --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    Refresh-PathFromRegistry
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                             -property installationPath 2>$null
    }
    if (-not $vsPath) { throw 'VS C++ 工具链安装未完成，请手动安装 "Desktop development with C++" 后重试。' }
    Write-Ok "VS 已安装：$vsPath"
}

# --------------------------------------------------------------------------- #
Write-Step '3/5 检查 LLVM / clang-cl'
# --------------------------------------------------------------------------- #
function Find-ClangCl {
    $c = Get-Command clang-cl -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in @("$env:ProgramFiles\LLVM\bin\clang-cl.exe",
                     "${env:ProgramFiles(x86)}\LLVM\bin\clang-cl.exe")) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

$clangCl = Find-ClangCl
if (-not $clangCl) {
    if ($SkipPrereq) { throw 'clang-cl 未安装，但指定了 -SkipPrereq。' }
    Write-Warn2 "未找到 clang-cl，尝试 winget 安装 LLVM $LlvmVer（需要管理员权限）"
    Assert-Winget
    if (-not (Test-Admin)) { Write-Warn2 '当前不是管理员，安装会失败；请用管理员 PowerShell 重跑。' }
    winget install --id LLVM.LLVM -e --version $LlvmVer --source winget `
        --accept-source-agreements --accept-package-agreements
    Refresh-PathFromRegistry
    $clangCl = Find-ClangCl
    if (-not $clangCl) { throw 'LLVM 安装未完成，请手动安装后重试（https://github.com/llvm/llvm-project/releases）。' }
}
$llvmBin = Split-Path $clangCl -Parent
$env:Path = "$llvmBin;$env:Path"
Write-Ok "clang-cl：$(& $clangCl --version 2>&1 | Select-Object -First 1)"

# --------------------------------------------------------------------------- #
Write-Step '4/5 配置并构建（xmake, target_type=client）'
# --------------------------------------------------------------------------- #
Push-Location $RepoRoot
try {
    $env:XMAKE_PKG_CACHEDIR = Join-Path $ToolsDir 'xmake-cache'

    Write-Host '  xmake repo -u ...'
    & $xmakeExe repo -u
    if ($LASTEXITCODE -ne 0) { throw 'xmake repo -u 失败（检查网络/代理）。' }

    Write-Host '  xmake f --target_type=client ...'
    & $xmakeExe f -a x64 -m release -p windows -v --target_type=client -y
    if ($LASTEXITCODE -ne 0) { throw 'xmake configure 失败。' }

    Write-Host '  xmake -v -y ...'
    & $xmakeExe -v -y
    if ($LASTEXITCODE -ne 0) { throw 'xmake build 失败，请把上面的报错发出来。' }

    Write-Host '  运行主机端回归测试 ...'
    & $xmakeExe -b java-lighting-tests
    if ($LASTEXITCODE -eq 0) {
        & $xmakeExe run java-lighting-tests
        if ($LASTEXITCODE -ne 0) { Write-Warn2 '回归测试失败（不影响安装，但请提 issue）。' }
    }

    if (-not $NoCopy) {
        $binDir = Join-Path $RepoRoot 'bin'
        $srcDir = Get-ChildItem $binDir -Directory -ErrorAction SilentlyContinue |
                  Where-Object { $_.Name -eq 'java-lighting' } |
                  Select-Object -First 1
        if (-not $srcDir) {
            # 兼容不同 modpacker 输出布局：取包含 java-lighting.dll 的第一层目录
            $dll = Get-ChildItem $binDir -Recurse -Filter 'java-lighting.dll' -ErrorAction SilentlyContinue |
                   Select-Object -First 1
            if ($dll) { $srcDir = $dll.Directory } else { $srcDir = $null }
        }
        if ($srcDir) {
            $distDir = Join-Path $RepoRoot 'dist'
            New-Item -ItemType Directory -Force -Path $distDir | Out-Null
            $target = Join-Path $distDir 'java-lighting'
            if (Test-Path $target) { Remove-Item $target -Recurse -Force }
            Copy-Item $srcDir.FullName $target -Recurse -Force
            Write-Step '5/5 完成'
            Write-Ok "产物：$target"
            Write-Host @"
  安装：把 dist\java-lighting 整个目录复制到客户端实例的 mods\ 下，即：
        <实例根目录>\mods\java-lighting\java-lighting.dll

  资源包：把 resource_pack 复制到 <实例根目录>\resource_packs\JavaLighting\，
          游戏内 设置 -> 全局资源 启用，并在 视频 -> 图形模式 打开 Vibrant Visuals。

  首次启动后：数据目录会生成 config.json（默认 mode=observe，只观测不改画面），
          按 docs\TESTING.md 校准后再把 mode 改成 "java"。
"@
        } else {
            Write-Warn2 '构建成功但未在 bin\ 找到 java-lighting.dll，请检查 bin\ 内容。'
        }
    }
} finally {
    Pop-Location
}
