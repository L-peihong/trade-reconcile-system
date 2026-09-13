// ============================================================================
// 日终对账定时任务（链路⑤，亮点④）
//
// 每分钟检查一次：今天的批次是否已存在、今天的账单文件是否就绪 ——
// 都满足才触发跑批。幂等由 t_reconcile_batch.uk_date_channel 保证；
// 防重入用原子标志（CLAUDE.md 6.8），实际跑批在线程池线程（6.6）。
// ============================================================================
#pragma once

#include <atomic>

namespace tasks {

class ReconcileTask {
public:
    static ReconcileTask& instance();

    // 注册定时器（每 60 秒）。由 main.cc 调用一次。
    void start();

private:
    ReconcileTask() = default;

    void runOnce();   // 定时器回调（事件循环线程）：防重入 → 提交线程池
    void checkAndRun();  // 池线程：账单文件存在 && 今天没批 → 跑批

    std::atomic<bool> running_{false};
};

}  // namespace tasks
