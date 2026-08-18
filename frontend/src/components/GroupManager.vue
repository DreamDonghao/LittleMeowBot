<script lang="ts" setup>
/**
 * @file GroupManager.vue
 * @brief 群管理组件 - 管理群启用状态和记忆
 */
import {inject, onMounted, ref, type Ref} from 'vue'
import type {ApiResponse, Group} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')

// 群列表
const groups: Ref<(Group & { enabled: boolean })[]> = ref([])
const loading: Ref<boolean> = ref(false)
const newGroupId: Ref<number | undefined> = ref(undefined)
const saving: Ref<boolean> = ref(false)

// 记忆弹窗
const selectedGroup: Ref<number | null> = ref(null)
const selectedGroupName: Ref<string> = ref('')
const groupMemory: Ref<string> = ref('')
const memoryLoading: Ref<boolean> = ref(false)
const memorySaving: Ref<boolean> = ref(false)

// 加载群列表
const loadGroups = async (): Promise<void> => {
  loading.value = true
  try {
    const resp = await fetch('/admin/api/groups')
    if (!resp.ok) {
      showToast!('加载失败: ' + resp.status, true)
      groups.value = []
      return
    }
    const data = await resp.json()
    if (Array.isArray(data)) {
      groups.value = data
    } else {
      groups.value = []
    }
  } catch {
    showToast!('网络错误，请检查后端服务', true)
    groups.value = []
  } finally {
    loading.value = false
  }
}

// 添加群
const addGroup = async (): Promise<void> => {
  if (!newGroupId.value) {
    showToast!('请输入群号', true)
    return
  }
  saving.value = true
  try {
    const resp = await fetch('/admin/api/group', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({groupId: newGroupId.value})
    })
    const data: ApiResponse = await resp.json()
    if (data.success) {
      showToast!('群已添加')
      newGroupId.value = undefined
      await loadGroups()
    } else {
      showToast!(data.error || '添加失败', true)
    }
  } finally {
    saving.value = false
  }
}

// 切换启用状态
const toggleGroup = async (groupId: number): Promise<void> => {
  const resp = await fetch(`/admin/api/group/${groupId}/toggle`, {method: 'POST'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    const group = groups.value.find(g => g.groupId === groupId)
    if (group) {
      group.enabled = !group.enabled
      showToast!(group.enabled ? '群已启用' : '群已禁用')
    }
  }
}

// 删除群
const removeGroup = async (groupId: number): Promise<void> => {
  if (!confirm('确定要删除该群吗？聊天记录将保留。')) return
  const resp = await fetch(`/admin/api/group/${groupId}`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    showToast!('群已删除')
    await loadGroups()
  }
}

// 刷新所有群名称
const refreshAllGroupNames = async (): Promise<void> => {
  const resp = await fetch('/admin/api/groups/refresh-names', {method: 'POST'})
  const data = await resp.json()
  if (data.success) {
    await loadGroups()
    showToast!('群名称已刷新')
  }
}

// 查看记忆
const viewMemory = async (groupId: number, groupName: string): Promise<void> => {
  selectedGroup.value = groupId
  selectedGroupName.value = groupName || `群 ${groupId}`
  memoryLoading.value = true
  groupMemory.value = ''

  try {
    const resp = await fetch(`/admin/api/memory/${groupId}`)
    const data = await resp.json()
    groupMemory.value = data.memory || ''
  } finally {
    memoryLoading.value = false
  }
}

// 关闭记忆弹窗
const closeMemory = (): void => {
  selectedGroup.value = null
  selectedGroupName.value = ''
  groupMemory.value = ''
}

// 保存记忆
const saveMemory = async (): Promise<void> => {
  if (!selectedGroup.value) return
  memorySaving.value = true
  try {
    const resp = await fetch(`/admin/api/memory/${selectedGroup.value}`, {
      method: 'PUT',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({memory: groupMemory.value})
    })
    const data: ApiResponse = await resp.json()
    if (data.success) {
      showToast!('记忆已保存')
    } else {
      showToast!(data.error || '保存失败', true)
    }
  } finally {
    memorySaving.value = false
  }
}

onMounted(loadGroups)
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">群管理</h1>
      <p class="page-subtitle">管理Bot启用的群及群记忆</p>
    </div>

    <!-- 添加群 -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">添加群</h3>
      </div>
      <div style="display: flex; gap: 12px; align-items: flex-end;">
        <div class="form-group" style="flex: 1; max-width: 300px; margin: 0;">
          <label class="form-label">群号</label>
          <input v-model.number="newGroupId" class="form-input" placeholder="输入群号" type="number">
        </div>
        <button :disabled="saving" class="btn btn-success" @click="addGroup">
          {{ saving ? '添加中...' : '添加群' }}
        </button>
      </div>
    </div>

    <!-- 群列表 -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">群列表</h3>
        <div style="display: flex; gap: 8px; align-items: center;">
          <span style="color: var(--text-secondary); font-size: 13px;">共 {{ groups.length }} 个群</span>
          <button class="btn btn-secondary btn-sm" @click="refreshAllGroupNames">刷新群名</button>
        </div>
      </div>
      <div class="table-container">
        <template v-if="loading">
          <div class="empty-state">
            <p>加载中...</p>
          </div>
        </template>
        <template v-else-if="groups.length === 0">
          <div class="empty-state">
            <div class="empty-icon">👥</div>
            <p>暂无群，请添加群号</p>
          </div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th style="width: 60px;">状态</th>
              <th>群名称</th>
              <th style="width: 120px;">群号</th>
              <th style="width: 70px;">消息</th>
              <th style="width: 140px;">操作</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="group in groups" :key="group.groupId" :class="{ 'row-disabled': !group.enabled }">
              <td>
                <span
                    :class="group.enabled ? 'status-enabled' : 'status-disabled'"
                    :title="group.enabled ? '点击禁用' : '点击启用'"
                    class="status-badge"
                    @click="toggleGroup(group.groupId)"
                >
                  {{ group.enabled ? '启用' : '禁用' }}
                </span>
              </td>
              <td>
                <strong v-if="group.groupName">{{ group.groupName }}</strong>
                <span v-else style="color: var(--text-light)">-</span>
              </td>
              <td><code>{{ group.groupId }}</code></td>
              <td>{{ group.messageCount || 0 }}</td>
              <td>
                <button class="btn btn-primary btn-sm" @click="viewMemory(group.groupId, group.groupName || '')">记忆
                </button>
                <button class="btn btn-danger btn-sm" @click="removeGroup(group.groupId)">删除</button>
              </td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <!-- 记忆弹窗 -->
    <div v-if="selectedGroup" class="modal-overlay" @click.self="closeMemory">
      <div class="modal-content">
        <div class="modal-header">
          <h2>{{ selectedGroupName }} - 群记忆</h2>
          <button class="btn btn-secondary btn-sm" @click="closeMemory">关闭</button>
        </div>
        <div class="modal-body">
          <div v-if="memoryLoading" class="memory-loading">
            <p>加载中...</p>
          </div>
          <textarea
              v-else
              v-model="groupMemory"
              class="memory-editor"
              placeholder="暂无记忆，可在此编辑..."
          ></textarea>
        </div>
        <div class="modal-footer">
          <button :disabled="memorySaving" class="btn btn-primary" @click="saveMemory">
            {{ memorySaving ? '保存中...' : '保存' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.status-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
}

.status-enabled {
  background: rgba(16, 185, 129, 0.15);
  color: var(--success);
}

.status-enabled:hover {
  background: rgba(16, 185, 129, 0.3);
}

.status-disabled {
  background: rgba(100, 116, 139, 0.15);
  color: var(--text-secondary);
}

.status-disabled:hover {
  background: rgba(100, 116, 139, 0.3);
}

.row-disabled {
  opacity: 0.5;
}

/* 弹窗 */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1001;
}

.modal-content {
  background: var(--card-bg);
  border-radius: 16px;
  width: 90%;
  max-width: 600px;
  max-height: 70vh;
  display: flex;
  flex-direction: column;
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
}

.modal-header h2 {
  margin: 0;
  font-size: 16px;
}

.modal-body {
  flex: 1;
  padding: 16px 20px;
  overflow: hidden;
}

.memory-loading {
  text-align: center;
  padding: 48px;
  color: var(--text-light);
}

.memory-editor {
  width: 100%;
  height: 280px;
  padding: 12px;
  border: 1px solid var(--border);
  border-radius: 8px;
  font-size: 13px;
  font-family: inherit;
  resize: vertical;
  line-height: 1.6;
}

.memory-editor:focus {
  outline: none;
  border-color: var(--primary);
}

.modal-footer {
  padding: 12px 20px;
  border-top: 1px solid var(--border);
  display: flex;
  justify-content: flex-end;
}
</style>