(() => {
  "use strict";
  const root = document.documentElement;
  const form = document.querySelector("[data-support-form]");
  const formStatus = document.querySelector("[data-form-status]");
  const list = document.querySelector("[data-sponsor-list]");
  const count = document.querySelector("[data-sponsor-count]");
  const pendingCard = document.querySelector("[data-pending-card]");
  const pendingText = document.querySelector("[data-pending-text]");
  const support = window.CineVaultSupport;
  const THEME_KEY = "cinevault-web-theme";

  function applyTheme(value) {
    const next = value === "light" ? "light" : "dark";
    root.dataset.theme = next;
    document.querySelectorAll("[data-theme-toggle]").forEach((button) => {
      button.querySelector(".theme-toggle__label").textContent = next === "dark" ? "暗色" : "浅色";
      button.setAttribute("aria-label", next === "dark" ? "切换为浅色主题" : "切换为暗色主题");
    });
  }
  applyTheme(localStorage.getItem(THEME_KEY) || "dark");
  document.querySelectorAll("[data-theme-toggle]").forEach((button) => button.addEventListener("click", () => {
    const next = root.dataset.theme === "dark" ? "light" : "dark";
    localStorage.setItem(THEME_KEY, next); applyTheme(next);
  }));

  function money(value) { return new Intl.NumberFormat("zh-CN", { style: "currency", currency: "CNY" }).format(Number(value)); }
  function time(value) { const date = new Date(value); return Number.isNaN(date.getTime()) ? "时间待确认" : new Intl.DateTimeFormat("zh-CN", { year: "numeric", month: "2-digit", day: "2-digit" }).format(date); }

  function renderPending(items) {
    pendingCard.hidden = items.length === 0;
    if (!items.length) return;
    const first = items[0];
    pendingText.textContent = `${first.displayName} · ${money(first.amount)}${items.length > 1 ? `，另有 ${items.length - 1} 条待确认` : ""}`;
  }

  function renderSponsors(items) {
    count.textContent = String(items.length);
    list.replaceChildren(); list.setAttribute("aria-busy", "false");
    if (!items.length) { const empty = document.createElement("div"); empty.className = "sponsor-empty"; empty.textContent = "赞助榜还在等待第一位热心朋友。"; list.appendChild(empty); return; }
    items.forEach((item) => {
      const card = document.createElement("article"); card.className = "sponsor-item";
      const avatar = document.createElement("span"); avatar.className = "sponsor-avatar"; avatar.textContent = String(item.displayName || "友").trim().slice(0, 1) || "友";
      const info = document.createElement("div"); info.className = "sponsor-info";
      const name = document.createElement("strong"); name.textContent = item.displayName || "热心朋友";
      const submitted = document.createElement("time"); submitted.dateTime = item.submittedAt || ""; submitted.textContent = time(item.submittedAt);
      const amount = document.createElement("span"); amount.className = "sponsor-amount"; amount.textContent = money(item.amount);
      info.append(name, submitted); card.append(avatar, info, amount); list.appendChild(card);
    });
  }

  async function loadSponsors() {
    try { const response = await fetch(support.api("/sponsors"), { headers: { Accept: "application/json" } }); if (!response.ok) throw new Error(); const payload = await response.json(); renderSponsors(Array.isArray(payload.sponsors) ? payload.sponsors : []); }
    catch { count.textContent = "—"; list.setAttribute("aria-busy", "false"); list.innerHTML = '<div class="sponsor-empty">赞助榜暂时无法加载，请稍后再试。</div>'; }
  }

  async function refreshPending() {
    const items = support.readPending(); const next = [];
    for (const item of items) {
      try { const response = await fetch(support.api(`/claims/${encodeURIComponent(item.id)}`)); if (!response.ok) { next.push(item); continue; } const payload = await response.json(); if (payload.claim?.status === "pending") next.push(payload.claim); }
      catch { next.push(item); }
    }
    support.savePending(next); renderPending(next); if (next.length !== items.length) loadSponsors();
  }

  form?.addEventListener("submit", async (event) => {
    event.preventDefault(); if (!form.reportValidity()) return;
    const submit = form.querySelector('button[type="submit"]');
    submit.disabled = true; formStatus.className = "form-status"; formStatus.textContent = "正在提交赞助记录…";
    try {
      await support.submitClaim(form); renderPending(support.readPending());
      form.reset(); formStatus.className = "form-status is-success"; formStatus.textContent = "提交成功！管理员核实到账后会公开显示在赞助榜。";
    } catch (error) { formStatus.className = "form-status is-error"; formStatus.textContent = error.message || "提交失败，请稍后再试。"; }
    finally { submit.disabled = false; }
  });
  document.querySelector("[data-refresh-pending]")?.addEventListener("click", refreshPending);
  document.querySelectorAll("[data-year]").forEach((item) => { item.textContent = String(new Date().getFullYear()); });
  renderPending(support.readPending()); loadSponsors(); refreshPending();
  if (new URLSearchParams(location.search).get("support") === "1") document.querySelector("#support")?.scrollIntoView();
})();
