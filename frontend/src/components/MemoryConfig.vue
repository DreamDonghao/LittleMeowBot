<script lang="ts" setup>
/**
 * @file MemoryConfig.vue
 * @brief 记忆配置组件
 */
import {inject, onMounted, reactive, ref, type Ref} from 'vue'
import type {ApiResponse, MemoryConfig} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')

const memoryConfig = reactive<MemoryConfig>({
  windowTriggerCount: 100,
  windowKeepCount: 50,
  memoryExtractMaxTokens: 4000,
  routerWindowTriggerCount: 20,
  routerWindowKeepCount: 10,
  shortTermMemoryMax: 15,
  memoryMigrateCount: 5
})
const saving: Ref<boolean> = ref(false)

onMounted(async () => {
  const resp = await fetch('/admin/api/memory-config')
  const data = await resp.json()
  if (data.windowTriggerCount !== undefined) {
    Object.assign(memoryConfig, data)
  }
})

const saveMemoryConfig = async (): Promise<void> => {
  saving.value = true
  try {
    const resp = await fetch('/admin/api/memory-config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(memoryConfig)
    })
    const data: ApiResponse = await resp.json()
    if (data.success) {
      showToast!('记忆配置已保存')
    } else {
      showToast!(data.error || '保存失败', true)
    }
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">记忆与上下文</h1>
      <p class="page-subtitle">配置记忆系统与回复上下文参数</p>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">参数设置</h3>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">窗口触发条数</label>
          <input v-model="memoryConfig.windowTriggerCount" class="form-input" type="number">
          <p class="form-hint">上下文窗口超过 N 条时触发记忆提取与窗口滑动</p>
        </div>
        <div class="form-group">
          <label class="form-label">窗口保留条数</label>
          <input v-model="memoryConfig.windowKeepCount" class="form-input" type="number">
          <p class="form-hint">滑动后保留的最近消息数，必须小于触发条数</p>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">记忆提取 maxTokens</label>
          <input v-model="memoryConfig.memoryExtractMaxTokens" class="form-input" type="number">
          <p class="form-hint">记忆提取 LLM 调用的输出 token 上限</p>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">Router 窗口触发条数</label>
          <input v-model="memoryConfig.routerWindowTriggerCount" class="form-input" type="number">
          <p class="form-hint">Router 上下文超过 N 条时批量滑动（建议 40~60，前缀需超过缓存阈值）</p>
        </div>
        <div class="form-group">
          <label class="form-label">Router 窗口保留条数</label>
          <input v-model="memoryConfig.routerWindowKeepCount" class="form-input" type="number">
          <p class="form-hint">滑动后保留的条数，必须小于触发条数</p>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">短期记忆上限</label>
          <input v-model="memoryConfig.shortTermMemoryMax" class="form-input" type="number">
          <p class="form-hint">超过此数量触发迁移，合并时也以此为上限</p>
        </div>
        <div class="form-group">
          <label class="form-label">每次迁移条数</label>
          <input v-model="memoryConfig.memoryMigrateCount" class="form-input" type="number">
          <p class="form-hint">每次迁移到长期记忆的条数</p>
        </div>
      </div>
      <button :disabled="saving" class="btn btn-primary" @click="saveMemoryConfig">
        {{ saving ? '保存中...' : '保存配置' }}
      </button>
    </div>
  </div>
</template>