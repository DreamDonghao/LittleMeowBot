/// <reference types="vite/client" />

export interface QQConfig {
    accessToken: string
    selfQQNumber: number
    qqHttpHost: string
    botName: string
}

export interface LLMConfig {
    name?: string
    apiKey: string
    baseUrl: string
    path: string
    model: string
    maxTokens: number
    temperature: number
    topP: number
    reasoningEffort: string
}

export interface KBConfig {
    enabled: boolean
    apiKey: string
    baseUrl: string
    knowledgeDatasetId: string
    memoryDatasetId: string
    memoryDocumentId: string
}

export interface MemoryConfig {
    windowTriggerCount: number
    windowKeepCount: number
    memoryExtractMaxTokens: number
    routerWindowTriggerCount: number
    routerWindowKeepCount: number
    shortTermMemoryMax: number
    memoryMigrateCount: number
}

export interface Emoji {
    name: string
    summary: string
    emoji_id: string
    emoji_package_id: string
    key: string
    url: string
    md5: string
    res_id: string
    is_mark_face: boolean
}

export interface Admin {
    qq: number
}

export interface Group {
    groupId: number
    /** 会话 ID 的字符串形式（私聊会话 ID 带标志位，超过 Number 安全范围） */
    groupIdStr?: string
    groupName: string
    messageCount: number
    enabled?: boolean
    sessionType?: 'private'
    userId?: number
}

export interface ChatMessage {
    id?: number
    role: 'user' | 'assistant'
    content: string
}

export interface ApiResponse {
    success: boolean
    error?: string
    data?: any
}

export interface LogEntry {
    id: number
    timestamp: string
    level: string
    message: string
    /** 会话 ID 的字符串形式（私聊会话 ID 带标志位） */
    groupId?: string | null
}

export interface LogQueryResult {
    entries: LogEntry[]
    hasMore: boolean
    nextAfterId: number
    nextBeforeId: number
    oldestId: number
    newestId: number
    size: number
    currentLevel: string
}