# SkyFS

SkyFS 是一个面向高性能计算场景的分布式文件系统原型。当前代码以 C/C++ 为主，核心路径围绕 FUSE 客户端、元数据服务 MDS、对象存储服务 OSD，以及自研 AMP/RDMA 消息通信层展开。

本仓库导入的是 `skyfs_core_src` 源码树，源码保持原始目录结构，未对业务逻辑、构建脚本或历史补丁做清理性改动。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `client/` | FUSE 客户端入口、路径解析、元数据 RPC、对象读写 RPC、客户端缓存、压缩线程与测试脚本 |
| `mds/` | Metadata Server，负责 inode/dentry 元数据、目录子集、元数据缓存、布局、锁、负载均衡与写回 |
| `osd/` | Object Storage Daemon，负责对象读写、数据布局、分区、复制、恢复、xattr、压缩数据处理与 OSD 缓存 |
| `include/` | SkyFS 公共常量、类型、消息协议、文件系统结构、哈希与链表工具 |
| `amp-rdma/` | AMP 异步消息通信库，当前构建启用 RDMA 相关宏和 `libibverbs`/`librdmacm` |
| `docs/` | 项目文档与代码阅读说明 |

## 架构概览

SkyFS 的运行角色分为三类：

- Client：通过 FUSE 暴露文件系统接口，处理路径解析和 POSIX 文件操作。
- MDS：维护文件/目录元数据，处理 lookup、create、remove、rename、readdir、flock、layout 等请求。
- OSD：存储对象数据，处理 read/write/remove、数据布局、复制恢复、xattr 和压缩数据。

通信由 `amp-rdma/` 提供的 AMP 接口承载。SkyFS 消息头、消息类型和 RPC 参数定义集中在 `include/skyfs_msg.h`，公共容量、路径和端口常量集中在 `include/skyfs_const.h`。

更完整的代码阅读说明见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 构建依赖

当前 Makefile 面向 Linux/HPC 环境，依赖项包括：

- C/C++ 编译工具链：`cc`、`g++`、`make`、`ar`、`ranlib`
- FUSE：源码中客户端 Makefile 默认引用 `client/fuse-2.9.4/lib/libfuse3.la`
- RDMA：`libibverbs`、`librdmacm`
- 线程与系统库：`pthread`、`rt`、`dl`、`m`
- 压缩/数据处理：`zstd`、`zlib`、OpenMP、PANS/MANS、SZ3C
- GPU 压缩路径：CUDA、nvcomp、`zstd_gpu_nvcomp`

部分构建脚本和 Makefile 写入了集群路径，例如 `/cluster/skyfs/bin`、`/cluster/skyfs/conf`、`/cluster/skyfs/meta`、`/cluster/skyfs/dl`、`/cluster/skyfs/obj`。在新环境中构建前，需要先确认这些路径、库文件和 FUSE 源码目录是否存在。

## 推荐构建顺序

在满足依赖后，建议按通信库到服务进程的顺序构建：

```bash
cd amp-rdma/source/userspace
make lib

cd ../../../mds
make

cd ../osd
make

cd ../client
make
```

客户端、MDS、OSD 目录下也存在 `Makefile.rdma`、`Makefile.tcp` 等变体，用于不同通信配置。

## 运行配置

核心配置路径由 `include/skyfs_const.h` 定义，常见默认值包括：

- 可执行文件路径：`/cluster/skyfs/bin/`
- 架构配置路径：`/cluster/skyfs/conf/`
- 元数据路径：`/cluster/skyfs/meta/`
- 本地元数据路径：`/cluster/skyfs/local_meta/`
- 数据布局路径：`/cluster/skyfs/dl/`
- 对象数据路径：`/cluster/skyfs/obj/`
- 主机配置文件：`skyfs_hostname.conf`

典型启动顺序是先启动 MDS，再启动 OSD，最后启动客户端挂载进程。客户端入口 `client/client.c` 会初始化通信、线程、布局、缓存和 GPU 压缩相关状态后进入 `fuse_main`。

## 注意事项

- 本次导入保留原始源码状态，包括历史 `.patch` 文件、测试程序、实验性压缩实现和硬编码集群路径。
- 当前仓库没有 CI 配置；由于本地环境是 Windows，未执行 Linux/FUSE/RDMA/CUDA 编译验证。
- 代码中包含多种实验路径，例如异步写、多对象写、xattr、GPU 压缩、数据复制恢复和负载均衡，阅读时建议先从 `client/client.c`、`mds/mds.c`、`osd/osd.c` 三个入口开始。
