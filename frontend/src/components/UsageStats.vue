<script lang="ts" setup>
/**
 * @file UsageStats.vue
 * @brief 用量统计 - 今日优先，按模型/按天分析，含趋势对比
 */
import {computed, inject, onMounted, ref, type Ref} from 'vue'

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

// 30 天汇总
const totalCalls: Ref<number> = ref(0)
const totalPrompt: Ref<number> = ref(0)
const totalCompletion: Ref<number> = ref(0)
const totalTokens: Ref<number> = ref(0)
const totalCached: Ref<number> = ref(0)

const byModel: Ref<ModelUsage[]> = ref([])
const byDay: Ref<DayUsage[]> = ref([])
const recent: Ref<RecentCall[]> = ref([])

// 今天
const today = new Date().toISOString().slice(0, 10)
const todayData = computed<DayUsage | null>(() => {
  return byDay.value.find(d => d.day === today) || null
})

// 昨天（用于对比）
const yesterday = computed<string>(() => {
  const d = new Date(Date.now() - 86400000)
  return d.toISOString().slice(0, 10)
})
const yesterdayData = computed<DayUsage | null>(() => {
  return byDay.value.find(d => d.day === yesterday.value) || null
})

// 趋势指示
const trend = computed<'up' | 'down' | 'flat' | null>(() => {
  if (!todayData.value || !yesterdayData.value) return null
  if (todayData.value.total > yesterdayData.value.total) return 'up'
  if (todayData.value.total < yesterdayData.value.total) return 'down'
  return 'flat'
})

// 今日调用明细（排除 image，image 模型无缓存）
const todayCalls = computed<RecentCall[]>(() => {
  return recent.value.filter(r => r.time.startsWith(today))
})

// 今日非 image 调用（用于缓存命中率计算）
const todayNonImageCalls = computed<RecentCall[]>(() => {
  return todayCalls.value.filter(r => r.model !== 'image')
})

// 今日缓存命中率（排除 image）
const todayPrompt = computed<number>(() => {
  return todayNonImageCalls.value.reduce((s, r) => s + r.prompt, 0)
})
const todayCached = computed<number>(() => {
  return todayNonImageCalls.value.reduce((s, r) => s + r.cached, 0)
})
const todayHitRate = computed<number>(() => {
  return todayPrompt.value > 0 ? (todayCached.value / todayPrompt.value) * 100 : 0
})

// 30天缓存命中率（排除 image 模型）
const nonImageModels = computed<ModelUsage[]>(() => {
  return byModel.value.filter(m => m.model !== 'image')
})
const nonImagePrompt = computed<number>(() => {
  return nonImageModels.value.reduce((s, m) => s + m.prompt, 0)
})
const nonImageCached = computed<number>(() => {
  return nonImageModels.value.reduce((s, m) => s + m.cached, 0)
})
const hitRate = computed<number>(() => {
  return nonImagePrompt.value > 0 ? (nonImageCached.value / nonImagePrompt.value) * 100 : 0
})

// 30 天平均
const dailyAvgCalls = computed<number>(() => {
  if (byDay.value.length === 0) return 0
  return Math.round(totalCalls.value / byDay.value.length)
})
const dailyAvgTokens = computed<number>(() => {
  if (byDay.value.length === 0) return 0
  return Math.round(totalTokens.value / byDay.value.length)
})

// 所有模型的总 token 用于占比
const modelTotal = computed<number>(() => {
  return byModel.value.reduce((s, m) => s + m.total, 0)
})

const fmtNum = (n: number): string => n.toLocaleString()
const fmtPct = (n: number): string => n.toFixed(1) + '%'

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
      <p class="page-subtitle">LLM 调用量与缓存命中情况</p>
    </div>

    <!-- 今日 & 30天概览 -->
    <div class="overview-row">
      <!-- 今日 -->
      <div class="card today-card">
        <div class="card-header">
          <h3 class="card-title">今日</h3>
          <span class="today-date">{{ today }}</span>
        </div>
        <template v-if="todayData">
          <div class="stat-grid">
            <div class="stat-card">
              <div class="stat-label">调用次数</div>
              <div class="stat-value">{{ fmtNum(todayData.calls) }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Token 消耗</div>
              <div class="stat-value">{{ fmtNum(todayData.total) }}</div>
              <div v-if="trend" class="stat-sub trend" :class="'trend-' + trend">
                {{ trend === 'up' ? '↑' : trend === 'down' ? '↓' : '→' }} 较昨日{{ trend === 'up' ? '增长' : trend === 'down' ? '下降' : '持平' }}
              </div>
            </div>
            <div class="stat-card">
              <div class="stat-label">缓存命中率</div>
              <div class="stat-value">{{ fmtPct(todayHitRate) }}</div>
              <div class="stat-sub">缓存 {{ fmtNum(todayCached) }} tokens</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">平均/次</div>
              <div class="stat-value">{{ fmtNum(todayData.calls > 0 ? Math.round(todayData.total / todayData.calls) : 0) }}</div>
            </div>
          </div>
        </template>
        <template v-else>
          <div class="empty-state"><p>今日暂无调用</p></div>
        </template>
      </div>

      <!-- 30天汇总 -->
      <div class="card">
        <div class="card-header">
          <h3 class="card-title">近 30 天</h3>
          <button :disabled="loading" class="btn btn-success btn-sm" @click="loadUsage">
            {{ loading ? '加载中...' : '刷新' }}
          </button>
        </div>
        <div class="stat-grid">
          <div class="stat-card">
            <div class="stat-label">总调用</div>
            <div class="stat-value">{{ fmtNum(totalCalls) }}</div>
            <div class="stat-sub">日均 {{ fmtNum(dailyAvgCalls) }} 次</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">总 Token</div>
            <div class="stat-value">{{ fmtNum(totalTokens) }}</div>
            <div class="stat-sub">日均 {{ fmtNum(dailyAvgTokens) }} · 输入 {{ fmtNum(totalPrompt) }} / 输出 {{ fmtNum(totalCompletion) }}</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">缓存命中率</div>
            <div class="stat-value">{{ fmtPct(hitRate) }}</div>
            <div class="stat-sub">缓存 {{ fmtNum(nonImageCached) }} tokens（不含 image）</div>
          </div>
        </div>
      </div>
    </div>

    <!-- 今日调用明细 -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">今日调用明细</h3>
        <span style="font-size:13px;color:var(--text-secondary)">{{ todayCalls.length }} 次</span>
      </div>
      <div class="table-container">
        <template v-if="todayCalls.length === 0">
          <div class="empty-state"><p>今日暂无调用</p></div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>时间</th>
              <th>模型</th>
              <th>输入</th>
              <th>输出</th>
              <th>总 Token</th>
              <th>缓存命中率</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="(r, i) in todayCalls" :key="i">
              <td style="font-size:12px">{{ r.time.slice(11, 19) }}</td>
              <td><code style="font-size:12px">{{ r.model }}</code></td>
              <td>{{ fmtNum(r.prompt) }}</td>
              <td>{{ fmtNum(r.completion) }}</td>
              <td>{{ fmtNum(r.total) }}</td>
              <td>{{ r.model === 'image' ? 'N/A' : (r.prompt > 0 ? fmtPct((r.cached / r.prompt) * 100) : 'N/A') }}</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <!-- 按模型统计 -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">按模型统计（近30天）</h3>
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
              <th>调用</th>
              <th>总 Token</th>
              <th>占比</th>
              <th>平均/次</th>
              <th>缓存命中率</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="m in byModel" :key="m.model">
              <td><code style="font-size:12px">{{ m.model }}</code></td>
              <td>{{ fmtNum(m.calls) }}</td>
              <td>{{ fmtNum(m.total) }}</td>
              <td>{{ fmtPct(modelTotal > 0 ? (m.total / modelTotal) * 100 : 0) }}</td>
              <td>{{ fmtNum(m.calls > 0 ? Math.round(m.total / m.calls) : 0) }}</td>
              <td>{{ m.model === 'image' ? 'N/A' : (m.prompt > 0 ? fmtPct((m.cached / m.prompt) * 100) : 'N/A') }}</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <!-- 按天趋势 -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">按天趋势</h3>
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
              <th>调用</th>
              <th>Token</th>
              <th>平均/次</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="d in byDay" :key="d.day" :class="{ 'today-row': d.day === today }">
              <td>
                {{ d.day }}
                <span v-if="d.day === today" class="today-tag">今天</span>
              </td>
              <td>{{ fmtNum(d.calls) }}</td>
              <td>{{ fmtNum(d.total) }}</td>
              <td>{{ fmtNum(d.calls > 0 ? Math.round(d.total / d.calls) : 0) }}</td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.overview-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  margin-bottom: 20px;
}

@media (max-width: 800px) {
  .overview-row {
    grid-template-columns: 1fr;
  }
}

.today-card {
  border-color: var(--neon-cyan);
}

.today-date {
  font-size: 12px;
  color: var(--text-light);
  font-weight: 400;
}

.trend {
  font-weight: 600;
}

.trend-up {
  color: var(--success);
}

.trend-down {
  color: var(--danger);
}

.trend-flat {
  color: var(--text-light);
}

.today-row {
  background: var(--primary-softer);
}

.today-tag {
  display: inline-block;
  font-size: 10px;
  font-weight: 600;
  color: var(--primary);
  background: var(--primary-soft);
  padding: 1px 6px;
  border-radius: 4px;
  margin-left: 6px;
  vertical-align: middle;
}
</style>