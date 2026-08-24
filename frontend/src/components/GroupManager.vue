<script lang="ts" setup>
/**
 * @file GroupManager.vue
 * @brief 群管理组件 - 群启用状态、群记忆与聊天记录
 */
import {inject, nextTick, onMounted, onUnmounted, ref, watch, type Ref} from 'vue'
import type {ApiResponse, ChatMessage, Group, QQConfig} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')
const qqConfig = inject<QQConfig>('qqConfig')
const wsConnected = inject<Ref<boolean>>('wsConnected') as Ref<boolean>
const wsObj = inject<{ get: () => WebSocket | null }>('ws')

// 群列表
const groups: Ref<(Group & { enabled: boolean })[]> = ref([])
const loading: Ref<boolean> = ref(false)
const newGroupId: Ref<number | undefined> = ref(undefined)
const saving: Ref<boolean> = ref(false)

// 当前查看聊天记录的群
const selectedGroup: Ref<number | null> = ref(null)
const selectedGroupName: Ref<string> = ref('')
const chatRecords: Ref<(ChatMessage & { id: number })[]> = ref([])
const chatContainer: Ref<HTMLDivElement | null> = ref(null)
const chatLoading: Ref<boolean> = ref(false)

// 编辑状态
const editingId: Ref<number | null> = ref(null)
const editContent: Ref<string> = ref('')

// 记忆弹窗
const memoryGroupId: Ref<number | null> = ref(null)
const memoryGroupName: Ref<string> = ref('')
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

// 选择群查看聊天记录
const selectGroup = async (groupId: number, groupName: string): Promise<void> => {
  selectedGroup.value = groupId
  selectedGroupName.value = groupName
  chatLoading.value = true
  chatRecords.value = []

  try {
    const resp = await fetch(`/admin/api/chat-records/${groupId}?limit=200`)
    chatRecords.value = await resp.json()

    // 订阅 WebSocket
    const ws = wsObj!.get()
    if (ws && wsConnected.value) {
      ws.send(JSON.stringify({action: 'subscribe', groupId}))
    }
  } finally {
    chatLoading.value = false
  }

  // 等待消息列表渲染完成后滚动到底部
  await nextTick()
  scrollToBottom()
}

// 返回列表
const backToList = (): void => {
  const ws = wsObj!.get()
  if (ws) {
    ws.send(JSON.stringify({action: 'unsubscribe'}))
  }
  selectedGroup.value = null
  selectedGroupName.value = ''
  chatRecords.value = []
}

// 滚动到底部
const scrollToBottom = (): void => {
  if (chatContainer.value) {
    chatContainer.value.scrollTop = chatContainer.value.scrollHeight
  }
}

// 开始编辑
const startEdit = (record: ChatMessage & { id: number }): void => {
  editingId.value = record.id
  editContent.value = record.content
}

// 取消编辑
const cancelEdit = (): void => {
  editingId.value = null
  editContent.value = ''
}

// 保存编辑
const saveEdit = async (): Promise<void> => {
  if (!editingId.value || !editContent.value.trim()) return

  const resp = await fetch(`/admin/api/chat-record/${editingId.value}`, {
    method: 'PUT',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({content: editContent.value})
  })
  const data: ApiResponse = await resp.json()
  if (data.success) {
    const record = chatRecords.value.find(r => r.id === editingId.value)
    if (record) record.content = editContent.value
    showToast!('已更新')
    cancelEdit()
  }
}

// 删除记录
const deleteRecord = async (recordId: number): Promise<void> => {
  if (!confirm('确定删除这条记录？')) return

  const resp = await fetch(`/admin/api/chat-record/${recordId}`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    chatRecords.value = chatRecords.value.filter(r => r.id !== recordId)
    showToast!('已删除')
  }
}

// 清空群聊天记录
const clearGroupRecords = async (): Promise<void> => {
  if (!selectedGroup.value) return
  if (!confirm('确定清空该群的所有聊天记录？此操作不可恢复！')) return

  const resp = await fetch(`/admin/api/chat-records/${selectedGroup.value}/clear`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    chatRecords.value = []
    showToast!('聊天记录已清空')
    await loadGroups()
  }
}

// 查看记忆
const viewMemory = async (groupId: number, groupName: string): Promise<void> => {
  memoryGroupId.value = groupId
  memoryGroupName.value = groupName || `群 ${groupId}`
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
  memoryGroupId.value = null
  memoryGroupName.value = ''
  groupMemory.value = ''
}

// 保存记忆
const saveMemory = async (): Promise<void> => {
  if (!memoryGroupId.value) return
  memorySaving.value = true
  try {
    const resp = await fetch(`/admin/api/memory/${memoryGroupId.value}`, {
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

// WebSocket 消息处理
let originalOnMessage: ((event: MessageEvent) => void) | null | undefined = null

const setupWebSocket = (): void => {
  const ws = wsObj!.get()
  if (ws && wsConnected.value) {
    originalOnMessage = ws.onmessage ? ws.onmessage.bind(ws) : null
    ws.onmessage = (event: MessageEvent) => {
      const data = JSON.parse(event.data)
      if (data.type === 'new_message' && data.groupId === selectedGroup.value) {
        chatRecords.value.push(data.data)
        nextTick(scrollToBottom)
      }
      if (originalOnMessage) originalOnMessage(event)
    }
  }
}

// WebSocket 重连时重新设置消息处理器并重新订阅
watch(wsConnected, (connected) => {
  if (connected && selectedGroup.value) {
    setupWebSocket()
    const ws = wsObj!.get()
    if (ws) {
      ws.send(JSON.stringify({action: 'subscribe', groupId: selectedGroup.value}))
    }
  }
})

const restoreWebSocket = (): void => {
  const ws = wsObj!.get()
  if (ws && originalOnMessage) {
    ws.onmessage = originalOnMessage
  }
}

onMounted(async () => {
  await loadGroups()
  setupWebSocket()
})

onUnmounted(restoreWebSocket)
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">群管理</h1>
      <p class="page-subtitle">管理 Bot 启用的群、群记忆与聊天记录</p>
    </div>

    <template v-if="!selectedGroup">
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
          <div style="display: flex; gap: 12px; align-items: center;">
            <h3 class="card-title">群列表</h3>
            <div :class="{ connected: wsConnected, disconnected: !wsConnected }" class="connection-status">
              <span class="dot"></span>
              {{ wsConnected ? '实时连接' : '未连接' }}
            </div>
          </div>
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
                <th style="width: 190px;">操作</th>
              </tr>
              </thead>
              <tbody>
              <tr v-for="group in groups" :key="group.groupId" :class="{ 'row-disabled': !group.enabled }"
                  class="group-row"
                  @click="selectGroup(group.groupId, group.groupName || String(group.groupId))">
                <td>
                  <span
                      :class="group.enabled ? 'status-enabled' : 'status-disabled'"
                      :title="group.enabled ? '点击禁用' : '点击启用'"
                      class="status-badge"
                      @click.stop="toggleGroup(group.groupId)"
                  >
                    {{ group.enabled ? '启用' : '禁用' }}
                  </span>
                </td>
                <td>
                  <strong v-if="group.groupName">{{ group.groupName }}</strong>
                  <span v-else style="color: var(--text-light)">群 {{ group.groupId }}</span>
                </td>
                <td><code>{{ group.groupId }}</code></td>
                <td>{{ group.messageCount || 0 }}</td>
                <td style="white-space: nowrap;">
                  <button class="btn btn-primary btn-sm"
                          @click.stop="selectGroup(group.groupId, group.groupName || String(group.groupId))">记录
                  </button>
                  <button class="btn btn-secondary btn-sm" style="margin-left: 6px;"
                          @click.stop="viewMemory(group.groupId, group.groupName || '')">记忆
                  </button>
                  <button class="btn btn-danger btn-sm" style="margin-left: 6px;"
                          @click.stop="removeGroup(group.groupId)">删除
                  </button>
                </td>
              </tr>
              </tbody>
            </table>
          </template>
        </div>
      </div>
    </template>

    <!-- 聊天记录 -->
    <div v-else class="card chat-card">
      <div class="card-header">
        <div>
          <h3 class="card-title">{{ selectedGroupName }}</h3>
          <span class="msg-count">{{ chatRecords.length }} 条记录</span>
        </div>
        <div style="display: flex; gap: 8px;">
          <button :disabled="chatRecords.length === 0" class="btn btn-danger btn-sm" @click="clearGroupRecords">
            清空记录
          </button>
          <button class="btn btn-secondary btn-sm" @click="backToList">返回列表</button>
        </div>
      </div>

      <div ref="chatContainer" class="chat-container">
        <div v-if="chatLoading" class="chat-empty">
          <p>加载中...</p>
        </div>
        <template v-else>
          <div
              v-for="msg in chatRecords"
              :key="msg.id"
              :class="msg.role"
              class="chat-message"
          >
            <!-- 普通显示 -->
            <template v-if="editingId !== msg.id">
              <div class="msg-header">
                <span class="msg-role">{{ msg.role === 'user' ? '用户' : qqConfig!.botName }}</span>
                <div class="msg-actions">
                  <button class="action-btn" title="编辑" @click="startEdit(msg)">✏️</button>
                  <button class="action-btn delete" title="删除" @click="deleteRecord(msg.id)">🗑️</button>
                </div>
              </div>
              <div class="msg-content">{{ msg.content }}</div>
            </template>

            <!-- 编辑模式 -->
            <template v-else>
              <div class="edit-form">
                <textarea v-model="editContent" class="edit-textarea" rows="3"></textarea>
                <div class="edit-actions">
                  <button class="btn btn-success btn-sm" @click="saveEdit">保存</button>
                  <button class="btn btn-secondary btn-sm" @click="cancelEdit">取消</button>
                </div>
              </div>
            </template>
          </div>

          <div v-if="chatRecords.length === 0" class="chat-empty">
            <div class="chat-empty-icon">💬</div>
            <p>暂无聊天记录</p>
          </div>
        </template>
      </div>
    </div>

    <!-- 记忆弹窗 -->
    <div v-if="memoryGroupId" class="modal-overlay" @click.self="closeMemory">
      <div class="modal-content">
        <div class="modal-header">
          <h2>{{ memoryGroupName }} - 群记忆</h2>
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
  background: var(--success-soft);
  color: var(--success);
}

.status-enabled:hover {
  opacity: 0.8;
}

.status-disabled {
  background: var(--bg-secondary);
  color: var(--text-secondary);
}

.status-disabled:hover {
  opacity: 0.8;
}

.row-disabled {
  opacity: 0.5;
}

.connection-status {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--text-secondary);
}

.connection-status .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.connected .dot {
  background: var(--success);
}

.disconnected .dot {
  background: var(--danger);
}

.group-row {
  cursor: pointer;
}

.group-row:hover td {
  background: var(--primary-softer);
}

.chat-card {
  display: flex;
  flex-direction: column;
  min-height: calc(100vh - 200px);
}

.chat-card .card-header {
  flex-shrink: 0;
}

.msg-count {
  font-size: 12px;
  color: var(--text-secondary);
}

.chat-container {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  background: var(--bg-secondary);
  min-height: 400px;
  max-height: calc(100vh - 280px);
}

.chat-message {
  padding: 12px 16px;
  margin-bottom: 12px;
  border-radius: 12px;
  max-width: 85%;
  position: relative;
}

.chat-message.user {
  background: var(--card-bg);
  border: 1px solid var(--border);
  margin-right: auto;
}

.chat-message.assistant {
  background: linear-gradient(135deg, var(--bubble-from), var(--bubble-to));
  color: var(--on-primary);
  margin-left: auto;
}

.msg-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.msg-role {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  opacity: 0.7;
}

.msg-actions {
  display: flex;
  gap: 4px;
  opacity: 0;
  transition: opacity 0.2s;
}

.chat-message:hover .msg-actions {
  opacity: 1;
}

.action-btn {
  background: none;
  border: none;
  cursor: pointer;
  font-size: 14px;
  padding: 2px 4px;
  opacity: 0.6;
}

.action-btn:hover {
  opacity: 1;
}

.action-btn.delete:hover {
  opacity: 1;
  filter: brightness(0) saturate(100%) invert(27%) sepia(94%) saturate(6514%) hue-rotate(355deg) brightness(93%) contrast(127%);
}

.msg-content {
  font-size: 14px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-word;
}

.edit-form {
  width: 100%;
}

.edit-textarea {
  width: 100%;
  padding: 8px 12px;
  border: 1px solid var(--border);
  border-radius: 8px;
  font-size: 14px;
  font-family: inherit;
  resize: vertical;
  background: var(--input-bg);
  color: var(--text-primary);
}

.chat-message.assistant .edit-textarea {
  background: var(--input-bg);
  color: var(--text-primary);
}

.edit-actions {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}

.chat-empty {
  text-align: center;
  padding: 48px;
  color: var(--text-light);
}

.chat-empty-icon {
  font-size: 48px;
  margin-bottom: 16px;
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
  background: var(--input-bg);
  color: var(--text-primary);
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