# xproc API 命名规范化修改记录

## 修改目标
1. 全部采用 lower_snake_case 命名风格
2. 统一枚举命名
3. 文件重命名
4. 采用 C++ 标准库风格

## 修改计划

### 阶段 1：枚举统一 ✓
- [x] 修改枚举类型名
- [x] 修改枚举值名

### 阶段 2：类名统一 ✓
- [x] 修改接口类名（移除 I 前缀）
- [x] 修改具体类名（简化）

### 阶段 3：方法名统一 ✓
- [x] 修改方法名（移除 try_ 前缀等）

### 阶段 4：常量统一 ✓
- [x] 修改常量名（移除 _v 后缀等）

### 阶段 5：文件重命名
- [ ] 重命名头文件
- [ ] 重命名源文件
- [ ] 创建向后兼容别名

## 详细修改清单

### 枚举修改 ✓

#### ipc_options.hpp
- [x] `channel_type::varlen` → `channel_type::varlen`
- [x] `transport_backend::shm` → `transport_backend::shared_memory`

#### layout_manager.hpp
- [x] `layout_attach_behavior` → `attach_behavior`
- [x] `layout_attach_behavior::count_ref` → `attach_behavior::ref_count`
- [x] `layout_attach_behavior::observe_only` → `attach_behavior::readonly`
- [x] `validate_error` → `validate_error`

### 类名修改 ✓

#### ipc_channel_interface.hpp
- [x] `IProducerChannel` → `producer_channel_interface`
- [x] `IConsumerChannel` → `consumer_channel_interface`
- [x] `shm_producer_transport` → `shm_producer`
- [x] `shm_consumer_transport` → `shm_consumer`

#### ipc_inspector.hpp
- [x] `IIpcRingInspector` → `ring_inspector_interface`
- [x] `IIpcAttachCountView` → `attach_count_view_interface`
- [x] `ipc_ring_snapshot` → `ring_snapshot`

#### ipc_observer.hpp
- [x] `ipc_observer` → `observer`

#### endpoint.hpp
- [x] `endpoint` → `endpoint`

#### channel.hpp
- [x] `channel` → `channel`
- [x] `producer` → `producer`
- [x] `consumer` → `consumer`

#### ipc_runtime.hpp
- [x] `ipc_runtime` → `runtime`

#### shm_layout.hpp
- [x] `control_block` → `control_block`
- [x] `shm_meta` → `meta`

#### layout_manager.hpp
- [x] `layout_manager` → `layout_manager`
- [x] `embedded_layout_version` → `embedded_version`

#### layout_exception.hpp
- [x] 更新 validate_error 引用

### 方法名修改

#### fixed_reader.hpp, varlen_reader.hpp
- [x] `try_read` → `read`
- [x] `try_peek` → `peek`

#### ipc_channel_interface.hpp
- [x] `wait_when_empty` → `wait`

#### endpoint.hpp
- [x] `is_connect` → `is_connected`
- [x] `user_role` → `get_role`

#### ipc_inspector.hpp
- [x] `ring_snapshot` → `snapshot`

### 常量修改

#### ipc_options.hpp
- [x] `shm_min_size_v` → `min_shm_size`

#### layout_manager.hpp
- [x] `is_ready_spin_limit_v` → `is_ready_spin_limit`
- [x] `EXPECTED_MAGIC` → `expected_magic`
- [x] `version_major` → `version_major`
- [x] `version_minor` → `version_minor`

### 文件重命名

#### 头文件
- [ ] `ipc_channel_interface.hpp` → `channel_interface.hpp`
- [ ] `channel.hpp` → `channel.hpp`
- [ ] `endpoint.hpp` → `endpoint.hpp`
- [ ] `ipc_inspector.hpp` → `inspector.hpp`
- [ ] `ipc_messaging.hpp` → `messaging.hpp`
- [ ] `ipc_observer.hpp` → `observer.hpp`
- [ ] `ipc_options.hpp` → `options.hpp`
- [ ] `ipc_runtime.hpp` → `runtime.hpp`
- [ ] `ipc_transport_factory.hpp` → `transport_factory.hpp`

#### 源文件
- [ ] `ipc_channel_interface.cpp` → `channel_interface.cpp`

## 实施状态

- [x] 创建修改计划文档
- [x] 实施枚举修改
- [x] 实施类名修改
- [x] 实施方法名修改
- [x] 实施常量修改
- [ ] 实施文件重命名
- [ ] 更新主头文件 xproc.hpp
- [ ] 运行测试验证

## 已修改文件列表

1. include/xproc/ipc/ipc_options.hpp
2. include/xproc/shm/layout_manager.hpp
3. include/xproc/shm/layout_exception.hpp
4. include/xproc/ipc/ipc_inspector.hpp
5. include/xproc/ipc/ipc_observer.hpp
6. include/xproc/ipc/endpoint.hpp
7. include/xproc/ipc/channel.hpp
8. include/xproc/ipc/ipc_channel_interface.hpp
9. include/xproc/ipc/ipc_runtime.hpp
10. include/xproc/shm/shm_layout.hpp

## 下一步工作

1. 更新 fixed_reader.hpp 和 varlen_reader.hpp 中的方法名
2. 修改 ipc_options.hpp 中的常量名
3. 更新主头文件 xproc.hpp
4. 实施文件重命名
5. 运行测试验证
