<script lang="ts" setup>
/**
 * @file Dashboard.vue
 * @brief 首页仪表盘 - 各模块基础信息概览
 */
import {inject, onMounted, ref, type Ref} from 'vue'
import type {LLMConfig, QQConfig} from '../vite-env.d'

const qqConfig = inject<QQConfig>('qqConfig')
const wsConnected = inject<Ref<boolean>>('wsConnected') as Ref<boolean>

// LLM 模型
const llmModels: Ref<Record<string, string>> = ref({}) // { router: 'deepseek-v3', ... }
const llmLoading: Ref<boolean> = ref(true)

// OneBot 连接
const botOnline: Ref<boolean> = ref(false)

// 群统计
const groupCount: Ref<number> = ref(0)
const enabledCount: Ref<number> = ref(0)
const totalMessages: Ref<number> = ref(0)

// 用量统计
const totalCalls: Ref<number> = ref(0)
const totalTokens: Ref<number> = ref(0)
const hitRate: Ref<number> = ref(0)

// 管理员/表情/工具
const adminCount: Ref<number> = ref(0)
const emojiCount: Ref<number> = ref(0)
const toolCount: Ref<number> = ref(0)

// 知识库
const kbEnabled: Ref<boolean> = ref(false)
const kbBaseUrl: Ref<string> = ref('')

// 记忆配置
const shortTermMax: Ref<number> = ref(0)
const windowTrigger: Ref<number> = ref(0)

const fmtNum = (n: number): string => n.toLocaleString()

const loadLLMModels = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/llm-configs')
    const data = await resp.json()
    llmModels.value = {}
    for (const [name, cfg] of Object.entries(data)) {
      const c = cfg as LLMConfig
      if (c.model) llmModels.value[name] = c.model
    }
  } catch { /* ignore */ }
  finally { llmLoading.value = false }
}

const loadQQStatus = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/qq-config')
    const data = await resp.json()
    if (data.qqHttpHost) {
      botOnline.value = true
    }
  } catch { /* ignore */ }
}

const loadGroups = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/groups')
    const data = await resp.json()
    if (Array.isArray(data)) {
      groupCount.value = data.length
      enabledCount.value = data.filter((g: any) => g.enabled).length
      totalMessages.value = data.reduce((sum: number, g: any) => sum + (g.messageCount || 0), 0)
    }
  } catch { /* ignore */ }
}

const loadUsage = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/usage?days=30')
    const data = await resp.json()
    totalCalls.value = data.total_calls || 0
    totalTokens.value = data.total_tokens || 0
    const cached = data.total_cached || 0
    const prompt = data.total_prompt || 0
    hitRate.value = prompt > 0 ? (cached / prompt) * 100 : 0
  } catch { /* ignore */ }
}

const loadAdmins = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/admins')
    const data = await resp.json()
    adminCount.value = Array.isArray(data) ? data.length : 0
  } catch { /* ignore */ }
}

const loadEmojis = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/emojis')
    const data = await resp.json()
    emojiCount.value = Array.isArray(data) ? data.length : 0
  } catch { /* ignore */ }
}

const loadTools = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/custom-tools')
    const data = await resp.json()
    toolCount.value = Array.isArray(data) ? data.length : 0
  } catch { /* ignore */ }
}

const loadKB = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/kb-config')
    const data = await resp.json()
    kbEnabled.value = data.enabled || false
    kbBaseUrl.value = data.baseUrl || ''
  } catch { /* ignore */ }
}

const loadMemoryConfig = async (): Promise<void> => {
  try {
    const resp = await fetch('/admin/api/memory-config')
    const data = await resp.json()
    shortTermMax.value = data.shortTermMemoryMax || 0
    windowTrigger.value = data.windowTriggerCount || 0
  } catch { /* ignore */ }
}

onMounted(() => {
  loadLLMModels()
  loadQQStatus()
  loadGroups()
  loadUsage()
  loadAdmins()
  loadEmojis()
  loadTools()
  loadKB()
  loadMemoryConfig()
})
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">首页</h1>
      <p class="page-subtitle">{{ qqConfig?.botName || 'LittleMeowBot' }} 运行概况</p>
    </div>

    <!-- 状态条 -->
    <div class="dash-status-bar">
      <div class="status-item">
        <span class="status-dot" :class="wsConnected ? 'dot-green' : 'dot-red'"></span>
        <span>{{ wsConnected ? 'WebSocket 已连接' : 'WebSocket 未连接' }}</span>
      </div>
      <div class="status-item">
        <span class="status-dot" :class="botOnline ? 'dot-green' : 'dot-gray'"></span>
        <span>{{ botOnline ? 'OneBot 已配置' : 'OneBot 未配置' }}</span>
      </div>
      <div class="status-item">
        <span class="status-dot" :class="kbEnabled ? 'dot-green' : 'dot-gray'"></span>
        <span>{{ kbEnabled ? '知识库已启用' : '知识库已禁用' }}</span>
      </div>
    </div>

    <!-- 卡片网格 -->
    <div class="dash-grid">
      <!-- LLM 模型 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/>
          </svg>
          <span>LLM 模型</span>
        </div>
        <div class="dash-card-body">
          <template v-if="llmLoading">
            <span class="dash-muted">加载中...</span>
          </template>
          <template v-else-if="Object.keys(llmModels).length === 0">
            <span class="dash-muted">未配置</span>
          </template>
          <template v-else>
            <div v-for="(model, name) in llmModels" :key="name" class="dash-row">
              <span class="dash-label">{{ name }}</span>
              <code class="dash-code">{{ model }}</code>
            </div>
          </template>
        </div>
      </div>

      <!-- 群统计 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M17 8h2a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2h-2v4l-4-4H9a2 2 0 0 1-2-2v-1"/>
            <path d="M15 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h2v4l4-4"/>
          </svg>
          <span>群聊</span>
        </div>
        <div class="dash-card-body">
          <div class="dash-kv">
            <span class="dash-label">总群数</span>
            <span class="dash-value">{{ groupCount }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">已启用</span>
            <span class="dash-value">{{ enabledCount }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">总消息</span>
            <span class="dash-value">{{ fmtNum(totalMessages) }}</span>
          </div>
        </div>
      </div>

      <!-- 用量统计 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8z"/>
            <path d="M12 6v6l4 2"/>
          </svg>
          <span>用量 (30天)</span>
        </div>
        <div class="dash-card-body">
          <div class="dash-kv">
            <span class="dash-label">调用次数</span>
            <span class="dash-value">{{ fmtNum(totalCalls) }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">总 Token</span>
            <span class="dash-value">{{ fmtNum(totalTokens) }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">缓存命中率</span>
            <span class="dash-value">{{ hitRate.toFixed(1) }}%</span>
          </div>
        </div>
      </div>

      <!-- 管理员 & 表情 & 工具 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/>
            <circle cx="9" cy="7" r="4"/>
          </svg>
          <span>资源</span>
        </div>
        <div class="dash-card-body">
          <div class="dash-kv">
            <span class="dash-label">管理员</span>
            <span class="dash-value">{{ adminCount }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">表情</span>
            <span class="dash-value">{{ emojiCount }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">自定义工具</span>
            <span class="dash-value">{{ toolCount }}</span>
          </div>
        </div>
      </div>

      <!-- 记忆配置 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8z"/>
            <path d="M12 6v6l4 2"/>
          </svg>
          <span>记忆配置</span>
        </div>
        <div class="dash-card-body">
          <div class="dash-kv">
            <span class="dash-label">窗口触发条数</span>
            <span class="dash-value">{{ windowTrigger }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">短期记忆上限</span>
            <span class="dash-value">{{ shortTermMax }}</span>
          </div>
        </div>
      </div>

      <!-- OneBot 配置 -->
      <div class="dash-card">
        <div class="dash-card-header">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" class="dash-card-icon">
            <path d="M8 12h.01M12 12h.01M16 12h.01M21 12c0 4.418-4.03 8-9 8a9.863 9.863 0 01-4.255-.949L3 20l1.395-3.72C3.512 15.042 3 13.574 3 12c0-4.418 4.03-8 9-8s9 3.582 9 8z"/>
          </svg>
          <span>OneBot</span>
        </div>
        <div class="dash-card-body">
          <div class="dash-kv">
            <span class="dash-label">Bot 名称</span>
            <span class="dash-value">{{ qqConfig?.botName || '—' }}</span>
          </div>
          <div class="dash-kv">
            <span class="dash-label">QQ 号</span>
            <code class="dash-code">{{ qqConfig?.selfQQNumber || '—' }}</code>
          </div>
          <div class="dash-kv">
            <span class="dash-label">HTTP 地址</span>
            <code class="dash-code dash-truncate">{{ qqConfig?.qqHttpHost || '—' }}</code>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.dash-status-bar {
  display: flex;
  gap: 24px;
  margin-bottom: 20px;
  padding: 12px 18px;
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  flex-wrap: wrap;
}

.status-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--text-secondary);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.dot-green {
  background: var(--success);
  box-shadow: 0 0 6px var(--success);
}

.dot-red {
  background: var(--danger);
  box-shadow: 0 0 6px var(--danger);
}

.dot-gray {
  background: var(--text-light);
}

.dash-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.dash-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 18px 20px;
  transition: border-color 0.15s ease, box-shadow 0.15s ease;
}

.dash-card:hover {
  border-color: var(--neon-cyan);
  box-shadow: 0 0 14px var(--neon-cyan-glow);
}

.dash-card-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;
  font-weight: 600;
  font-size: 14px;
  color: var(--text-primary);
}

.dash-card-icon {
  width: 18px;
  height: 18px;
  flex-shrink: 0;
  color: var(--primary);
  opacity: 0.8;
}

.dash-card-body {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.dash-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.dash-kv {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.dash-label {
  font-size: 13px;
  color: var(--text-secondary);
  text-transform: capitalize;
}

.dash-value {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
}

.dash-code {
  font-size: 12px;
  background: var(--code-bg);
  color: var(--code-text);
  padding: 1px 6px;
  border-radius: 4px;
  max-width: 160px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.dash-muted {
  font-size: 13px;
  color: var(--text-light);
}

.dash-truncate {
  max-width: 140px;
}
</style>