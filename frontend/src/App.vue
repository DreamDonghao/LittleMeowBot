<script lang="ts" setup>
/**
 * @file App.vue
 * @brief 主应用组件 - 管理后台入口
 */
import {onMounted, provide, reactive, ref, type Ref} from 'vue'
import {useToast} from './composables/useToast'
import type {QQConfig as QQConfigType} from './vite-env.d'

import NavIcon from './components/NavIcon.vue'
import LLMConfig from './components/LLMConfig.vue'
import PromptEditor from './components/PromptEditor.vue'
import EmojiManager from './components/EmojiManager.vue'
import AdminManager from './components/AdminManager.vue'
import GroupManager from './components/GroupManager.vue'
import ChatRecords from './components/ChatRecords.vue'
import KBConfig from './components/KBConfig.vue'
import MemoryConfig from './components/MemoryConfig.vue'
import QQConfigVue from './components/QQConfig.vue'
import CustomTools from './components/CustomTools.vue'
import UsageStats from './components/UsageStats.vue'

interface NavItem {
  key: string
  label: string
  icon: string
}

// 导航配置
const navItems: NavItem[] = [
  {key: 'llm', label: 'LLM配置', icon: 'llm'},
  {key: 'prompts', label: '提示词', icon: 'prompts'},
  {key: 'customTools', label: '自定义工具', icon: 'tool'},
  {key: 'emojis', label: '表情库', icon: 'emojis'},
  {key: 'admins', label: '管理员', icon: 'admins'},
  {key: 'groups', label: '群管理', icon: 'groups'},
  {key: 'chatRecords', label: '聊天记录', icon: 'chat'},
  {key: 'kb', label: '知识库配置', icon: 'kb'},
  {key: 'memoryConfig', label: '记忆与上下文', icon: 'memory'},
  {key: 'qqConfig', label: 'OneBot 配置', icon: 'qq'},
  {key: 'usage', label: '用量统计', icon: 'memory'}
]

const currentView: Ref<string> = ref('llm')

// 全局 Toast
const {toast, toastError, showToast} = useToast()

// OneBot 配置
const qqConfig = reactive<QQConfigType>({
  accessToken: '',
  selfQQNumber: 0,
  qqHttpHost: '',
  botName: '小喵'
})

// WebSocket
const wsConnected: Ref<boolean> = ref(false)
let ws: WebSocket | null = null

const loadQQConfig = async (): Promise<void> => {
  const resp = await fetch('/admin/api/qq-config')
  const data = await resp.json()
  if (data.accessToken !== undefined) {
    Object.assign(qqConfig, data)
  }
}

const connectWebSocket = (): void => {
  const wsUrl = `ws://${location.host}/admin/ws`
  ws = new WebSocket(wsUrl)
  ws.onopen = () => wsConnected.value = true
  ws.onclose = () => {
    wsConnected.value = false
    setTimeout(connectWebSocket, 3000)
  }
}

// 提供给子组件
provide('showToast', showToast)
provide('qqConfig', qqConfig)
provide('wsConnected', wsConnected)
provide('ws', {get: () => ws})

onMounted(async () => {
  await loadQQConfig()
  connectWebSocket()
})
</script>

<template>
  <div class="container">
    <!-- 侧边栏 -->
    <div class="sidebar">
      <div class="sidebar-header">
        <div class="sidebar-logo">
          <div class="logo-icon">🐱</div>
          <div>
            <div class="sidebar-title">LittleMeowBot</div>
            <div class="sidebar-subtitle">管理后台</div>
          </div>
        </div>
      </div>

      <nav class="sidebar-nav">
        <div class="nav-section">
          <div class="nav-section-title">配置管理</div>
          <div
              v-for="item in navItems"
              :key="item.key"
              :class="{ active: currentView === item.key }"
              class="nav-item"
              @click="currentView = item.key"
          >
            <NavIcon :name="item.icon"/>
            {{ item.label }}
          </div>
        </div>
      </nav>
    </div>

    <!-- 主内容区 -->
    <div class="main">
      <LLMConfig v-if="currentView === 'llm'"/>
      <PromptEditor v-else-if="currentView === 'prompts'"/>
      <CustomTools v-else-if="currentView === 'customTools'"/>
      <EmojiManager v-else-if="currentView === 'emojis'"/>
      <AdminManager v-else-if="currentView === 'admins'"/>
      <GroupManager v-else-if="currentView === 'groups'"/>
      <ChatRecords v-else-if="currentView === 'chatRecords'"/>
      <KBConfig v-else-if="currentView === 'kb'"/>
      <MemoryConfig v-else-if="currentView === 'memoryConfig'"/>
      <QQConfigVue v-else-if="currentView === 'qqConfig'"/>
      <UsageStats v-else-if="currentView === 'usage'"/>
    </div>
  </div>

  <!-- Toast 提示 -->
  <div v-if="toast" :class="{ error: toastError }" class="toast">{{ toast }}</div>
</template>