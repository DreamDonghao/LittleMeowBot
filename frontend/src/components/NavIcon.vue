<script lang="ts" setup>
/**
 * @file NavIcon.vue
 * @brief 导航图标组件 - 统一的 SVG 图标
 */
defineProps<{ name: string }>()
</script>

<template>
  <!-- LLM配置 -->
  <svg v-if="name === 'llm'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/>
  </svg>
  <!-- 提示词 -->
  <svg v-else-if="name === 'prompts'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
    <polyline points="14 2 14 8 20 8"/>
    <line x1="16" x2="8" y1="13" y2="13"/>
    <line x1="16" x2="8" y1="17" y2="17"/>
  </svg>
  <!-- 表情库 -->
  <svg v-else-if="name === 'emojis'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <circle cx="12" cy="12" r="10"/>
    <path d="M8 14s1.5 2 4 2 4-2 4-2"/>
    <circle cx="9" cy="9" r="1.5"/>
    <circle cx="15" cy="9" r="1.5"/>
  </svg>
  <!-- 管理员 -->
  <svg v-else-if="name === 'admins'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/>
    <circle cx="9" cy="7" r="4"/>
    <path d="M23 21v-2a4 4 0 0 0-3-3.87"/>
    <path d="M16 3.13a4 4 0 0 1 0 7.75"/>
  </svg>
  <!-- 启用群 -->
  <svg v-else-if="name === 'groups'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M17 8h2a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2h-2v4l-4-4H9a2 2 0 0 1-2-2v-1"/>
    <path d="M15 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h2v4l4-4"/>
  </svg>
  <!-- 聊天记录 -->
  <svg v-else-if="name === 'chat'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>
  </svg>
  <!-- 知识库 -->
  <svg v-else-if="name === 'kb'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <circle cx="12" cy="12" r="10"/>
    <line x1="2" x2="22" y1="12" y2="12"/>
    <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
  </svg>
  <!-- 记忆配置 -->
  <svg v-else-if="name === 'memory'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8z"/>
    <path d="M12 6v6l4 2"/>
  </svg>
  <!-- OneBot -->
  <svg v-else-if="name === 'qq'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path
        d="M8 12h.01M12 12h.01M16 12h.01M21 12c0 4.418-4.03 8-9 8a9.863 9.863 0 01-4.255-.949L3 20l1.395-3.72C3.512 15.042 3 13.574 3 12c0-4.418 4.03-8 9-8s9 3.582 9 8z"/>
  </svg>
  <!-- 自定义工具 -->
  <svg v-else-if="name === 'tool'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
    <path
        d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2 2 0 0 1-2.83 0l-1.41-1.41a2 2 0 0 1 0-2.83l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/>
  </svg>
</template>