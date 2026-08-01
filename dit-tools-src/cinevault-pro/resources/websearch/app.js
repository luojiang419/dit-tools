const shell = document.querySelector(".search-shell");
const form = document.querySelector("#searchForm");
const input = document.querySelector("#searchInput");
const clearButton = document.querySelector("#clearButton");
const panel = document.querySelector("#resultPanel");
const meta = document.querySelector("#resultMeta");
const list = document.querySelector("#resultList");
const themeToggle = document.querySelector("#themeToggle");
const themeMoon = document.querySelector("#themeMoon");
const themeSun = document.querySelector("#themeSun");
const detailModal = document.querySelector("#detailModal");
const detailDialog = document.querySelector(".detail-dialog");
const detailClose = document.querySelector("#detailClose");
const detailLoading = document.querySelector("#detailLoading");
const detailBody = document.querySelector("#detailBody");
const detailEmpty = document.querySelector("#detailEmpty");
const detailTitle = document.querySelector("#detailTitle");
const detailSubtitle = document.querySelector("#detailSubtitle");
const detailStatus = document.querySelector("#detailStatus");
const frameCount = document.querySelector("#frameCount");
const frameList = document.querySelector("#frameList");
const stageImage = document.querySelector("#stageImage");
const stageFrameLabel = document.querySelector("#stageFrameLabel");
const stageTimestamp = document.querySelector("#stageTimestamp");
const stageState = document.querySelector("#stageState");
const stageCaption = document.querySelector("#stageCaption");
const analysisModel = document.querySelector("#analysisModel");
const analysisSummary = document.querySelector("#analysisSummary");
const analysisTags = document.querySelector("#analysisTags");
const analysisObjects = document.querySelector("#analysisObjects");
const analysisContext = document.querySelector("#analysisContext");
const analysisOcr = document.querySelector("#analysisOcr");
const analysisMeta = document.querySelector("#analysisMeta");

let searchTimer = 0;
let activeController = null;
let detailController = null;
let detailData = null;
let selectedFrameIndex = 0;
let lastFocusedElement = null;

function storedTheme() {
  try {
    return localStorage.getItem("websearch-theme");
  } catch {
    return null;
  }
}

function applyTheme(theme, persist = true) {
  const nextTheme = theme === "dark" ? "dark" : "light";
  document.documentElement.dataset.theme = nextTheme;
  themeMoon.toggleAttribute("hidden", nextTheme === "dark");
  themeSun.toggleAttribute("hidden", nextTheme !== "dark");
  const nextLabel = nextTheme === "dark" ? "切换到浅色模式" : "切换到暗黑模式";
  themeToggle.setAttribute("aria-label", nextLabel);
  themeToggle.setAttribute("title", nextLabel);
  if (persist) {
    try {
      localStorage.setItem("websearch-theme", nextTheme);
    } catch {
      // Local storage may be unavailable in an embedded browser.
    }
  }
}

applyTheme(storedTheme() || "light", false);

function displayName(item) {
  return item.kind === "folder" ? item.name : item.fileName;
}

function displayBadge(item) {
  if (item.kind === "frame") return "帧";
  if (item.kind === "folder") return "文件夹";
  return item.assetTypeLabel || "素材";
}

function displaySummary(item) {
  if (item.kind === "frame") return item.caption || item.ocrText || "";
  return item.summary || item.technicalSummary || item.matchedFrameCaption || "";
}

function displayPath(item) {
  return [item.projectName, item.sourceRootName, item.relativePath]
    .filter(Boolean)
    .join(" / ");
}

function dateSourceLabel(source) {
  const labels = {
    quicktime_creation_date: "QuickTime 媒体元数据",
    exif_datetime_original: "EXIF 媒体元数据",
    media_creation_time: "媒体创建时间",
    ExifTool: "ExifTool 元数据",
    folder_date: "目录日期",
    file_modified_time: "文件修改时间",
    legacy_file_modified_time: "文件修改时间兜底",
  };
  return labels[source] || (source ? "索引日期" : "");
}

function displayDate(item) {
  const date = item.kind === "folder"
    ? String(item.normalizedDate || "").slice(0, 10)
    : String(item.captureDate || item.modifiedAt || "").slice(0, 10);
  if (!date) return "";
  const source = item.kind === "folder"
    ? "folder_date"
    : (item.captureDate ? item.captureTimeSource : "file_modified_time");
  const sourceLabel = dateSourceLabel(source);
  return sourceLabel ? `日期 ${date} · ${sourceLabel}` : `日期 ${date}`;
}

function displayReason(item) {
  const reasons = Array.isArray(item.reasons) ? [...item.reasons] : [];
  if (item.kind === "frame" && item.timestampMs) {
    if (!reasons.some((reason) => reason.includes("视觉帧命中"))) reasons.push("视觉帧命中");
    reasons.push(formatTimestamp(item.timestampMs));
  }
  return reasons.join(" · ");
}

function formatTimestamp(value) {
  const milliseconds = Number(value) || 0;
  const totalSeconds = Math.max(0, Math.floor(milliseconds / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return hours > 0
    ? `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`
    : `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function formatDuration(value) {
  const milliseconds = Number(value) || 0;
  return formatTimestamp(milliseconds);
}

function formatBytes(value) {
  const bytes = Number(value) || 0;
  if (bytes <= 0) return "—";
  const units = ["B", "KB", "MB", "GB", "TB"];
  const index = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1);
  return `${(bytes / (1024 ** index)).toFixed(index > 1 ? 2 : 0)} ${units[index]}`;
}

function setText(element, value, fallback = "—") {
  if (element) element.textContent = value || fallback;
}

function chipList(element, values, fallback = "暂无记录") {
  element.replaceChildren();
  const items = Array.isArray(values) ? values.filter(Boolean) : [];
  if (!items.length) {
    const empty = document.createElement("span");
    empty.className = "analysis-chip analysis-chip--empty";
    empty.textContent = fallback;
    element.append(empty);
    return;
  }
  for (const value of items) {
    const chip = document.createElement("span");
    chip.className = "analysis-chip";
    chip.textContent = value;
    element.append(chip);
  }
}

function setResultsState(active) {
  shell.classList.toggle("has-results", active);
  clearButton.hidden = !active || !input.value;
}

function createFallbackIcon() {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("class", "thumb-fallback");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.innerHTML = '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="m6 16 3.5-4 2.7 3 2-2.2L19 17"/>';
  return svg;
}

function createThumbnail(item) {
  const thumb = document.createElement("div");
  thumb.className = "result-thumb";
  const url = item.thumbnailUrl;
  if (!url) {
    thumb.append(createFallbackIcon());
    return thumb;
  }

  const image = document.createElement("img");
  image.src = url;
  image.alt = "";
  image.loading = "lazy";
  image.decoding = "async";
  image.addEventListener("error", () => {
    thumb.replaceChildren(createFallbackIcon());
  }, { once: true });
  thumb.append(image);
  return thumb;
}

function videoKeyForItem(item) {
  if (item.kind === "frame") return item.videoKey || "";
  if (item.kind === "asset") return item.key || "";
  return "";
}

function createDetailThumbnail(url, fallbackClass = "") {
  const thumb = document.createElement("div");
  thumb.className = `detail-thumb ${fallbackClass}`.trim();
  if (!url) {
    thumb.append(createFallbackIcon());
    return thumb;
  }
  const image = document.createElement("img");
  image.src = url;
  image.alt = "";
  image.loading = "lazy";
  image.decoding = "async";
  image.addEventListener("error", () => {
    thumb.replaceChildren(createFallbackIcon());
  }, { once: true });
  thumb.append(image);
  return thumb;
}

function renderDetailFrameList(frames) {
  frameList.replaceChildren();
  frames.forEach((frame, index) => {
    const button = document.createElement("button");
    button.className = "frame-card";
    button.type = "button";
    button.dataset.frameIndex = String(index);
    button.setAttribute("aria-label", `查看第 ${frame.frameNumber || index + 1} 帧`);
    button.append(createDetailThumbnail(frame.imageUrl));

    const copy = document.createElement("span");
    copy.className = "frame-card-copy";
    const title = document.createElement("strong");
    title.textContent = `F${String(frame.frameNumber || index + 1).padStart(3, "0")}`;
    const time = document.createElement("span");
    time.textContent = formatTimestamp(frame.timestampMs);
    const caption = document.createElement("small");
    caption.textContent = frame.caption || frame.ocrText || "暂无描述";
    copy.append(title, time, caption);
    button.append(copy);
    frameList.append(button);
  });
}

function renderSelectedFrame(index) {
  const frames = Array.isArray(detailData?.frames) ? detailData.frames : [];
  if (!frames.length) return;
  selectedFrameIndex = Math.max(0, Math.min(index, frames.length - 1));
  const frame = frames[selectedFrameIndex];
  frameList.querySelectorAll(".frame-card").forEach((button, buttonIndex) => {
    const selected = buttonIndex === selectedFrameIndex;
    button.classList.toggle("is-selected", selected);
    button.setAttribute("aria-current", selected ? "true" : "false");
  });

  stageImage.replaceChildren(createDetailThumbnail(frame.imageUrl, "detail-thumb--stage"));
  setText(stageFrameLabel, `第 ${frame.frameNumber || selectedFrameIndex + 1} 帧`);
  setText(stageTimestamp, formatTimestamp(frame.timestampMs), "00:00");
  setText(stageState, frame.analysisState, "待解析");
  setText(stageCaption, frame.caption || frame.ocrText, "此帧暂无文字描述。");

  const asset = detailData.asset || {};
  setText(analysisSummary, frame.caption || asset.summary, "该帧暂无摘要。");
  chipList(analysisTags, frame.tags);
  const entityLabels = (Array.isArray(frame.entities) ? frame.entities : [])
    .map((entity) => entity.label || entity.category)
    .filter(Boolean);
  chipList(analysisObjects, [...(frame.objects || []), ...entityLabels]);
  const context = [
    frame.setting ? `场景：${frame.setting}` : "",
    frame.actions ? `动作：${frame.actions}` : "",
  ].filter(Boolean).join(" · ");
  setText(analysisContext, context);
  setText(analysisOcr, frame.ocrText || (frame.ocrBlocks || []).join("、"));
  const metaItems = [
    frame.factsComplete ? "结构化事实完整" : "基础视觉描述",
    frame.analyzedAt ? `分析于 ${frame.analyzedAt}` : "",
    `${selectedFrameIndex + 1} / ${frames.length}`,
  ].filter(Boolean);
  setText(analysisMeta, metaItems.join("  ·  "));
}

function renderDetail(data, initialFrameNumber = 0) {
  detailData = data;
  const asset = data.asset || {};
  const frames = Array.isArray(data.frames) ? data.frames : [];
  const frameIndex = Math.max(0, frames.findIndex((frame) => Number(frame.frameNumber) === Number(initialFrameNumber)));

  setText(detailTitle, asset.fileName, "素材分析结果");
  setText(detailSubtitle, [asset.projectName, asset.relativePath].filter(Boolean).join("  /  "), "本机素材索引");
  setText(detailStatus, asset.status, "未解析");
  setText(frameCount, `${frames.length} 帧`, "0 帧");
  setText(analysisModel, asset.analyzedAt ? "视觉索引已更新" : "本地视觉索引");

  detailLoading.hidden = true;
  detailEmpty.hidden = frames.length > 0;
  detailBody.hidden = frames.length === 0;
  if (!frames.length) {
    const emptySummary = detailEmpty.querySelector("span");
    setText(emptySummary, asset.summary || "该素材还没有可展示的画面分析帧。", "该素材还没有可展示的画面分析帧。");
    return;
  }

  renderDetailFrameList(frames);
  renderSelectedFrame(frameIndex);
}

function showDetailError() {
  detailLoading.hidden = true;
  detailBody.hidden = true;
  detailEmpty.hidden = false;
  setText(detailStatus, "读取失败");
  setText(detailTitle, "无法打开逐帧分析");
  setText(detailSubtitle, "请确认软件仍在运行，或稍后重试。");
  const emptyTitle = detailEmpty.querySelector("strong");
  const emptySummary = detailEmpty.querySelector("span");
  setText(emptyTitle, "逐帧分析暂时不可用");
  setText(emptySummary, "Web 搜索服务没有返回该素材的详细分析结果。");
}

function openDetail(videoKey, initialFrameNumber = 0) {
  if (!videoKey) return;
  if (detailController) detailController.abort();
  detailController = new AbortController();
  lastFocusedElement = document.activeElement;
  detailData = null;
  detailModal.hidden = false;
  detailLoading.hidden = false;
  detailBody.hidden = true;
  detailEmpty.hidden = true;
  setText(detailTitle, "素材分析结果");
  setText(detailSubtitle, "正在加载素材详情…");
  setText(detailStatus, "读取中");
  document.body.classList.add("detail-open");
  detailClose.focus();

  fetch(`/api/video-detail?key=${encodeURIComponent(videoKey)}`, {
    signal: detailController.signal,
    headers: { Accept: "application/json" }
  })
    .then((response) => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.json();
    })
    .then((data) => renderDetail(data, initialFrameNumber))
    .catch((error) => {
      if (error.name !== "AbortError") showDetailError();
    });
}

function closeDetail() {
  if (detailController) detailController.abort();
  detailController = null;
  detailModal.hidden = true;
  document.body.classList.remove("detail-open");
  detailData = null;
  if (lastFocusedElement && document.contains(lastFocusedElement)) lastFocusedElement.focus();
}

function renderResults(data) {
  const results = Array.isArray(data.results) ? data.results : [];
  const query = String(data.query || "").trim();
  const active = Boolean(query);
  setResultsState(active);
  panel.hidden = !active;
  list.replaceChildren();

  if (!active) return;

  const interpretation = Array.isArray(data.interpretation)
    ? data.interpretation.filter(Boolean)
    : [];
  meta.textContent = [`找到 ${data.total || 0} 条结果`, ...interpretation].join(" · ");

  if (results.length === 0) {
    const empty = document.createElement("div");
    empty.className = "empty-result";
    empty.textContent = "没有找到匹配素材";
    list.append(empty);
    return;
  }

  for (const item of results) {
    const row = document.createElement("article");
    const videoKey = videoKeyForItem(item);
    row.className = `result-item${videoKey ? " is-clickable" : ""}`;
    if (videoKey) {
      row.dataset.videoKey = videoKey;
      row.dataset.frameNumber = item.kind === "frame" ? String(item.frameNumber || 0) : "0";
      row.tabIndex = 0;
      row.setAttribute("role", "button");
      row.setAttribute("aria-label", `打开 ${displayName(item) || "素材"} 的逐帧分析`);
    }

    const content = document.createElement("div");
    content.className = "result-content";

    const title = document.createElement("div");
    title.className = "result-title";

    const badge = document.createElement("span");
    badge.className = "badge";
    badge.textContent = displayBadge(item);

    const name = document.createElement("span");
    name.className = "name";
    name.textContent = displayName(item) || "未命名素材";

    const summary = document.createElement("div");
    summary.className = "summary";
    summary.textContent = displaySummary(item);

    const path = document.createElement("div");
    path.className = "path";
    path.textContent = displayPath(item);

    const date = document.createElement("div");
    date.className = "date-line";
    date.textContent = displayDate(item);

    const reason = document.createElement("div");
    reason.className = "reason";
    reason.textContent = displayReason(item);

    title.append(badge, name);
    content.append(title);
    if (summary.textContent) content.append(summary);
    if (path.textContent) content.append(path);
    if (date.textContent) content.append(date);
    if (reason.textContent) content.append(reason);
    if (videoKey) {
      const hint = document.createElement("span");
      hint.className = "result-open-hint";
      hint.textContent = item.kind === "frame" ? "查看所属视频 · 逐帧分析" : "点击聚焦 · 查看逐帧分析";
      content.append(hint);
    }
    row.append(createThumbnail(item), content);
    list.append(row);
  }
}

async function runSearch() {
  const query = input.value.trim();
  if (!query) {
    renderResults({ query: "", results: [] });
    return;
  }

  setResultsState(true);
  panel.hidden = false;
  meta.textContent = "正在检索...";
  list.replaceChildren();

  if (activeController) activeController.abort();
  activeController = new AbortController();

  try {
    const response = await fetch(`/api/search?q=${encodeURIComponent(query)}&limit=80`, {
      signal: activeController.signal,
      headers: { Accept: "application/json" }
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    renderResults(await response.json());
  } catch (error) {
    if (error.name === "AbortError") return;
    setResultsState(true);
    panel.hidden = false;
    meta.textContent = "检索失败，请确认软件仍在运行";
    list.replaceChildren();
  }
}

function scheduleSearch() {
  window.clearTimeout(searchTimer);
  clearButton.hidden = !input.value;
  searchTimer = window.setTimeout(runSearch, 180);
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  runSearch();
});

clearButton.addEventListener("click", () => {
  input.value = "";
  renderResults({ query: "", results: [] });
  input.focus();
});

input.addEventListener("input", scheduleSearch);
themeToggle.addEventListener("click", () => {
  applyTheme(document.documentElement.dataset.theme === "dark" ? "light" : "dark");
});

document.addEventListener("click", (event) => {
  if (event.target.closest("[data-detail-close]")) {
    closeDetail();
    return;
  }

  const frameButton = event.target.closest("[data-frame-index]");
  if (frameButton && detailData) {
    renderSelectedFrame(Number(frameButton.dataset.frameIndex));
    return;
  }

  const resultItem = event.target.closest(".result-item[data-video-key]");
  if (resultItem) openDetail(resultItem.dataset.videoKey, Number(resultItem.dataset.frameNumber || 0));
});

document.addEventListener("keydown", (event) => {
  if (detailModal.hidden) {
    const resultItem = event.target.closest?.(".result-item[data-video-key]");
    if (resultItem && (event.key === "Enter" || event.key === " ")) {
      event.preventDefault();
      openDetail(resultItem.dataset.videoKey, Number(resultItem.dataset.frameNumber || 0));
    }
    return;
  }
  if (event.key === "Escape") {
    event.preventDefault();
    closeDetail();
    return;
  }

  const frames = Array.isArray(detailData?.frames) ? detailData.frames : [];
  if ((event.key === "ArrowDown" || event.key === "ArrowRight") && frames.length) {
    event.preventDefault();
    renderSelectedFrame(selectedFrameIndex + 1);
    frameList.querySelector(`[data-frame-index="${selectedFrameIndex}"]`)?.scrollIntoView({ block: "nearest" });
    return;
  }
  if ((event.key === "ArrowUp" || event.key === "ArrowLeft") && frames.length) {
    event.preventDefault();
    renderSelectedFrame(selectedFrameIndex - 1);
    frameList.querySelector(`[data-frame-index="${selectedFrameIndex}"]`)?.scrollIntoView({ block: "nearest" });
    return;
  }

  if (event.key !== "Tab") return;
  const focusable = [...detailModal.querySelectorAll("button:not([disabled]), [tabindex]:not([tabindex=\"-1\"])")];
  if (!focusable.length) return;
  const first = focusable[0];
  const last = focusable[focusable.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault();
    first.focus();
  }
});
window.addEventListener("load", () => input.focus());
