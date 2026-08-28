/// @file MemoryManager.cpp
/// @brief 短期记忆管理器 - 实现

#include <model/MemoryManager.hpp>

namespace insoulforge {
    MemoryManager::MemoryManager(uint64_t sessionId) : m_sessionId(sessionId) {
    }

    std::string MemoryManager::getMemory() const {
        return Database::instance().getShortTermMemory(m_sessionId);
    }
}
