# 编译和运行

本文说明如何从源码构建 LATS、安装 x86 客户机，并使用系统级翻译器运行客户机
操作系统，例如 x86 架构 Ubuntu 系统、Windows 系统。构建依赖以仓库 README
为准。

> LATS 不随源码或发布包提供 Ubuntu、Windows 或其它客户机镜像，也不提供
> Windows 安装介质。请从操作系统官方渠道或自有授权介质获取 ISO，并遵守对应
> 操作系统的许可条款。

## 1. 环境要求

- 构建和运行主机：LoongArch Linux。
- 客户机：x86 架构操作系统，例如 Ubuntu、Windows。
- 完整优化路径需要宿主 CPU 和内核提供 LSX、LASX 和 LBT_X86 扩展。
- 使用 HAMT 时，宿主内核必须支持 HAMT，并按要求关闭 SFB。
- 用户需要有图形显示环境；也可以通过 VNC 或串口方式运行客户机。

安装 Debian/Ubuntu 构建依赖：

```bash
sudo apt install -y git make gcc g++ pkg-config python3 python3-venv \
    python3-pip python3-setuptools meson ninja-build libglib2.0-dev \
    libpixman-1-dev libspice-protocol-dev libspice-server-dev \
    libsdl2-dev libslirp-dev libusb-1.0-0-dev zlib1g-dev
```

## 2. 编译 LATS

```bash
git clone --depth=1 --recursive https://github.com/lat-opensource/lats.git
cd lats
./latxbuild/build.sh -c def -j"$(nproc)"
```

构建完成后，系统级翻译器位于：

```text
build/lats-x86_64
```

常用构建配置：

```bash
./latxbuild/build.sh -c rel   # 发布构建，输出到 build-release/
./latxbuild/build.sh -c dbg   # 调试构建，输出到 build-dbg/
```

确认二进制可执行：

```bash
./build/lats-x86_64 --version
```

后续命令都从仓库根目录执行，并通过 `-L ./pc-bios` 使用源码树内的固件。

## 3. HAMT 运行环境

HAMT 是 LATS 默认启用的关键优化，但它直接依赖宿主内核和硬件地址转换接口。
运行前应确认以下条件。

### 检查 ABI 版本

LoongArch Linux 使用 ABI1.0 或 ABI2.0。运行以下命令查看系统 ELF 头中的 ABI
标志：

```bash
LC_ALL=C readelf -h /bin/sh | grep '^  Flags:'
```

命令通过 `LC_ALL=C` 固定使用英文工具输出，避免不同系统语言下 `Flags` 显示为
“标志”。出现 `Flags: 0x43, ...` 等包含 `0x40` 位的标志时，当前系统为
ABI2.0；出现 `Flags: 0x3, ...` 等不包含 `0x40` 位的标志时，当前系统为
ABI1.0。

不要仅凭 `uname -m` 判断 ABI 版本；ABI1.0 和 ABI2.0 通常都显示为
`loongarch64`。如果系统没有 `readelf`，请先安装 `binutils`。

### 内核版本

| 宿主环境 | 最低要求 |
| --- | --- |
| ABI2.0 上游内核 | 6.16 |
| ABI2.0 Loongnix 25 Desktop | 6.6.52.1.8 |
| ABI1.0 Loongnix 20 | 4.19.0-19-8.26 |

确认 ABI 版本后，使用 `uname -a` 检查当前内核版本，并确认宿主内核与 KVM 模块
满足对应平台的最低要求。

同时确认当前用户可以访问 `/dev/kvm`：

```bash
ls -l /dev/kvm
id
```

### 关闭 SFB

HAMT 要求关闭宿主 CPU 的 store fill buffer（SFB）。

ABI2.0 内核通常已提供 debugfs 控制接口：

```bash
sudo sh -c 'echo 0 > /sys/kernel/debug/loongarch/sfb_state'
```

如果 `/sys/kernel/debug` 未挂载，先执行：

```bash
sudo mount -t debugfs none /sys/kernel/debug
```

ABI1.0 Loongnix 20 使用本仓库提供的 [hamt-pm](../hamt/hamt-pm) 模块。模块会
在加载时关闭所有 CPU 的 `CSR_STFILL`，并在系统恢复后重新关闭。构建前需要
安装与当前运行内核完全匹配的头文件：

```bash
cd hamt/hamt-pm
make
sudo insmod hamt-pm.ko
```

检查模块和内核日志：

```bash
lsmod | grep hamt_pm
dmesg | tail
```

需要开机自动加载时，可以安装模块并配置 `modules-load.d`：

```bash
sudo make modules_install
sudo depmod -a
echo hamt_pm | sudo tee /etc/modules-load.d/hamt-pm.conf
sudo modprobe hamt_pm
```

卸载模块会恢复 `CSR_STFILL`：

```bash
sudo rmmod hamt_pm
```

升级内核后必须重新编译 `hamt-pm`。该模块只用于 ABI1.0 环境；ABI2.0 应优先使用
内核提供的 `sfb_state` 接口。

### HAMT 不可用时

如果宿主内核版本、KVM 接口或 SFB 控制不满足要求，使用以下参数关闭 HAMT：

```text
-latx select,hamt=0
```

也可以通过 `LATS_OPT="select,hamt=0"` 设置。关闭 HAMT 会显著降低地址转换性能，
应只用于兼容性验证或缺少必要内核支持的环境。

### 使用 OSX-KVM 启动 macOS 恢复环境

以 [OSX-KVM 的启动脚本](https://github.com/kholia/OSX-KVM/blob/master/boot-macOS-headless.sh)
为基础，沿用它的 OpenCore 镜像、CPU 拓扑和设备配置。使用仓库提供的
`OVMF_CODE_4M.fd`，并为每台虚拟机复制一份 `OVMF_VARS-1920x1080.fd`。

按 [OpenCore-ISO 的 CPU 和网卡配置](https://github.com/LongQT-sea/OpenCore-ISO/blob/main/README.md#6-cpu)
为 macOS 10.11–26 选用 `Skylake-Client-v4`。macOS 10.11–10.15 选用
`vmxnet3`，macOS 11–26 选用 `virtio-net-pci`，macOS 10.4–10.10 选用
`e1000`。

原生恢复测试使用 `build-release/lats-x86_64`，SFB 的 `Current State`
为 `0`。使用下列参数替换上游脚本的 KVM 和 CPU 配置。

```text
-accel tcg,mode=lat
-latx select,hamt=1,ghbr=1,shbr=1,aotsys=0
-fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=0
-cpu Skylake-Client-v4,kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,-pcid,-invpcid,-x2apic,-tsc-deadline,-xsavec,-xsaves,-spec-ctrl,-fma,-f16c,x-force-features=on,check
```

OVMF 参数将 PCI MMIO 限于低地址。这个设置在测试主机上绕过了 HAMT
初始化遇到的高地址 MMIO 问题。原生 AVX 和 AVX2 仍只实现子集，CPUID
默认屏蔽这两项。此处的 `x-force-features=on` 用于恢复环境实验。

AVX 或 AVX2 客体使用 `./latxbuild/build.sh -c tcg` 生成的
`build-tcg/lats-x86_64`，启动参数为 `-accel tcg,mode=tcg` 和
`-latx select,hamt=0`。此构建可保留上游 Penryn 配置的
`+avx,+xsave,+xsaveopt`。需要 AVX2 时可再添加 `+avx2`，或者使用
相应 macOS 版本的上游 CPU 型号。TCG 模式当前无法使用 HAMT。

[OpenCore-ISO 的 Proxmox/QEMU 指南](https://github.com/LongQT-sea/OpenCore-ISO#6-cpu)
为 macOS 10.11 至 26 推荐 Skylake 型号，内存建议至少 4 GB，并为 macOS 11
及以后推荐 VirtIO 网卡。该指南的安装介质是 CD/DVD 格式。上面的恢复盘流程
直接将 Apple 恢复 DMG 转为磁盘镜像，仍按 OSX-KVM 的方式通过 AHCI 挂载。

## 4. 默认参数与常用参数组合

不传入 `-latx select,...` 时，LATS 使用默认配置：

- JIT 使用 TB 模式，动态翻译开销低，系统响应速度快。
- AOT 使用 TU 模式，AOT 缓存在客户机关机时于后台生成。
- `aotsys=1`、`tumode=aot`、`tucpopt=max`。
- `tucpopt=max` 只作用于 AOT TU，用于开启最大程度的跨页优化。
- HAMT 默认开启，`verbose` 默认关闭。

默认配置在响应速度和翻译性能之间取得平衡。下文客户机运行命令都不显式添加
`-latx select,...`；需要改变行为时，再选择一个组合加到命令行末尾。多个
选项应写入同一个 `-latx select,...`，不要重复传多个 `-latx select`。

常用组合：

| 用途 | 参数 |
| --- | --- |
| 使用全部默认配置 | 不添加 `-latx` 参数 |
| 禁用 AOT，JIT 使用 TB，关闭跨页优化 | `-latx select,aotsys=0,tumode=off,tucpopt=off` |
| 禁用 AOT，JIT 使用 TU，关闭跨页优化 | `-latx select,aotsys=0,tumode=jit,tucpopt=off` |
| 禁用 AOT，JIT 使用 TU，启用最大跨页优化 | `-latx select,aotsys=0,tumode=jit,tucpopt=max` |
| 关闭 HAMT | `-latx select,hamt=0` |
| 输出最终参数配置 | `-latx select,verbose=on` |
| 组合多个测试选项 | `-latx select,aotsys=0,tumode=off,tucpopt=off,hamt=0,verbose=on` |

### 使用 `LATS_OPT` 环境变量

系统模式构建也可以通过 `LATS_OPT` 设置参数。变量值采用与 `-latx` 相同的
逗号分隔格式，但不需要写 `-latx`：

```bash
LATS_OPT="select,aotsys=0,tumode=off,tucpopt=off,hamt=0" \
    ./build/lats-x86_64 -L ./pc-bios -m 4096 -smp 4 \
    -hda images/guest.qcow2
```

`LATS_OPT` 在命令行参数解析完成后读取，因此同名选项会覆盖命令行 `-latx` 中
的值。不要同时用两种方式设置冲突的参数；需要覆盖时，可以让 `LATS_OPT`
明确包含完整组合。

`aotsys=0,tumode=jit,tucpopt=max` 会提高动态翻译开销，通常只用于测试或性能
分析。安装客户机、排查兼容性或对比问题现象时，优先使用
`aotsys=0,tumode=off,tucpopt=off`。

## 5. 安装客户机操作系统

客户机磁盘由用户自行创建。下面的示例使用 64 GiB，可按客户机系统的要求调整
容量。

```bash
mkdir -p images
qemu-img create -f qcow2 images/guest.qcow2 64G
```

镜像文件和 ISO 不应提交到 LATS 仓库。可以将 `images/` 加入个人工作目录的
忽略规则，或放在仓库之外。

LATS 可运行 x86 架构的客户机操作系统，例如 x86 架构 Ubuntu 系统、Windows
系统。请自行准备对应的 ISO 安装介质；LATS 不提供客户机镜像、下载链接或
Windows 安装介质，客户机操作系统的许可由用户负责。

将 ISO 路径替换为实际文件，例如 `/path/to/guest.iso`。首次安装需要从光驱
启动：

```bash
./build/lats-x86_64 \
    -L ./pc-bios \
    -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -cdrom /path/to/guest.iso \
    -boot d \
    -usb -device usb-tablet \
    -display gtk
```

在安装程序中完成分区、用户和启动引导安装。安装结束后关闭客户机，移除
`-cdrom` 和 `-boot d`。后续从已安装磁盘启动的方式见下一节。

## 6. 启动运行客户机

客户机安装完成后，从已安装磁盘启动。LATS 默认使用 TCG LAT 模式，以下示例
只需选择图形输出和客户机设备。

### 使用 GTK

GTK 是默认图形后端，适合本地桌面环境：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -usb -device usb-tablet \
    -display gtk
```

### 使用 SDL

SDL 后端适合直接使用 SDL 窗口和输入设备：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -usb -device usb-tablet \
    -display sdl
```

### 使用 SPICE

构建启用 SPICE 时，有两种使用方式。

#### 使用内置 `spice-app`

`spice-app` 会启动内置的 SPICE 图形客户端：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -usb -device usb-tablet \
    -display spice-app
```

#### 使用外部 SPICE 客户端

也可以只启动 SPICE 服务，不打开本机图形窗口，然后使用独立客户端连接。下面的
示例把密码放在 QEMU `secret` 对象中：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -usb -device usb-tablet \
    -object secret,id=spiceid,data=1234 \
    -spice port=59595,password-secret=spiceid,image-compression=off \
    -display none
```

客户端机器需要安装 SPICE 客户端。例如 Debian/Ubuntu 或兼容发行版可以安装
`spice-client-gtk`，其中包含 `spicy`：

```bash
sudo apt install spice-client-gtk
spicy --host=127.0.0.1 --port=59595 --password=1234
```

`spicy` 也可以省略 `--password`，连接时再输入密码。客户端不在 LATS 主机上时，
将 `--host` 改为 LATS 主机的 IP 地址，并按需调整 `-spice addr=...`、防火墙和
网络访问策略。

示例中的 `data=1234` 只用于说明参数格式。直接写在命令行中的密码可能出现在
进程列表和日志中，实际使用时应换成强密码；长期运行环境应使用文件或加密
secret，并限制 SPICE 服务只监听可信地址。除 `spicy` 外，也可以使用其它支持
SPICE 协议的客户端。

如果某个图形后端不可用，可运行 `./build/lats-x86_64 -display help` 查看当前
构建实际支持的类型。

### 无界面启动

已经安装好的磁盘可以用只读快照运行，避免测试修改原镜像：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -snapshot \
    -display none \
    -qmp unix:/tmp/lats-qmp.sock,server=on,wait=off -S
```

### 自动化验证

通过 QMP 发送 `cont` 后客户机开始运行，可用 `screendump` 保存画面。自动化
脚本应等待 QMP 事件或截图确认启动完成，不要仅凭 `lats-x86_64` 进程存在判断
客户机已经启动。

Ubuntu 客户机也可以使用安装后的内核进行串口启动验证。内核路径需要从客户机
磁盘中提取，以下仅为示例：

```bash
./build/lats-x86_64 \
    -L ./pc-bios -m 4096 -smp 4 \
    -hda images/guest.qcow2 -snapshot \
    -kernel /path/to/vmlinuz \
    -append 'console=ttyS0 root=/dev/sda1 rw' \
    -nographic
```

启动成功的判据包括出现 Ubuntu 版本信息、`Reached target Multi-User System`
或登录提示。Windows 客户机则应通过 QMP 截图确认桌面、运行测试程序，并通过
QMP 或客户机内关机命令结束运行。

## 7. 常见问题

### 启动后直接退出

先运行 `./build/lats-x86_64 --version`，确认使用的是本次构建的二进制。再检查
磁盘和 ISO 路径、`-M pc`、`-L ./pc-bios` 以及图形后端参数是否完整。

### 无法从 ISO 引导

安装阶段必须包含 `-cdrom /path/to/image.iso -boot d`。安装完成后应移除
`-cdrom`，并使用磁盘启动。

### 图形窗口无法打开

宿主没有可用图形环境时，将 `-display gtk` 改为
`-display none`，并使用串口和 QMP 进行无界面验证。

### HAMT 初始化失败

使用 `-latx select,hamt=0` 运行。若必须测试 HAMT，请确认宿主内核、SFB 状态
和当前 CPU 平台支持该功能。

### 客户机安装介质许可

LATS 只提供虚拟机执行环境，不提供客户机 ISO、产品密钥或激活服务。Ubuntu、
Windows 等客户机操作系统的介质、授权和激活由用户自行负责。

## 8. GPU 加速

LATS 支持通过本地 vGPU 软件栈和客户机 vGPU 驱动启用 GPU 加速。本地 vGPU
库、客户机驱动及配套运行环境均不由本项目提供，使用前需要自行准备并确认
版本兼容。

### 准备本地 vGPU 库

在 LATS 源码根目录准备 `vgpu-release/`，其中至少包含：

```text
vgpu-release/
└── lib/
    ├── pkgconfig/ 或 loongarch64-linux-gnu/pkgconfig/
    └── 对应的动态库
```

### 编译 vGPU 版本

从 LATS 源码根目录执行：

```bash
./latxbuild/build-vgpu.sh
```

默认为 release 配置，产物位于：

```text
build-vgpu/lats-x86_64
```

调试构建：

```bash
./latxbuild/build-vgpu.sh -c dbg
./latxbuild/build-vgpu.sh -c dbg-opt
```

对应产物分别位于 `build-vgpu-dbg/` 和 `build-vgpu-dbg-opt/`。

如果本地 vGPU 动态库没有安装到系统库目录，运行前还需要让动态链接器找到
它们：

```bash
export LD_LIBRARY_PATH="$PWD/vgpu-release/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

### 运行 vGPU 客户机

客户机需要安装与本机 vGPU 软件栈匹配的 vGPU 驱动。驱动未安装或版本不匹配时，
GPU 加速不会生效。

使用 GTK 和 OpenGL 输出启动客户机：

```bash
./build-vgpu/lats-x86_64 \
    -L ./pc-bios -M pc -m 4096 -smp 4 \
    -hda images/guest.qcow2 \
    -usb -device usb-tablet \
    -display gtk,gl=on
```

也可以根据本地图形栈使用 `-display sdl,gl=on`。客户机设备、显示和驱动参数
可能需要按所使用 vGPU 库的说明进一步调整。
