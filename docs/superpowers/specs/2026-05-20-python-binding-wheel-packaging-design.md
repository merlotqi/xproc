# xproc Python Binding Wheel Packaging Design

## Scope

这一版设计只解决 Python binding 的构建与分发问题，不改动 `xproc`、`xproc_c` 或现有 `pybind11` 公开 API 的语义。

交付目标是：

- 保留当前基于 `pybind11` 的绑定实现
- 从“仓库内 stage 包”升级为标准 Python wheel 分发
- 为每个支持的 CPython 小版本分别构建 wheel
- 同时保留 `sdist` 作为无预编译平台的兜底
- 保留现有 CMake 本地开发和 `ctest` 验证链路

这一版明确不做：

- `abi3` / limited API
- 切换到 `cffi`、`ctypes` 或其他绑定技术
- 改写 Python API 形状
- 引入额外运行时第三方依赖

## Problem Statement

当前 Python binding 位于 `Python/` 目录，使用 `find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)` 与 `pybind11_add_module(_xproc_pybind ...)` 直接编译 CPython 扩展模块。

这种模式在本地开发时工作正常，但产物天然绑定到编译时所选 Python 小版本：

- 用 Python 3.11 编译的扩展不能直接在 Python 3.12 中导入
- 当前 `xproc_python_package` 更偏向仓库内部 staging，不是标准 wheel 发布入口
- 后续如果接入 PyPI 或 GitHub Releases，需要额外脚本去收集和包装产物

由于 `xproc` 的 Python 绑定体积小、无额外运行时三方依赖，多版本 wheel 的发布成本可接受，因此最合适的方向是保留现有绑定实现，改造分发方式而不是追求单 wheel 跨多版本 ABI。

## Decision

采用“`scikit-build-core + CMake + pybind11 + cibuildwheel`”的组合：

- `scikit-build-core` 作为 Python 构建前端
- 现有 CMake 继续负责编译 `_xproc_pybind` 与所需本地库
- 每个 CPython 小版本单独构建 wheel
- CI 使用 `cibuildwheel` 产出 Linux、macOS、Windows 的 wheel
- 发布时同时提供 `sdist`

这个决策的核心原因是：

- 与当前工程结构最匹配，迁移成本最低
- 不需要为 `abi3` 收缩绑定能力
- Python 用户安装体验标准化，`pip` 自动选择匹配解释器的 wheel
- 仍然可以保留仓库内的 CMake 测试入口，避免把开发流程完全切到纯 Python 工具链

## Architecture

### Packaging Entry Point

在仓库根新增 `pyproject.toml`，将 Python 包构建入口标准化。Python 分发元数据以根目录为入口，但源码仍然保留在 `Python/` 下：

- Python 包源码：`Python/xproc/`
- 绑定源码：`Python/src/python_binding.cpp`
- Python 说明文档：`Python/README.md`
- 构建元数据：仓库根 `pyproject.toml`

`python -m build` 或 `pip wheel .` 时，构建前端根据当前解释器调用 CMake，并只生成适配该解释器的扩展模块。

### CMake Responsibilities

`Python/CMakeLists.txt` 继续负责：

- 解析当前 Python 解释器与开发头文件
- 构建 `_xproc_pybind`
- 链接 `xproc_c`，并在需要时一并携带 `xproc`
- 生成本地开发使用的 stage 包
- 注册 `ctest` smoke test

在 wheel 构建模式下，`Python/CMakeLists.txt` 需要额外支持“安装到 wheel staging 目录”的标准安装流程，而不再只依赖 `xproc_python_package` 自定义目标。

### Native Library Strategy

wheel 应包含 Python 运行所需的全部原生产物，避免用户额外安装 `xproc` 或 `xproc_c` 动态库。

推荐策略：

- 如果 `xproc` / `xproc_c` 为共享库，则与 `_xproc_pybind` 一起打包进 `xproc/` 包目录
- 如果为静态库，则继续静态链接到扩展模块或其中间库，不额外暴露共享对象

这保证 wheel 安装完成后即可直接 `import xproc`，不会依赖系统级库搜索路径。

## Build Flow

### Local Development

保留当前 CMake 开发流：

- `cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON`
- `cmake --build build --target xproc_python_package`
- `ctest --test-dir build -L python`

这一链路继续服务于仓库开发者和跨语言联调场景。

### Wheel Build

新增标准 Python 构建流：

- `python -m build`
- 或 `pip wheel .`

其行为应为：

- 使用当前 Python 解释器解析 build backend
- 调 CMake 构建对应版本 `_xproc_pybind`
- 收集 `Python/xproc/__init__.py`、`__init__.pyi`、`py.typed`
- 收集扩展模块和必要本地库
- 生成带 `cp3x` tag 的 wheel

### CI Distribution

CI 使用 `cibuildwheel` 对支持的平台和 Python 版本矩阵构建 wheel。第一阶段以 CPython 为主，不覆盖 PyPy。

建议首批支持：

- Linux: `x86_64`, `aarch64`
- macOS: `x86_64`, `arm64`
- Windows: `AMD64`
- Python: 3.10, 3.11, 3.12, 3.13

同时构建一个 `sdist`，用于少数没有预编译 wheel 的平台回退到本地源码编译。

## Repository Changes

第一阶段需要新增或调整的文件边界：

- 新增仓库根 `pyproject.toml`
- 视需要新增仓库根 `README` 引用或 Python 打包说明
- 调整 `Python/CMakeLists.txt`，兼容 wheel 构建与现有 stage 构建双模式
- 视需要新增 `Python/pyproject` 辅助配置片段或 package data 清单
- 新增 CI workflow，用于 `cibuildwheel` 构建与产物上传
- 更新 `Python/README.md`，补充 `pip wheel` / `python -m build` / 安装说明

这个变更不应破坏现有：

- `XPROC_BUILD_PYTHON`
- `xproc_python_package`
- `xproc_python_smoke`
- 现有 Python examples 的导入路径约定

## Error Handling And Compatibility

设计上需要明确处理以下兼容性问题：

- Python 版本不匹配不再通过“跨版本复用同一个本地构建产物”解决，而是通过安装匹配 wheel 解决
- wheel 构建失败时，错误应直接暴露为标准 Python build backend 失败，而不是静默回退到 stage 目录
- 对于没有对应 wheel 的平台，`sdist` 本地构建仍需给出清晰失败信息，例如缺少编译器、CMake 或 Python headers
- `Python/tests/smoke_test.py` 需要既能覆盖本地 stage 构建，也能覆盖 wheel 安装后的导入与最小功能验证

## Testing

验证分三层：

### 1. Existing CMake Smoke Coverage

继续运行现有：

- `ctest --test-dir build -L python`

确保仓库内开发路径未回归。

### 2. Wheel Import Smoke

新增 wheel 安装验证：

- 构建 wheel
- 在干净虚拟环境中安装该 wheel
- 运行最小 import 和基础 roundtrip smoke test

目标是验证：

- `import xproc` 成功
- `_xproc_pybind` 可加载
- 必要共享库可被解析
- 现有 fixed / varlen 最小 happy path 不回归

### 3. sdist Fallback

至少在一个 CI 平台上验证 `sdist` 安装链路，确保源码分发不是名义支持。

## Migration Plan

建议分两步推进：

### Phase 1: Packaging Foundation

- 接入 `pyproject.toml`
- 让 `scikit-build-core` 能驱动当前 CMake 构建出 wheel
- 保持现有 `xproc_python_package` 与 `ctest` 可用
- 在本地确认 wheel 构建和安装成功

### Phase 2: CI And Release

- 接入 `cibuildwheel`
- 跑通多平台多 Python 版本矩阵
- 补全 wheel smoke test 与 `sdist` smoke test
- 文档化发布流程

## Open Questions Resolved

本设计中，以下问题已经定案：

- 不追求 `abi3`
- 不切换绑定技术
- 发布多版本 wheel 是可接受成本
- Python wheel 内应自带运行所需原生库
- 现有 CMake stage 构建要保留，作为仓库开发入口

## Success Criteria

完成后应满足：

- 开发者仍可通过现有 CMake 目标构建并测试 Python binding
- `python -m build` 能在仓库根生成合法 wheel 与 `sdist`
- 不同 Python 小版本各自产出匹配 wheel
- wheel 安装后无需额外配置即可 `import xproc`
- CI 可自动构建并验证所有目标平台的 Python wheel
