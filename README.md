# DrawCursor

DrawCursor 是一个 Windows 鼠标光标重绘工具。它会在系统光标位置额外绘制一份光标，帮助远程控制、桌面采集和录屏软件正确捕获鼠标。

程序没有普通窗口，运行后显示在系统托盘。右键托盘图标可以打开或关闭重绘、切换兼容模式与极速模式，以及退出程序。

## 构建

需要 64 位 MinGW-w64，并确保 `gcc` 和 `windres` 已加入系统 `PATH`：

```powershell
gcc --version
windres --version
```

在项目目录运行：

```powershell
.\build.ps1
```

构建结果位于：

```text
build\DrawCursor.exe
```

生成的 exe 已嵌入图标、manifest 和版本信息，可以单独复制运行。

## 仓库外的更低延迟方案

普通分层窗口仍属于 DWM 合成内容，无法获得系统硬件光标的独立扫描路径。如果可以修改桌面采集或远程控制端，更低延迟的架构是读取 Desktop Duplication/DXGI frame 中的 pointer position 和 pointer shape metadata，在编码端或远端客户端通过独立光标通道绘制。这样光标不必等待桌面视频帧更新。

本仓库只包含覆盖层工具，没有采集端、编码器或远端客户端源码，因此无法在这里实现或验证该独立光标通道。
