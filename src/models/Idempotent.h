// ============================================================================
// 幂等记录模型（CLAUDE.md 5.6）
// 语义边界：与业务同事务；status=0 冲突返回 10003；status=1 返回响应快照。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace models {

struct Idempotent {
    enum class Status : int {
        kProcessing = 0,  // 处理中
        kSuccess    = 1,  // 成功（response_body 为首次成功快照）
        kFailed     = 2,  // 失败（V1 不使用，预留）
    };

    std::string requestId;     // 幂等键，uk_request_id
    std::uint64_t userId = 0;  // 请求发起人
    std::string apiPath;       // 接口路径
    std::string requestHash;   // 请求体 SHA-256（同 ID 异内容检测）
    std::string responseBody;  // 首次成功后的响应快照
    Status status = Status::kProcessing;
    std::string expireTime;    // 过期时间，定时任务清理
};

}  // namespace models
