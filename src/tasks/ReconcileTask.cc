#include "tasks/ReconcileTask.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <string>

#include <drogon/drogon.h>

#include "common/Logger.h"
#include "dao/ReconcileDao.h"
#include "services/ReconcileService.h"
#include "utils/ThreadPool.h"

namespace tasks {
namespace {

constexpr double kCheckIntervalSeconds = 60.0;
constexpr const char* kChannel = "MOCK";

// 北京时间（显式 UTC+8，不依赖进程 TZ）
std::string todayBeijing() {
    const auto tp = std::chrono::system_clock::now() + std::chrono::hours(8);
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmBuf{};
    gmtime_r(&t, &tmBuf);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmBuf);
    return std::string(buf);
}

}  // namespace

ReconcileTask& ReconcileTask::instance() {
    static ReconcileTask task;
    return task;
}

void ReconcileTask::start() {
    drogon::app().getLoop()->runEvery(kCheckIntervalSeconds, []() {
        ReconcileTask::instance().runOnce();
    });
}

void ReconcileTask::runOnce() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    utils::globalThreadPool().submit([]() {
        ReconcileTask::instance().checkAndRun();
        ReconcileTask::instance().running_.store(false);
    });
}

void ReconcileTask::checkAndRun() {
    auto log = common::Logger::get();

    const std::string billDate = todayBeijing();
    const std::string billPath = "scripts/bills/bill_" + billDate + ".csv";

    // 账单文件没生成，静默跳过，等下一轮
    std::ifstream probe(billPath);
    if (!probe.is_open()) {
        return;
    }

    dao::ReconcileDao dao;
    if (dao.hasBatchForDate(billDate, kChannel)) {
        return;  // 今天已对过账，唯一索引兜底
    }

    services::ReconcileService service;
    const auto result = service.run(billDate);
    if (!result.isOk()) {
        log->warn("reconcile task run failed: date={} code={} msg={}",
                  billDate, result.code, result.message);
    }
}

}  // namespace tasks
