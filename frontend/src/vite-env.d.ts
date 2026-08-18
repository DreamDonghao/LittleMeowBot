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
    groupName: string
    messageCount: number
    enabled?: boolean
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