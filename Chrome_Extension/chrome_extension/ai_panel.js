function t(key, substitutions) {
  const value = chrome.i18n.getMessage(key, substitutions);
  return value || key;
}

function applyI18n() {
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const key = el.getAttribute("data-i18n");
    if (!key) return;
    const value = t(key);
    if (el.tagName === "TITLE") {
      document.title = value;
    } else {
      el.textContent = value;
    }
  });
  document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
    const key = el.getAttribute("data-i18n-placeholder");
    if (!key) return;
    el.setAttribute("placeholder", t(key));
  });
}

const els = {
  provider: document.getElementById("provider"),
  apiType: document.getElementById("api_type"),
  baseUrl: document.getElementById("base_url"),
  model: document.getElementById("model"),
  modelSelect: document.getElementById("model_select"),
  imageModel: document.getElementById("image_model"),
  imageApiType: document.getElementById("image_api_type"),
  providerCapabilities: document.getElementById("provider-capabilities"),
  apiKey: document.getElementById("api_key"),
  temperature: document.getElementById("temperature"),
  history: document.getElementById("history"),
  robotName: document.getElementById("robot_name"),
  userName: document.getElementById("user_name"),
  template: document.getElementById("template"),
  serverStatus: document.getElementById("server-status"),
  stateProvider: document.getElementById("state-provider"),
  stateModel: document.getElementById("state-model"),
  stateApi: document.getElementById("state-api"),
  sceneMeta: document.getElementById("scene-meta"),
  fontChip: document.getElementById("chat-font-chip"),
  editChip: document.getElementById("edit-mode-chip"),
  portChip: document.getElementById("port-chip"),
  messages: document.getElementById("messages"),
  prompt: document.getElementById("prompt"),
  fileInput: document.getElementById("file-input"),
  imageStatus: document.getElementById("image-status"),
  actionStatus: document.getElementById("action-status"),
  sidebarActionStatus: document.getElementById("sidebar-action-status"),
  sendBadge: document.getElementById("send-badge"),
  syncTime: document.getElementById("sync-time"),
  diagBox: document.getElementById("diag-box"),
  actionMenu: document.getElementById("action-menu"),
  uploadProgress: document.getElementById("upload-progress"),
  uploadProgressBar: document.getElementById("upload-progress-bar"),
  imagePreview: document.getElementById("image-preview"),
  imagePreviewImg: document.getElementById("image-preview-img"),
  imageEditorModal: document.getElementById("image-editor-modal"),
  imageEditorFile: document.getElementById("image-editor-file"),
  imageEditorPrompt: document.getElementById("image-editor-prompt"),
  imageEditorCurrent: document.getElementById("image-editor-current"),
  imageEditorStatus: document.getElementById("image-editor-status"),
  providerList: document.getElementById("provider-list"),
  standaloneConfigSection: document.getElementById("standalone-config-section"),
  toggleStandaloneConfig: document.getElementById("btn-toggle-standalone-config"),
  customProviderName: document.getElementById("custom-provider-name"),
  customProviderSiteUrl: document.getElementById("custom-provider-site-url"),
  customProviderBaseUrl: document.getElementById("custom-provider-base-url"),
  customProviderModel: document.getElementById("custom-provider-model"),
  customProviderImageModel: document.getElementById("custom-provider-image-model"),
  customProviderImageEdit: document.getElementById("custom-provider-image-edit"),
  providerAddModal: document.getElementById("provider-add-modal"),
  customProviderNameModal: document.getElementById("custom-provider-name-modal"),
  customProviderSiteUrlModal: document.getElementById("custom-provider-site-url-modal"),
  customProviderBaseUrlModal: document.getElementById("custom-provider-base-url-modal"),
  customProviderModelModal: document.getElementById("custom-provider-model-modal"),
  customProviderImageModelModal: document.getElementById("custom-provider-image-model-modal"),
  customProviderImageEditModal: document.getElementById("custom-provider-image-edit-modal"),
  modeMax: document.getElementById("mode-max"),
  modeStandalone: document.getElementById("mode-standalone"),
  searchApiUrl: document.getElementById("search_api_url"),
  searchApiKey: document.getElementById("search_api_key"),
  btnWebSearch: document.getElementById("btn-web-search")
};

let state = null;
let appMode = "standalone";
let standaloneMessages = [];
let currentPort = 19527;
let sendPollTimer = null;
let autoRefreshTimer = null;
let searchEnabled = false;
let lastSyncAt = null;
let lastStandaloneProvider = "";
let diagVisible = false;
let lastMessageSignature = "";
let lastFormSignature = "";
let isEditingConfig = false;
let lastBusyState = null;
let standaloneDiagnosis = "";
let standaloneModelOptions = [];
let standaloneProviderConfigs = {};
let customStandaloneProviders = [];
let standaloneDefaultProvider = "";
let switchingStandaloneProvider = false;
let maxPendingMessage = null;
const GREETINGS = [
  "今天想整理哪一部分场景？",
  "需要我帮你排查什么问题？",
  "今天要优化哪个模型或材质？",
  "想从哪里开始？"
];
const AI_API_TYPES = ["OpenAI兼容", "Ollama /api/chat"];
const WELCOME_LINES = [
  "今天想从哪里开始？",
  "需要我帮你处理什么？",
  "把问题发给我，我们一起拆开看。",
  "有想法就先丢过来。",
  "今天要解决哪件事？",
  "要写点什么，还是改点什么？",
  "我在，直接说需求。",
  "先说目标，我帮你整理路径。",
  "复杂问题也可以一句话开始。",
  "想查资料、写方案，还是改图片？",
  "把卡住的地方发给我。",
  "今天要让哪个想法落地？",
  "先不用组织语言，直接发。",
  "要不要先从一个小问题开始？",
  "我可以帮你梳理、改写、分析或生成。",
  "需要灵感，还是需要答案？",
  "把素材发来，我帮你处理。",
  "有什么需要马上完成？",
  "从一个问题开始就行。",
  "今天想做点什么有意思的？",
  "要我帮你把思路理顺吗？",
  "发一句话，我来接住。",
  "想聊创意、方案，还是技术问题？",
  "需要更清楚的表达吗？",
  "把你的初稿发来，我帮你改。",
  "今天要优化哪里？",
  "想让我扮演什么角色？",
  "先告诉我结果要长什么样。",
  "要快一点，还是细一点？",
  "我可以先给你一个版本。",
  "有什么画面想改？",
  "想生成、重写，还是继续完善？",
  "把需求说粗一点也没关系。",
  "我们先把问题变简单。",
  "需要我给你几个方向吗？",
  "今天的第一个任务是什么？",
  "发图、发文字都可以。",
  "想让我帮你找第三方工具吗？",
  "要不要先做一个可用版本？",
  "我准备好了，你说。"
];
const STANDALONE_PROVIDERS = [
  { name: "ChatGPT 网页", api_type: "OpenAI兼容", base_url: "https://api.openai.com/v1", model: "gpt-4o-mini", imageModel: "gpt-image-1", imageApiType: "openai", siteUrl: "https://chatgpt.com/", vision: true, imageEdit: true, note: "网页端适合直接对话、上传图片、使用官方图片编辑。" },
  { name: "Gemini 网页", api_type: "OpenAI兼容", base_url: "https://generativelanguage.googleapis.com/v1beta/openai", model: "gemini-1.5-flash", siteUrl: "https://gemini.google.com/", vision: true, imageEdit: "取决于网页功能", note: "网页端适合看图和长上下文，图片编辑能力以 Google 当前网页功能为准。" },
  { name: "Claude 网页", api_type: "OpenAI兼容", base_url: "https://api.anthropic.com/v1", model: "claude-3-5-sonnet-latest", siteUrl: "https://claude.ai/", vision: true, imageEdit: false, note: "网页端适合分析图片、写作和代码；通常不是图片编辑工具。" },
  { name: "Krea AI 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://www.krea.ai/", vision: false, imageEdit: true, note: "第三方网页，偏图像生成/编辑/增强，适合复杂改图工作流。" },
  { name: "Runway 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://runwayml.com/", vision: false, imageEdit: true, note: "第三方网页，偏图片/视频生成与编辑。" },
  { name: "Adobe Firefly 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://firefly.adobe.com/", vision: false, imageEdit: true, note: "第三方网页，适合生成填充、风格化和商业设计工作流。" },
  { name: "豆包 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://www.doubao.com/", vision: true, imageEdit: true, note: "字节系 AI 网页，适合中文对话、写作、看图和图片创作。" },
  { name: "Kimi 网页", api_type: "OpenAI兼容", base_url: "https://api.moonshot.cn/v1", model: "moonshot-v1-8k", imageModel: "", imageApiType: "auto", siteUrl: "https://kimi.moonshot.cn/", vision: true, imageEdit: false, note: "长文档、搜索、总结能力强；通常不是专业改图工具。" },
  { name: "通义千问 网页", api_type: "OpenAI兼容", base_url: "https://dashscope.aliyuncs.com/compatible-mode/v1", model: "qwen-plus", imageModel: "wanx2.1-t2i-turbo", imageApiType: "auto", siteUrl: "https://tongyi.aliyun.com/qianwen/", vision: true, imageEdit: "取决于网页功能", note: "阿里通义网页，适合中文对话、文档、看图；图片能力以网页当前功能为准。" },
  { name: "通义万相 网页", api_type: "OpenAI兼容", base_url: "https://dashscope.aliyuncs.com/compatible-mode/v1", model: "qwen-plus", imageModel: "wanx2.1-t2i-turbo", imageApiType: "auto", siteUrl: "https://tongyi.aliyun.com/wanxiang/", vision: false, imageEdit: true, note: "阿里图片/视频创作网页，适合生图、风格化和设计类工作。" },
  { name: "文心一言 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://yiyan.baidu.com/", vision: true, imageEdit: "取决于网页功能", note: "百度 AI 网页，适合中文问答、写作、搜索和多模态使用。" },
  { name: "智谱清言 网页", api_type: "OpenAI兼容", base_url: "https://open.bigmodel.cn/api/paas/v4", model: "glm-4-flash", imageModel: "", imageApiType: "auto", siteUrl: "https://chatglm.cn/", vision: true, imageEdit: "取决于网页功能", note: "智谱 AI 网页，适合中文对话、智能体、看图和办公场景。" },
  { name: "腾讯元宝 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://yuanbao.tencent.com/", vision: true, imageEdit: "取决于网页功能", note: "腾讯 AI 网页，适合中文搜索、对话、文档和看图。" },
  { name: "DeepSeek 网页", api_type: "OpenAI兼容", base_url: "https://api.deepseek.com/v1", model: "deepseek-chat", imageModel: "", imageApiType: "auto", siteUrl: "https://chat.deepseek.com/", vision: false, imageEdit: false, note: "推理、代码、写作强；不是图片编辑平台。" },
  { name: "即梦 AI 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://jimeng.jianying.com/", vision: false, imageEdit: true, note: "字节系图片/视频创作网页，适合生图、改图、视频生成类任务。" },
  { name: "可灵 AI 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://klingai.kuaishou.com/", vision: false, imageEdit: true, note: "快手图片/视频生成网页，适合图生视频、文生视频和创意视觉。" },
  { name: "LiblibAI 网页", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "", imageApiType: "auto", siteUrl: "https://www.liblib.art/", vision: false, imageEdit: true, note: "国内 AI 图像创作社区和工具，适合模型风格图、参考图和设计出图。" },
  { name: "OpenAI GPT-4o mini", api_type: "OpenAI兼容", base_url: "https://api.openai.com/v1", model: "gpt-4o-mini", imageModel: "gpt-image-1", imageApiType: "openai", vision: true, imageEdit: true, note: "支持文本、图片理解；图片接口使用 OpenAI 格式。" },
  { name: "OpenAI GPT-4o", api_type: "OpenAI兼容", base_url: "https://api.openai.com/v1", model: "gpt-4o", imageModel: "gpt-image-1", imageApiType: "openai", vision: true, imageEdit: true, note: "支持文本、图片理解；图片接口使用 OpenAI 格式。" },
  { name: "OpenAI GPT-4.1 mini", api_type: "OpenAI兼容", base_url: "https://api.openai.com/v1", model: "gpt-4.1-mini", imageModel: "gpt-image-1", imageApiType: "openai", vision: true, imageEdit: true, note: "支持文本、图片理解；图片接口使用 OpenAI 格式。" },
  { name: "OpenAI兼容接口", api_type: "OpenAI兼容", base_url: "", model: "", imageModel: "gpt-image-1", imageApiType: "auto", vision: "取决于模型", imageEdit: "取决于接口", note: "聊天走 /chat/completions；图片接口可选自动/OpenAI/JSON。" },
  { name: "DeepSeek Chat", api_type: "OpenAI兼容", base_url: "https://api.deepseek.com/v1", model: "deepseek-chat", vision: false, imageEdit: false, note: "主要用于文本推理/写作/代码；当前面板不按图片模型处理。" },
  { name: "DeepSeek Reasoner", api_type: "OpenAI兼容", base_url: "https://api.deepseek.com/v1", model: "deepseek-reasoner", vision: false, imageEdit: false, note: "偏推理文本模型；不建议发送图片。" },
  { name: "阿里云 Qwen Plus", api_type: "OpenAI兼容", base_url: "https://dashscope.aliyuncs.com/compatible-mode/v1", model: "qwen-plus", vision: false, imageEdit: false, note: "文本模型预设；如需图片理解请改用 VL 模型。" },
  { name: "阿里云 Qwen Max", api_type: "OpenAI兼容", base_url: "https://dashscope.aliyuncs.com/compatible-mode/v1", model: "qwen-max", vision: false, imageEdit: false, note: "文本能力更强；当前预设不作为图片模型。" },
  { name: "阿里云 Qwen VL Max", api_type: "OpenAI兼容", base_url: "https://dashscope.aliyuncs.com/compatible-mode/v1", model: "qwen-vl-max", vision: true, imageEdit: false, note: "支持图片理解；不支持在当前面板内编辑图片。" },
  { name: "智谱 GLM-4-Flash", api_type: "OpenAI兼容", base_url: "https://open.bigmodel.cn/api/paas/v4", model: "glm-4-flash", vision: false, imageEdit: false, note: "文本模型，速度快；不建议发送图片。" },
  { name: "智谱 GLM-4V", api_type: "OpenAI兼容", base_url: "https://open.bigmodel.cn/api/paas/v4", model: "glm-4v", vision: true, imageEdit: false, note: "支持图片理解；图片编辑不在当前面板支持范围内。" },
  { name: "Moonshot Kimi 8K", api_type: "OpenAI兼容", base_url: "https://api.moonshot.cn/v1", model: "moonshot-v1-8k", vision: false, imageEdit: false, note: "文本/长文对话预设；当前面板不发送图片给该预设。" },
  { name: "Moonshot Kimi 32K", api_type: "OpenAI兼容", base_url: "https://api.moonshot.cn/v1", model: "moonshot-v1-32k", vision: false, imageEdit: false, note: "更长上下文文本模型；不支持当前面板图片输入。" },
  { name: "硅基流动 通用", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Qwen/Qwen2.5-7B-Instruct", imageModel: "Kwai-Kolors/Kolors", imageApiType: "siliconflow", vision: "取决于模型", imageEdit: "取决于模型", note: "硅基流动模型较多，点“获取模型”后选择模型，下方会按模型名判断能力。" },
  { name: "硅基流动 Qwen2.5 7B", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Qwen/Qwen2.5-7B-Instruct", vision: false, imageEdit: false, note: "文本模型；不适合图片理解或图片编辑。" },
  { name: "硅基流动 Qwen2-VL", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Qwen/Qwen2-VL-72B-Instruct", vision: true, imageEdit: false, note: "视觉语言模型，适合看图分析；通常不是图片编辑/生成模型。" },
  { name: "硅基流动 Kolors", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Kwai-Kolors/Kolors", imageModel: "Kwai-Kolors/Kolors", imageApiType: "siliconflow", vision: false, imageEdit: true, note: "偏图片生成模型；使用硅基流动 JSON 图片接口。" },
  { name: "SiliconFlow Qwen2.5 7B", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Qwen/Qwen2.5-7B-Instruct", vision: false, imageEdit: false, note: "文本模型；模型列表可用“获取模型”更新。" },
  { name: "SiliconFlow Qwen2-VL", api_type: "OpenAI兼容", base_url: "https://api.siliconflow.cn/v1", model: "Qwen/Qwen2-VL-72B-Instruct", vision: true, imageEdit: false, note: "视觉语言模型预设；是否可用取决于账号和服务商当前模型列表。" },
  { name: "OpenRouter GPT-4o mini", api_type: "OpenAI兼容", base_url: "https://openrouter.ai/api/v1", model: "openai/gpt-4o-mini", vision: true, imageEdit: false, note: "OpenRouter 聚合接口；图片能力取决于所选上游模型。" },
  { name: "OpenRouter Claude 3.5 Sonnet", api_type: "OpenAI兼容", base_url: "https://openrouter.ai/api/v1", model: "anthropic/claude-3.5-sonnet", vision: true, imageEdit: false, note: "支持图片理解的聚合预设；需 OpenRouter Key。" },
  { name: "OpenRouter Gemini 1.5 Pro", api_type: "OpenAI兼容", base_url: "https://openrouter.ai/api/v1", model: "google/gemini-pro-1.5", vision: true, imageEdit: false, note: "聚合接口预设；可用性以 OpenRouter 模型列表为准。" },
  { name: "Gemini OpenAI兼容", api_type: "OpenAI兼容", base_url: "https://generativelanguage.googleapis.com/v1beta/openai", model: "gemini-1.5-flash", vision: true, imageEdit: false, note: "Google Gemini 的 OpenAI 兼容接口；支持图片理解。" },
  { name: "Ollama Llama 3.1", api_type: "Ollama /api/chat", base_url: "http://127.0.0.1:11434", model: "llama3.1", vision: false, imageEdit: false, note: "本地文本模型；不发送图片。" },
  { name: "Ollama Llava", api_type: "Ollama /api/chat", base_url: "http://127.0.0.1:11434", model: "llava", vision: false, imageEdit: false, note: "当前面板的 Ollama 路径暂只发送文本，Llava 图片输入后续可单独接 images 字段。" },
  { name: "本地 LM Studio", api_type: "OpenAI兼容", base_url: "http://127.0.0.1:1234/v1", model: "local-model", vision: "取决于模型", imageEdit: false, note: "LM Studio OpenAI 兼容接口；模型名按本地服务实际填写或获取。" },
  { name: "本地 vLLM/OpenAI兼容", api_type: "OpenAI兼容", base_url: "http://127.0.0.1:8000/v1", model: "local-model", vision: "取决于模型", imageEdit: false, note: "本地 OpenAI 兼容服务；图片能力取决于部署模型。" }
];
const AI_TEMPLATES = [
  "常规问题",
  "分析3ds Max报错",
  "MAXScript报错分析",
  "Python/PySide报错分析",
  "插件使用指导",
  "安装/工具栏/图标问题",
  "PBR材质问题",
  "PBR通道识别问题",
  "V-Ray材质问题",
  "Corona材质问题",
  "Physical/PBR材质问题",
  "法线DX/GL判断",
  "UE贴图流送问题",
  "UE导入前检查清单",
  "下载库问题",
  "贴图丢失/路径问题",
  "模型整理/重命名建议",
  "场景卡顿优化",
  "写给客户/同事的说明",
  "给我排查清单"
];
const STANDALONE_KEY = "aiStandaloneState";

function serverUrl(path) {
  return "http://127.0.0.1:" + Number(currentPort || 19527) + path;
}

function sendRuntimeMessage(message, timeoutMs = 20000) {
  return new Promise((resolve) => {
    let done = false;
    const timer = setTimeout(() => {
      if (!done) {
        done = true;
        resolve({ ok: false, error: "请求超时" });
      }
    }, timeoutMs);
    try {
      chrome.runtime.sendMessage(message, (resp) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        if (chrome.runtime.lastError) {
          resolve({ ok: false, error: chrome.runtime.lastError.message });
        } else {
          resolve(resp || { ok: false, error: "没有收到响应" });
        }
      });
    } catch (e) {
      if (!done) {
        done = true;
        clearTimeout(timer);
        resolve({ ok: false, error: String(e) });
      }
    }
  });
}

function escapeHtml(v) {
  return String(v || "").replace(/[&<>\"']/g, (m) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[m]));
}

function applyBrowserTheme() {
  try {
    const dark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
    document.body.dataset.theme = dark ? "dark" : "light";
  } catch (_e) {}
  try {
    if (!chrome.theme || !chrome.theme.getCurrent) return;
    chrome.theme.getCurrent((theme) => {
      const colors = (theme && theme.colors) || {};
      const accent = colors.toolbar || colors.frame || colors.button_background;
      if (Array.isArray(accent) && accent.length >= 3) {
        document.documentElement.style.setProperty("--chrome-accent", `rgb(${accent[0]}, ${accent[1]}, ${accent[2]})`);
      }
    });
  } catch (_e) {}
}

function roleAlign(role) {
  if (role === "user") return "right";
  if (role === "system" || role === "script_running") return "center";
  return "left";
}

function roleName(msg, cfg) {
  if (msg.role === "ai_thinking") return "";
  if (msg.role === "user") return (cfg.user_name || "用户") + ":";
  if (msg.role === "assistant") return (cfg.robot_name || "AI小助手") + ":";
  if (msg.role === "script_result") return "执行结果:";
  if (msg.role === "script_running") return "处理中:";
  if (msg.role === "system") return "提示:";
  return (msg.role || "消息") + ":";
}

function renderInlineMarkdown(text) {
  let s = escapeHtml(text);
  s = s.replace(/\\\((.+?)\\\)/gs, (_m, expr) => renderMath(expr.trim(), false));
  s = s.replace(/\$(?!\$)(.+?)(?<!\$)\$/gs, (_m, expr) => renderMath(expr.trim(), false));
  s = s.replace(/`([^`]+)`/g, (_m, code) => `<code>${escapeHtml(code)}</code>`);
  s = s.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
  s = s.replace(/__([^_]+)__/g, "<strong>$1</strong>");
  s = s.replace(/\*([^*\n]+)\*/g, "<em>$1</em>");
  s = s.replace(/_([^_\n]+)_/g, "<em>$1</em>");
  s = s.replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, '<a href="$2" target="_blank" rel="noreferrer">$1</a>');
  s = s.replace(/(https?:\/\/[^\s<]+)/g, '<a href="$1" target="_blank" rel="noreferrer">$1</a>');
  return s;
}

const MATH_SYMBOLS = {
  "\\alpha": "α", "\\beta": "β", "\\gamma": "γ", "\\delta": "δ", "\\epsilon": "ε", "\\theta": "θ",
  "\\lambda": "λ", "\\mu": "μ", "\\pi": "π", "\\rho": "ρ", "\\sigma": "σ", "\\phi": "φ",
  "\\omega": "ω", "\\Delta": "Δ", "\\Theta": "Θ", "\\Lambda": "Λ", "\\Pi": "Π", "\\Sigma": "Σ",
  "\\Phi": "Φ", "\\Omega": "Ω", "\\times": "×", "\\cdot": "·", "\\pm": "±", "\\le": "≤",
  "\\ge": "≥", "\\neq": "≠", "\\approx": "≈", "\\infty": "∞", "\\sum": "∑", "\\int": "∫",
  "\\partial": "∂", "\\nabla": "∇", "\\rightarrow": "→", "\\leftarrow": "←", "\\to": "→"
};

function extractBrace(src, start) {
  if (src[start] !== "{") return null;
  let depth = 0;
  for (let i = start; i < src.length; i++) {
    if (src[i] === "{") depth++;
    if (src[i] === "}") depth--;
    if (depth === 0) return { value: src.slice(start + 1, i), end: i + 1 };
  }
  return null;
}

function renderKatex(expr, displayMode) {
  try {
    if (window.katex && typeof window.katex.renderToString === "function") {
      return window.katex.renderToString(expr, {
        displayMode: !!displayMode,
        throwOnError: false,
        strict: "ignore",
        trust: false,
        output: "html"
      });
    }
  } catch (_e) {}
  return "";
}

function renderMath(expr, displayMode) {
  const katexHtml = renderKatex(expr, displayMode);
  if (katexHtml) return katexHtml;
  let src = String(expr || "").trim();
  function walk(s) {
    let out = "";
    for (let i = 0; i < s.length; i++) {
      if (s.startsWith("\\frac", i)) {
        const a = extractBrace(s, i + 5);
        const b = a ? extractBrace(s, a.end) : null;
        if (a && b) {
          out += `<span class="frac"><span class="frac-num">${walk(a.value)}</span><span class="frac-den">${walk(b.value)}</span></span>`;
          i = b.end - 1;
          continue;
        }
      }
      if (s.startsWith("\\sqrt", i)) {
        const a = extractBrace(s, i + 5);
        if (a) {
          out += `<span class="sqrt"><span class="sqrt-symbol">√</span><span class="sqrt-body">${walk(a.value)}</span></span>`;
          i = a.end - 1;
          continue;
        }
      }
      const sym = Object.keys(MATH_SYMBOLS).find((key) => s.startsWith(key, i));
      if (sym) {
        out += MATH_SYMBOLS[sym];
        i += sym.length - 1;
        continue;
      }
      if ((s[i] === "^" || s[i] === "_") && i + 1 < s.length) {
        const tag = s[i] === "^" ? "sup" : "sub";
        if (s[i + 1] === "{") {
          const a = extractBrace(s, i + 1);
          if (a) {
            out += `<${tag}>${walk(a.value)}</${tag}>`;
            i = a.end - 1;
            continue;
          }
        }
        out += `<${tag}>${escapeHtml(s[i + 1])}</${tag}>`;
        i++;
        continue;
      }
      if (s[i] === "\\") continue;
      out += escapeHtml(s[i]);
    }
    return out;
  }
  return `<span class="${displayMode ? "math-block" : "math-inline"}">${walk(src)}</span>`;
}

function renderMarkdown(text) {
  const src = String(text || "").replace(/\r\n/g, "\n");
  const blocks = [];
  let rest = src.replace(/```([a-zA-Z0-9_-]+)?\n([\s\S]*?)```/g, (_m, lang, code) => {
    const token = `\u0000CODE${blocks.length}\u0000`;
    const rawCode = code.trimEnd();
    blocks.push(`<pre><button class="copy-code-btn" data-smart-action="copy" data-smart-value="${escapeHtml(rawCode)}">复制代码</button><code${lang ? ` data-lang="${escapeHtml(lang)}"` : ""}>${escapeHtml(rawCode)}</code></pre>`);
    return token;
  });
  rest = rest.replace(/\$\$([\s\S]+?)\$\$/g, (_m, expr) => {
    const token = `\u0000CODE${blocks.length}\u0000`;
    blocks.push(renderMath(expr.trim(), true));
    return token;
  });
  rest = rest.replace(/\\\[([\s\S]+?)\\\]/g, (_m, expr) => {
    const token = `\u0000CODE${blocks.length}\u0000`;
    blocks.push(renderMath(expr.trim(), true));
    return token;
  });
  const lines = rest.split("\n");
  const out = [];
  let list = null;
  function closeList() {
    if (list) {
      out.push(`</${list}>`);
      list = null;
    }
  }
  for (const raw of lines) {
    const line = raw.trimEnd();
    if (!line.trim()) {
      closeList();
      continue;
    }
    const tokenMatch = line.match(/^\u0000CODE(\d+)\u0000$/);
    if (tokenMatch) {
      closeList();
      out.push(blocks[Number(tokenMatch[1])] || "");
      continue;
    }
    const heading = line.match(/^(#{1,3})\s+(.+)$/);
    if (heading) {
      closeList();
      out.push(`<h${heading[1].length}>${renderInlineMarkdown(heading[2])}</h${heading[1].length}>`);
      continue;
    }
    const bullet = line.match(/^\s*[-*]\s+(.+)$/);
    if (bullet) {
      if (list !== "ul") {
        closeList();
        list = "ul";
        out.push("<ul>");
      }
      out.push(`<li>${renderInlineMarkdown(bullet[1])}</li>`);
      continue;
    }
    const ordered = line.match(/^\s*\d+\.\s+(.+)$/);
    if (ordered) {
      if (list !== "ol") {
        closeList();
        list = "ol";
        out.push("<ol>");
      }
      out.push(`<li>${renderInlineMarkdown(ordered[1])}</li>`);
      continue;
    }
    if (line.startsWith("> ")) {
      closeList();
      out.push(`<blockquote>${renderInlineMarkdown(line.slice(2))}</blockquote>`);
      continue;
    }
    closeList();
    out.push(`<p>${renderInlineMarkdown(line)}</p>`);
  }
  closeList();
  return out.join("");
}

function extractSmartActions(text) {
  const src = String(text || "");
  const actions = [];
  const seen = new Set();
  const urlRe = /\bhttps?:\/\/[^\s<>"'）)】]+/gi;
  let m;
  while ((m = urlRe.exec(src))) {
    const url = m[0].replace(/[，。；、,.]+$/g, "");
    if (!seen.has("url:" + url)) {
      seen.add("url:" + url);
      actions.push({ type: "url", label: "打开网站", value: url });
      actions.push({ type: "copy", label: "复制链接", value: url });
    }
  }
  return actions.slice(0, 6);
  const codeRe = /```([a-zA-Z0-9_-]+)?\n([\s\S]*?)```/g;
  while ((m = codeRe.exec(src))) {
    const code = String(m[2] || "").trim();
    if (code && !seen.has("code:" + code)) {
      seen.add("code:" + code);
      actions.push({ type: "copy", label: "复制代码", value: code });
    }
  }
  return actions.slice(0, 6);
}

function renderSmartActions(text) {
  const actions = extractSmartActions(text);
  if (!actions.length) return "";
  return `<div class="smart-actions">${actions.map((a) => `<button data-smart-action="${escapeHtml(a.type)}" data-smart-value="${escapeHtml(a.value)}">${escapeHtml(a.label)}</button>`).join("")}</div>`;
}

function setActionStatus(text, type) {
  els.actionStatus.textContent = text || "";
  els.actionStatus.style.color = type === "error" ? "var(--danger)" : (type === "ok" ? "var(--ok)" : "var(--muted)");
  if (els.sidebarActionStatus) {
    els.sidebarActionStatus.textContent = text || "";
    els.sidebarActionStatus.style.color = type === "error" ? "var(--danger)" : (type === "ok" ? "var(--ok)" : "var(--muted)");
  }
}

function updateSendBadge(mode, text) {
  els.sendBadge.className = "statusbadge" + (mode ? (" " + mode) : "");
  els.sendBadge.textContent = text;
}

function setBusy(busy) {
  if (lastBusyState === !!busy) return;
  lastBusyState = !!busy;
  document.getElementById("btn-send").disabled = !!busy;
  document.getElementById("btn-save-config").disabled = !!busy;
  document.getElementById("btn-test").disabled = !!busy;
  document.getElementById("btn-diagnose").disabled = !!busy;
  els.prompt.disabled = !!busy;
  els.fileInput.disabled = !!busy;
}

function fillSelect(selectEl, values, currentValue) {
  if (!selectEl) return;
  const old = selectEl.value;
  selectEl.innerHTML = "";
  values.forEach((value) => {
    const opt = document.createElement("option");
    opt.value = value;
    opt.textContent = value;
    selectEl.appendChild(opt);
  });
  const finalValue = currentValue || old;
  if (finalValue && values.includes(finalValue)) {
    selectEl.value = finalValue;
  } else if (values.length) {
    selectEl.value = values[0];
  }
}

function renderMessages() {
  const cfg = appMode === "standalone" ? collectConfigPatch() : ((state && state.config) || {});
  const baseArr = appMode === "standalone" ? standaloneMessages : ((state && state.messages) || []);
  const hasServerThinking = appMode === "max" && baseArr.some((m) => m && m.role === "ai_thinking");
  const arr = appMode === "standalone" || !maxPendingMessage || hasServerThinking ? baseArr : baseArr.concat([maxPendingMessage]);
  document.body.classList.toggle("empty-chat", appMode === "standalone" && arr.length === 0);
  const sig = appMode + ":" + JSON.stringify(arr.map((m) => ({
    role: m.role,
    content: m.content,
    thinking: m.thinking || "",
    images: (m.images || []).map((it) => it.path || it.url || it.data_url || it.name || "")
  })));
  if (sig === lastMessageSignature) return;
  lastMessageSignature = sig;
  if (!arr.length) {
    const lines = appMode === "standalone" ? WELCOME_LINES : GREETINGS;
    const seed = Math.floor((Date.now() / 60000) + Math.random() * 1000) % lines.length;
    els.messages.classList.add("empty");
    els.messages.innerHTML = `<div class="empty-state"><h1>${escapeHtml(lines[seed])}</h1></div>`;
    return;
  }
  els.messages.classList.remove("empty");
  els.messages.innerHTML = arr.map((msg) => {
    const imgs = Array.isArray(msg.images) ? msg.images : [];
    const thinkingHtml = (msg.thinking && msg.thinking.trim())
      ? `<div class="thinking-block collapsed" onclick="this.classList.toggle('collapsed')"><div class="thinking-header">💭 思考过程 <span style="font-size:10px;opacity:.6">（点击展开）</span></div><div class="thinking-body">${escapeHtml(msg.thinking.trim())}</div></div>`
      : "";
    return `<div class="msg ${roleAlign(msg.role)}"><div class="bubble"><span class="name">${escapeHtml(roleName(msg, cfg))}</span>${thinkingHtml}${renderMarkdown(msg.content || "")}${renderSmartActions(msg.content || "")}${imgs.length ? `<div class="images">${imgs.map((it) => {
      const src = it.thumb || it.url || it.data_url || "";
      const href = it.url || it.data_url || "#";
      return `<button class="image-open" data-src="${escapeHtml(href)}" title="预览图片"><img src="${escapeHtml(src)}"></button>`;
    }).join("")}</div>` : ""}</div></div>`;
  }).join("");
  if (appMode === "standalone") {
    els.messages.querySelectorAll(".image-open").forEach((btn) => {
      const wrap = document.createElement("span");
      wrap.className = "image-item";
      btn.parentNode.insertBefore(wrap, btn);
      wrap.appendChild(btn);
      const editBtn = document.createElement("button");
      editBtn.className = "image-reedit";
      editBtn.dataset.src = btn.getAttribute("data-src") || "";
      editBtn.textContent = "继续修改";
      wrap.appendChild(editBtn);
    });
  }
  els.messages.scrollTop = els.messages.scrollHeight;
}

function replaceMessageById(id, patch) {
  const idx = standaloneMessages.findIndex((m) => m.id === id);
  if (idx < 0) return;
  standaloneMessages[idx] = { ...standaloneMessages[idx], ...patch };
  lastMessageSignature = "";
  renderMessages();
}

function resizePrompt() {
  els.prompt.style.height = "0px";
  const next = Math.min(Math.max(34, els.prompt.scrollHeight), 132);
  els.prompt.style.height = next + "px";
}

function setUploadProgress(percent, label) {
  const p = Math.max(0, Math.min(100, Number(percent || 0)));
  if (els.uploadProgress) els.uploadProgress.classList.toggle("hidden", p <= 0 || p >= 100);
  if (els.uploadProgressBar) els.uploadProgressBar.style.width = p + "%";
  if (label) els.imageStatus.textContent = label;
}

function setStandaloneConfigCollapsed(collapsed) {
  if (!els.standaloneConfigSection) return;
  els.standaloneConfigSection.classList.toggle("collapsed", !!collapsed);
  if (els.toggleStandaloneConfig) els.toggleStandaloneConfig.textContent = collapsed ? "展开" : "收起";
}

function readImageCompressed(file, maxEdge = 1600, quality = 0.86) {
  return new Promise((resolve, reject) => {
    if (!file || !/^image\//i.test(file.type || "")) {
      const rd = new FileReader();
      rd.onload = () => resolve({ name: file.name, data_url: String(rd.result || "") });
      rd.onerror = () => reject(new Error("读取文件失败: " + file.name));
      rd.readAsDataURL(file);
      return;
    }
    const rd = new FileReader();
    rd.onerror = () => reject(new Error("读取图片失败: " + file.name));
    rd.onload = () => {
      const img = new Image();
      img.onerror = () => resolve({ name: file.name, data_url: String(rd.result || "") });
      img.onload = () => {
        const scale = Math.min(1, maxEdge / Math.max(img.width || 1, img.height || 1));
        const w = Math.max(1, Math.round((img.width || 1) * scale));
        const h = Math.max(1, Math.round((img.height || 1) * scale));
        const canvas = document.createElement("canvas");
        canvas.width = w;
        canvas.height = h;
        const ctx = canvas.getContext("2d");
        ctx.drawImage(img, 0, 0, w, h);
        const mime = /png|webp/i.test(file.type || "") ? file.type : "image/jpeg";
        const dataUrl = canvas.toDataURL(mime, quality);
        resolve({ name: file.name, data_url: dataUrl, original_size: file.size, width: w, height: h });
      };
      img.src = String(rd.result || "");
    };
    rd.readAsDataURL(file);
  });
}

function standaloneProviderNames() {
  return apiStandaloneProviders().map((p) => p.name);
}

function standaloneProviderByName(name) {
  return apiStandaloneProviders().find((p) => p.name === name) || apiStandaloneProviders()[0] || STANDALONE_PROVIDERS[0];
}

function allStandaloneProviders() {
  const seen = new Set();
  return [...STANDALONE_PROVIDERS, ...customStandaloneProviders].filter((p) => {
    const key = String(p.siteUrl || p.base_url || p.name || "").replace(/\/+$/, "").toLowerCase();
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function apiStandaloneProviders() {
  return allStandaloneProviders().filter((p) => {
    const saved = standaloneProviderConfigs[standaloneProviderKey(p.name)] || {};
    if (p.siteUrl && STANDALONE_PROVIDERS.includes(p)) return false;
    if (p.siteUrl && !(p.base_url || saved.base_url || p.apiProvider)) return false;
    return !!(p.base_url || saved.base_url || !p.siteUrl);
  });
}

function websiteStandaloneProviders() {
  return allStandaloneProviders().filter((p) => !!standaloneProviderWebUrl(p));
}

function standaloneProviderKey(name) {
  return String(name || "default").trim();
}

function standaloneApiKeyUrl(providerName) {
  const name = String(providerName || "").toLowerCase();
  if (name.includes("openai")) return "https://platform.openai.com/api-keys";
  if (name.includes("deepseek")) return "https://platform.deepseek.com/api_keys";
  if (name.includes("阿里") || name.includes("qwen") || name.includes("dashscope")) return "https://dashscope.console.aliyun.com/apiKey";
  if (name.includes("智谱") || name.includes("glm")) return "https://open.bigmodel.cn/usercenter/apikeys";
  if (name.includes("moonshot") || name.includes("kimi")) return "https://platform.moonshot.cn/console/api-keys";
  if (name.includes("硅基") || name.includes("siliconflow")) return "https://cloud.siliconflow.cn/account/ak";
  if (name.includes("openrouter")) return "https://openrouter.ai/settings/keys";
  if (name.includes("gemini")) return "https://aistudio.google.com/app/apikey";
  return "";
}

function standaloneProviderWebUrl(provider) {
  const name = String(provider && provider.name || provider || "").toLowerCase();
  if (provider && provider.siteUrl) return provider.siteUrl;
  if (name.includes("chatgpt")) return "https://chatgpt.com/";
  if (name.includes("openai")) return "https://openai.com/";
  if (name.includes("deepseek")) return "https://platform.deepseek.com/";
  if (name.includes("阿里") || name.includes("qwen") || name.includes("dashscope")) return "https://dashscope.aliyun.com/";
  if (name.includes("智谱") || name.includes("glm")) return "https://open.bigmodel.cn/";
  if (name.includes("moonshot") || name.includes("kimi")) return "https://platform.moonshot.cn/";
  if (name.includes("硅基") || name.includes("siliconflow")) return "https://siliconflow.cn/";
  if (name.includes("openrouter")) return "https://openrouter.ai/";
  if (name.includes("gemini")) return "https://aistudio.google.com/";
  return "";
}

function openProviderPage(providerName) {
  const provider = allStandaloneProviders().find((p) => p.name === providerName) || standaloneProviderByName(providerName);
  const url = standaloneProviderWebUrl(provider);
  if (!url) {
    setActionStatus("当前平台没有官网地址，请在自定义平台里填写网站 URL。", "error");
    return;
  }
  chrome.tabs.create({ url });
  setActionStatus("已打开平台网页", "ok");
}

function openStandaloneApiKeyPage() {
  if (appMode !== "standalone") {
    setActionStatus("Max 联动模式请使用 Max 端的“获取API Key”按钮", "error");
    return;
  }
  const url = standaloneApiKeyUrl(els.provider.value);
  if (!url) {
    setActionStatus("当前方案没有内置 API Key 页面，请到服务商控制台获取。", "error");
    return;
  }
  chrome.tabs.create({ url });
  setActionStatus("已打开 API Key 页面", "ok");
}

function applyStandaloneProvider(name, force) {
  const preset = standaloneProviderByName(name);
  if (!preset) return;
  fillSelect(els.apiType, AI_API_TYPES, preset.api_type);
  if (force || !els.baseUrl.value.trim()) els.baseUrl.value = preset.base_url || "";
  if (force || !els.model.value.trim()) els.model.value = preset.model || "";
  if (force) standaloneModelOptions = preset.model ? [preset.model] : [];
  syncModelSelect(standaloneModelOptions, els.model.value.trim());
  if (els.imageModel && (force || !els.imageModel.value.trim())) els.imageModel.value = preset.imageModel || "gpt-image-1";
  syncImageModelForCurrentModel(force);
  if (els.imageApiType && (force || !els.imageApiType.value)) els.imageApiType.value = preset.imageApiType || "auto";
  if (!els.temperature.value) els.temperature.value = 0.3;
  if (!els.history.value) els.history.value = 8;
  if (!els.robotName.value.trim()) els.robotName.value = "AI小助手";
  if (!els.userName.value.trim()) els.userName.value = "用户";
  updateProviderCapabilities();
}

function formatCapability(v) {
  if (v === true) return "支持";
  if (v === false) return "不支持";
  return String(v || "未知");
}

function providerImageEditLabel(provider) {
  const value = provider && provider.imageEdit;
  if (value === true) return "改图";
  if (value === false) return "文本/看图";
  return "不确定";
}

function renderProviderManager() {
  if (!els.providerList) return;
  els.providerList.innerHTML = websiteStandaloneProviders().map((p) => {
    const cls = p.imageEdit === true ? "provider-card editable" : "provider-card";
    const site = standaloneProviderWebUrl(p);
    return `<div class="${cls}" data-provider-name="${escapeHtml(p.name)}" title="${escapeHtml(p.note || "")}"><strong>${escapeHtml(p.name)}</strong><span class="provider-tag">${escapeHtml(providerImageEditLabel(p))}</span><div class="provider-actions">${site ? `<button data-provider-open="${escapeHtml(p.name)}">打开</button>` : "<span></span>"}<button data-provider-select="${escapeHtml(p.name)}">API</button><button data-provider-key="${escapeHtml(p.name)}">Key</button></div></div>`;
  }).join("");
  els.providerList.querySelectorAll("[data-provider-select]").forEach((btn) => btn.remove());
}

function addCustomProvider() {
  const modalOpen = els.providerAddModal && !els.providerAddModal.classList.contains("hidden");
  const nameEl = modalOpen ? els.customProviderNameModal : els.customProviderName;
  const siteEl = modalOpen ? els.customProviderSiteUrlModal : els.customProviderSiteUrl;
  const baseEl = modalOpen ? els.customProviderBaseUrlModal : els.customProviderBaseUrl;
  const modelEl = modalOpen ? els.customProviderModelModal : els.customProviderModel;
  const imageModelEl = modalOpen ? els.customProviderImageModelModal : els.customProviderImageModel;
  const imageEditEl = modalOpen ? els.customProviderImageEditModal : els.customProviderImageEdit;
  const name = (nameEl && nameEl.value || "").trim();
  if (!name) {
    setActionStatus("请先填写平台名称。", "error");
    return;
  }
  const imageEditRaw = imageEditEl ? imageEditEl.value : "unknown";
  const siteUrl = (siteEl && siteEl.value || "").trim();
  const provider = {
    name,
    api_type: "OpenAI兼容",
    base_url: (baseEl && baseEl.value || "").trim(),
    model: (modelEl && modelEl.value || "").trim(),
    imageModel: (imageModelEl && imageModelEl.value || "").trim(),
    imageApiType: "auto",
    siteUrl,
    apiProvider: !!(baseEl && baseEl.value && baseEl.value.trim()),
    vision: "取决于模型",
    imageEdit: imageEditRaw === "unknown" ? "取决于接口" : imageEditRaw === "true",
    note: "用户自定义第三方平台。请按该平台文档确认图片编辑接口格式。"
  };
  const normalizedSite = siteUrl.replace(/\/+$/, "").toLowerCase();
  customStandaloneProviders = customStandaloneProviders.filter((p) => {
    const sameName = p.name === name;
    const sameSite = normalizedSite && String(p.siteUrl || "").replace(/\/+$/, "").toLowerCase() === normalizedSite;
    return !sameName && !sameSite;
  }).concat(provider);
  fillSelect(els.provider, standaloneProviderNames(), name);
  setStandaloneProviderConfig(name, {
    provider: name,
    api_type: provider.api_type,
    base_url: provider.base_url,
    model: provider.model,
    image_model: provider.imageModel,
    image_api_type: provider.imageApiType
  });
  loadStandaloneProviderConfig(name);
  saveStandaloneState({ saveProviderConfig: true });
  renderProviderManager();
  if (modalOpen) els.providerAddModal.classList.add("hidden");
  setActionStatus("已添加第三方 AI 平台。", "ok");
}

function inferModelCapabilities(modelName, providerName) {
  const name = String(modelName || "").toLowerCase();
  const provider = String(providerName || "").toLowerCase();
  const isImageGen = /(gpt-image|dall|kolors|stable-diffusion|sdxl|flux|image|wanx|cogview|midjourney)/i.test(name);
  const isVision = /(vl|vision|gpt-4o|4\.1|gemini|claude-3|llava|qwen2-vl|glm-4v|pixtral|minicpm-v|internvl)/i.test(name);
  const textOnly = /(deepseek|reasoner|qwen2\.5|llama|moonshot|kimi|glm-4-flash)/i.test(name) && !isVision && !isImageGen;
  let imageEdit = isImageGen;
  if (/openai/i.test(provider) && /(gpt-image|dall)/i.test(name)) imageEdit = true;
  return {
    vision: isVision || (!textOnly && /openrouter|gemini|openai/.test(provider) && /(gpt-4o|gemini|claude)/i.test(name)),
    imageEdit,
    kind: isImageGen ? "图片生成/编辑模型" : (isVision ? "视觉理解模型" : (textOnly ? "文本模型" : "能力取决于服务商"))
  };
}

function suggestedImageModel(providerName, modelName) {
  const provider = String(providerName || "").toLowerCase();
  const model = String(modelName || "").toLowerCase();
  if (/siliconflow|硅基/.test(provider)) {
    if (/kolors|flux|stable|sdxl|image/.test(model)) return modelName;
    return "Kwai-Kolors/Kolors";
  }
  if (/openai/.test(provider)) return "gpt-image-1";
  if (/阿里|qwen|dashscope/.test(provider)) return "wanx2.1-t2i-turbo";
  return "";
}

function syncImageModelForCurrentModel(force = false) {
  if (!els.imageModel) return;
  const caps = inferModelCapabilities(els.model.value, els.provider.value);
  const recommended = suggestedImageModel(els.provider.value, els.model.value);
  if (!caps.imageEdit && !recommended) {
    els.imageModel.value = "不支持";
    els.imageModel.disabled = true;
    return;
  }
  els.imageModel.disabled = false;
  if (force || !els.imageModel.value.trim() || els.imageModel.value === "不支持") {
    els.imageModel.value = recommended || els.model.value || "gpt-image-1";
  }
}

function imageModelIsUsable(value) {
  const text = String(value || "").trim();
  return !!text && text !== "不支持" && text !== "涓嶆敮鎸?";
}

function updateImageEditorSummary() {
  if (!els.imageEditorCurrent) return;
  const cfg = collectConfigPatch();
  els.imageEditorCurrent.value = [
    cfg.provider || "未选择方案",
    cfg.image_model || cfg.model || "未填写图片模型",
    cfg.image_api_type || "auto"
  ].join(" / ");
}

function applyImageEditPreset(kind) {
  if (appMode !== "standalone") {
    appMode = "standalone";
  }
  if (kind === "openai") {
    fillSelect(els.provider, standaloneProviderNames(), "OpenAI GPT-4o mini");
    els.apiType.value = "OpenAI兼容";
    els.baseUrl.value = "https://api.openai.com/v1";
    els.model.value = "gpt-4o-mini";
    els.imageModel.value = "gpt-image-1";
    els.imageApiType.value = "openai";
  } else if (kind === "siliconflow") {
    fillSelect(els.provider, standaloneProviderNames(), "硅基流动 Kolors");
    els.apiType.value = "OpenAI兼容";
    els.baseUrl.value = "https://api.siliconflow.cn/v1";
    els.model.value = "Qwen/Qwen2.5-7B-Instruct";
    els.imageModel.value = "Kwai-Kolors/Kolors";
    els.imageApiType.value = "siliconflow";
  }
  const imagePresetPatch = kind === "openai" ? {
    provider: "OpenAI GPT-4o mini",
    api_type: "OpenAI兼容",
    base_url: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
    image_model: "gpt-image-1",
    image_api_type: "openai",
    model_options: ["gpt-4o-mini", "gpt-4o", "gpt-4.1-mini"]
  } : (kind === "siliconflow" ? {
    provider: "硅基流动 Kolors",
    api_type: "OpenAI兼容",
    base_url: "https://api.siliconflow.cn/v1",
    model: "Qwen/Qwen2.5-7B-Instruct",
    image_model: "Kwai-Kolors/Kolors",
    image_api_type: "siliconflow",
    model_options: ["Qwen/Qwen2.5-7B-Instruct", "Qwen/Qwen2-VL-72B-Instruct", "Kwai-Kolors/Kolors"]
  } : null);
  if (imagePresetPatch) {
    fillSelect(els.provider, standaloneProviderNames(), imagePresetPatch.provider);
    setStandaloneProviderConfig(imagePresetPatch.provider, imagePresetPatch);
    loadStandaloneProviderConfig(imagePresetPatch.provider);
  } else {
    saveCurrentStandaloneProviderConfig();
  }
  saveStandaloneState();
  syncForm();
  updateImageEditorSummary();
  setActionStatus("已套用图片编辑推荐方案", "ok");
}

function openImageEditor() {
  if (appMode !== "standalone") {
    appMode = "standalone";
    enterStandaloneConfig({});
    loadStandaloneProviderConfig(els.provider.value);
    saveStandaloneState({ saveProviderConfig: true });
    syncForm();
  }
  updateImageEditorSummary();
  if (els.imageEditorStatus) els.imageEditorStatus.textContent = "";
  els.imageEditorModal.classList.remove("hidden");
}

function openImageEditorWithImage(imageUrl) {
  openImageEditor();
  if (els.imageEditorFile) els.imageEditorFile.value = "";
  els.imageEditorModal.dataset.sourceImage = imageUrl || "";
  if (els.imageEditorStatus) els.imageEditorStatus.textContent = "已选中上一张生成图，输入新的修改要求即可继续。";
}

function closeImageEditor() {
  els.imageEditorModal.classList.add("hidden");
  if (els.imageEditorModal) els.imageEditorModal.dataset.sourceImage = "";
}

async function readImageEditorFiles() {
  const sourceImage = els.imageEditorModal && els.imageEditorModal.dataset.sourceImage;
  if (sourceImage) {
    return [{ name: "previous_result.png", data_url: sourceImage, url: sourceImage }];
  }
  const ownFiles = Array.from((els.imageEditorFile && els.imageEditorFile.files) || []);
  const chatFiles = Array.from((els.fileInput && els.fileInput.files) || []);
  const files = ownFiles.length ? ownFiles : chatFiles;
  const images = [];
  for (let i = 0; i < files.length; i++) {
    if (els.imageEditorStatus) els.imageEditorStatus.textContent = "正在压缩图片 " + String(i + 1) + "/" + String(files.length) + "...";
    images.push(await readImageCompressed(files[i]));
  }
  return images;
}

async function runImageEditor() {
  if (appMode !== "standalone") openImageEditor();
  const prompt = (els.imageEditorPrompt.value || els.prompt.value || "").trim();
  if (!prompt) {
    els.imageEditorStatus.textContent = "请先填写修改要求。";
    return;
  }
  const cfg = collectConfigPatch();
  if (!imageModelIsUsable(cfg.image_model)) {
    els.imageEditorStatus.textContent = "请先选择推荐方案，或填写可用的图片模型名。";
    return;
  }
  const images = await readImageEditorFiles();
  if (!images.length) {
    els.imageEditorStatus.textContent = "请先选择要编辑的图片。";
    return;
  }
  const pendingId = "image_edit_" + Date.now();
  standaloneMessages.push({ role: "user", content: prompt, images });
  standaloneMessages.push({ id: pendingId, role: "assistant", content: "正在编辑图片..." });
  renderMessages();
  closeImageEditor();
  updateSendBadge("busy", "图片编辑中");
  setUploadProgress(70, "正在请求图片编辑接口...");
  const resp = await sendRuntimeMessage({ action: "independentAiImage", config: cfg, prompt, images }, 90000);
  if (!resp || resp.ok === false) {
    replaceMessageById(pendingId, { role: "system", content: "图片编辑失败：\n" + ((resp && resp.error) || "未知错误") });
    updateSendBadge("error", "图片编辑失败");
  } else {
    replaceMessageById(pendingId, { content: resp.text || "图片编辑完成。", images: resp.images || [] });
    updateSendBadge("ok", "空闲");
  }
  saveStandaloneState();
  renderMessages();
  setUploadProgress(100, "");
}

function wantsImageOutput(text, imageCount) {
  const s = String(text || "").toLowerCase();
  if (imageCount > 0 && /(编辑|修改|改图|重绘|换|去掉|添加|变成|修图|edit|modify|remove|replace|add|inpaint|outpaint)/i.test(s)) return true;
  return /(生成|画|绘制|出图|生图|图片|海报|logo|图像|效果图|插画|壁纸|generate|draw|create an image|make an image|image)/i.test(s);
}

function updateProviderCapabilities() {
  if (!els.providerCapabilities) return;
  if (appMode !== "standalone") {
    els.providerCapabilities.textContent = "Max 联动模式使用 3ds Max 插件里的 AI 配置。";
    return;
  }
  const preset = standaloneProviderByName(els.provider.value);
  syncImageModelForCurrentModel(false);
  const inferred = inferModelCapabilities(els.model.value || preset.model, els.provider.value);
  const vision = preset.vision === "取决于模型" ? inferred.vision : preset.vision;
  const imageEdit = preset.imageEdit === "取决于模型" || preset.imageEdit === "取决于接口" ? inferred.imageEdit : preset.imageEdit || inferred.imageEdit;
  els.providerCapabilities.innerHTML = [
    "当前模型类型：" + escapeHtml(inferred.kind),
    "文本对话：支持",
    "图片理解：" + formatCapability(vision),
    "图片编辑/生成：" + formatCapability(imageEdit),
    "发送规则：模型支持图片编辑/生成时自动走图片接口；否则按聊天/看图接口发送。",
    escapeHtml(preset.note || "")
  ].filter(Boolean).join("<br>");
}

function syncModelSelect(options, currentValue) {
  const values = [...new Set([...(options || []), currentValue].filter(Boolean))];
  els.modelSelect.innerHTML = "";
  if (!values.length) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = "手动输入模型名";
    els.modelSelect.appendChild(opt);
    return;
  }
  values.forEach((value) => {
    const opt = document.createElement("option");
    opt.value = value;
    opt.textContent = value;
    els.modelSelect.appendChild(opt);
  });
  els.modelSelect.value = currentValue && values.includes(currentValue) ? currentValue : values[0];
  if (!els.model.value.trim()) els.model.value = els.modelSelect.value;
}

function enterStandaloneConfig(savedConfig) {
  const cfg = savedConfig || {};
  fillSelect(els.provider, standaloneProviderNames(), cfg.provider || "OpenAI");
  fillSelect(els.apiType, AI_API_TYPES, cfg.api_type || standaloneProviderByName(els.provider.value).api_type);
  applyStandaloneProvider(els.provider.value, false);
  if ("base_url" in cfg) els.baseUrl.value = cfg.base_url || "";
  if ("model" in cfg) els.model.value = cfg.model || "";
  syncModelSelect(standaloneModelOptions, els.model.value.trim());
  if (els.imageModel && "image_model" in cfg) els.imageModel.value = cfg.image_model || "";
  syncImageModelForCurrentModel(false);
  if (els.imageApiType && "image_api_type" in cfg) els.imageApiType.value = cfg.image_api_type || "auto";
  if (els.searchApiUrl && "search_api_url" in cfg) els.searchApiUrl.value = cfg.search_api_url || "";
  if (els.searchApiKey && "search_api_key" in cfg) els.searchApiKey.value = cfg.search_api_key || "";
  if ("temperature" in cfg) els.temperature.value = cfg.temperature ?? 0.3;
  if ("history" in cfg) els.history.value = cfg.history ?? 8;
  if ("robot_name" in cfg) els.robotName.value = cfg.robot_name || "AI小助手";
  if ("user_name" in cfg) els.userName.value = cfg.user_name || "用户";
  if (els.template) els.template.value = "";
  updateProviderCapabilities();
}

function currentStandaloneConfigKey() {
  return standaloneProviderKey(els.provider.value);
}

function saveCurrentStandaloneProviderConfig() {
  if (appMode !== "standalone" || switchingStandaloneProvider) return;
  standaloneProviderConfigs[currentStandaloneConfigKey()] = collectConfigPatch();
}

function saveStandaloneProviderByName(providerName, patch) {
  const key = standaloneProviderKey(providerName);
  standaloneProviderConfigs[key] = Object.assign({}, standaloneProviderConfigs[key] || {}, patch);
}

function collectConfigPatchForProvider(providerName) {
  const cfg = collectConfigPatch();
  cfg.provider = providerName || cfg.provider;
  return cfg;
}

function setStandaloneProviderConfig(providerName, patch) {
  const key = standaloneProviderKey(providerName);
  standaloneProviderConfigs[key] = Object.assign({}, standaloneProviderConfigs[key] || {}, patch);
}

function loadStandaloneProviderConfig(providerName, options = {}) {
  const preset = standaloneProviderByName(providerName);
  const saved = options.resetToPreset ? {} : (standaloneProviderConfigs[standaloneProviderKey(providerName)] || {});
  fillSelect(els.apiType, AI_API_TYPES, saved.api_type || preset.api_type || "OpenAI兼容");
  els.baseUrl.value = options.resetToPreset ? (preset.base_url || "") : (saved.base_url ?? preset.base_url ?? "");
  els.model.value = options.resetToPreset ? (preset.model || "") : (saved.model ?? preset.model ?? "");
  if (els.imageModel) els.imageModel.value = options.resetToPreset ? (preset.imageModel || "gpt-image-1") : (saved.image_model ?? preset.imageModel ?? "gpt-image-1");
  syncImageModelForCurrentModel(false);
  if (els.imageApiType) els.imageApiType.value = options.resetToPreset ? (preset.imageApiType || "auto") : (saved.image_api_type ?? preset.imageApiType ?? "auto");
  els.apiKey.value = options.resetToPreset ? "" : (saved.api_key || "");
  if (els.searchApiUrl) els.searchApiUrl.value = options.resetToPreset ? "" : (saved.search_api_url ?? "");
  if (els.searchApiKey) els.searchApiKey.value = options.resetToPreset ? "" : (saved.search_api_key ?? "");
  els.temperature.value = saved.temperature ?? 0.3;
  els.history.value = saved.history ?? 8;
  els.robotName.value = saved.robot_name || "AI小助手";
  els.userName.value = saved.user_name || "用户";
  standaloneModelOptions = options.resetToPreset ? (preset.model ? [preset.model] : []) : (Array.isArray(saved.model_options) ? saved.model_options : (preset.model ? [preset.model] : []));
  syncModelSelect(standaloneModelOptions, els.model.value.trim());
  updateProviderCapabilities();
}

function syncForm() {
  document.body.dataset.mode = appMode;
  els.modeMax.classList.toggle("active", appMode === "max");
  els.modeStandalone.classList.toggle("active", appMode === "standalone");
  document.querySelectorAll(".max-only").forEach((el) => el.classList.toggle("hidden", appMode !== "max"));
  document.querySelectorAll(".standalone-only").forEach((el) => el.classList.toggle("hidden", appMode !== "standalone"));
  if (appMode === "standalone") {
    if (!standaloneProviderNames().includes(els.provider.value)) enterStandaloneConfig(collectConfigPatch());
    els.serverStatus.textContent = "独立 GPT 模式不依赖 3ds Max。";
    els.imageStatus.textContent = els.fileInput.files && els.fileInput.files.length ? els.imageStatus.textContent : t("aiPanelNoImages");
    updateSendBadge("", "");
    els.syncTime.textContent = "";
    updateProviderCapabilities();
    renderProviderManager();
    els.diagBox.textContent = standaloneDiagnosis;
    els.diagBox.classList.toggle("hidden", !diagVisible || !standaloneDiagnosis);
    renderMessages();
    return;
  }
  if (!state) return;
  const cfg = state.config || {};
  const formSig = JSON.stringify({
    provider: cfg.provider || "",
    api_type: cfg.api_type || "",
    base_url: cfg.base_url || "",
    model: cfg.model || "",
    temperature: cfg.temperature ?? 0.3,
    history: cfg.history ?? 8,
    robot_name: cfg.robot_name || "AI小助手",
    user_name: cfg.user_name || "用户",
    template: cfg.template || "常规问题"
  });
  if (!isEditingConfig && formSig !== lastFormSignature) {
    fillSelect(els.provider, state.provider_names || [cfg.provider || "OpenAI兼容接口"], cfg.provider || "");
    fillSelect(els.apiType, AI_API_TYPES, cfg.api_type || "OpenAI兼容");
    els.baseUrl.value = cfg.base_url || "";
    els.model.value = cfg.model || "";
    els.temperature.value = cfg.temperature ?? 0.3;
    els.history.value = cfg.history ?? 8;
    els.robotName.value = cfg.robot_name || "AI小助手";
    els.userName.value = cfg.user_name || "用户";
    fillSelect(els.template, AI_TEMPLATES, cfg.template || "常规问题");
    lastFormSignature = formSig;
  }
  els.serverStatus.textContent = "本地服务端口 " + currentPort + "，3ds Max 端 " + ((state.online) ? "已连接" : "未连接");
  els.stateProvider.textContent = t("aiPanelProvider") + ": " + (cfg.provider || "未设置");
  els.stateModel.textContent = t("aiPanelModel") + ": " + (cfg.model || "未设置");
  els.stateApi.textContent = t("aiPanelApiType") + ": " + (cfg.api_type || "未设置");
  els.sceneMeta.textContent = state.scene_summary_short || "";
  els.fontChip.textContent = "聊天字号 " + String(cfg.display_font_size || 8);
  els.editChip.textContent = "图片编辑模式：" + ((cfg.image_edit_mode) ? "开" : "关");
  els.portChip.textContent = "端口 " + String(currentPort);
  els.imageStatus.textContent = state.pending_image_text || t("aiPanelNoImages");
  setBusy(!!state.sending);
  if (state.sending) {
    updateSendBadge("busy", "发送中");
  } else if (state.last_error) {
    updateSendBadge("error", "请求失败");
  } else {
    updateSendBadge("ok", "空闲");
  }
  els.syncTime.textContent = lastSyncAt ? ("最近同步 " + lastSyncAt.toLocaleTimeString()) : "";
  const diag = state.diagnosis || "";
  els.diagBox.textContent = diag;
  els.diagBox.classList.toggle("hidden", !diagVisible || !diag);
  renderMessages();
}

async function api(path, payload) {
  const resp = await sendRuntimeMessage({ action: "aiProxy", path, payload }, path === "/api/ai/send" ? 15000 : 10000);
  currentPort = (resp && resp.port) || currentPort;
  if (!resp || resp.ok === false) throw new Error((resp && resp.error) || t("aiPanelServerOffline"));
  return resp.payload;
}

async function loadState() {
  if (appMode === "standalone") {
    syncForm();
    return;
  }
  const bridge = await sendRuntimeMessage({ action: "getAiState" }, 8000);
  currentPort = (bridge && bridge.port) || 19527;
  if (!bridge || bridge.ok === false) {
    throw new Error((bridge && bridge.error) || t("aiPanelServerOffline"));
  }
  if (!bridge.payload || bridge.payload.ok === false) {
    throw new Error((bridge.payload && bridge.payload.error) || t("aiPanelServerOffline"));
  }
  state = bridge.payload;
  lastSyncAt = new Date();
  if (state.config && state.config.provider && state.provider_names && !state.provider_names.includes(state.config.provider)) {
    state.provider_names.unshift(state.config.provider);
  }
  if (!state.sending) maxPendingMessage = null;
  syncForm();
}

function startAutoRefresh() {
  if (autoRefreshTimer) clearInterval(autoRefreshTimer);
  autoRefreshTimer = setInterval(async () => {
    if (appMode !== "max") return;
    try {
      await loadState();
    } catch (_e) {}
  }, 1800);
}

function saveStandaloneState(options = {}) {
  if (options.saveProviderConfig) saveCurrentStandaloneProviderConfig();
  if (options.setDefaultProvider) standaloneDefaultProvider = els.provider.value || standaloneDefaultProvider;
  const currentConfig = appMode === "standalone"
    ? collectConfigPatch()
    : (standaloneProviderConfigs[standaloneProviderKey(lastStandaloneProvider)] || {});
  const patch = {
    mode: appMode,
    default_provider: standaloneDefaultProvider,
    config: currentConfig,
    provider_configs: standaloneProviderConfigs,
    custom_providers: customStandaloneProviders,
    messages: standaloneMessages.slice(-80),
    diagnosis: standaloneDiagnosis,
    model_options: standaloneModelOptions
  };
  chrome.storage.local.set({ [STANDALONE_KEY]: patch });
}

async function loadStandaloneState() {
  const data = await new Promise((resolve) => chrome.storage.local.get([STANDALONE_KEY], resolve));
  const saved = data && data[STANDALONE_KEY] ? data[STANDALONE_KEY] : {};
  if (saved.mode === "standalone") appMode = "standalone";
  standaloneMessages = Array.isArray(saved.messages) ? saved.messages : [];
  standaloneDiagnosis = String(saved.diagnosis || "");
  standaloneProviderConfigs = saved.provider_configs && typeof saved.provider_configs === "object" ? saved.provider_configs : {};
  customStandaloneProviders = Array.isArray(saved.custom_providers) ? saved.custom_providers : [];
  standaloneModelOptions = Array.isArray(saved.model_options) ? saved.model_options : [];
  standaloneDefaultProvider = String(saved.default_provider || (saved.config && saved.config.provider) || "");
  const savedConfig = Object.assign({}, saved.config || {});
  if (standaloneDefaultProvider) savedConfig.provider = standaloneDefaultProvider;
  enterStandaloneConfig(savedConfig);
  lastStandaloneProvider = els.provider.value;
  if (appMode === "standalone") loadStandaloneProviderConfig(els.provider.value);
}

async function fetchStandaloneModels() {
  if (appMode !== "standalone") {
    setActionStatus("获取模型只在独立 GPT 模式可用", "error");
    return;
  }
  const cfg = collectConfigPatch();
  setActionStatus("正在获取模型列表…");
  const resp = await sendRuntimeMessage({ action: "independentAiModels", config: cfg }, 20000);
  if (!resp || resp.ok === false) {
    throw new Error((resp && resp.error) || "获取模型失败");
  }
  standaloneModelOptions = resp.models || [];
  if (standaloneModelOptions.length) {
    els.model.value = standaloneModelOptions.includes(els.model.value) ? els.model.value : standaloneModelOptions[0];
  }
  syncModelSelect(standaloneModelOptions, els.model.value.trim());
  saveCurrentStandaloneProviderConfig();
  setActionStatus("已获取 " + String((resp.models || []).length) + " 个模型", "ok");
  updateProviderCapabilities();
  saveStandaloneState();
}

async function runButtonTask(buttonId, task) {
  const btn = document.getElementById(buttonId);
  if (btn) btn.disabled = true;
  try {
    await task();
  } finally {
    if (btn) btn.disabled = false;
  }
}

async function diagnoseStandalone() {
  const cfg = collectConfigPatch();
  const lines = [
    "独立 GPT 诊断",
    "时间: " + new Date().toLocaleString(),
    "方案: " + (cfg.provider || "未选择"),
    "接口类型: " + (cfg.api_type || "未设置"),
    "Base URL: " + (cfg.base_url || "未填写"),
    "模型: " + (cfg.model || "未填写"),
    "图片模型: " + (cfg.image_model || "未填写"),
    "API Key: " + (cfg.api_key ? "已填写" : "未填写"),
    "图片能力: OpenAI兼容接口可发送图片；Ollama 当前仅发送文本。"
  ];
  setActionStatus("正在诊断独立 GPT…");
  const test = await sendRuntimeMessage({ action: "independentAiTest", config: cfg }, 25000);
  lines.push("连接测试: " + (test && test.ok ? (test.message || "成功") : ("失败 - " + ((test && test.error) || "未知错误"))));
  const models = await sendRuntimeMessage({ action: "independentAiModels", config: cfg }, 20000);
  lines.push("模型列表: " + (models && models.ok ? ("获取到 " + String((models.models || []).length) + " 个") : ("失败 - " + ((models && models.error) || "未知错误"))));
  standaloneDiagnosis = lines.join("\n");
  diagVisible = true;
  saveStandaloneState();
  syncForm();
  setActionStatus("独立 GPT 诊断完成", test && test.ok ? "ok" : "error");
}

function collectConfigPatch() {
  return {
    provider: els.provider.value.trim(),
    api_type: els.apiType.value.trim(),
    base_url: els.baseUrl.value.trim(),
    model: els.model.value.trim(),
    image_model: els.imageModel ? els.imageModel.value.trim() : "",
    image_api_type: els.imageApiType ? els.imageApiType.value : "auto",
    api_key: els.apiKey.value,
    temperature: Number(els.temperature.value || 0.3),
    history: Number(els.history.value || 8),
    robot_name: els.robotName.value.trim(),
    user_name: els.userName.value.trim(),
    template: els.template.value.trim(),
    search_api_url: els.searchApiUrl ? els.searchApiUrl.value.trim() : "",
    search_api_key: els.searchApiKey ? els.searchApiKey.value : ""
  };
}

async function saveConfig() {
  if (appMode === "standalone") {
    saveStandaloneState({ saveProviderConfig: true, setDefaultProvider: true });
    setActionStatus("独立 GPT 配置已保存", "ok");
    return;
  }
  setActionStatus("正在保存配置…");
  updateSendBadge("busy", "保存中");
  setActionStatus("Max linked AI config is only editable in 3ds Max.", "error");
  return;
  await api("/api/ai/config", collectConfigPatch());
  els.apiKey.value = "";
  await loadState();
  setActionStatus(t("aiPanelConfigSaved"), "ok");
}

async function sendMessage() {
  if (appMode === "max" && state && state.sending) return;
  const text = els.prompt.value.trim();
  if (!text) {
    setActionStatus(t("aiPanelNeedQuestion"), "error");
    return;
  }
  setActionStatus("正在发送…");
  updateSendBadge("busy", "发送中");
  const files = Array.from(els.fileInput.files || []);
  let images = [];
  if (files.length) {
    setUploadProgress(8, "正在读取图片…");
    for (let i = 0; i < files.length; i++) {
      setUploadProgress(10 + Math.round((i / files.length) * 35), "正在压缩图片 " + String(i + 1) + "/" + String(files.length) + "…");
      images.push(await readImageCompressed(files[i]));
    }
    setUploadProgress(55, "图片已压缩，正在发送…");
  }
  if (appMode === "standalone") {
    const cfg = collectConfigPatch();
    const caps = inferModelCapabilities(cfg.model, cfg.provider);
    const imageCaps = inferModelCapabilities(cfg.image_model || cfg.model, cfg.provider);
    const hasImageModel = imageModelIsUsable(cfg.image_model);
    const useImageApi = (hasImageModel || !!imageCaps.imageEdit || !!caps.imageEdit) && (images.length > 0 || wantsImageOutput(text, images.length));
    if ((caps.imageEdit || imageCaps.imageEdit) && !useImageApi && !caps.vision && images.length === 0) {
      standaloneMessages.push({ role: "user", content: text, images });
      standaloneMessages.push({ role: "system", content: "当前选择的是图片生成/编辑模型，不适合普通聊天。请切换到文本模型，例如 Qwen、DeepSeek、GPT-4o mini，或输入明确的生图/改图需求。" });
      els.prompt.value = "";
      resizePrompt();
      els.fileInput.value = "";
      updateSendBadge("error", "模型不匹配");
      saveStandaloneState();
      renderMessages();
      setUploadProgress(100, "");
      return;
    }
    standaloneMessages.push({ role: "user", content: text, images });
    const pendingId = "pending_" + Date.now();

    let searchContext = "";
    if (searchEnabled && !useImageApi) {
      standaloneMessages.push({ id: pendingId, role: "assistant", content: "🔍 正在搜索网络…" });
      renderMessages();
      const searchCfg = collectConfigPatch();
      setUploadProgress(0, "正在搜索: " + text.slice(0, 40) + "...");
      const searchResp = await sendRuntimeMessage({ action: "webSearch", query: text, config: searchCfg }, 15000);
      if (searchResp && searchResp.ok && searchResp.results) {
        searchContext = "\n\n以下是与用户问题相关的网络搜索结果，请基于这些信息回答：\n\n" + searchResp.results + "\n\n请根据以上搜索结果回答用户的问题。如果搜索结果不足以回答问题，请如实告知。";
        standaloneMessages[standaloneMessages.length - 1] = { id: pendingId, role: "assistant", content: "🔍 已搜索网络，找到以下参考信息：\n\n" + searchResp.results };
      } else {
        standaloneMessages[standaloneMessages.length - 1] = { id: pendingId, role: "system", content: "🔍 搜索完成，未找到相关结果。" };
      }
      renderMessages();
      setUploadProgress(0, searchContext ? "搜索完成，正在请求 AI…" : "搜索无结果，正在请求 AI…");
    }

    standaloneMessages.push({ id: pendingId + "_think", role: "assistant", content: useImageApi ? "正在生成图片…" : (images.length ? "正在理解图片…" : "正在思考…") });
    els.prompt.value = "";
    resizePrompt();
    els.fileInput.value = "";
    renderMessages();
    const action = useImageApi ? "independentAiImage" : "independentAiChat";
    setUploadProgress(images.length ? 70 : 0, useImageApi ? "正在请求图片接口…" : "正在请求 AI…");

    const chatMessages = standaloneMessages.filter((m) => !m.id && m.role !== "system").slice(-Math.max(1, Number(cfg.history || 8)) * 2);
    if (searchContext && chatMessages.length > 0) {
      chatMessages[chatMessages.length - 1] = {
        role: chatMessages[chatMessages.length - 1].role,
        content: (chatMessages[chatMessages.length - 1].content || "") + searchContext
      };
    }

    const resp = await sendRuntimeMessage(useImageApi ? {
      action,
      config: cfg,
      prompt: text,
      images
    } : {
      action,
      config: cfg,
      messages: chatMessages
    }, useImageApi ? 90000 : 65000);
    if (!resp || resp.ok === false) {
      const errText = (resp && resp.error) || "未知错误";
      if (useImageApi && /HTTP 404/i.test(errText) && caps.vision) {
        const fallback = await sendRuntimeMessage({
          action: "independentAiChat",
          config: cfg,
          messages: chatMessages
        }, 65000);
        if (fallback && fallback.ok) {
          replaceMessageById(pendingId + "_think", { content: "图片接口不可用，已自动改用聊天/看图接口。\n\n" + (fallback.text || ""), images: fallback.images || [] });
          updateSendBadge("ok", "空闲");
        } else {
          replaceMessageById(pendingId + "_think", { role: "system", content: "独立图片请求失败：\n" + errText + "\n\n回退聊天接口也失败：\n" + ((fallback && fallback.error) || "未知错误") });
          updateSendBadge("error", "请求失败");
        }
      } else if (useImageApi && /HTTP 404/i.test(errText) && !caps.vision) {
        replaceMessageById(pendingId + "_think", { role: "system", content: "独立图片请求失败：\n" + errText + "\n\n当前聊天模型不是看图模型，所以不再回退到聊天接口。请检查「图片模型名」和「图片接口格式」：OpenAI 通常用 gpt-image-1 + OpenAI 图片接口；硅基流动通常用 Kwai-Kolors/Kolors + 硅基流动图片接口。" });
        updateSendBadge("error", "图片接口不可用");
      } else {
        replaceMessageById(pendingId + "_think", { role: "system", content: (useImageApi ? "独立图片请求失败：\n" : "独立 GPT 请求失败：\n") + errText });
        updateSendBadge("error", "请求失败");
      }
    } else {
      replaceMessageById(pendingId + "_think", { content: resp.text || "AI没有返回内容", images: resp.images || [], thinking: resp.thinking || "" });
      updateSendBadge("ok", "空闲");
    }
    saveStandaloneState();
    renderMessages();
    setUploadProgress(100, "");
    return;
  }
  state = await api("/api/ai/send", { text, images });
  maxPendingMessage = { role: "assistant", content: images.length ? "正在理解图片…" : "正在思考…" };
  els.prompt.value = "";
  resizePrompt();
  els.fileInput.value = "";
  lastMessageSignature = "";
  syncForm();
  setActionStatus("正在发送…");
  if (sendPollTimer) clearInterval(sendPollTimer);
  sendPollTimer = setInterval(async () => {
    try {
      await loadState();
      if (!state || !state.sending) {
        maxPendingMessage = null;
        clearInterval(sendPollTimer);
        sendPollTimer = null;
        setActionStatus(state && state.last_error ? state.last_error : t("aiPanelSendDone"), state && state.last_error ? "error" : "ok");
        syncForm();
      }
    } catch (e) {
      maxPendingMessage = null;
      clearInterval(sendPollTimer);
      sendPollTimer = null;
      setActionStatus(String(e), "error");
      updateSendBadge("error", "请求失败");
    }
  }, 1200);
  setUploadProgress(100, "");
}

document.getElementById("btn-refresh").addEventListener("click", () => loadState().catch((e) => setActionStatus(String(e), "error")));
document.getElementById("btn-sync-state").addEventListener("click", async () => {
  setActionStatus("正在同步消息…");
  updateSendBadge("busy", "同步中");
  await loadState();
  setActionStatus(t("aiPanelStateSynced"), "ok");
});
document.getElementById("btn-save-config").addEventListener("click", () => runButtonTask("btn-save-config", () => saveConfig()).catch((e) => setActionStatus(String(e), "error")));
document.getElementById("btn-api-key").addEventListener("click", openStandaloneApiKeyPage);
document.getElementById("btn-reset-provider").addEventListener("click", () => {
  if (appMode !== "standalone") {
    setActionStatus("重置当前方案只用于独立 GPT 模式", "error");
    return;
  }
  const key = currentStandaloneConfigKey();
  delete standaloneProviderConfigs[key];
  loadStandaloneProviderConfig(els.provider.value, { resetToPreset: true });
  saveStandaloneState({ saveProviderConfig: true });
  setActionStatus("已恢复当前方案的默认 URL 和模型", "ok");
});
document.getElementById("btn-reset-all-providers").addEventListener("click", () => {
  if (appMode !== "standalone") {
    setActionStatus("重置所有方案只用于独立 GPT 模式。", "error");
    return;
  }
  standaloneProviderConfigs = {};
  standaloneDefaultProvider = "";
  standaloneModelOptions = [];
  standaloneDiagnosis = "";
  enterStandaloneConfig({});
  lastStandaloneProvider = els.provider.value;
  saveStandaloneState({ saveProviderConfig: true, setDefaultProvider: true });
  syncForm();
  setActionStatus("已重置所有独立 GPT 方案配置，用户添加的平台已保留。", "ok");
});
document.getElementById("btn-fetch-models").addEventListener("click", () => runButtonTask("btn-fetch-models", () => fetchStandaloneModels()).catch((e) => setActionStatus(String(e), "error")));
document.getElementById("btn-open-image-editor").addEventListener("click", openImageEditor);
document.getElementById("btn-close-image-editor").addEventListener("click", closeImageEditor);
document.getElementById("btn-image-editor-use-chat").addEventListener("click", () => {
  const count = els.fileInput.files ? els.fileInput.files.length : 0;
  els.imageEditorStatus.textContent = count ? ("将使用聊天输入里的 " + String(count) + " 张图片。") : "聊天输入里还没有图片。";
});
document.getElementById("btn-run-image-editor").addEventListener("click", () => runImageEditor().catch((e) => {
  setUploadProgress(100, "");
  if (els.imageEditorStatus) els.imageEditorStatus.textContent = String(e);
  setActionStatus(String(e), "error");
}));
document.querySelectorAll("[data-image-preset]").forEach((btn) => {
  btn.addEventListener("click", () => applyImageEditPreset(btn.getAttribute("data-image-preset")));
});
document.getElementById("btn-add-provider").addEventListener("click", addCustomProvider);
document.getElementById("btn-open-provider-add").addEventListener("click", () => {
  els.providerAddModal.classList.remove("hidden");
});
document.getElementById("btn-close-provider-add").addEventListener("click", () => {
  els.providerAddModal.classList.add("hidden");
});
document.getElementById("btn-add-provider-modal").addEventListener("click", addCustomProvider);
els.providerList.addEventListener("click", (e) => {
  const openBtn = e.target && e.target.closest ? e.target.closest("[data-provider-open]") : null;
  if (openBtn) {
    e.preventDefault();
    openProviderPage(openBtn.getAttribute("data-provider-open") || "");
    return;
  }
  const keyBtn = e.target && e.target.closest ? e.target.closest("[data-provider-key]") : null;
  if (keyBtn) {
    e.preventDefault();
    const url = standaloneApiKeyUrl(keyBtn.getAttribute("data-provider-key") || "");
    if (url) chrome.tabs.create({ url });
    else openProviderPage(keyBtn.getAttribute("data-provider-key") || "");
    return;
  }
  const selectBtn = e.target && e.target.closest ? e.target.closest("[data-provider-select]") : null;
  const card = selectBtn || (e.target && e.target.closest ? e.target.closest("[data-provider-name]") : null);
  if (!card) return;
  const name = card.getAttribute("data-provider-select") || card.getAttribute("data-provider-name") || "";
  if (lastStandaloneProvider) {
    saveStandaloneProviderByName(lastStandaloneProvider, collectConfigPatchForProvider(lastStandaloneProvider));
  }
  fillSelect(els.provider, standaloneProviderNames(), name);
  switchingStandaloneProvider = true;
  loadStandaloneProviderConfig(name, { resetToPreset: false });
  switchingStandaloneProvider = false;
  lastStandaloneProvider = name;
  saveStandaloneState({ saveProviderConfig: true });
  updateImageEditorSummary();
});
els.btnWebSearch.addEventListener("click", () => {
  searchEnabled = !searchEnabled;
  els.btnWebSearch.classList.toggle("web-search-active", searchEnabled);
  els.btnWebSearch.title = searchEnabled ? "联网搜索已开启" : "联网搜索已关闭";
  setActionStatus(searchEnabled ? "联网搜索已开启" : "联网搜索已关闭", "ok");
});

document.getElementById("btn-send").addEventListener("click", () => sendMessage().catch((e) => {
  setUploadProgress(100, "");
  setActionStatus(String(e), "error");
}));
document.getElementById("btn-scene-summary").addEventListener("click", async () => {
  setActionStatus("正在插入场景摘要…");
  state = await api("/api/ai/scene_summary");
  syncForm();
  setActionStatus("已插入场景摘要", "ok");
});
document.getElementById("btn-recent-log").addEventListener("click", async () => {
  setActionStatus("正在插入最近日志…");
  state = await api("/api/ai/recent_log");
  syncForm();
  setActionStatus("已插入最近日志", "ok");
});
const btnToggleEdit = document.getElementById("btn-toggle-edit");
if (btnToggleEdit) {
  btnToggleEdit.addEventListener("click", async () => {
    setActionStatus("Max linked image edit mode is only editable in 3ds Max.", "error");
  });
}

async function toggleImageEditMode() {
  state = await api("/api/ai/toggle_image_edit");
  syncForm();
  setActionStatus("图片编辑模式已切换", "ok");
}
document.getElementById("btn-clear-chat").addEventListener("click", () => runButtonTask("btn-clear-chat", async () => {
  if (appMode === "standalone") {
    standaloneMessages = [];
    lastMessageSignature = "";
    saveStandaloneState();
    syncForm();
    setActionStatus("独立 GPT 对话已清空", "ok");
    return;
  }
  state = await api("/api/ai/clear");
  lastMessageSignature = "";
  syncForm();
  setActionStatus("对话已清空", "ok");
}).catch((e) => setActionStatus(String(e), "error")));

els.modeMax.addEventListener("click", async () => {
  appMode = "max";
  saveStandaloneState({ saveProviderConfig: true });
  await loadState().catch((e) => setActionStatus(String(e), "error"));
  syncForm();
});

els.modeStandalone.addEventListener("click", () => {
  appMode = "standalone";
  enterStandaloneConfig({});
  loadStandaloneProviderConfig(els.provider.value);
  saveStandaloneState();
  syncForm();
});

[els.apiType, els.baseUrl, els.model, els.imageModel, els.imageApiType, els.apiKey, els.temperature, els.history, els.robotName, els.userName, els.template, els.searchApiUrl, els.searchApiKey].filter(Boolean).forEach((el) => {
  if (!el) return;
  el.addEventListener("focus", () => { isEditingConfig = true; });
  el.addEventListener("blur", () => { setTimeout(() => { isEditingConfig = false; }, 200); });
  el.addEventListener("change", () => {
  if (appMode === "standalone") {
    saveStandaloneState({ saveProviderConfig: true });
    updateImageEditorSummary();
    if (el === els.robotName || el === els.userName) {
      lastMessageSignature = "";
      renderMessages();
    }
  }
  });
});
els.provider.addEventListener("change", () => {
  if (appMode !== "standalone") return;
  if (lastStandaloneProvider) {
    saveStandaloneProviderByName(lastStandaloneProvider, collectConfigPatchForProvider(lastStandaloneProvider));
  }
  switchingStandaloneProvider = true;
  loadStandaloneProviderConfig(els.provider.value, { resetToPreset: false });
  switchingStandaloneProvider = false;
  lastStandaloneProvider = els.provider.value;
  saveStandaloneState({ saveProviderConfig: true });
  updateImageEditorSummary();
  setActionStatus("已切换方案，URL 和模型已按当前方案加载", "ok");
});
els.modelSelect.addEventListener("change", () => {
  if (appMode !== "standalone") return;
  els.model.value = els.modelSelect.value;
  const selectedCaps = inferModelCapabilities(els.modelSelect.value, els.provider.value);
  syncImageModelForCurrentModel(!!selectedCaps.imageEdit);
  saveCurrentStandaloneProviderConfig();
  saveStandaloneState({ saveProviderConfig: true });
  updateProviderCapabilities();
});
[els.apiType, els.baseUrl, els.model].forEach((el) => {
  if (!el) return;
  el.addEventListener("input", () => {
    if (appMode === "standalone") {
      if (el === els.model) syncImageModelForCurrentModel(false);
      updateProviderCapabilities();
    }
  });
});
document.getElementById("btn-test").addEventListener("click", () => runButtonTask("btn-test", async () => {
  if (appMode === "standalone") {
    setActionStatus("正在测试独立 GPT 连接…");
    const resp = await sendRuntimeMessage({ action: "independentAiTest", config: collectConfigPatch() }, 25000);
    if (!resp || resp.ok === false) throw new Error((resp && resp.error) || "连接失败");
    setActionStatus(resp.message || "连接成功", "ok");
    saveStandaloneState();
    return;
  }
  setActionStatus("正在测试连接…");
  state = await api("/api/ai/test");
  syncForm();
  setActionStatus("连接测试完成", "ok");
}).catch((e) => setActionStatus(String(e), "error")));
document.getElementById("btn-diagnose").addEventListener("click", () => runButtonTask("btn-diagnose", async () => {
  if (appMode === "standalone") {
    await diagnoseStandalone();
    return;
  }
  setActionStatus("正在完整诊断…");
  state = await api("/api/ai/diagnose");
  diagVisible = true;
  syncForm();
  setActionStatus("完整诊断完成", "ok");
}).catch((e) => setActionStatus(String(e), "error")));

document.getElementById("btn-toggle-diag").addEventListener("click", () => {
  diagVisible = !diagVisible;
  syncForm();
});

document.getElementById("btn-sidebar-toggle").addEventListener("click", () => {
  document.body.classList.toggle("sidebar-collapsed");
});

if (els.toggleStandaloneConfig) {
  els.toggleStandaloneConfig.addEventListener("click", () => {
    const collapsed = !els.standaloneConfigSection.classList.contains("collapsed");
    setStandaloneConfigCollapsed(collapsed);
  });
}

document.getElementById("btn-more-actions").addEventListener("click", (e) => {
  e.stopPropagation();
  els.actionMenu.classList.toggle("open");
});

els.actionMenu.addEventListener("click", (e) => {
  if (e.target && e.target.tagName === "BUTTON") {
    els.actionMenu.classList.remove("open");
  }
});

document.addEventListener("click", () => {
  els.actionMenu.classList.remove("open");
});

els.messages.addEventListener("click", (e) => {
  const smart = e.target && e.target.closest ? e.target.closest("[data-smart-action]") : null;
  if (smart) {
    e.preventDefault();
    const type = smart.getAttribute("data-smart-action") || "";
    const value = smart.getAttribute("data-smart-value") || "";
    if (type === "url") {
      chrome.tabs.create({ url: value });
      setActionStatus("已打开网站", "ok");
    } else if (type === "copy") {
      navigator.clipboard.writeText(value).then(() => setActionStatus("已复制", "ok")).catch((err) => setActionStatus(String(err), "error"));
    }
    return;
  }
  const reedit = e.target && e.target.closest ? e.target.closest(".image-reedit") : null;
  if (reedit) {
    e.preventDefault();
    openImageEditorWithImage(reedit.getAttribute("data-src") || "");
    return;
  }
  const btn = e.target && e.target.closest ? e.target.closest(".image-open") : null;
  if (!btn) return;
  e.preventDefault();
  const src = btn.getAttribute("data-src") || "";
  if (!src) return;
  els.imagePreviewImg.src = src;
  els.imagePreview.classList.remove("hidden");
});

els.imagePreview.addEventListener("click", () => {
  els.imagePreview.classList.add("hidden");
  els.imagePreviewImg.src = "";
});

els.imageEditorModal.addEventListener("click", (e) => {
  if (e.target === els.imageEditorModal) closeImageEditor();
});

els.providerAddModal.addEventListener("click", (e) => {
  if (e.target === els.providerAddModal) els.providerAddModal.classList.add("hidden");
});

els.prompt.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && !e.shiftKey) {
    e.preventDefault();
    sendMessage().catch((err) => setActionStatus(String(err), "error"));
  }
});
els.prompt.addEventListener("input", resizePrompt);

els.fileInput.addEventListener("change", () => {
  const files = Array.from(els.fileInput.files || []);
  els.imageStatus.textContent = files.length ? (t("aiPanelPendingImages") + files.map((f) => f.name).join("；")) : t("aiPanelNoImages");
});

applyI18n();
applyBrowserTheme();
resizePrompt();
loadStandaloneState().then(() => {
  if (appMode === "max") {
    return loadState().catch((e) => setActionStatus(String(e), "error"));
  }
  syncForm();
});
try {
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", applyBrowserTheme);
} catch (_e) {}
startAutoRefresh();
