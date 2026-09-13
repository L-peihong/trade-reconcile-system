// 日终对账定时任务
// 每分钟检查一次：当日账单文件就绪且当日批次不存在才触发跑批。
// 批次幂等由 uk_date_channel 唯一索引保证，防重入用原子标志，
// 实际跑批在线程池线程执行。
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
