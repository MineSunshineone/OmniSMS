// 平台能力边界汇总。业务逻辑只依赖这些接口，不依赖 ESP-IDF/POSIX/Android API。
#pragma once

#include <string>

#include "omnisms/sms.h"

namespace omnisms {

class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;
    virtual bool connected() const = 0;
    virtual std::string local_address() const = 0;
};

// 与现有 KeyValueStore 同一语义；保留更清晰的架构命名。
using StorageInterface = KeyValueStore;

// WiFi Calling/通话音频预留。ESP 端应返回 available=false，不硬塞实现。
class AudioInterface {
public:
    virtual ~AudioInterface() = default;
    virtual bool available() const = 0;
};

}  // namespace omnisms
