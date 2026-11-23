const storageKey = "dit-feedback-admin-token";

const state = {
  token: "",
  conversations: [],
  selectedConversationId: "",
  messages: [],
  pendingFiles: [],
  socket: null,
  reconnectTimer: null,
  heartbeatTimer: null,
  loadingMessages: false,
};

const elements = {
  loginOverlay: document.getElementById("login-overlay"),
  loginForm: document.getElementById("login-form"),
  loginUsername: document.getElementById("login-username"),
  loginPassword: document.getElementById("login-password"),
  loginError: document.getElementById("login-error"),
  refreshConversations: document.getElementById("refresh-conversations"),
  conversationSearch: document.getElementById("conversation-search"),
  conversationList: document.getElementById("conversation-list"),
  conversationTitle: document.getElementById("conversation-title"),
  conversationSubtitle: document.getElementById("conversation-subtitle"),
  conversationStatus: document.getElementById("conversation-status"),
  deleteConversation: document.getElementById("delete-conversation"),
  messageList: document.getElementById("message-list"),
  replyText: document.getElementById("reply-text"),
  replyFiles: document.getElementById("reply-files"),
  replyFileList: document.getElementById("reply-file-list"),
  sendReply: document.getElementById("send-reply"),
  conversationMeta: document.getElementById("conversation-meta"),
  metricUnreadAdmin: document.getElementById("metric-unread-admin"),
  metricUnreadClient: document.getElementById("metric-unread-client"),
  metricMessageCount: document.getElementById("metric-message-count"),
  metricUpdatedAt: document.getElementById("metric-updated-at"),
  connectionStatus: document.getElementById("connection-status"),
};

function appBasePath() {
  const path = window.location.pathname || "/";
  if (path.endsWith("/")) {
    return path;
  }
  const lastSlash = path.lastIndexOf("/");
  return lastSlash >= 0 ? path.slice(0, lastSlash + 1) : "/";
}

function endpoint(path) {
  return new URL(path.replace(/^\/+/, ""), `${window.location.origin}${appBasePath()}`).toString();
}

function wsEndpoint(path) {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  return `${protocol}//${window.location.host}${appBasePath()}${path.replace(/^\/+/, "")}`;
}

function statusLabel(status) {
  switch (status) {
    case "pending":
      return "待处理";
    case "in_progress":
      return "跟进中";
    case "resolved":
      return "已解决";
    default:
      return "未知";
  }
}

function formatDateTime(value) {
  if (!value) {
    return "-";
  }
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }
  return new Intl.DateTimeFormat("zh-CN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(date);
}

function formatRelative(value) {
  if (!value) {
    return "暂无消息";
  }
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }
  const minutes = Math.round((Date.now() - date.getTime()) / 60000);
  if (minutes <= 0) {
    return "刚刚";
  }
  if (minutes < 60) {
    return `${minutes} 分钟前`;
  }
  const hours = Math.round(minutes / 60);
  if (hours < 24) {
    return `${hours} 小时前`;
  }
  const days = Math.round(hours / 24);
  if (days < 7) {
    return `${days} 天前`;
  }
  return formatDateTime(value);
}

function formatBytes(value) {
  const size = Number(value || 0);
  if (!Number.isFinite(size) || size <= 0) {
    return "0 B";
  }
  const units = ["B", "KB", "MB", "GB"];
  let next = size;
  let index = 0;
  while (next >= 1024 && index < units.length - 1) {
    next /= 1024;
    index += 1;
  }
  return `${next.toFixed(index === 0 ? 0 : 1)} ${units[index]}`;
}

const imageAttachmentExtensions = new Set([
  "jpg",
  "jpeg",
  "png",
  "gif",
  "bmp",
  "webp",
  "svg",
  "tif",
  "tiff",
  "avif",
  "heic",
  "heif",
]);

function attachmentExtension(attachment) {
  const candidates = [
    attachment && attachment.name,
    attachment && attachment.url,
  ];
  for (const candidate of candidates) {
    const normalized = String(candidate || "").trim().toLowerCase();
    const match = normalized.match(/\.([a-z0-9]+)(?:$|[?#])/);
    if (match) {
      return match[1];
    }
  }
  return "";
}

function isImageAttachment(attachment) {
  const mimeType = String((attachment && attachment.mime_type) || "").trim().toLowerCase();
  return mimeType.startsWith("image/") || imageAttachmentExtensions.has(attachmentExtension(attachment));
}

function authHeaders(extra = {}) {
  return {
    Authorization: `Bearer ${state.token}`,
    ...extra,
  };
}

async function requestJson(url, options = {}) {
  const response = await fetch(url, options);
  if (response.status === 401) {
    logout("登录已失效，请重新登录。");
    throw new Error("登录已失效，请重新登录。");
  }
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(data.detail || "请求失败");
  }
  return data;
}

function setConnectionStatus(text, weak = false) {
  elements.connectionStatus.textContent = text;
  elements.connectionStatus.className = weak ? "weak" : "muted";
}

function selectedConversation() {
  return state.conversations.find((item) => item.conversation_id === state.selectedConversationId) || null;
}

function persistToken(token) {
  if (token) {
    window.localStorage.setItem(storageKey, token);
  } else {
    window.localStorage.removeItem(storageKey);
  }
}

function logout(message = "") {
  state.token = "";
  persistToken("");
  disconnectSocket();
  state.selectedConversationId = "";
  state.conversations = [];
  state.messages = [];
  renderConversationList();
  renderMessages();
  renderConversationMeta();
  setComposerEnabled(false);
  setConversationActionsEnabled(false);
  elements.loginOverlay.classList.remove("hidden");
  elements.loginPassword.value = "";
  elements.loginError.textContent = message;
  setConnectionStatus("未登录", true);
}

function setComposerEnabled(enabled) {
  elements.sendReply.disabled = !enabled;
  elements.replyText.disabled = !enabled;
  elements.replyFiles.disabled = !enabled;
}

function setConversationActionsEnabled(enabled) {
  elements.conversationStatus.disabled = !enabled;
  elements.deleteConversation.disabled = !enabled;
}

function updatePendingFiles() {
  elements.replyFileList.innerHTML = "";
  state.pendingFiles.forEach((file, index) => {
    const pill = document.createElement("span");
    pill.className = "pending-file";
    pill.innerHTML = `<span>${file.name}</span><button type="button" aria-label="移除附件">×</button>`;
    pill.querySelector("button").addEventListener("click", () => {
      state.pendingFiles.splice(index, 1);
      updatePendingFiles();
    });
    elements.replyFileList.appendChild(pill);
  });
}

function mergeConversation(conversation) {
  const index = state.conversations.findIndex((item) => item.conversation_id === conversation.conversation_id);
  if (index >= 0) {
    state.conversations[index] = { ...state.conversations[index], ...conversation };
  } else {
    state.conversations.push(conversation);
  }
  state.conversations.sort((left, right) => {
    const leftTime = left.latest_message_at || left.updated_at || "";
    const rightTime = right.latest_message_at || right.updated_at || "";
    return rightTime.localeCompare(leftTime);
  });
}

function removeConversation(conversationId) {
  state.conversations = state.conversations.filter((item) => item.conversation_id !== conversationId);
}

function renderConversationList() {
  const keyword = elements.conversationSearch.value.trim().toLowerCase();
  const filtered = state.conversations.filter((conversation) => {
    if (!keyword) {
      return true;
    }
    const haystack = [
      conversation.nickname,
      conversation.contact,
      conversation.project_name,
      conversation.latest_preview,
    ]
      .join(" ")
      .toLowerCase();
    return haystack.includes(keyword);
  });

  if (filtered.length === 0) {
    elements.conversationList.innerHTML = '<div class="empty-state">当前没有匹配的用户会话。</div>';
    return;
  }

  elements.conversationList.innerHTML = "";
  filtered.forEach((conversation) => {
    const item = document.createElement("div");
    item.className = `conversation-item${conversation.conversation_id === state.selectedConversationId ? " active" : ""}`;
    item.innerHTML = `
      <button type="button">
        <div class="conversation-row space">
          <div class="conversation-name">${escapeHtml(conversation.nickname || "未命名用户")}</div>
          <span class="status-chip ${conversation.status}">${statusLabel(conversation.status)}</span>
        </div>
        <div class="conversation-row space" style="margin-top: 6px;">
          <span class="conversation-contact">${escapeHtml(conversation.contact || "未填写联系方式")}</span>
          ${conversation.unread_admin > 0 ? `<span class="badge">${conversation.unread_admin}</span>` : `<span class="conversation-updated">${escapeHtml(formatRelative(conversation.latest_message_at || conversation.updated_at))}</span>`}
        </div>
        <div class="conversation-preview">${escapeHtml(conversation.latest_preview || "等待用户发起第一条消息")}</div>
      </button>
    `;
    item.querySelector("button").addEventListener("click", () => {
      selectConversation(conversation.conversation_id);
    });
    elements.conversationList.appendChild(item);
  });
}

function renderConversationMeta() {
  const conversation = selectedConversation();
  if (!conversation) {
    elements.conversationTitle.textContent = "选择左侧会话开始处理";
    elements.conversationSubtitle.textContent = "可查看用户的历史消息、附件与当前处理状态。";
    elements.conversationStatus.value = "pending";
    elements.metricUnreadAdmin.textContent = "0";
    elements.metricUnreadClient.textContent = "0";
    elements.metricMessageCount.textContent = "0";
    elements.metricUpdatedAt.textContent = "-";
    elements.conversationMeta.innerHTML = `
      <div><dt>昵称</dt><dd>未选择</dd></div>
      <div><dt>联系方式</dt><dd>未选择</dd></div>
      <div><dt>软件版本</dt><dd>未选择</dd></div>
      <div><dt>系统信息</dt><dd>未选择</dd></div>
      <div><dt>项目上下文</dt><dd>未选择</dd></div>
    `;
    return;
  }

  elements.conversationTitle.textContent = conversation.nickname || "未命名用户";
  elements.conversationSubtitle.textContent = `${conversation.contact || "未填写联系方式"} · ${statusLabel(conversation.status)} · 最近更新 ${formatRelative(conversation.latest_message_at || conversation.updated_at)}`;
  elements.conversationStatus.value = conversation.status || "pending";
  elements.metricUnreadAdmin.textContent = String(conversation.unread_admin || 0);
  elements.metricUnreadClient.textContent = String(conversation.unread_client || 0);
  elements.metricMessageCount.textContent = String(state.messages.length);
  elements.metricUpdatedAt.textContent = formatDateTime(conversation.updated_at || conversation.latest_message_at);
  const projectText = conversation.project_name
    ? `${conversation.project_name}${conversation.project_path ? `\n${conversation.project_path}` : ""}`
    : (conversation.project_path || "未提供");
  const projectHtml = escapeHtml(projectText).replace(/\n/g, "<br>");
  elements.conversationMeta.innerHTML = `
    <div><dt>昵称</dt><dd>${escapeHtml(conversation.nickname || "未填写")}</dd></div>
    <div><dt>联系方式</dt><dd>${escapeHtml(conversation.contact || "未填写")}</dd></div>
    <div><dt>软件版本</dt><dd>${escapeHtml(conversation.app_version || "未提供")}</dd></div>
    <div><dt>系统信息</dt><dd>${escapeHtml(conversation.system_summary || "未提供")}</dd></div>
    <div><dt>项目上下文</dt><dd>${projectHtml}</dd></div>
  `;
}

function renderMessages() {
  if (state.messages.length === 0) {
    elements.messageList.innerHTML = '<div class="empty-state">选择会话后，这里会显示完整聊天记录。</div>';
    return;
  }

  elements.messageList.innerHTML = state.messages
    .map((message) => {
      const roleLabel = message.sender_role === "admin" ? "开发者回复" : "用户反馈";
      const attachments = (message.attachments || [])
        .map((attachment) => {
          const preview = isImageAttachment(attachment) ? `<img src="${attachment.url}" alt="${escapeHtml(attachment.name)}">` : "";
          return `
            <a class="attachment-card" href="${attachment.url}" target="_blank" rel="noreferrer">
              ${preview}
              <span class="attachment-name">${escapeHtml(attachment.name)}</span>
              <span class="attachment-meta">${escapeHtml(attachment.mime_type || "application/octet-stream")} · ${formatBytes(attachment.size_bytes)}</span>
            </a>
          `;
        })
        .join("");
      return `
        <div class="message-group ${message.sender_role}">
          <div class="message-bubble${attachments ? " has-attachments" : ""}">
            <div class="message-role">
              <strong>${roleLabel}</strong>
              <span>${escapeHtml(formatDateTime(message.created_at))}</span>
            </div>
            ${message.text ? `<p class="message-text">${escapeHtml(message.text)}</p>` : ""}
            ${attachments ? `<div class="attachment-grid">${attachments}</div>` : ""}
          </div>
        </div>
      `;
    })
    .join("");
  elements.messageList.scrollTop = elements.messageList.scrollHeight;
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

async function loadConversations() {
  const data = await requestJson(endpoint("api/admin/conversations"), {
    headers: authHeaders(),
  });
  state.conversations = data.conversations || [];
  renderConversationList();
  if (!state.selectedConversationId && state.conversations.length > 0) {
    await selectConversation(state.conversations[0].conversation_id);
    return;
  }
  if (state.selectedConversationId && !selectedConversation()) {
    clearSelectedConversation();
    return;
  }
  renderConversationMeta();
}

async function loadConversationMessages(conversationId) {
  if (!conversationId) {
    return;
  }
  state.loadingMessages = true;
  const data = await requestJson(endpoint(`api/admin/conversations/${conversationId}/messages`), {
    headers: authHeaders(),
  });
  mergeConversation(data.conversation);
  state.selectedConversationId = conversationId;
  state.messages = data.messages || [];
  renderConversationList();
  renderConversationMeta();
  renderMessages();
  state.loadingMessages = false;
}

async function selectConversation(conversationId) {
  if (!conversationId) {
    return;
  }
  setConversationActionsEnabled(true);
  setComposerEnabled(true);
  await loadConversationMessages(conversationId);
}

function appendMessage(message) {
  if (state.messages.some((item) => item.id === message.id)) {
    return;
  }
  state.messages.push(message);
  state.messages.sort((left, right) => left.id - right.id);
}

function removeMessagesByIds(messageIds) {
  if (!Array.isArray(messageIds) || messageIds.length === 0) {
    return;
  }
  const deleted = new Set(messageIds.map((value) => Number(value)));
  state.messages = state.messages.filter((item) => !deleted.has(Number(item.id)));
}

function clearSelectedConversation() {
  state.selectedConversationId = "";
  state.messages = [];
  renderConversationList();
  renderConversationMeta();
  renderMessages();
  setComposerEnabled(false);
  setConversationActionsEnabled(false);
}

async function sendReply() {
  const conversation = selectedConversation();
  if (!conversation) {
    return;
  }
  const text = elements.replyText.value.trim();
  if (!text && state.pendingFiles.length === 0) {
    return;
  }

  const payload = new FormData();
  payload.append("text", text);
  state.pendingFiles.forEach((file) => payload.append("files", file, file.name));

  elements.sendReply.disabled = true;
  try {
    const data = await requestJson(endpoint(`api/admin/conversations/${conversation.conversation_id}/messages`), {
      method: "POST",
      headers: authHeaders(),
      body: payload,
    });
    mergeConversation(data.conversation);
    appendMessage(data.message);
    elements.replyText.value = "";
    state.pendingFiles = [];
    updatePendingFiles();
    renderConversationList();
    renderConversationMeta();
    renderMessages();
  } finally {
    elements.sendReply.disabled = false;
  }
}

async function submitReplyFromComposer() {
  try {
    await sendReply();
  } catch (error) {
    setConnectionStatus(error.message || "发送回复失败", true);
  }
}

async function updateStatus(status) {
  const conversation = selectedConversation();
  if (!conversation) {
    return;
  }
  const data = await requestJson(endpoint(`api/admin/conversations/${conversation.conversation_id}/status`), {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({ status }),
  });
  mergeConversation(data.conversation);
  renderConversationList();
  renderConversationMeta();
}

async function deleteConversation() {
  const conversation = selectedConversation();
  if (!conversation) {
    return;
  }

  const confirmed = window.confirm(`确认永久删除会话“${conversation.nickname || "未命名用户"}”吗？此操作会删除全部消息与附件，且无法恢复。`);
  if (!confirmed) {
    return;
  }

  elements.deleteConversation.disabled = true;
  try {
    const data = await requestJson(endpoint(`api/admin/conversations/${conversation.conversation_id}`), {
      method: "DELETE",
      headers: authHeaders(),
    });
    removeConversation(data.conversation_id);
    if (state.selectedConversationId === data.conversation_id) {
      clearSelectedConversation();
    } else {
      renderConversationList();
      renderConversationMeta();
      renderMessages();
    }
    setConnectionStatus("会话已删除");
  } finally {
    if (selectedConversation()) {
      elements.deleteConversation.disabled = false;
    }
  }
}

function disconnectSocket() {
  if (state.heartbeatTimer) {
    window.clearInterval(state.heartbeatTimer);
    state.heartbeatTimer = null;
  }
  if (state.reconnectTimer) {
    window.clearTimeout(state.reconnectTimer);
    state.reconnectTimer = null;
  }
  if (state.socket) {
    state.socket.onclose = null;
    state.socket.close();
    state.socket = null;
  }
}

function connectSocket() {
  if (!state.token) {
    return;
  }
  disconnectSocket();
  setConnectionStatus("实时通道连接中…");
  const socket = new WebSocket(`${wsEndpoint("ws/admin")}?token=${encodeURIComponent(state.token)}`);
  state.socket = socket;

  socket.onopen = () => {
    setConnectionStatus("已连接实时推送");
    state.heartbeatTimer = window.setInterval(() => {
      if (state.socket && state.socket.readyState === WebSocket.OPEN) {
        state.socket.send("ping");
      }
    }, 20000);
  };

  socket.onmessage = async (event) => {
    const payload = JSON.parse(event.data);
    if (payload.conversation) {
      mergeConversation(payload.conversation);
      renderConversationList();
      if (payload.conversation.conversation_id === state.selectedConversationId) {
        renderConversationMeta();
      }
    }

    if (payload.type === "message.created" && payload.message) {
      if (payload.message.conversation_id === state.selectedConversationId) {
        appendMessage(payload.message);
        renderMessages();
        if (payload.message.sender_role === "client" && !state.loadingMessages) {
          await loadConversationMessages(state.selectedConversationId);
        }
      }
    }

    if (payload.type === "message.deleted" && payload.message_id) {
      if (payload.conversation?.conversation_id === state.selectedConversationId) {
        removeMessagesByIds([payload.message_id]);
        renderConversationMeta();
        renderMessages();
      }
    }

    if (payload.type === "messages.cleared") {
      if (payload.conversation?.conversation_id === state.selectedConversationId) {
        removeMessagesByIds(payload.message_ids || []);
        renderConversationMeta();
        renderMessages();
      }
    }

    if (payload.type === "conversation.deleted" && payload.conversation_id) {
      removeConversation(payload.conversation_id);
      if (payload.conversation_id === state.selectedConversationId) {
        clearSelectedConversation();
      } else {
        renderConversationList();
      }
    }
  };

  socket.onclose = () => {
    if (state.heartbeatTimer) {
      window.clearInterval(state.heartbeatTimer);
      state.heartbeatTimer = null;
    }
    state.socket = null;
    if (!state.token) {
      return;
    }
    setConnectionStatus("实时通道已断开，正在重连…");
    state.reconnectTimer = window.setTimeout(connectSocket, 1800);
  };

  socket.onerror = () => {
    setConnectionStatus("实时通道异常，正在尝试恢复…");
  };
}

async function login(username, password) {
  const data = await requestJson(endpoint("api/admin/login"), {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  state.token = data.token;
  persistToken(data.token);
  elements.loginOverlay.classList.add("hidden");
  elements.loginError.textContent = "";
  await loadConversations();
  connectSocket();
}

async function bootstrap() {
  setComposerEnabled(false);
  setConversationActionsEnabled(false);
  renderConversationMeta();
  renderConversationList();
  updatePendingFiles();

  elements.loginForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    elements.loginError.textContent = "";
    try {
      await login(elements.loginUsername.value.trim(), elements.loginPassword.value);
    } catch (error) {
      elements.loginError.textContent = error.message || "登录失败";
    }
  });

  elements.refreshConversations.addEventListener("click", async () => {
    try {
      await loadConversations();
      if (state.selectedConversationId) {
        await loadConversationMessages(state.selectedConversationId);
      }
    } catch (error) {
      setConnectionStatus(error.message || "刷新失败", true);
    }
  });

  elements.conversationSearch.addEventListener("input", renderConversationList);
  elements.conversationStatus.addEventListener("change", async () => {
    try {
      await updateStatus(elements.conversationStatus.value);
    } catch (error) {
      setConnectionStatus(error.message || "状态更新失败", true);
    }
  });

  elements.deleteConversation.addEventListener("click", async () => {
    try {
      await deleteConversation();
    } catch (error) {
      setConnectionStatus(error.message || "删除会话失败", true);
      if (selectedConversation()) {
        elements.deleteConversation.disabled = false;
      }
    }
  });

  elements.replyFiles.addEventListener("change", () => {
    const files = Array.from(elements.replyFiles.files || []);
    if (files.length > 0) {
      state.pendingFiles.push(...files);
      updatePendingFiles();
    }
    elements.replyFiles.value = "";
  });

  elements.sendReply.addEventListener("click", submitReplyFromComposer);

  elements.replyText.addEventListener("keydown", async (event) => {
    if (event.isComposing) {
      return;
    }

    if ((event.key === "Enter" || event.key === "NumpadEnter")
        && !event.ctrlKey
        && !event.metaKey
        && !event.shiftKey
        && !event.altKey) {
      event.preventDefault();
      await submitReplyFromComposer();
    }
  });

  const savedToken = window.localStorage.getItem(storageKey) || "";
  if (!savedToken) {
    setConnectionStatus("未登录", true);
    return;
  }

  state.token = savedToken;
  elements.loginOverlay.classList.add("hidden");
  try {
    await loadConversations();
    connectSocket();
  } catch (error) {
    logout(error.message || "自动登录失败，请重新登录。");
  }
}

bootstrap().catch((error) => {
  console.error(error);
  logout(error.message || "初始化失败，请重新登录。");
});
