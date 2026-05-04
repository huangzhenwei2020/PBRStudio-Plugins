const DOWNLOAD_EXTS = [
  ".zip", ".rar", ".7z", ".tar", ".gz",
  ".jpg", ".jpeg", ".png", ".tif", ".tiff",
  ".exr", ".hdr", ".tga", ".webp", ".bmp",
  ".fbx", ".obj", ".3ds", ".abc", ".max",
  ".mat", ".c4d", ".blend", ".mb", ".ma",
  ".pdf", ".sbsar"
];

function normalizeUrl(url) {
  if (!url) return "";
  try {
    return new URL(url, location.href).href;
  } catch (_e) {
    return "";
  }
}

function extractRealUrl(el) {
  const candidates = [
    el.getAttribute("data-download-url"),
    el.getAttribute("data-url"),
    el.getAttribute("data-href"),
    el.getAttribute("data-file"),
    el.getAttribute("data-file-url"),
    el.getAttribute("data-src"),
    el.getAttribute("data-link"),
    el.getAttribute("data-original"),
    el.getAttribute("href"),
  ];
  for (const raw of candidates) {
    if (!raw || !raw.trim() || raw.startsWith("javascript:") || raw.startsWith("#")) continue;
    const normalized = normalizeUrl(raw.trim());
    if (normalized) return normalized;
  }
  return null;
}

function isDownloadUrl(url) {
  if (!url) return false;
  try {
    const u = new URL(url);
    const path = u.pathname.toLowerCase().split("?")[0];
    if (DOWNLOAD_EXTS.some((ext) => path.endsWith(ext))) return true;
    const fname = (u.searchParams.get("filename") || u.searchParams.get("file") || u.searchParams.get("download") || "").toLowerCase();
    return !!fname && DOWNLOAD_EXTS.some((ext) => fname.endsWith(ext));
  } catch (_e) {
    return false;
  }
}

function looksLikeDownload(el) {
  const text = (el.textContent || "").trim().toLowerCase();
  const cls = String(el.className || "").toLowerCase();
  const title = (el.getAttribute("title") || "").toLowerCase();
  return ["download", "dl", "get file"].some((k) => text.includes(k) || cls.includes(k) || title.includes(k));
}

function polyhavenSlugFromLocation() {
  const parts = location.pathname.split("/").filter(Boolean);
  if (location.hostname !== "polyhaven.com") return "";
  if (parts.length < 2) return "";
  if (!["a", "textures", "hdris", "models"].includes(parts[0])) return "";
  return parts[1] || "";
}

function polyhavenSyntheticDownloadUrl() {
  const slug = polyhavenSlugFromLocation();
  if (!slug) return "";

  let resolution = "";
  let mode = "";
  let filename = "";

  document.querySelectorAll("a[href], [data-download-url], [data-url], [data-href]").forEach((el) => {
    [
      el.getAttribute && el.getAttribute("href"),
      el.getAttribute && el.getAttribute("data-download-url"),
      el.getAttribute && el.getAttribute("data-url"),
      el.getAttribute && el.getAttribute("data-href")
    ].forEach((value) => {
      const full = normalizeUrl(value);
      if (!/\/__download__\//i.test(full)) return;
      const tempName = decodeURIComponent(full.split("/").pop() || "");
      const low = tempName.toLowerCase();
      const resMatch = low.match(/_(1k|2k|4k|8k|16k|32k)(?:[._-]|$)/i);
      if (resMatch && !resolution) resolution = resMatch[1].toLowerCase();
      const extMatch = low.match(/\.(zip|blend|gltf|fbx|usd|mtlx)(?:$|\?)/i);
      if (extMatch && !mode) mode = extMatch[1].toLowerCase();
      if (tempName && !filename) filename = tempName;
    });
  });

  if (!resolution) resolution = "2k";
  if (!mode) mode = "zip";
  if (!filename) filename = slug + "_" + resolution + "." + mode;

  const stable = new URL("https://polyhaven.com/a/" + encodeURIComponent(slug));
  stable.searchParams.set("iss_polyhaven", "1");
  stable.searchParams.set("iss_res", resolution);
  stable.searchParams.set("iss_dl", mode);
  stable.searchParams.set("download", filename);
  return stable.href;
}

async function collectPolyhavenUrls() {
  const stableUrl = polyhavenSyntheticDownloadUrl();
  return stableUrl ? [stableUrl] : [];
}

function makeButton(urls, target) {
  const btn = document.createElement("span");
  btn.dataset.pbrBtn = "1";
  btn.textContent = target === "ue" ? "UE" : "Max";
  btn.title = (target === "ue" ? "推送到 UE：" : "推送到 Max：") + String(urls.length);
  btn.style.cssText = [
    "display:inline-block",
    "cursor:pointer",
    "font-size:10px",
    "font-weight:700",
    "color:#fff",
    "background:#1a6fd4",
    "border-radius:10px",
    "padding:1px 6px",
    "line-height:16px",
    "white-space:nowrap",
    "user-select:none",
    "vertical-align:middle"
  ].join(";");
  btn.addEventListener("mouseenter", () => { btn.style.background = "#0f52a8"; });
  btn.addEventListener("mouseleave", () => { btn.style.background = "#1a6fd4"; });
  btn.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    btn.textContent = "...";
    chrome.runtime.sendMessage({ action: "push", urls, target }, (resp) => {
      btn.textContent = resp && resp.ok ? "OK" : "ERR";
      btn.style.background = resp && resp.ok ? "#1c8a3e" : "#c0392b";
      setTimeout(() => {
        btn.textContent = target === "ue" ? "UE" : "Max";
        btn.style.background = "#1a6fd4";
      }, 1800);
    });
  });
  return btn;
}

function injectButtonPair(anchor, urls) {
  if (anchor.nextSibling && anchor.nextSibling.dataset && anchor.nextSibling.dataset.pbrBtnGroup) return;
  const wrap = document.createElement("span");
  wrap.dataset.pbrBtnGroup = "1";
  wrap.style.cssText = "display:inline-flex;gap:2px;margin-left:4px;vertical-align:middle;align-items:center";
  wrap.appendChild(makeButton(urls, "max"));
  wrap.appendChild(makeButton(urls, "ue"));
  anchor.insertAdjacentElement("afterend", wrap);
}

function bindPolyhavenDirectTakeover() {
  if (location.hostname !== "polyhaven.com") return;
  const stableUrl = polyhavenSyntheticDownloadUrl();
  if (!stableUrl) return;
  document.querySelectorAll("a[href], button").forEach((el) => {
    if (el.dataset.pbrTakeoverBound === "1") return;
    const text = ((el.textContent || "") + " " + (el.getAttribute("aria-label") || "") + " " + (el.getAttribute("title") || "")).toLowerCase();
    const href = normalizeUrl(el.getAttribute && el.getAttribute("href"));
    const looksDownload = /download|get\b|zip|fbx|blend|gltf|usd/i.test(text) || /\/__download__\//i.test(href);
    if (!looksDownload) return;
    el.dataset.pbrTakeoverBound = "1";
    el.addEventListener("dblclick", (e) => {
      e.preventDefault();
      e.stopPropagation();
      e.stopImmediatePropagation();
      chrome.runtime.sendMessage({ action: "pushAndDownloadNow", urls: [stableUrl], target: "ue" });
      return false;
    }, true);
  });
}

async function scanAndInjectButtons() {
  document.querySelectorAll("a:not([data-pbr-injected])").forEach((link) => {
    link.dataset.pbrInjected = "1";
    const url = extractRealUrl(link);
    if (!url) return;
    const hasDlAttr = link.hasAttribute("download");
    const urlMatch = isDownloadUrl(url);
    const looksLike = looksLikeDownload(link);
    if (urlMatch || hasDlAttr || (looksLike && url.startsWith("http"))) {
      injectButtonPair(link, [url]);
    }
  });

  document.querySelectorAll(
    "[data-download-url]:not([data-pbr-injected])," +
    "[data-url]:not([data-pbr-injected])," +
    "[data-file-url]:not([data-pbr-injected])"
  ).forEach((el) => {
    el.dataset.pbrInjected = "1";
    const url = extractRealUrl(el);
    if (!url || !isDownloadUrl(url)) return;
    injectButtonPair(el, [url]);
  });

  const pageUrls = Array.from(document.querySelectorAll("a[href]")).map((a) => {
    const u = extractRealUrl(a);
    return (u && (isDownloadUrl(u) || a.hasAttribute("download"))) ? u : null;
  }).filter(Boolean);
  const merged = [...new Set(pageUrls)];
  const polyhavenUrls = await collectPolyhavenUrls();
  polyhavenUrls.forEach((u) => {
    if (!merged.includes(u)) merged.push(u);
  });
  window.__pbrPageUrls = merged;
  bindPolyhavenDirectTakeover();
}

scanAndInjectButtons();

let scanTimer = null;
const observer = new MutationObserver(() => {
  clearTimeout(scanTimer);
  scanTimer = setTimeout(scanAndInjectButtons, 300);
});
observer.observe(document.body, { childList: true, subtree: true });

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg.action === "collectLinks") {
    sendResponse({ urls: window.__pbrPageUrls || [] });
    return true;
  }
  if (msg.action === "getPageUrlCount") {
    sendResponse({ count: (window.__pbrPageUrls || []).length });
    return true;
  }
});
