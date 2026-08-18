<script lang="ts" setup>
/**
 * @file UsageStats.vue
 * @brief Token 用量统计 - LLM 调用量与缓存命中率
 */
import {inject, onMounted, ref, type Ref} from 'vue'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')

interface ModelUsage {
  model: string
  calls: number
  prompt: number
  completion: number
  total: number
  cached: number
}

interface DayUsage {
  day: string
  calls: number
  total: number
}

interface RecentCall {
  time: string
  model: string
  prompt: number
  completion: number
  total: number
  cached: number
}

const loading: Ref<boolean> = ref(false)
const totalCalls: Ref<number> = ref(0)
const totalPrompt: Ref<number> = ref(0)
const totalCompletion: Ref<number> = ref(0)
const totalTokens: Ref<number> = ref(0)
const totalCached: Ref<number> = ref(0)
const hitRate: Ref<number> = ref(0)
const byModel: Ref<ModelUsage[]> = ref([])
const byDay: Ref<DayUsage[]> = ref([])
const recent: Ref<RecentCall[]> = ref([])

const fmtNum = (n: number): string => n.toLocaleString()

const loadUsage = async (): Promise<void> => {
  loading.value = true
  try {
    const resp = await fetch('/admin/api/usage?days=30')
    if (!resp.ok) {
      showToast!('加载失败: ' + resp.status, true)
      return
    }
    const data = await resp.json()
    totalCalls.value = data.total_calls || 0
    totalPrompt.value = data.total_prompt || 0
    totalCompletion.value = data.total_completion || 0
    totalTokens.value = data.total_tokens || 0
    totalCached.value = data.total_cached || 0
    hitRate.value = totalPrompt.value > 0
      ? (totalCached.value / totalPrompt.value) * 100
      : 0
    byModel.value = data.by_model || []
    byDay.value = data.by_day || []
    recent.value = data.recent || []
  } catch {
    showToast!('网络错误，请检查后端服务', true)
  } finally {
    loading.value = false
  }
}

onMounted(loadUsage)
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">用量统计</h1>
      <p class="page-subtitle">最近 30 天的 LLM Token 消耗与缓存命中率</p>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">总览</h3>
        <button :disabled="loading" class="btn btn-success" @click="loadUsage">
          {{ loading ? '加载中...' : '刷新' }}
        </button>
      </div>
      <div style="display:flex;gap:16px;flex-wrap:wrap;padding:16px">
        <div style="flex:1;min-width:140px;text-align:center;padding:16px;border:1px solid var(--border-color);border-radius:8px">
          <div style="font-size:13px;color:var(--text-muted)">调用次数</div>
          <div style="font-size:24px;font-weight:bold;margin-top:4px">{{ fmtNum(totalCalls) }}</div>
        </div>
        <div style="flex:1;min-width:140px;text-align:center;padding:16px;border:1px solid var(--border-color);border-radius:8px">
          <div style="font-size:13px;color:var(--text-muted)">总 Token</div>
          <div style="font-size:24px;font-weight:bold;margin-top:4px">{{ fmtNum(totalTokens) }}</div>
          <div style="font-size:12px;color:var(--text-muted);margin-top:4px">
            prompt {{ fmtNum(totalPrompt) }} + 输出 {{ fmtNum(totalCompletion) }}
          </div>
        </div>
        <div style="flex:1;min-width:140px;text-align:center;padding:16px;border:1px solid var(--border-color);border-radius:8px">
          <div style="font-size:13px;color:var(--text-muted)">缓存命中率</div>
          <div style="font-size:24px;font-weight:bold;margin-top:4px">{{ hitRate.toFixed(1) }}%</div>
          <div style="font-size:12px;color:var(--text-muted);margin-top:4px">缓存 {{ fmtNum(totalCached) }} tokens</div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">按模型统计</h3>
      </div>
      <div class="table-container">
        <template v-if="byModel.length === 0">
          <div class="empty-state"><p>暂无数据</p></div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>模型</th>
              <th>调用次数</th>
              <th>Prompt</th>
              <th>输出</th>
              <th>总 Token</th>
              <th>缓存</th>
              <th>命中率</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="m in byModel" :key="m.model">
              <td><code style="font-size:12px">{{ m.model }}</code></td>
              <td>{{ fmtNum(m.calls) }}</td>
              <td>{{ fmtNum(m.prompt) }}</td>
              <td>{{ fmtNum(m.completion) }}</td>
              <td>{{ fmtNum(m.total) }}</td>
              <td>{{ fmtNum(m.cached) }}</td>
              <td>{{ m.prompt > 0 ? ((m.cached / m.prompt) * 100).toFixed(1) : 'N/A' }}%</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">按天统计</h3>
      </div>
      <div class="table-container">
        <template v-if="byDay.length === 0">
          <div class="empty-state"><p>暂无数据</p></div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>日期</th>
              <th>调用次数</th>
              <th>Token</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="d in byDay" :key="d.day">
              <td>{{ d.day }}</td>
              <td>{{ fmtNum(d.calls) }}</td>
              <td>{{ fmtNum(d.total) }}</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">最近调用</h3>
      </div>
      <div class="table-container">
        <template v-if="recent.length === 0">
          <div class="empty-state"><p>暂无数据</p></div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>时间</th>
              <th>模型</th>
              <th>Prompt</th>
              <th>输出</th>
              <th>总 Token</th>
              <th>缓存</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="(r, i) in recent" :key="i">
              <td style="font-size:12px">{{ r.time }}</td>
              <td><code style="font-size:12px">{{ r.model }}</code></td>
              <td>{{ fmtNum(r.prompt) }}</td>
              <td>{{ fmtNum(r.completion) }}</td>
              <td>{{ fmtNum(r.total) }}</td>
              <td>{{ fmtNum(r.cached) }}</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>
  </div>
</template>