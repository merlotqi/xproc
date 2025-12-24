include/
└── xproc/
    ├── platform/      # 跨平台与编译器抽象
    ├── sync/          # 同步原语（atomic wait / wake）
    ├── memory/        # 共享内存抽象
    ├── ringbuffer/    # RingBuffer 体系（传输层）
    ├── protocol/      # 消息协议（JSON / Protobuf / Custom）
    ├── ipc/           # IPC 核心 glue
    ├── task/          # 异步 Task Runtime
    └── xproc.hpp      # 对外统一入口

include/xproc/platform/
├── platform.hpp          # OS / compiler / arch detection
├── platform_atomic.hpp   # cacheline size / atomic traits
├── platform_time.hpp     # sleep / clock / timeout
└── platform_error.hpp    # errno / GetLastError 抽象

include/xproc/sync/
├── atomic_wait.hpp           # IAtomicWait 接口
├── atomic_wait_futex.hpp     # Linux futex 实现
├── atomic_wait_win32.hpp     # Windows WaitOnAddress 实现
└── atomic_backoff.hpp        # spin / yield / sleep 策略

include/xproc/shm/
├── shm.hpp         # SharedMemory 抽象
├── shm_layout.hpp  # 共享内存头部 / layout
├── shm_open_mode.hpp         # Create / Open / OpenOrWait
└── shm_layout_manager.hpp

include/xproc/ringbuffer/
├── ringbuffer.hpp        # IRingBuffer（仅状态查询）
├── ringbuffer_view.hpp   # Reader / Writer 视图
├── ringbuffer_stats.hpp
└── ringbuffer_error.hpp

include/xproc/ringbuffer/ # 固定长度 RingBuffer
├── byte_ringbuffer.hpp
├── byte_rb_layout.hpp
├── byte_rb_writer.hpp
└── byte_rb_reader.hpp

include/xproc/ringbuffer/ # 可变长度抽象
├── varlen_ringbuffer.hpp
├── varlen_message.hpp


include/xproc/ringbuffer/
├── fixed_ringbuffer.hpp

include/xproc/protocol/
├── protocol.hpp            # IProtocol
├── json_protocol.hpp
├── protobuf_protocol.hpp
└── custom_protocol.hpp

include/xproc/ipc/
├── ipc_endpoint.hpp     # Producer / Consumer 角色
├── ipc_channel.hpp      # send / poll 抽象
├── ipc_options.hpp
├── ipc_inspector.hpp    # 非侵入式监控
├── ipc_refcount.hpp     # 进程级引用计数
└── ipc_runtime.hpp

include/xproc/task/
├── task.hpp
├── task_id.hpp
├── task_status.hpp
├── task_promise.hpp
├── executor.hpp        # thread pool
├── dispatcher.hpp      # IPC 单 consumer → workers
└── task_runtime.hpp
