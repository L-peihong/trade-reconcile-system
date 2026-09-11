#include "utils/DbUtil.h"

#include <drogon/drogon.h>

#include <stdexcept>

namespace utils {

std::shared_ptr<drogon::orm::Transaction> beginTransaction() {
    auto client = drogon::app().getDbClient();  // 名为 "default" 的客户端
    if (!client) {
        throw std::runtime_error(
            "default DbClient 未配置（检查 config.json 的 db_clients）");
    }
    // DbClientImpl（仅 is_fast=false 时存在）的 newTransaction 内部用
    // promise/future 封装 newTransactionAsync，无 loop 线程断言，
    // 任意非循环线程可调用（DbClientImpl.cc:319-334）。
    return client->newTransaction();
}

}  // namespace utils
