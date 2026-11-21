# FastSend · V-1

用 C++ 和 Qt Widgets 编写的 Windows 局域网文件传输应用。简洁的浅色界面，提供发送、接收和传输进度。

## 当前版本

- 选择一个文件，发送给另一台运行 FastSend 的电脑。
- 自动发现同一局域网中的设备，也可以手动输入 IPv4 地址和端口。
- 接收方明确确认后，才开始发送文件内容。
- 显示进度，支持取消；拒绝、超时、断开和写入失败会显示原因。
- 自定义设备名和接收目录，设置保存在当前用户配置中。
- 同名文件自动添加编号；接收完成后验证 SHA-256，再保存为最终文件。
- 文件分块读写，发送缓冲有上限，界面无需等待整份文件载入内存。

当前版本聚焦 Windows APP 的基础收发，每个节点同时处理一个任务。两端须连接可以相互访问的局域网，传输过程需要保持应用打开。使用 FastSend 自定义协议，两端直接通过 TCP 通信。

本版使用明文 TCP，设备名和发现公告不代表身份认证，适用于可信局域网中的普通文件传输。哈希用于检查传输完整性。加密、设备配对、批量文件、目录传输和断点续传属于后续版本。

## Windows 使用

1. 在两台电脑分别解压完整应用包，双击 FastSend.exe。Qt DLL 和 platforms 等子目录需与程序一起保留。
2. 接收方在“接收”页查看 IP、端口和保存目录。默认保存到当前用户下载目录下的 FastSend 文件夹。
3. 发送方选择文件，点击附近设备；也可以填写接收方的 IP 和端口，再点击“发送文件”。
4. 接收方点击“接收”或“拒绝”；完成后可点击“打开文件夹”。

默认 TCP 端口 **45455**，UDP 发现端口 **45454**。每 3 秒发送发现公告，离线设备约 12 秒后移除。等待确认或网络无进展约 60 秒后超时。

发现不到设备时使用手动 IP。若手动连接也失败，检查地址、网络互通条件及 Windows 对应用在所用网络上的访问权限。校园网、访客网络或无线隔离可能阻止互访。应用不会自动修改系统防火墙。

## 开发环境

| 项目 | 配置 |
| --- | --- |
| 语言 | C++17 |
| UI | Qt 6 Widgets |
| 网络 | Qt Network：QTcpServer / QTcpSocket / QUdpSocket |
| 文件与校验 | QFile / QTemporaryFile / QCryptographicHash |
| 构建 | CMake 3.21+、Visual Studio 2022 C++ x64 工具链 |
| 本机 Qt | Qt 6.8.3 MSVC 2022 x64，D:\Qt\6.8.3\msvc2022_64 |

本机只安装 Qt 基础桌面组件，未安装 Qt Creator。Core、GUI、Widgets、Network 和 Test 包含在 qtbase 中。

## 编译、测试与打包

在项目根目录使用 PowerShell：

```powershell
.\scripts\build.ps1
.\scripts\package.ps1
```

指定其他 Qt 安装位置：

```powershell
.\scripts\build.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64'
.\scripts\package.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64'
```

构建脚本会运行传输测试。编译产物为 build/Release/FastSend.exe；完整应用目录和 ZIP 位于 dist/。打包使用 windeployqt 收集动态运行库和插件。dist、构建文件和 Qt SDK 不提交到源码仓库。

也可以使用 Visual Studio“打开文件夹”，并将 Qt 安装前缀配置给 CMake。

## 两实例本机验证

在已打包应用目录分别执行，启动两个窗口：

```powershell
.\FastSend.exe --port 45455 --name '电脑 A'
.\FastSend.exe --port 45456 --name '电脑 B' --receive-dir 'C:\Users\Public\Downloads\FastSend-Test'
```

从 A 发送时，IP 填 127.0.0.1，端口填 45456，B 确认接收。自动化测试也通过回环地址验证真实 TCP 传输。本机测试不能替代两台物理电脑在实际网络上的验证。

## 项目结构

```text
src/
  main.cpp           入口、图标、命令行参数
  mainwindow.*       Qt Widgets 界面与交互
  transfer.*         TCP 请求、确认、流式传输、落盘
  discovery.*        UDP 设备发现和离线移除
  protocol.*         消息封装、文件名校验、格式化
tests/
  transfer_test.cpp  真实 TCP 收发与异常测试
scripts/
  build.ps1          Windows 编译和测试
  package.ps1        收集依赖并生成 ZIP
docs/
  protocol.md        协议与状态
  test-results.md    实测结果与未验证范围
  original-course-plan.md  早期课设规划
```

早期规划中的网页取件、SQLite 和 Linux 部署属于后续方向，当前实现范围以本 README 为准。Linux 保留 Qt/CMake 的代码基础，尚需实机编译、网络和界面回归。

## 课设后续材料

结合实现准备成员分工、可行性分析、功能与非功能需求、数据流图、状态图、软件层次图、流程图与盒图、测试数据与实际结果。当前不使用数据库；若课程明确要求数据库设计，可在后续增加传输历史持久化，并据此准备 E-R 图和表设计。

## 许可

项目源码采用 MIT 许可证。Qt 采用动态链接形式分发，相关 LGPL 文本和来源见 THIRD_PARTY_NOTICES.md 与 licenses/。依赖保留各自许可证。

