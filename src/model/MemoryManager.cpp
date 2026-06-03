/// @file MemoryManager.cpp
/// @brief 短期记忆管理器 - 实现

#include <model/MemoryManager.hpp>

namespace LittleMeowBot {
    MemoryManager::MemoryManager(uint64_t groupId) : m_groupId(groupId){}

    std::string MemoryManager::getMemory() const{
        return Database::instance().getShortTermMemory(m_groupId);
    }
}