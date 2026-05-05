const DEFAULT_MAX_PORT = 19527;
const DEFAULT_UE_PORT = 19528;
const DEFAULT_PORT = DEFAULT_MAX_PORT;

function t(key, substitutions) {
  const value = chrome.i18n.getMessage(key, substitutions);
  return value || key;
}

function validatePort(port) {
  const n = Number(port);
  if (!Number.isInteger(n)) return t("portMustBeInteger");
  if (n < 1025 || n > 65535) return t("portRangeError");
  if ([80, 443, 3306, 3389].includes(n)) return t("portReservedError");
  return "";
}

function getSettings() {
  return new Promise((resolve) => {
    chrome.storage.local.get([
      "serverPort", "pendingQueue", "serverOnline", "lastCheckedAt", "lastTriedPorts",
      "ueServerPort", "uePendingQueue", "ueServerOnline", "ueLastCheckedAt", "ueLastTriedPorts",
      "pushTarget"
    ], (data) => {
      resolve({
        port: Number(data.serverPort || DEFAULT_MAX_PORT),
        pendingQueue: data.pendingQueue || [],
        serverOnline: !!data.serverOnline,
        lastCheckedAt: Number(data.lastCheckedAt || 0),
        lastTriedPorts: Array.isArray(data.lastTriedPorts) ? data.lastTriedPorts : [],
        uePort: Number(data.ueServerPort || DEFAULT_UE_PORT),
        uePendingQueue: data.uePendingQueue || [],
        ueServerOnline: !!data.ueServerOnline,
        ueLastCheckedAt: Number(data.ueLastCheckedAt || 0),
        ueLastTriedPorts: Array.isArray(data.ueLastTriedPorts) ? data.ueLastTriedPorts : [],
        pushTarget: data.pushTarget || "max",
      });
    });
  });
}

function setSettings(patch) {
  return new Promise((resolve) => chrome.storage.local.set(patch, resolve));
}

function serverUrl(port) {
  return "http://127.0.0.1:" + Number(port || DEFAULT_PORT);
}

async function fetchWithTimeout(url, options = {}, timeoutMs = 20000) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, { ...options, signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

async function pingPort(port) {
  try {
    const resp = await fetchWithTimeout(serverUrl(port) + "/ping", { method: "GET" }, 2500);
    const data = await resp.json().catch(() => ({}));
    return !!(resp.ok && data && data.status === "ok");
  } catch (_e) {
    return false;
  }
}

function candidatePorts(preferredPort) {
  return [...new Set([Number(preferredPort || 0), DEFAULT_PORT, 19527, 19528, 19529].filter(Boolean))];
}

function targetPort(settings, target) {
  return target === "ue" ? Number(settings.uePort || DEFAULT_UE_PORT) : Number(settings.port || DEFAULT_MAX_PORT);
}

function pendingKey(target) {
  return target === "ue" ? "uePendingQueue" : "pendingQueue";
}

function onlineKey(target) {
  return target === "ue" ? "ueServerOnline" : "serverOnline";
}

function checkedAtKey(target) {
  return target === "ue" ? "ueLastCheckedAt" : "lastCheckedAt";
}

function triedPortsKey(target) {
  return target === "ue" ? "ueLastTriedPorts" : "lastTriedPorts";
}

function portKey(target) {
  return target === "ue" ? "ueServerPort" : "serverPort";
}

function targetName(target) {
  return target === "ue" ? "UE" : "3ds Max";
}

async function discoverServerPort(preferredPort) {
  const candidates = candidatePorts(preferredPort);
  for (const port of candidates) {
    if (await pingPort(port)) {
      await setSettings({ serverPort: Number(port), serverOnline: true, lastCheckedAt: Date.now(), lastTriedPorts: candidates });
      return Number(port);
    }
  }
  await setSettings({ serverOnline: false, lastCheckedAt: Date.now(), lastTriedPorts: candidates });
  return Number(preferredPort || DEFAULT_PORT);
}

function normalizeDownloadUrl(url) {
  if (!url || typeof url !== "string") return "";
  try {
    const normalized = new URL(url).href;
    if (!/^https?:/i.test(normalized)) return "";
    return normalized;
  } catch (_e) {
    return "";
  }
}

function isEphemeralPolyhavenDownload(url) {
  const normalized = normalizeDownloadUrl(url);
  if (!normalized) return false;
  try {
    const parsed = new URL(normalized);
    return /(^|\.)polyhaven\.com$/i.test(parsed.hostname) && /\/__download__\//i.test(parsed.pathname);
  } catch (_e) {
    return false;
  }
}

chrome.runtime.onInstalled.addListener(async () => {
  chrome.contextMenus.create({
    id: "pushToPBR",
    title: t("menuPushLink"),
    contexts: ["link"]
  });
  chrome.contextMenus.create({
    id: "pushToUEPBR",
    title: "推送到 UE PBR 队列",
    contexts: ["link"]
  });
  chrome.contextMenus.create({
    id: "pushPageLinks",
    title: t("menuPushPageLinks"),
    contexts: ["page"]
  });
  chrome.contextMenus.create({
    id: "pushPageLinksUE",
    title: "推送本页下载链接到 UE",
    contexts: ["page"]
  });
});

chrome.downloads.onCreated.addListener(async (item) => {
  try {
    const url = normalizeDownloadUrl(item && item.finalUrl ? item.finalUrl : item && item.url);
    if (!url) return;
    if (isEphemeralPolyhavenDownload(url)) return;
    const settings = await getSettings();
    const target = settings.pushTarget || "max";
    const isOnline = target === "ue" ? settings.ueServerOnline : settings.serverOnline;
    if (!isOnline) return;
    const result = await pushUrls([url], { target });
    if (result && result.ok) {
      showNotification(t("capturedBrowserDownload"));
    }
  } catch (_e) {}
});

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  if (info.menuItemId === "pushToPBR" && info.linkUrl) {
    await pushUrls([info.linkUrl], { target: "max" });
  } else if (info.menuItemId === "pushToUEPBR" && info.linkUrl) {
    await pushUrls([info.linkUrl], { target: "ue" });
  } else if (info.menuItemId === "pushPageLinks" || info.menuItemId === "pushPageLinksUE") {
    if (!tab || typeof tab.id !== "number") {
      showNotification(t("noActiveTab"));
      return;
    }
    chrome.tabs.sendMessage(tab.id, { action: "collectLinks" }, async (resp) => {
      if (chrome.runtime.lastError) {
        showNotification(t("couldNotScanPage"));
        return;
      }
      if (resp && resp.urls && resp.urls.length > 0) {
        await pushUrls(resp.urls, { target: info.menuItemId === "pushPageLinksUE" ? "ue" : "max" });
      } else {
        showNotification(t("noDownloadableLinksOnPage"));
      }
    });
  }
});

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg.action === "push") {
    pushUrls(msg.urls || [], { target: msg.target || "max" }).then(sendResponse);
    return true;
  }
  if (msg.action === "pushAndDownloadNow") {
    pushUrls(msg.urls || [], { autoStart: true, target: msg.target || "max" }).then(sendResponse);
    return true;
  }
  if (msg.action === "getStatus") {
    getSettings().then((data) => {
      const target = msg.target || data.pushTarget || "max";
      const pending = target === "ue" ? data.uePendingQueue : data.pendingQueue;
      sendResponse({
        online: target === "ue" ? data.ueServerOnline : data.serverOnline,
        pending: pending.length,
        pendingList: pending,
        port: targetPort(data, target),
        target,
        lastCheckedAt: target === "ue" ? data.ueLastCheckedAt : data.lastCheckedAt,
        triedPorts: target === "ue" ? [targetPort(data, target)] : [targetPort(data, target)]
      });
    });
    return true;
  }
  if (msg.action === "retryPending") {
    retryPendingQueue(msg.target || "max").then(sendResponse);
    return true;
  }
  if (msg.action === "clearPending") {
    setSettings({ [pendingKey(msg.target || "max")]: [] }).then(() => sendResponse({ ok: true }));
    return true;
  }
  if (msg.action === "setPort") {
    const err = validatePort(msg.port);
    if (err) {
      sendResponse({ ok: false, message: err });
      return true;
    }
    const target = msg.target || "max";
    setSettings({ [portKey(target)]: Number(msg.port), [onlineKey(target)]: false, pushTarget: target }).then(() => {
      sendResponse({
        ok: true,
        message: targetName(target) + " 端口已保存为 " + String(Number(msg.port))
      });
    });
    return true;
  }
  if (msg.action === "checkPort") {
    const err = validatePort(msg.port);
    if (err) {
      sendResponse({ ok: false, message: err });
      return true;
    }
    const target = msg.target || "max";
    const url = serverUrl(msg.port) + "/ping";
    fetch(url, { method: "GET" }).then(async (resp) => {
      const data = await resp.json().catch(() => ({}));
      const ok = !!(data && data.status === "ok");
      await setSettings({
        [portKey(target)]: Number(msg.port),
        [onlineKey(target)]: ok,
        [checkedAtKey(target)]: Date.now(),
        [triedPortsKey(target)]: [Number(msg.port)],
        pushTarget: target
      });
      if (ok) {
        try {
          const tabs = await chrome.tabs.query({});
          for (const tab of tabs) {
            if (tab.id) {
              chrome.tabs.sendMessage(tab.id, { action: "recheckOnline" }).catch(() => {});
            }
          }
        } catch (_) {}
      }
      sendResponse({
        ok,
        message: ok
          ? targetName(target) + " 端口 " + String(Number(msg.port)) + " 可访问"
          : targetName(target) + " 端口 " + String(Number(msg.port)) + " 没有返回有效响应"
      });
    }).catch(() => {
      setSettings({
        [portKey(target)]: Number(msg.port),
        [onlineKey(target)]: false,
        [checkedAtKey(target)]: Date.now(),
        [triedPortsKey(target)]: [Number(msg.port)],
        pushTarget: target
      }).then(() => sendResponse({ ok: false, message: targetName(target) + " 端口 " + String(Number(msg.port)) + " 无法访问" }));
    });
    return true;
  }
  if (msg.action === "setTarget") {
    setSettings({ pushTarget: msg.target === "ue" ? "ue" : "max" }).then(() => sendResponse({ ok: true }));
    return true;
  }
  if (msg.action === "getAiState") {
    getSettings().then(async (data) => {
      try {
        const port = await discoverServerPort(data.port);
        const resp = await fetchWithTimeout(serverUrl(port) + "/api/ai/state", { method: "GET" }, 8000);
        const payload = await resp.json().catch(() => ({}));
        sendResponse({
          ok: !!resp.ok,
          port,
          online: await pingPort(port),
          payload
        });
      } catch (e) {
        sendResponse({
          ok: false,
          port: data.port,
          online: false,
          error: String(e)
        });
      }
    });
    return true;
  }
  if (msg.action === "aiProxy") {
    proxyAiRequest(msg.path, msg.payload).then(sendResponse);
    return true;
  }
  if (msg.action === "independentAiChat") {
    independentAiChat(msg.config || {}, msg.messages || []).then(sendResponse);
    return true;
  }
  if (msg.action === "independentAiImage") {
    independentAiImage(msg.config || {}, msg.prompt || "", msg.images || []).then(sendResponse);
    return true;
  }
  if (msg.action === "independentAiModels") {
    independentAiModels(msg.config || {}).then(sendResponse);
    return true;
  }
  if (msg.action === "independentAiTest") {
    independentAiTest(msg.config || {}).then(sendResponse);
    return true;
  }
  if (msg.action === "webSearch") {
    webSearch(msg.query || "", msg.config || {}).then(sendResponse);
    return true;
  }
});

async function proxyAiRequest(path, payload) {
  const settings = await getSettings();
  const port = await discoverServerPort(settings.port);
  try {
    const resp = await fetchWithTimeout(serverUrl(port) + path, {
      method: payload ? "POST" : "GET",
      headers: payload ? { "Content-Type": "application/json" } : undefined,
      body: payload ? JSON.stringify(payload) : undefined
    }, path === "/api/ai/send" ? 12000 : 8000);
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok || data.ok === false) {
      throw new Error(data.error || data.message || ("HTTP " + resp.status));
    }
    await setSettings({ serverPort: port, serverOnline: true, lastCheckedAt: Date.now() });
    return { ok: true, port, payload: data };
  } catch (e) {
    await setSettings({ serverOnline: false, lastCheckedAt: Date.now() });
    return { ok: false, port, error: String(e && e.name === "AbortError" ? "请求超时，请确认 3ds Max 本地桥接服务正在运行。" : e) };
  }
}

function normalizeBaseUrl(baseUrl) {
  return String(baseUrl || "").trim().replace(/\/+$/, "");
}

function imageEndpoint(baseUrl, kind) {
  const base = normalizeBaseUrl(baseUrl);
  if (/\/images\/(edits|generations)$/i.test(base)) return base;
  if (/siliconflow\.cn/i.test(base)) return base + "/images/generations";
  return base + (kind === "edit" ? "/images/edits" : "/images/generations");
}

function dataUrlToBlob(dataUrl) {
  const text = String(dataUrl || "");
  const m = text.match(/^data:([^;,]+)?(;base64)?,(.*)$/);
  if (!m) return null;
  const mime = m[1] || "image/png";
  const raw = m[2] ? atob(m[3]) : decodeURIComponent(m[3]);
  const bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
  return new Blob([bytes], { type: mime });
}

function extractImagesFromText(text) {
  let cleaned = String(text || "");
  const images = [];
  cleaned = cleaned.replace(/data:image\/(png|jpe?g|webp|gif);base64,[A-Za-z0-9+/=]+/gi, (m) => {
    images.push({ name: "image_" + String(images.length + 1) + ".png", url: m, thumb: m });
    return "[图片已提取]";
  });
  cleaned = cleaned.replace(/!\[[^\]]*]\((data:image\/[^)]+)\)/gi, (_m, url) => {
    images.push({ name: "image_" + String(images.length + 1) + ".png", url, thumb: url });
    return "[图片已提取]";
  });
  cleaned = cleaned.replace(/\b([A-Za-z0-9+/]{800,}={0,2})\b/g, (m) => {
    if (m.length < 800) return m;
    const url = "data:image/png;base64," + m;
    images.push({ name: "image_" + String(images.length + 1) + ".png", url, thumb: url });
    return "[图片base64已提取]";
  });
  return { text: cleaned.trim(), images };
}

function supportsChatImageUrl(config) {
  const baseUrl = normalizeBaseUrl(config.base_url);
  const model = String(config.model || "").toLowerCase();
  const mode = String(config.chat_image_mode || "auto").toLowerCase();
  if (mode === "text") return false;
  if (mode === "image_url") return true;
  if (/siliconflow\.cn|deepseek\.com|moonshot\.cn/i.test(baseUrl)) return false;
  if (/qwen2-vl|glm-4v|gpt-4o|gpt-4\.1|gemini|claude|llava|vision|vl/i.test(model)) return true;
  return false;
}

function buildChatMessages(config, messages, forceTextImages = false) {
  const allowImages = !forceTextImages && supportsChatImageUrl(config);
  return messages.map((m) => {
    const imgs = Array.isArray(m.images) ? m.images : [];
    if (imgs.length && m.role === "user") {
      if (allowImages) {
        return {
          role: "user",
          content: [
            { type: "text", text: m.content || "" },
            ...imgs.map((it) => ({ type: "image_url", image_url: { url: it.data_url || it.url || "" } }))
          ]
        };
      }
      return {
        role: "user",
        content: (m.content || "") + "\n\n[用户附加了 " + String(imgs.length) + " 张图片，但当前接口不支持 image_url 消息格式，已改为文字说明。]"
      };
    }
    return { role: m.role, content: m.content || "" };
  });
}

function buildUserOnlyPartMessages(config, messages, forceTextImages = false) {
  const allowImages = !forceTextImages && supportsChatImageUrl(config);
  const users = (messages || []).filter((m) => m && m.role === "user");
  const source = users.length ? users.slice(-1) : (messages || []).slice(-1);
  return source.map((m) => {
    const parts = [];
    const text = String((m && m.content) || "").trim();
    if (text) parts.push({ type: "text", text });
    const imgs = Array.isArray(m && m.images) ? m.images : [];
    if (imgs.length && allowImages) {
      imgs.forEach((it) => {
        const url = it && (it.data_url || it.url || "");
        if (url) parts.push({ type: "image_url", image_url: { url } });
      });
    } else if (imgs.length) {
      parts.push({ type: "text", text: "[User attached " + String(imgs.length) + " image(s), but this endpoint does not accept image_url content.]" });
    }
    return { role: "user", content: parts.length ? parts : [{ type: "text", text: "" }] };
  });
}

function chatErrorText(data, status) {
  return data && data.error && data.error.message ? String(data.error.message) : ("HTTP " + String(status));
}

function shouldRetryAsUserOnlyParts(data) {
  const s = JSON.stringify(data || {});
  return /content.*valid list|valid list.*content|should be 'user'|input\.messages|messages\.\d+\.content/i.test(s);
}

async function independentAiChat(config, messages) {
  const apiType = String(config.api_type || "OpenAI兼容");
  const baseUrl = normalizeBaseUrl(config.base_url);
  const model = String(config.model || "").trim();
  const apiKey = String(config.api_key || "").trim();
  if (!baseUrl) return { ok: false, error: "独立 GPT 模式需要填写 Base URL。" };
  if (!model) return { ok: false, error: "独立 GPT 模式需要填写模型名。" };
  try {
    let url = baseUrl;
    let body;
    if (/ollama/i.test(apiType)) {
      url = /\/api\/chat$/i.test(baseUrl) ? baseUrl : baseUrl + "/api/chat";
      body = {
        model,
        stream: false,
        messages: messages.map((m) => ({ role: m.role, content: m.content || "" })),
        options: { temperature: Number(config.temperature ?? 0.3) }
      };
    } else {
      url = /\/chat\/completions$/i.test(baseUrl) ? baseUrl : baseUrl + "/chat/completions";
      body = {
        model,
        temperature: Number(config.temperature ?? 0.3),
        messages: buildChatMessages(config, messages)
      };
      if (/deepseek/i.test(model) || /deepseek/i.test(baseUrl)) {
        body.thinking = { type: "enabled" };
      }
    }
    const headers = { "Content-Type": "application/json" };
    if (apiKey) headers.Authorization = "Bearer " + apiKey;
    let resp = await fetchWithTimeout(url, { method: "POST", headers, body: JSON.stringify(body) }, 60000);
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok && /unknown variant image_url|expected text|deserialize/i.test(JSON.stringify(data))) {
      body.messages = buildChatMessages(config, messages, true);
      resp = await fetchWithTimeout(url, { method: "POST", headers, body: JSON.stringify(body) }, 60000);
      const retryData = await resp.json().catch(() => ({}));
      if (!resp.ok) throw new Error(retryData.error && retryData.error.message ? retryData.error.message : ("HTTP " + resp.status));
      const retryText = String((retryData.choices && retryData.choices[0] && retryData.choices[0].message && retryData.choices[0].message.content) || "");
      const retryExtracted = extractImagesFromText(retryText);
      return { ok: true, text: retryExtracted.text || "已自动改用纯文本图片说明重试。", images: retryExtracted.images };
    }
    if (!resp.ok && !/ollama/i.test(apiType) && shouldRetryAsUserOnlyParts(data)) {
      body.messages = buildUserOnlyPartMessages(config, messages, /unknown variant image_url|expected text|deserialize/i.test(JSON.stringify(data)));
      resp = await fetchWithTimeout(url, { method: "POST", headers, body: JSON.stringify(body) }, 60000);
      const retryData = await resp.json().catch(() => ({}));
      if (!resp.ok) throw new Error(chatErrorText(retryData, resp.status));
      const retryText = String((retryData.choices && retryData.choices[0] && retryData.choices[0].message && retryData.choices[0].message.content) || "");
      const retryExtracted = extractImagesFromText(retryText);
      return { ok: true, text: retryExtracted.text || "AI娌℃湁杩斿洖鍐呭", images: retryExtracted.images };
    }
    if (!resp.ok) {
      const detail = data.error && data.error.message ? data.error.message : ("HTTP " + resp.status);
      throw new Error(detail + " | chat_url=" + (resp.url || url) + " | model=" + model + " | api_type=" + apiType);
    }
    let rawText, thinking = "";
    if (/ollama/i.test(apiType)) {
      rawText = String((data.message && data.message.content) || data.response || "");
      thinking = String((data.message && data.message.reasoning_content) || (data.message && data.message.thinking) || "");
    } else {
      const msg = (data.choices && data.choices[0] && data.choices[0].message) || {};
      rawText = String(msg.content || "");
      thinking = String(msg.reasoning_content || msg.thinking || msg.reasoning || "");
      if (!thinking && data.choices && data.choices[0] && data.choices[0].thinking) {
        thinking = String(data.choices[0].thinking || "");
      }
    }
    const extracted = extractImagesFromText(rawText);
    return { ok: true, text: extracted.text || (extracted.images.length ? "已收到图片。" : "AI没有返回内容"), images: extracted.images, thinking };
  } catch (e) {
    return { ok: false, error: String(e && e.name === "AbortError" ? "独立 GPT 请求超时。" : e) };
  }
}

async function independentAiImage(config, prompt, images) {
  const baseUrl = normalizeBaseUrl(config.base_url);
  const apiKey = String(config.api_key || "").trim();
  const model = String(config.image_model || config.model || "gpt-image-1").trim();
  let imageApiType = String(config.image_api_type || "auto").toLowerCase();
  if (imageApiType === "auto") {
    imageApiType = /siliconflow\.cn/i.test(baseUrl) ? "siliconflow" : "openai";
  }
  if (!baseUrl) return { ok: false, error: "图片接口需要填写 Base URL。" };
  if (!apiKey && !/^http:\/\/127\.0\.0\.1|^http:\/\/localhost/i.test(baseUrl)) return { ok: false, error: "图片接口需要填写 API Key。" };
  try {
    const hasImages = Array.isArray(images) && images.length > 0;
    const finalPrompt = hasImages
      ? "Edit the provided image according to this request. Keep the same main subject, composition, and important structure unless the user explicitly asks to change them. Do not create an unrelated new image. Request: " + String(prompt || "")
      : String(prompt || "");
    const headers = {};
    if (apiKey) headers.Authorization = "Bearer " + apiKey;
    let resp;
    if (imageApiType === "siliconflow" || imageApiType === "json") {
      headers["Content-Type"] = "application/json";
      const payload = {
        model,
        prompt: finalPrompt,
        image_size: String(config.image_size || "1024x1024"),
        size: String(config.image_size || "1024x1024")
      };
      if (hasImages) payload.image = images[0].data_url || images[0].url || "";
      resp = await fetchWithTimeout(imageEndpoint(baseUrl, "generation"), {
        method: "POST",
        headers,
        body: JSON.stringify(payload)
      }, 90000);
    } else if (hasImages) {
      const form = new FormData();
      form.append("model", model);
      form.append("prompt", finalPrompt);
      form.append("size", String(config.image_size || "1024x1024"));
      images.slice(0, 4).forEach((it, idx) => {
        const blob = dataUrlToBlob(it.data_url || it.url || "");
        if (blob) form.append("image", blob, it.name || ("image_" + idx + ".png"));
      });
      resp = await fetchWithTimeout(imageEndpoint(baseUrl, "edit"), { method: "POST", headers, body: form }, 90000);
    } else {
      headers["Content-Type"] = "application/json";
      resp = await fetchWithTimeout(imageEndpoint(baseUrl, "generation"), {
        method: "POST",
        headers,
        body: JSON.stringify({ model, prompt: finalPrompt, size: String(config.image_size || "1024x1024") })
      }, 90000);
    }
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok) throw new Error(data.error && data.error.message ? data.error.message : ("HTTP " + resp.status));
    const out = Array.isArray(data.data) ? data.data : [];
    const resultImages = out.map((item, idx) => {
      const raw = item.url || item.image_url || item.b64_json || "";
      let url = "";
      if (/^data:image\//i.test(raw) || /^https?:\/\//i.test(raw)) {
        url = raw;
      } else if (item.b64_json || /^[A-Za-z0-9+/]{800,}={0,2}$/.test(raw)) {
        url = "data:image/png;base64," + raw;
      }
      return url ? { name: "result_" + String(idx + 1) + ".png", url, thumb: url } : null;
    }).filter(Boolean);
    const textParts = [];
    if (data.output_text) textParts.push(String(data.output_text));
    if (data.text) textParts.push(String(data.text));
    const extracted = extractImagesFromText(textParts.join("\n"));
    return { ok: true, text: extracted.text || (hasImages ? "图片编辑完成。" : "图片生成完成。"), images: resultImages.concat(extracted.images) };
  } catch (e) {
    return { ok: false, error: String(e && e.name === "AbortError" ? "图片请求超时。" : e) };
  }
}

async function independentAiModels(config) {
  const apiType = String(config.api_type || "OpenAI兼容");
  const baseUrl = normalizeBaseUrl(config.base_url);
  const apiKey = String(config.api_key || "").trim();
  if (!baseUrl) return { ok: false, error: "请先填写 Base URL。" };
  try {
    const url = /ollama/i.test(apiType)
      ? (/\/api\/tags$/i.test(baseUrl) ? baseUrl : baseUrl + "/api/tags")
      : (/\/models$/i.test(baseUrl) ? baseUrl : baseUrl + "/models");
    const headers = {};
    if (apiKey && !/ollama/i.test(apiType)) headers.Authorization = "Bearer " + apiKey;
    const resp = await fetchWithTimeout(url, { method: "GET", headers }, 15000);
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok) throw new Error(data.error && data.error.message ? data.error.message : ("HTTP " + resp.status));
    const models = /ollama/i.test(apiType)
      ? ((data.models || []).map((m) => m.name || m.model).filter(Boolean))
      : ((data.data || []).map((m) => m.id).filter(Boolean));
    return { ok: true, models: [...new Set(models)].sort() };
  } catch (e) {
    return { ok: false, error: String(e && e.name === "AbortError" ? "获取模型列表超时。" : e) };
  }
}

async function webSearch(query, config) {
  const searchUrl = String(config.search_api_url || "").trim();
  const apiKey = String(config.search_api_key || "").trim();
  const q = String(query || "").trim();
  if (!q) return { ok: false, error: "搜索关键词为空" };

  try {
    let resultsText = "";
    if (searchUrl) {
      const url = searchUrl.replace(/\{query\}/g, encodeURIComponent(q));
      const headers = { "Content-Type": "application/json" };
      if (apiKey) headers.Authorization = "Bearer " + apiKey;
      const resp = await fetchWithTimeout(url, { method: "GET", headers }, 15000);
      const data = await resp.json().catch(() => null);
      if (!resp.ok) throw new Error("HTTP " + resp.status);
      resultsText = extractSearchResults(data);
    } else {
      resultsText = await searchWithFallbacks(q);
    }
    if (!resultsText.trim()) return { ok: false, error: "未搜索到相关内容" };
    return { ok: true, results: resultsText, query: q };
  } catch (e) {
    return { ok: false, error: "搜索失败: " + (e && e.message ? e.message : String(e)) };
  }
}

async function searchWithFallbacks(q) {
  const backends = [
    { name: "Google", fn: () => searchGoogle(q) },
    { name: "DuckDuckGo", fn: () => searchDuckDuckGoAPI(q) },
    { name: "DuckDuckGo_HTML", fn: () => searchDuckDuckGoHTML(q) },
    { name: "Bing", fn: () => searchBing(q) }
  ];
  for (const backend of backends) {
    try {
      const text = await backend.fn();
      if (text && text.trim()) return text;
    } catch (_e) { /* try next */ }
  }
  return "";
}

async function searchDuckDuckGoAPI(q) {
  const url = "https://api.duckduckgo.com/?q=" + encodeURIComponent(q) + "&format=json&no_html=1&skip_disambig=1";
  const resp = await fetchWithTimeout(url, { method: "GET" }, 8000);
  if (!resp.ok) throw new Error("HTTP " + resp.status);
  const data = await resp.json().catch(() => null);
  return extractDuckDuckGoResults(data);
}

async function searchDuckDuckGoHTML(q) {
  const url = "https://html.duckduckgo.com/html/?q=" + encodeURIComponent(q);
  const resp = await fetchWithTimeout(url, { method: "GET" }, 8000);
  if (!resp.ok) throw new Error("HTTP " + resp.status);
  const html = await resp.text();
  return extractHTMLResults(html, /<a[^>]*class="result__a"[^>]*>([\s\S]*?)<\/a>[\s\S]*?<a[^>]*class="result__snippet"[^>]*>([\s\S]*?)<\/a>/gi, 2);
}

async function searchGoogle(q) {
  const url = "https://www.google.com/search?q=" + encodeURIComponent(q) + "&hl=zh-CN";
  const resp = await fetchWithTimeout(url, {
    method: "GET",
    headers: {
      "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
      "Accept-Language": "zh-CN,zh;q=0.9"
    }
  }, 10000);
  if (!resp.ok) throw new Error("HTTP " + resp.status);
  const html = await resp.text();
  const parts = [];
  const blockRe = /<div class="g"[^>]*>([\s\S]*?)<\/div>\s*<\/div>\s*<\/div>/gi;
  let m;
  while ((m = blockRe.exec(html))) {
    const block = m[1];
    const title = (block.match(/<h3[^>]*>([\s\S]*?)<\/h3>/i) || [])[1] || "";
    const snippet = (block.match(/<span[^>]*class="[^"]*\baCOpRe\b[^"]*"[^>]*>([\s\S]*?)<\/span>/i) || [])[1]
      || (block.match(/<div[^>]*class="[^"]*\bVwiC3b\b[^"]*"[^>]*>([\s\S]*?)<\/div>/i) || [])[1]
      || (block.match(/<span[^>]*>([\s\S]*?)<\/span>/i) || [])[1] || "";
    const text = (title + " " + snippet).replace(/<[^>]+>/g, "").replace(/&[a-z]+;/gi, " ").replace(/\s+/g, " ").trim();
    if (text.length > 20) parts.push(text);
  }
  return parts.slice(0, 10).join("\n");
}

async function searchBing(q) {
  const url = "https://www.bing.com/search?q=" + encodeURIComponent(q) + "&setlang=zh-Hans";
  const resp = await fetchWithTimeout(url, {
    method: "GET",
    headers: {
      "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
      "Accept-Language": "zh-CN,zh;q=0.9"
    }
  }, 10000);
  if (!resp.ok) throw new Error("HTTP " + resp.status);
  const html = await resp.text();
  const parts = [];
  const snippetRe = /<li class="b_algo"[^>]*>([\s\S]*?)<\/li>/gi;
  let m;
  while ((m = snippetRe.exec(html))) {
    const block = m[1];
    const title = (block.match(/<h2[^>]*>([\s\S]*?)<\/h2>/i) || [])[1] || "";
    const body = (block.match(/<p[^>]*>([\s\S]*?)<\/p>/i) || [])[1] || (block.match(/<div class="b_caption"[^>]*>([\s\S]*?)<\/div>/i) || [])[1] || "";
    const text = (title + " " + body).replace(/<[^>]+>/g, "").replace(/&[a-z]+;/gi, " ").replace(/\s+/g, " ").trim();
    if (text.length > 20) parts.push(text);
  }
  return parts.slice(0, 10).join("\n");
}

function extractHTMLResults(html, regex, snippetGroup) {
  const parts = [];
  let m;
  while ((m = regex.exec(html))) {
    const text = (m[snippetGroup] || m[1] || "").replace(/<[^>]+>/g, "").replace(/&[a-z]+;/gi, " ").replace(/\s+/g, " ").trim();
    if (text.length > 15) parts.push(text);
  }
  return parts.slice(0, 10).join("\n");
}

function extractDuckDuckGoResults(data) {
  if (!data) return "";
  const parts = [];
  if (data.AbstractText && data.AbstractText.trim()) {
    parts.push(data.AbstractText.trim());
    if (data.AbstractURL) parts.push("来源: " + data.AbstractURL);
  }
  const topics = Array.isArray(data.RelatedTopics) ? data.RelatedTopics : [];
  for (const t of topics) {
    if (t && t.Text && t.Text.trim()) {
      parts.push(t.Text.trim());
      if (t.FirstURL) parts.push("来源: " + t.FirstURL);
    }
  }
  return parts.filter(Boolean).slice(0, 12).join("\n");
}

function extractSearchResults(data) {
  if (!data) return "";
  if (typeof data === "string") return data;
  const text = JSON.stringify(data, null, 2);
  if (text.length < 3000) return text;
  return text.slice(0, 3000) + "\n...[truncated]";
}

async function independentAiTest(config) {
  const models = await independentAiModels(config);
  if (models.ok) return { ok: true, message: "连接成功，获取到 " + String((models.models || []).length) + " 个模型。" };
  const testMessages = [{ role: "user", content: "Reply with OK." }];
  const resp = await independentAiChat(config, testMessages);
  if (resp.ok) return { ok: true, message: "连接成功。" };
  return { ok: false, error: models.error || resp.error || "连接失败" };
}

async function pushUrls(urls, options = {}) {
  if (!urls || !urls.length) return { ok: false, error: t("noUrlsError") };
  const settings = await getSettings();
  const target = options.target === "ue" ? "ue" : "max";
  const port = targetPort(settings, target);
  const queueKey = pendingKey(target);
  const pushEndpoint = serverUrl(port) + "/push";

  const isOnline = target === "ue" ? settings.ueServerOnline : settings.serverOnline;
  if (!isOnline) {
    return { ok: false, offline: true, error: targetName(target) + " 未连接" };
  }

  try {
    const resp = await fetch(pushEndpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ urls, auto_start_download: !!options.autoStart })
    });
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok || !data || data.ok === false) {
      throw new Error((data && (data.error || data.message)) || ("HTTP " + resp.status));
    }
    await setSettings({ [onlineKey(target)]: true, [checkedAtKey(target)]: Date.now(), pushTarget: target });
    showNotification(options.autoStart ? targetName(target) + " 已接收并尝试立即下载" : "已推送到 " + targetName(target) + "：" + String(data.count || urls.length) + " 条");
    return { ok: true, count: data.count || urls.length, autoStarted: !!data.auto_started };
  } catch (e) {
    const currentQueue = target === "ue" ? settings.uePendingQueue : settings.pendingQueue;
    const merged = [...new Set([...(currentQueue || []), ...urls])];
    await setSettings({ [queueKey]: merged, [onlineKey(target)]: false, pushTarget: target });
    showNotification(targetName(target) + " 当前离线，链接已加入待发送队列");
    return { ok: false, error: String(e), queued: urls.length };
  }
}

async function retryPendingQueue(target = "max") {
  const settings = await getSettings();
  const pending = target === "ue" ? (settings.uePendingQueue || []) : (settings.pendingQueue || []);
  if (!pending.length) return { ok: true, count: 0 };
  const result = await pushUrls(pending, { target });
  if (result.ok) {
    await setSettings({ [pendingKey(target)]: [] });
  }
  return result;
}

async function updateConnectionStatus() {
  const settings = await getSettings();
  const port = await discoverServerPort(settings.port);
  await setSettings({ serverPort: port, serverOnline: await pingPort(port), lastCheckedAt: Date.now() });
}

function showNotification(message) {
  try {
    if (!chrome.notifications) return;
    chrome.notifications.create({
      type: "basic",
      iconUrl: "icon48.png",
      title: t("notificationTitle"),
      message
    });
  } catch (_e) {}
}

// Connection checks are manual from the popup to avoid constant flashing.
