# 构建 DrawCursor

DrawCursor 是一个纯 Win32 C 程序。目前构建脚本面向 64 位 Windows，使用 MinGW-w64 提供的 `gcc` 和 `windres`。

## 准备环境

安装 64 位 MinGW-w64，并将其 `bin` 目录加入系统 `PATH`。打开新的 PowerShell 窗口，确认下面两个命令都能运行：

```powershell
gcc --version
windres --version
```

`gcc` 用于编译程序，`windres` 用于把应用图标、manifest 和版本信息写入 exe。两者缺少任何一个，构建脚本都会停止并显示错误。

## 拉取并构建

```powershell
git clone <仓库地址>
cd DrawCursor
.\build.ps1
```

构建成功后会生成：

```text
build\DrawCursor.exe
```

这个 exe 可以单独复制到其他 Windows 电脑运行。图标、manifest 和版本信息都已经嵌入其中，不需要同时携带 `res` 目录或其他图片文件。

如果 PowerShell 阻止脚本运行，可以只为当前窗口临时放行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

## 完整验证

提交或发布前建议运行：

```powershell
.\verify.ps1
```

验证脚本会执行严格编译检查、正式构建、光标像素测试、启动与退出测试，并检查性能日志格式。运行前需要先退出正在运行的 DrawCursor，否则 Windows 会锁住 `build\DrawCursor.exe`，导致构建无法覆盖旧文件。

验证会产生测试程序、资源中间文件和日志，它们都位于已被 Git 忽略的 `build` 目录，不会进入源码提交。

## 源码构建与直接使用的区别

从 GitHub 克隆源码后，需要安装 MinGW-w64 才能运行 `build.ps1`。如果只是想使用 DrawCursor，不需要编译环境；可以直接下载项目 GitHub Releases 中发布的 `DrawCursor.exe`。

`build` 目录默认不会提交到 Git，因此本地生成的 exe 不会随着 `git push` 自动上传。发布正式版时，需要在 GitHub 创建 Release，并手动附加 `build\DrawCursor.exe`，或者以后配置 GitHub Actions 自动构建并发布。
