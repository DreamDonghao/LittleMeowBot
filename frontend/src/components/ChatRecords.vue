<script lang="ts" setup>
/**
 * @file ChatRecords.vue
 * @brief 聊天记录组件 - 实时查看、编辑、删除
 */
import {inject, nextTick, onMounted, onUnmounted, ref, watch, type Ref} from 'vue'
import type {ApiResponse, ChatMessage, Group, QQConfig} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')
const qqConfig = inject<QQConfig>('qqConfig')
const wsConnected = inject<Ref<boolean>>('wsConnected') as Ref<boolean>
const wsObj = inject<{ get: () => WebSocket | null }>('ws')

// 群列表
const groups: Ref<Group[]> = ref([])
const loading: Ref<boolean> = ref(false)

// 当前选中的群
const selectedGroup: Ref<number | null> = ref(null)
const selectedGroupName: Ref<string> = ref('')
const chatRecords: Ref<(ChatMessage & { id: number })[]> = ref([])
const chatContainer: Ref<HTMLDivElement | null> = ref(null)
const chatLoading: Ref<boolean> = ref(false)

// 编辑状态
const editingId: Ref<number | null> = ref(null)
const editContent: Ref<string> = ref('')

// 加载群列表
const loadGroups = async (): Promise<void> => {
  loading.value = true
  try {
    const resp = await fetch('/admin/api/chat-groups')
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

// 选择群
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

    // 等待渲染完成后滚动到底部
    await nextTick()
    await nextTick()
    scrollToBottom()
  } finally {
    chatLoading.value = false
  }
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
const clearGroupRecords = async (groupId: number): Promise<void> => {
  if (!confirm('确定清空该群的所有聊天记录？此操作不可恢复！')) return

  const resp = await fetch(`/admin/api/chat-records/${groupId}/clear`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    showToast!('聊天记录已清空')
    await loadGroups()
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
      <h1 class="page-title">聊天记录</h1>
      <p class="page-subtitle">实时查看和管理群聊天记录</p>
    </div>

    <!-- 群列表 -->
    <div v-if="!selectedGroup" class="card">
      <div class="card-header">
        <h3 class="card-title">选择群聊</h3>
        <div :class="{ connected: wsConnected, disconnected: !wsConnected }" class="connection-status">
          <span class="dot"></span>
          {{ wsConnected ? '已连接' : '未连接' }}
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
            <div class="empty-icon">💬</div>
            <p>暂无聊天记录</p>
          </div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>群名称</th>
              <th style="width: 140px;">群号</th>
              <th style="width: 100px;">消息数</th>
              <th style="width: 130px;">操作</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="group in groups" :key="group.groupId" class="group-row"
                @click="selectGroup(group.groupId, group.groupName || String(group.groupId))">
              <td>
                <strong v-if="group.groupName">{{ group.groupName }}</strong>
                <span v-else style="color: var(--text-light)">群 {{ group.groupId }}</span>
              </td>
              <td><code>{{ group.groupId }}</code></td>
              <td>{{ group.messageCount || 0 }}</td>
              <td style="white-space: nowrap;">
                <button class="btn btn-primary btn-sm"
                        @click.stop="selectGroup(group.groupId, group.groupName || String(group.groupId))">进入
                </button>
                <button :disabled="!group.messageCount" class="btn btn-danger btn-sm"
                        style="margin-left: 8px;" @click.stop="clearGroupRecords(group.groupId)">清空
                </button>
              </td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <!-- 聊天记录 -->
    <div v-else class="card chat-card">
      <div class="card-header">
        <div>
          <h3 class="card-title">{{ selectedGroupName }}</h3>
          <span class="msg-count">{{ chatRecords.length }} 条记录</span>
        </div>
        <button class="btn btn-secondary btn-sm" @click="backToList">返回列表</button>
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
  </div>
</template>

<style scoped>
.connection-status {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
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
  background: rgba(99, 102, 241, 0.05);
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
  background: var(--bg-main);
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
  background: #fff;
  border: 1px solid var(--border);
  margin-right: auto;
}

.chat-message.assistant {
  background: linear-gradient(135deg, var(--primary), var(--primary-dark));
  color: #fff;
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
}

.chat-message.assistant .edit-textarea {
  background: rgba(255, 255, 255, 0.9);
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
</style>