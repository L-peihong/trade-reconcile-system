#include "utils/DbUtil.h"

#include <drogon/drogon.h>

#include <stdexcept>

namespace utils {

std::shared_ptr<drogon::orm::Transaction> beginTransaction() {
    auto client = drogon::app().getDbClient();  // "default" 客户端
    if (!client) {
        throw std::runtime_error(
            "default DbClient 未配置（检查 config.json 的 db_clients）");
    }
    // is_fast=false 时是 DbClientImpl，newTransaction 内部是 promise/future
    // 封装的异步接口，没有循环线程断言，非循环线程可调
    return client->newTransaction();
}

}  // namespace utils
