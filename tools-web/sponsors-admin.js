(() => {
  "use strict";
  const TOKEN_KEY = "cinevault-sponsor-admin-token";
  const local = ["127.0.0.1", "localhost"].includes(location.hostname);
  const apiBase = local ? "http://127.0.0.1:3413" : "";
  const api = (path) => `${apiBase}/api/cinevault-support/v1${path}`;
  const login = document.querySelector("[data-admin-login]");
  const loginForm = document.querySelector("[data-admin-login-form]");
  const tokenInput = document.querySelector("[data-admin-token]");
  const loginStatus = document.querySelector("[data-admin-login-status]");
  const workspace = document.querySelector("[data-admin-workspace]");
  const claimsRoot = document.querySelector("[data-admin-claims]");
  const summary = document.querySelector("[data-admin-summary]");
  let activeStatus = "pending";

  const token = () => sessionStorage.getItem(TOKEN_KEY) || "";
  const money = (value) => new Intl.NumberFormat("zh-CN", { style: "currency", currency: "CNY" }).format(Number(value));
  const time = (value) => { const date = new Date(value); return Number.isNaN(date.getTime()) ? "时间未知" : new Intl.DateTimeFormat("zh-CN", { year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" }).format(date); };

  async function request(path, options = {}) {
    const response = await fetch(api(path), { ...options, headers: { Accept: "application/json", "X-Admin-Token": token(), ...(options.body ? { "Content-Type": "application/json" } : {}), ...(options.headers || {}) } });
    const payload = await response.json().catch(() => ({})); if (!response.ok) throw new Error(payload.detail || "请求失败"); return payload;
  }

  function renderClaims(items) {
    claimsRoot.replaceChildren(); const labels = { pending: "待确认", confirmed: "已确认", rejected: "已拒绝" }; summary.textContent = `共 ${items.length} 条${labels[activeStatus]}记录`;
    if (!items.length) { const empty = document.createElement("div"); empty.className = "admin-empty"; empty.textContent = "当前没有记录。"; claimsRoot.appendChild(empty); return; }
    items.forEach((item) => {
      const card = document.createElement("article"); card.className = "admin-claim";
      const main = document.createElement("div"); main.className = "admin-claim-main";
      const person = document.createElement("div"); const name = document.createElement("strong"); name.textContent = item.displayName; const channel = document.createElement("span"); channel.textContent = item.paymentChannel === "alipay" ? "支付宝" : "微信"; person.append(name, channel);
      const amountWrap = document.createElement("div"); const amount = document.createElement("strong"); amount.className = "admin-claim-amount"; amount.textContent = money(item.amount); const amountLabel = document.createElement("span"); amountLabel.textContent = "用户自报金额"; amountWrap.append(amount, amountLabel);
      const timeWrap = document.createElement("div"); const submitted = document.createElement("time"); submitted.dateTime = item.submittedAt; submitted.textContent = time(item.submittedAt); const timeLabel = document.createElement("span"); timeLabel.textContent = "提交时间"; timeWrap.append(submitted, timeLabel); main.append(person, amountWrap, timeWrap); card.appendChild(main);
      if (activeStatus === "pending") { const actions = document.createElement("div"); actions.className = "admin-claim-actions"; const confirm = document.createElement("button"); confirm.className = "admin-confirm"; confirm.type = "button"; confirm.textContent = "确认到账"; confirm.addEventListener("click", () => updateClaim(item, "confirm")); const reject = document.createElement("button"); reject.className = "admin-reject"; reject.type = "button"; reject.textContent = "拒绝"; reject.addEventListener("click", () => updateClaim(item, "reject")); actions.append(confirm, reject); card.appendChild(actions); }
      claimsRoot.appendChild(card);
    });
  }

  async function loadClaims() {
    summary.textContent = "正在加载…"; claimsRoot.replaceChildren();
    try { const payload = await request(`/admin/claims?claim_status=${encodeURIComponent(activeStatus)}`); login.hidden = true; workspace.hidden = false; renderClaims(Array.isArray(payload.claims) ? payload.claims : []); }
    catch (error) { if (/令牌|Token|token/.test(error.message)) { sessionStorage.removeItem(TOKEN_KEY); workspace.hidden = true; login.hidden = false; loginStatus.textContent = error.message; } else { summary.textContent = error.message || "加载失败"; } }
  }

  async function updateClaim(item, action) {
    const verb = action === "confirm" ? "确认到账并公开上榜" : "拒绝这条记录";
    if (!window.confirm(`${verb}：${item.displayName} ${money(item.amount)}？`)) return;
    try { await request(`/admin/claims/${encodeURIComponent(item.id)}`, { method: "POST", body: JSON.stringify({ action }) }); await loadClaims(); }
    catch (error) { window.alert(error.message || "操作失败"); }
  }

  loginForm.addEventListener("submit", async (event) => { event.preventDefault(); const value = tokenInput.value.trim(); if (!value) return; sessionStorage.setItem(TOKEN_KEY, value); loginStatus.textContent = "正在验证…"; await loadClaims(); });
  document.querySelectorAll("[data-admin-status]").forEach((button) => button.addEventListener("click", () => { activeStatus = button.dataset.adminStatus; document.querySelectorAll("[data-admin-status]").forEach((item) => item.classList.toggle("is-active", item === button)); loadClaims(); }));
  document.querySelector("[data-admin-refresh]").addEventListener("click", loadClaims);
  document.querySelector("[data-admin-logout]").addEventListener("click", () => { sessionStorage.removeItem(TOKEN_KEY); workspace.hidden = true; login.hidden = false; tokenInput.value = ""; loginStatus.textContent = ""; tokenInput.focus(); });
  if (token()) loadClaims();
})();
