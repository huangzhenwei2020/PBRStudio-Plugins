const dot = document.getElementById("status-dot");
const statusText = document.getElementById("status-text");
const pageInfo = document.getElementById("page-info");
const pendingBox = document.getElementById("pending-box");
const portInput = document.getElementById("port-input");
const portHint = document.getElementById("port-hint");
const btnPushPage = document.getElementById("btn-push-page");
const btnPushPageUE = document.getElementById("btn-push-page-ue");
const btnRetry = document.getElementById("btn-retry");
const btnClear = document.getElementById("btn-clear");
const btnSavePort = document.getElementById("btn-save-port");
const btnCheckPort = document.getElementById("btn-check-port");
const btnOpenAi = document.getElementById("btn-open-ai");
const btnTargetMax = document.getElementById("target-max");
const btnTargetUE = document.getElementById("target-ue");

let pageUrls = [];
let target = "max";
let portDirty = false;

function targetLabel() {
  return target === "ue" ? "UE" : "Max";
}

function defaultPortForTarget() {
  return target === "ue" ? 19528 : 19527;
}

function validatePort(value) {
  const port = Number(value);
  if (!Number.isInteger(port)) return "端口必须是整数";
  if (port < 1025 || port > 65535) return "端口必须在 1025 到 65535 之间";
  if ([80, 443, 3306, 3389].includes(port)) return "不要使用系统常见端口";
  return "";
}

function formatCheckTime(timestamp) {
  if (!timestamp) return "尚未检测";
  try {
    return new Date(timestamp).toLocaleTimeString();
  } catch (_e) {
    return "未知时间";
  }
}

function setTarget(nextTarget) {
  target = nextTarget === "ue" ? "ue" : "max";
  btnTargetMax.classList.toggle("active", target === "max");
  btnTargetUE.classList.toggle("active", target === "ue");
  chrome.runtime.sendMessage({ action: "setTarget", target }, () => refresh());
}

function refresh() {
  chrome.runtime.sendMessage({ action: "getStatus", target }, (resp) => {
    const online = !!(resp && resp.online);
    const pending = (resp && resp.pendingList) || [];
    const port = (resp && resp.port) || defaultPortForTarget();
    const checkedAt = resp && resp.lastCheckedAt;

    if (!portDirty && document.activeElement !== portInput) {
      portInput.value = port;
    }

    dot.className = "dot " + (online ? "online" : "offline");
    statusText.textContent = targetLabel() + (online ? " 在线" : " 离线或未检测") + " | 端口 " + port + " | " + formatCheckTime(checkedAt);

    if (pending.length === 0) {
      pendingBox.textContent = "没有待发送链接";
      pendingBox.className = "pending-box empty";
    } else {
      pendingBox.className = "pending-box";
      pendingBox.textContent = "";
      const title = document.createElement("b");
      title.textContent = "待发送链接：" + String(pending.length);
      pendingBox.appendChild(title);
      pendingBox.appendChild(document.createElement("br"));
      pending.forEach((u) => {
        const row = document.createElement("div");
        row.title = u;
        row.textContent = u.length > 62 ? u.slice(0, 59) + "..." : u;
        pendingBox.appendChild(row);
      });
    }
  });
}

function queryPageLinks() {
  chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
    if (!tabs || !tabs[0]) return;
    chrome.tabs.sendMessage(tabs[0].id, { action: "collectLinks" }, (resp) => {
      if (chrome.runtime.lastError || !resp) {
        pageInfo.textContent = "无法扫描当前页面，请刷新标签页后重试。";
        btnPushPage.disabled = true;
        btnPushPageUE.disabled = true;
        return;
      }
      pageUrls = resp.urls || [];
      pageInfo.textContent = pageUrls.length ? "识别到下载链接：" + String(pageUrls.length) : "没有识别到下载链接";
      btnPushPage.disabled = pageUrls.length === 0;
      btnPushPageUE.disabled = pageUrls.length === 0;
    });
  });
}

function pushPageTo(pushTarget, button) {
  if (!pageUrls.length) return;
  button.disabled = true;
  const oldText = button.textContent;
  button.textContent = "推送中...";
  chrome.runtime.sendMessage({ action: "push", urls: pageUrls, target: pushTarget }, (resp) => {
    button.textContent = resp && resp.ok ? "完成" : "已排队";
    setTimeout(() => {
      button.textContent = oldText;
      button.disabled = pageUrls.length === 0;
      refresh();
    }, 1200);
  });
}

btnTargetMax.addEventListener("click", () => setTarget("max"));
btnTargetUE.addEventListener("click", () => setTarget("ue"));

btnPushPage.addEventListener("click", () => pushPageTo(target, btnPushPage));
btnPushPageUE.addEventListener("click", () => pushPageTo("ue", btnPushPageUE));

btnRetry.addEventListener("click", () => {
  btnRetry.disabled = true;
  btnRetry.textContent = "重试中...";
  chrome.runtime.sendMessage({ action: "retryPending", target }, () => {
    btnRetry.disabled = false;
    btnRetry.textContent = "重试队列";
    refresh();
  });
});

btnClear.addEventListener("click", () => {
  if (!confirm("确认清空当前目标的待发送队列吗？")) return;
  chrome.runtime.sendMessage({ action: "clearPending", target }, () => refresh());
});

btnOpenAi.addEventListener("click", () => {
  chrome.tabs.create({ url: chrome.runtime.getURL("ai_panel.html") });
});

btnSavePort.addEventListener("click", () => {
  const msg = validatePort(portInput.value);
  if (msg) {
    portHint.textContent = msg;
    return;
  }
  chrome.runtime.sendMessage({ action: "setPort", target, port: Number(portInput.value) }, (resp) => {
    portDirty = false;
    portHint.textContent = (resp && resp.message) || "端口已保存";
    refresh();
  });
});

btnCheckPort.addEventListener("click", () => {
  const msg = validatePort(portInput.value);
  if (msg) {
    portHint.textContent = msg;
    return;
  }
  btnCheckPort.disabled = true;
  btnCheckPort.textContent = "检测中...";
  chrome.runtime.sendMessage({ action: "checkPort", target, port: Number(portInput.value) }, (resp) => {
    btnCheckPort.disabled = false;
    btnCheckPort.textContent = "手动检测";
    portHint.textContent = (resp && resp.message) || "检测完成";
    refresh();
  });
});

portInput.addEventListener("input", () => {
  portDirty = true;
});

chrome.storage.local.get(["pushTarget"], (data) => {
  target = data.pushTarget === "ue" ? "ue" : "max";
  setTarget(target);
  queryPageLinks();
});
