<script lang="ts" setup>
/**
 * @file AdminManager.vue
 * @brief 管理员管理组件
 */
import {inject, onMounted, ref, type Ref} from 'vue'
import type {Admin, ApiResponse} from '../vite-env.d'

const showToast = inject<(msg: string, isError?: boolean) => void>('showToast')

const admins: Ref<Admin[]> = ref([])
const newAdminQQ: Ref<number | undefined> = ref(undefined)
const loading: Ref<boolean> = ref(false)
const saving: Ref<boolean> = ref(false)

const loadAdmins = async (): Promise<void> => {
  loading.value = true
  try {
    const resp = await fetch('/admin/api/admins')
    if (!resp.ok) {
      showToast!('加载失败: ' + resp.status, true)
      admins.value = []
      return
    }
    const data = await resp.json()
    if (Array.isArray(data)) {
      admins.value = data
    } else {
      admins.value = []
    }
  } catch {
    showToast!('网络错误，请检查后端服务', true)
    admins.value = []
  } finally {
    loading.value = false
  }
}

const addAdmin = async (): Promise<void> => {
  if (!newAdminQQ.value) {
    showToast!('请输入QQ号', true)
    return
  }
  saving.value = true
  try {
    const resp = await fetch('/admin/api/admin', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({qq: newAdminQQ.value})
    })
    const data: ApiResponse = await resp.json()
    if (data.success) {
      showToast!('管理员已添加')
      newAdminQQ.value = undefined
      await loadAdmins()
    } else {
      showToast!(data.error || '添加失败', true)
    }
  } finally {
    saving.value = false
  }
}

const removeAdmin = async (qq: number): Promise<void> => {
  const resp = await fetch(`/admin/api/admin/${qq}`, {method: 'DELETE'})
  const data: ApiResponse = await resp.json()
  if (data.success) {
    showToast!('管理员已删除')
    await loadAdmins()
  }
}

onMounted(loadAdmins)
</script>

<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">管理员管理</h1>
      <p class="page-subtitle">添加或移除Bot管理员</p>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">添加管理员</h3>
      </div>
      <div class="form-group">
        <label class="form-label">QQ号</label>
        <input v-model.number="newAdminQQ" class="form-input" placeholder="输入QQ号" style="max-width:300px"
               type="number">
      </div>
      <button :disabled="saving" class="btn btn-success" @click="addAdmin">
        {{ saving ? '添加中...' : '添加管理员' }}
      </button>
    </div>

    <div class="card">
      <div class="card-header">
        <h3 class="card-title">管理员列表</h3>
      </div>
      <div class="table-container">
        <template v-if="loading">
          <div class="empty-state">
            <p>加载中...</p>
          </div>
        </template>
        <template v-else-if="admins.length === 0">
          <div class="empty-state">
            <div class="empty-icon">👤</div>
            <p>暂无管理员</p>
          </div>
        </template>
        <template v-else>
          <table>
            <thead>
            <tr>
              <th>QQ号</th>
              <th style="width:100px">操作</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="admin in admins" :key="admin.qq">
              <td><code>{{ admin.qq }}</code></td>
              <td>
                <button class="btn btn-danger btn-sm" @click="removeAdmin(admin.qq)">移除</button>
              </td>
            </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>
  </div>
</template>