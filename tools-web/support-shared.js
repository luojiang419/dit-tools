(() => {
  "use strict";

  const PENDING_KEY = "cinevault-pending-support-claims";
  const isLocalPreview = ["127.0.0.1", "localhost"].includes(window.location.hostname);
  const apiBase = isLocalPreview ? "http://127.0.0.1:3413" : "";
  const api = (path) => `${apiBase}/api/cinevault-support/v1${path}`;

  function readPending() {
    try {
      const value = JSON.parse(localStorage.getItem(PENDING_KEY) || "[]");
      return Array.isArray(value) ? value.slice(0, 8) : [];
    } catch (_) {
      return [];
    }
  }

  function savePending(items) {
    localStorage.setItem(PENDING_KEY, JSON.stringify(items.slice(0, 8)));
  }

  async function submitClaim(form) {
    const data = new FormData(form);
    const payload = {
      displayName: String(data.get("displayName") || "").trim(),
      amount: String(data.get("amount") || "").trim(),
      paymentChannel: String(data.get("paymentChannel") || "wechat"),
      website: String(data.get("website") || ""),
    };
    const response = await fetch(api("/claims"), {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(result.detail || "提交失败，请稍后重试。");
    const pending = [result.claim, ...readPending().filter((item) => item.id !== result.claim.id)];
    savePending(pending);
    return result.claim;
  }

  window.CineVaultSupport = Object.freeze({ api, readPending, savePending, submitClaim });
})();
