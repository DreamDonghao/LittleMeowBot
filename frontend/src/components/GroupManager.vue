<script lang="ts" setup>
/**
 * @file GroupManager.vue
 * @brief 群管理组件 - 群启用状态、群记忆与聊天记录
 */
import {computed, inject, nextTick, onMounted, onUnmounted, ref, type Ref, watch} from 'vue'
import type {AffinityEntry, ApiResponse, ChatMessage, Group, QQConfig, ScheduledTask} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')
const qqConfig = inject<QQConfig>('qqConfig')
const wsConnected = inject<Ref<boolean>>('wsConnected') as Ref<boolean>
const wsObj = inject<{ get: () => WebSocket | null }>('ws')

// 群列表
const groups: Ref<(Group & { enabled?: boolean })[]> = ref([])
const loading: Ref<boolean> = ref(false)
const newGroupId: Ref<number | undefined> = ref(undefined)
const saving: Ref<boolean> = ref(false)
// 添加与筛选
const addType: Ref<'group' | 'private'> = ref('group')
const typeFilter: Ref<'all' | 'group' | 'private'> = ref('all')
const filteredGroups = computed(() => {
  if (typeFilter.value === 'all') return groups.value
  return groups.value.filter(g =>
      typeFilter.value === 'private' ? isPrivateSession(g) : !isPrivateSession(g))
})

// 会话 ID 的字符串形式（私聊会话 ID 带标志位，超过 JS Number 安全范围，须以字符串操作）
const sessionKey = (g: Group): string => g.groupIdStr ?? String(g.groupId)
const isPrivateSession = (g: Group): boolean => g.sessionType === 'private'
const sessionLabel = (g: Group): string =>
    isPrivateSession(g)
        ? g.groupName ? `${g.groupName} (${g.userId ?? ''})` : `私聊 ${g.userId ?? ''}`
        : g.groupName || `群 ${g.groupId}`

// 当前查看聊天记录的会话
const selectedGroup: Ref<string | null> = ref(null)
const selectedGroupName: Ref<string> = ref('')
const chatRecords: Ref<(ChatMessage & { id: number })[]> = ref([])
const chatContainer: Ref<HTMLDivElement | null> = ref(null)
const chatLoading: Ref<boolean> = ref(false)

// 编辑状态
const editingId: Ref<number | null> = ref(null)
const editContent: Ref<string> = ref('')

// 记忆弹窗
const memoryGroupId: Ref<string | null> = ref(null)
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

// 添加会话（群聊按群号，私聊按 QQ 号，私聊会话 ID 由后端构造）
const addGroup = async (): Promise<void> => {
  if (!newGroupId.value) {
    showToast!(addType.value === 'private' ? '请输入QQ号' : '请输入群号', true)
    return
  }
  saving.value = true
  try {
    const body = addType.value === 'private'
        ? {sessionType: 'private', userId: newGroupId.value}
        : {groupId: newGroupId.value}
    const resp = await fetch('/admin/api/group', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body)
    })
    const data: ApiResponse = await resp.json()
    if (data.success) {
      showToast!('会话已添加')
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
const toggleGroup = async (groupId: string): Promise<void> => {
  const resp = await fetch(`/admin/api/group/${groupId}/toggle`, {method: 'POST'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    const group = groups.value.find(g => sessionKey(g) === groupId)
    if (group) {
      group.enabled = !group.enabled
      showToast!(group.enabled ? '已启用' : '已禁用')
    }
  }
}

// 删除群
const removeGroup = async (groupId: string): Promise<void> => {
  if (!confirm('确定要删除该会话吗？聊天记录将保留。')) return
  const resp = await fetch(`/admin/api/group/${groupId}`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    showToast!('已删除')
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

// 选择会话查看聊天记录
const selectGroup = async (sessionId: string, sessionName: string): Promise<void> => {
  selectedGroup.value = sessionId
  selectedGroupName.value = sessionName
  chatLoading.value = true
  chatRecords.value = []

  try {
    const resp = await fetch(`/admin/api/chat-records/${sessionId}?limit=200`)
    chatRecords.value = await resp.json()

    // 订阅 WebSocket（字符串形式传递，后端可正确解析大数）
    const ws = wsObj!.get()
    if (ws && wsConnected.value) {
      ws.send(JSON.stringify({action: 'subscribe', groupId: sessionId}))
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
const viewMemory = async (sessionId: string, sessionName: string): Promise<void> => {
  memoryGroupId.value = sessionId
  memoryGroupName.value = sessionName
  memoryLoading.value = true
  groupMemory.value = ''

  try {
    const resp = await fetch(`/admin/api/memory/${sessionId}`)
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

// 好感度弹窗
const affinityGroupId: Ref<string | null> = ref(null)
const affinityGroupName: Ref<string> = ref('')
const affinityList: Ref<AffinityEntry[]> = ref([])
const affinityLoading: Ref<boolean> = ref(false)
const affinityAvatarFailed: Ref<Set<string>> = ref(new Set())

// 查看好感度
const viewAffinity = async (sessionId: string, sessionName: string): Promise<void> => {
  affinityGroupId.value = sessionId
  affinityGroupName.value = sessionName
  affinityLoading.value = true
  affinityList.value = []
  affinityAvatarFailed.value = new Set()

  try {
    const resp = await fetch(`/admin/api/affinity/${sessionId}`)
    const data = await resp.json()
    affinityList.value = Array.isArray(data.affinities) ? data.affinities : []
  } finally {
    affinityLoading.value = false
  }
}

// 关闭好感度弹窗
const closeAffinity = (): void => {
  affinityGroupId.value = null
  affinityGroupName.value = ''
  affinityList.value = []
}

const affinityClass = (v: number): string =>
    v > 0 ? 'affinity-positive' : v < 0 ? 'affinity-negative' : 'affinity-neutral'

// 定时任务弹窗
const tasksGroupId: Ref<string | null> = ref(null)
const tasksGroupName: Ref<string> = ref('')
const tasksList: Ref<ScheduledTask[]> = ref([])
const tasksLoading: Ref<boolean> = ref(false)
const cancellingTaskId: Ref<number | null> = ref(null)

const fmtTaskTime = (unixSec: number): string => {
  const d = new Date(unixSec * 1000)
  const p = (n: number): string => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

const fmtTaskRelative = (unixSec: number): string => {
  const diff = unixSec - Math.floor(Date.now() / 1000)
  if (diff <= 0) return '即将触发'
  if (diff < 3600) return `${Math.ceil(diff / 60)} 分钟后`
  if (diff < 86400) return `${Math.floor(diff / 3600)} 小时 ${Math.floor((diff % 3600) / 60)} 分后`
  return `${Math.floor(diff / 86400)} 天后`
}

// 查看定时任务
const viewTasks = async (sessionId: string, sessionName: string): Promise<void> => {
  tasksGroupId.value = sessionId
  tasksGroupName.value = sessionName
  tasksLoading.value = true
  tasksList.value = []

  try {
    const resp = await fetch(`/admin/api/scheduled-tasks/${sessionId}`)
    const data = await resp.json()
    tasksList.value = Array.isArray(data.tasks) ? data.tasks : []
  } finally {
    tasksLoading.value = false
  }
}

// 关闭定时任务弹窗
const closeTasks = (): void => {
  tasksGroupId.value = null
  tasksGroupName.value = ''
  tasksList.value = []
}

// 取消定时任务
const cancelTask = async (task: ScheduledTask): Promise<void> => {
  if (!confirm(`确定取消定时任务 #${task.id}？\n触发时间：${fmtTaskTime(task.remindTime)}\n内容：${task.content}`)) {
    return
  }
  cancellingTaskId.value = task.id
  try {
    const resp = await fetch(`/admin/api/scheduled-task/${task.id}`, {method: 'DELETE'})
    const data: ApiResponse = await resp.json()
    if (data.success) {
      tasksList.value = tasksList.value.filter(t => t.id !== task.id)
      showToast!('定时任务已取消')
    } else {
      showToast!(data.error || '取消失败', true)
    }
  } finally {
    cancellingTaskId.value = null
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
      <h1 class="page-title">会话管理</h1>
      <p class="page-subtitle">管理 Bot 启用的群聊与私聊会话、记忆与聊天记录</p>
    </div>

    <template v-if="!selectedGroup">
      <!-- 添加会话 -->
      <div class="card" style="padding: 12px 16px; margin-bottom: 16px;">
        <div class="card-header" style="padding: 0 0 12px 0; margin-bottom: 0;">
          <h3 class="card-title" style="font-size: 15px; margin-bottom: 0;">添加会话</h3>
        </div>
        <div style="display: flex; gap: 12px; align-items: flex-end; margin-bottom: 8px;">
          <div class="form-group" style="width: 140px; margin: 0;">
            <label class="form-label" style="margin-bottom: 4px;">类型</label>
            <select v-model="addType" class="form-input" style="height: 36px; padding: 0 8px; font-size: 13px;">
              <option value="group">群聊</option>
              <option value="private">私聊</option>
            </select>
          </div>
          <div class="form-group" style="flex: 1; max-width: 300px; margin: 0;">
            <label class="form-label" style="margin-bottom: 4px;">{{ addType === 'private' ? 'QQ号' : '群号' }}</label>
            <input v-model.number="newGroupId" :placeholder="addType === 'private' ? '输入QQ号' : '输入群号'"
                   class="form-input" style="height: 36px; padding: 0 8px; font-size: 13px;" type="number">
          </div>
          <button :disabled="saving" class="btn btn-success" style="height: 36px; line-height: 36px; padding: 0 16px;"
                  @click="addGroup">
            {{ saving ? '添加中...' : '添加' }}
          </button>
        </div>
      </div>

      <!-- 会话列表 -->
      <div class="card">
        <div class="card-header">
          <div style="display: flex; gap: 12px; align-items: center;">
            <h3 class="card-title">会话列表</h3>
            <div :class="{ connected: wsConnected, disconnected: !wsConnected }" class="connection-status">
              <span class="dot"></span>
              {{ wsConnected ? '实时连接' : '未连接' }}
            </div>
          </div>
          <div style="display: flex; gap: 8px; align-items: center;">
            <div class="filter-tabs">
              <button :class="{ active: typeFilter === 'all' }" class="filter-tab"
                      @click="typeFilter = 'all'">全部
              </button>
              <button :class="{ active: typeFilter === 'group' }" class="filter-tab"
                      @click="typeFilter = 'group'">群聊
              </button>
              <button :class="{ active: typeFilter === 'private' }" class="filter-tab"
                      @click="typeFilter = 'private'">私聊
              </button>
            </div>
            <span style="color: var(--text-secondary); font-size: 13px;">共 {{ filteredGroups.length }} 个会话</span>
            <button class="btn btn-secondary btn-sm" @click="refreshAllGroupNames">刷新会话名称</button>
          </div>
        </div>
        <div class="table-container">
          <template v-if="loading">
            <div class="empty-state">
              <p>加载中...</p>
            </div>
          </template>
          <template v-else-if="filteredGroups.length === 0">
            <div class="empty-state">
              <div class="empty-icon">👥</div>
              <p>暂无会话，请添加或由用户在私聊中发送 /enable 启用</p>
            </div>
          </template>
          <template v-else>
            <table>
              <thead>
              <tr>
                <th style="width: 60px;">状态</th>
                <th>会话</th>
                <th style="width: 120px;">群号/QQ号</th>
                <th style="width: 70px;">消息</th>
                <th style="width: 300px;">操作</th>
              </tr>
              </thead>
              <tbody>
              <tr v-for="group in filteredGroups" :key="sessionKey(group)" :class="{ 'row-disabled': !group.enabled }"
                  class="group-row"
                  @click="selectGroup(sessionKey(group), sessionLabel(group))">
                <td>
                  <span
                      :class="group.enabled ? 'status-enabled' : 'status-disabled'"
                      :title="group.enabled ? '点击禁用' : '点击启用'"
                      class="status-badge"
                      @click.stop="toggleGroup(sessionKey(group))"
                  >
                    {{ group.enabled ? '启用' : '禁用' }}
                  </span>
                </td>
                <td>
                  <strong v-if="isPrivateSession(group)">{{ group.groupName || '私聊' }}</strong>
                  <strong v-else-if="group.groupName">{{ group.groupName }}</strong>
                  <span v-else style="color: var(--text-light)">群 {{ group.groupId }}</span>
                </td>
                <td><code :title="isPrivateSession(group) ? `QQ ${group.userId}` : ''">{{
                    isPrivateSession(group) ? group.userId : group.groupId
                  }}</code></td>
                <td>{{ group.messageCount || 0 }}</td>
                <td style="white-space: nowrap;">
                  <button class="btn btn-primary btn-sm"
                          @click.stop="selectGroup(sessionKey(group), sessionLabel(group))">记录
                  </button>
                  <button class="btn btn-secondary btn-sm" style="margin-left: 6px;"
                          @click.stop="viewMemory(sessionKey(group), sessionLabel(group))">记忆
                  </button>
                  <button class="btn btn-secondary btn-sm" style="margin-left: 6px;"
                          @click.stop="viewAffinity(sessionKey(group), sessionLabel(group))">好感度
                  </button>
                  <button class="btn btn-secondary btn-sm" style="margin-left: 6px;"
                          @click.stop="viewTasks(sessionKey(group), sessionLabel(group))">任务
                  </button>
                  <button class="btn btn-danger btn-sm" style="margin-left: 6px;"
                          @click.stop="removeGroup(sessionKey(group))">删除
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

    <!-- 好感度弹窗 -->
    <div v-if="affinityGroupId" class="modal-overlay" @click.self="closeAffinity">
      <div class="modal-content">
        <div class="modal-header">
          <h2>{{ affinityGroupName }} - 好感度</h2>
          <button class="btn btn-secondary btn-sm" @click="closeAffinity">关闭</button>
        </div>
        <div class="modal-body">
          <div v-if="affinityLoading" class="memory-loading">
            <p>加载中...</p>
          </div>
          <table v-else-if="affinityList.length > 0" class="affinity-table">
            <thead>
            <tr>
              <th style="width: 50px;"></th>
              <th>昵称</th>
              <th>QQ 号</th>
              <th style="width: 100px; text-align: right;">好感度</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="entry in affinityList" :key="entry.qq">
              <td>
                <img v-if="!affinityAvatarFailed.has(entry.qq)"
                     :alt="entry.qq"
                     :src="`https://q1.qlogo.cn/g?b=qq&nk=${entry.qq}&s=40`"
                     class="affinity-avatar"
                     @error="affinityAvatarFailed.add(entry.qq)">
                <div v-else class="affinity-avatar affinity-avatar-fallback">?</div>
              </td>
              <td>{{ entry.name || '未知' }}</td>
              <td><code>{{ entry.qq }}</code></td>
              <td style="text-align: right;">
                <span :class="affinityClass(entry.affinity)" class="affinity-badge">{{ entry.affinity }}</span>
              </td>
            </tr>
            </tbody>
          </table>
          <div v-else class="memory-loading">
            <p>暂无好感度数据，会随群聊互动自动累计</p>
          </div>
        </div>
      </div>
    </div>

    <!-- 定时任务弹窗 -->
    <div v-if="tasksGroupId" class="modal-overlay" @click.self="closeTasks">
      <div class="modal-content">
        <div class="modal-header">
          <h2>{{ tasksGroupName }} - 定时任务</h2>
          <button class="btn btn-secondary btn-sm" @click="closeTasks">关闭</button>
        </div>
        <div class="modal-body">
          <div v-if="tasksLoading" class="memory-loading">
            <p>加载中...</p>
          </div>
          <table v-else-if="tasksList.length > 0" class="affinity-table">
            <thead>
            <tr>
              <th style="width: 60px;">编号</th>
              <th style="width: 160px;">触发时间</th>
              <th>内容</th>
              <th style="width: 80px;"></th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="task in tasksList" :key="task.id">
              <td>#{{ task.id }}</td>
              <td>
                {{ fmtTaskTime(task.remindTime) }}
                <div class="task-relative">{{ fmtTaskRelative(task.remindTime) }}</div>
              </td>
              <td class="task-content">{{ task.content }}</td>
              <td style="text-align: right;">
                <button :disabled="cancellingTaskId === task.id" class="btn btn-danger btn-sm"
                        @click="cancelTask(task)">{{ cancellingTaskId === task.id ? '取消中' : '取消' }}
                </button>
              </td>
            </tr>
            </tbody>
          </table>
          <div v-else class="memory-loading">
            <p>暂无待触发的定时任务，可在群聊中让 AI 设置提醒</p>
          </div>
        </div>
      </div>
    </div>

    <!-- 记忆弹窗 -->
    <div v-if="memoryGroupId" class="modal-overlay" @click.self="closeMemory">
      <div class="modal-content">
        <div class="modal-header">
          <h2>{{ memoryGroupName }} - 会话记忆</h2>
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

.filter-tabs {
  display: flex;
  gap: 4px;
  background: var(--bg-secondary);
  border-radius: 8px;
  padding: 3px;
}

.filter-tab {
  border: none;
  background: none;
  padding: 4px 12px;
  border-radius: 6px;
  font-size: 13px;
  cursor: pointer;
  color: var(--text-secondary);
  transition: all 0.2s;
}

.filter-tab:hover {
  color: var(--text-primary);
}

.filter-tab.active {
  background: var(--card-bg);
  color: var(--primary);
  font-weight: 600;
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

/* 好感度 */
.affinity-table {
  width: 100%;
  border-collapse: collapse;
}

.affinity-table th,
.affinity-table td {
  padding: 8px 12px;
  border-bottom: 1px solid var(--border);
  text-align: left;
  font-size: 14px;
}

.affinity-table tbody tr:last-child td {
  border-bottom: none;
}

.affinity-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  display: block;
}

.affinity-avatar-fallback {
  background: var(--bg-secondary);
  color: var(--text-secondary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
}

.affinity-badge {
  display: inline-block;
  min-width: 44px;
  padding: 2px 10px;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 600;
  text-align: center;
}

.affinity-positive {
  background: var(--success-soft);
  color: var(--success);
}

.affinity-negative {
  background: var(--danger-soft);
  color: var(--danger);
}

.affinity-neutral {
  background: var(--bg-secondary);
  color: var(--text-secondary);
}

/* 定时任务 */
.task-relative {
  font-size: 12px;
  color: var(--primary);
}

.task-content {
  word-break: break-word;
}
</style>