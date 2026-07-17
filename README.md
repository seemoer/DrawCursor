# DrawCursor

DrawCursor 是一个纯 Win32 C 编写的 Windows 小工具。它通过置顶、点击穿透的分层窗口，在系统鼠标位置额外绘制一份光标，帮助远程控制、桌面采集和录屏软件捕获鼠标。

程序默认选择更适合传统 GDI/远程采集的“兼容模式”，也提供追求本机最低提交开销的可选“极速模式”。

## 使用

1. 运行 `build\DrawCursor.exe`。程序没有普通窗口，只显示托盘图标，并默认打开重绘。
2. 右键托盘图标可选择：
   - `打开重绘` / `关闭重绘`；
   - `兼容模式`：默认模式，当前选项带圆点；
   - `极速模式`：当前选项带圆点；
   - `关闭 DrawCursor`。
3. 模式切换仅在本次运行中有效，重新启动后仍回到兼容模式。

### 两种呈现模式

| 模式 | 呈现方式 | 适用场景 | 代价与风险 |
| --- | --- | --- | --- |
| 兼容模式（默认） | 保持 384×384 分层画布，普通移动只改画布中的光标像素；接近 32px 边界时才移动窗口 | GDI、远程控制和兼容性优先的采集链路 | 每个提交需要上传完整画布，但局部清理旧光标矩形减少了 CPU 写入 |
| 极速模式 | 使用光标尺寸的小表面；形状、显隐或模式变化时上传像素，普通移动只更新分层窗口位置 | 本机低延迟，且已确认采集端能稳定捕获移动窗口 | 高频移动 HWND 可能被部分 GDI/远程采集器遗漏或产生残影 |

极速模式的位置更新失败时会先执行安全的完整小表面更新；连续三次失败或完整更新失败时，程序会自动切回兼容模式。这个回退只能检测 Win32 API 失败，无法判断远程画面是否遗漏或残影。如果远程端表现异常，请手动使用兼容模式。

## 构建与验证

需要 64 位 MinGW-w64 的 `gcc` 和 `windres`：

```powershell
.\build.ps1
```

输出文件为 `build\DrawCursor.exe`，无额外运行时依赖。

完整的本地基础验证会执行 `-Werror` 静态语法检查、正式构建、短暂启动/正常退出，确认默认轻量模式不创建日志，并核对按需 profiling CSV 的字段数：

```powershell
.\verify.ps1
```

运行验证前请退出已经运行的 DrawCursor。验证不会移动鼠标或注入用户输入。

## 低延迟架构

- 主线程只负责 Raw Input、托盘和低频闲置检查。它读取当前 `HRAWINPUT` 后用 `GetRawInputBuffer` 排空积压消息，把一批输入合并成一次“最新状态已变更”通知，不排队旧位置。相对移动仅在坐标增量非零时唤醒渲染，绝对移动报告也会正确触发；纯按键和滚轮包不会启动位置渲染。
- 独立渲染线程拥有 overlay HWND、全部 GDI 位图和 `UpdateLayeredWindow`。输入线程不会因分层窗口提交阻塞。
- 渲染线程动态加入 MMCSS `Capture`；不可用时安全回退到 Above Normal，不使用实时优先级。
- 运动帧只调用一次 `GetCursorInfo`，同时获得最新屏幕坐标、显隐和光标句柄。样式变化会在同一帧重建 alpha/单色光标缓存。
- 使用高精度 waitable timer；旧系统或高精度标志不可用时回退到普通 waitable timer，不再使用低优先级 `WM_TIMER` 驱动运动渲染。
- 动态读取 DWM composition timing。连续运动的最大调度间隔为 `min(4ms, 刷新周期/2)`，并在不晚于该上限的前提下尝试靠近下一合成相位。DWM 查询失败或数据异常时安全回退到 4ms。
- 一次性 timer 使用绝对 QPC deadline 计算，避免每次提交耗时累积成周期漂移。运动停止约 20ms 后关闭高频调度；50ms timer 只做闲置状态兜底和日志快照。
- `UpdateLayeredWindow` 的目标 DC 为 `NULL`，热路径不再每帧 `GetDC(NULL)`。
- 默认关闭完整逐帧 Profiling，热路径不会执行其 QPC 采样、原子聚合和日志写入；明确启用后，主线程每秒只提交内存快照，后台线程批量写盘，退出时才强制 flush。
- 覆盖窗口启用 per-monitor DPI awareness、置顶、点击穿透和非激活样式；使用 32-bit premultiplied alpha。对于 I-beam、Cross 等无法由 alpha 直接表达的 XOR-only 光标，会转换为黑色主体加白色一像素轮廓，保证在亮色和暗色背景上都可见。

## Profiling

完整 Profiling 默认关闭。需要测量时使用任一种方式启动：

```powershell
.\build\DrawCursor.exe --profile
$env:DRAWCURSOR_PROFILE = "1"; .\build\DrawCursor.exe
```

命令行 `--no-profile` 可覆盖环境变量并关闭分析。启用后写入：

```text
build\logs\drawcursor-profile.csv
```

日志约每秒一行，退出时写入最后一个不足一秒的区间。字段按用途可分为：

- 输入与合并：`input_events`、`input_batches`、`input_buffered_events`、`input_coalesced`、`input_buffer_failures`、`render_requests`。
- 调度：`render_timer_ticks`、`timer_started`、`timer_stopped`、`dwm_timing_success`、`dwm_timing_fail`、`refresh_period_avg_us`、`refresh_period_max_us`、`deadline_late_avg_us`、`deadline_late_max_us`。
- 呈现结果：`render_attempts`、`render_success`、`render_fail`、`render_noop_same_pos`、`render_force`、`canvas_recenter`、`canvas_recreated`、`overlay_shown`。
- 光标状态：`cursor_changed`、`cursor_hidden`、`getcursorinfo_avg_us`、`getcursorinfo_max_us`。
- CPU/API 耗时：`fill_avg_us` / `fill_max_us`、`drawicon_avg_us` / `drawicon_max_us`、`update_layered_avg_us` / `update_layered_max_us`、`render_total_avg_us` / `render_total_max_us`。
- 输入到渲染线程：`input_to_render_avg_us`、`input_to_render_max_us`。它从最新一批 Raw Input 的发布时间量到渲染线程开始采样，而不是对批内每个旧事件分别计时。
- 极速模式：`fast_move_success`、`fast_full_update`、`fast_fallback`。

### 如何解读

- 首先看 `input_to_render_*`。它反映输入消息、线程唤醒和调度等待的综合软件延迟；持续运动时平均值应明显低于 4ms。偶发最大值需要结合系统抢占和下一项判断。
- `deadline_late_*` 是 waitable timer 相对目标 QPC deadline 的迟到量。它持续偏高通常说明 CPU 调度、电源策略或其他高优先级负载干扰。
- `update_layered_*` 只测量 `UpdateLayeredWindow` API 调用返回前的时间；`render_total_*` 测量一次实际呈现路径，但 `GetCursorInfo` 单独统计。
- `refresh_period_avg_us` 可换算刷新率：`Hz ≈ 1,000,000 / refresh_period_avg_us`。`dwm_timing_fail` 增加时，本区间使用 4ms 回退预算。
- 兼容模式中 `fill_*` 主要是清理上一帧光标小矩形；`fast_move_success` 则表示没有重新上传像素的位置更新。
- `render_fail`、`input_buffer_failures` 或 `fast_fallback` 持续增加都需要调查。单次 `fast_fallback` 表示程序已经恢复到兼容模式。

这些指标不是 input-to-photon。日志不包含 DWM 在 API 返回后的排队、显示扫描、远程采集、编码、网络、解码和远端显示时间，也不提供每帧原始样本或严格的 p95/p99。

## 端到端测量与验收

本机测量建议：

1. 固定显示器刷新率和电源模式，关闭可变刷新率后先建立基线。
2. 使用 1000Hz 或更高轮询率鼠标持续匀速移动，同时以 240fps 以上高帧率相机拍摄屏幕；若能用鼠标微控制器在同一输入报告时点亮 LED，可把 LED 作为输入时刻。
3. 逐帧测量输入标记到重绘光标首次移动的时间，并对照同一时段 CSV 的 `input_to_render`、`deadline_late` 和 `update_layered`。
4. 分别运行兼容和极速模式；每种模式至少记录 30 秒，报告中位数、p95、p99 和最大值。

远程链路测量建议：

1. 用同一高帧率相机同时拍到本机输入标记和远端显示器，或用硬件同步的两路视频。
2. 固定远程软件的采集帧率、编码器、网络和远端刷新率，分别测试静止后起步、匀速运动、快速变向、窗口边缘和不同光标样式。
3. 检查极速模式是否出现遗漏、跳跃或残影；存在任何不稳定就以兼容模式作为验收模式。

建议的软件侧目标是：持续运动时 `input_to_render_avg_us < 3000`、`deadline_late_avg_us < 1000`，`update_layered_avg_us` 稳定低于 1ms，且没有周期性的 10ms 以上长尾。最终体验目标应以端到端数据为准：本机重绘通常不超过一个显示刷新周期，远程结果则另外受采集和传输链路限制。

## 仓库外的更低延迟方案

普通分层窗口仍属于 DWM 合成内容，无法获得系统硬件光标的独立扫描路径。如果可以修改桌面采集或远程控制端，更低延迟的架构是读取 Desktop Duplication/DXGI frame 中的 pointer position 和 pointer shape metadata，在编码端或远端客户端通过独立光标通道绘制。这样光标不必等待桌面视频帧更新。

本仓库只包含覆盖层工具，没有采集端、编码器或远端客户端源码，因此无法在这里实现或验证该独立光标通道。
