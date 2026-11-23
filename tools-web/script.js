(() => {
  "use strict";

  const root = document.documentElement;
  const body = document.body;
  const header = document.querySelector(".site-header");
  const navToggle = document.querySelector(".nav-toggle");
  const nav = document.querySelector(".site-nav");
  const themeButtons = document.querySelectorAll("[data-theme-toggle]");
  const downloadModal = document.querySelector("[data-download-modal]");
  const modalSupportForm = document.querySelector("[data-modal-support-form]");
  const modalSupportStatus = document.querySelector("[data-modal-form-status]");
  const imageModal = document.querySelector("[data-image-modal]");
  const imagePreview = document.querySelector("[data-image-preview]");
  const imageTitle = document.querySelector("[data-image-title]");
  const demoFrame = document.querySelector("[data-demo-frame]");
  const demoPoster = document.querySelector("[data-demo-poster]");
  const demoStatus = document.querySelector("[data-demo-status]");
  const demoClose = document.querySelector("[data-close-demo]");
  const support = window.CineVaultSupport;
  const THEME_KEY = "cinevault-web-theme";
  let activeModal = null;
  let lastFocused = null;
  let imageModalParent = null;
  let imageModalTrigger = null;

  function formatBytes(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value <= 0) return "安装包";
    return `${(value / 1024 / 1024).toFixed(1)} MB`;
  }

  async function loadRelease() {
    try {
      const response = await fetch("downloads/release.json", { cache: "no-store" });
      if (!response.ok) throw new Error("版本清单不可用");
      const release = await response.json();
      document.querySelectorAll("[data-release-version]").forEach((item) => { item.textContent = release.version; });
      document.querySelectorAll("[data-release-size]").forEach((item) => { item.textContent = formatBytes(release.sizeBytes); });
      document.querySelectorAll("[data-release-date]").forEach((item) => { item.textContent = String(release.publishedAt || "").slice(0, 10); });
      document.querySelectorAll("[data-release-notes]").forEach((item) => { item.href = release.releaseNotesUrl; });

      let selectedUrl = release.fallbackUrl;
      let sourceLabel = "当前使用 GitHub Release 下载源";
      if (release.serverUrl && new URL(release.serverUrl, location.href).origin === location.origin) {
        try {
          const probe = await fetch(release.serverUrl, { method: "HEAD", cache: "no-store" });
          if (probe.ok) { selectedUrl = release.serverUrl; sourceLabel = "当前使用官网服务器直连下载"; }
        } catch (_) { /* 保留备用地址 */ }
      }
      document.querySelectorAll("[data-download-link]").forEach((item) => { item.href = selectedUrl; });
      const source = document.querySelector("[data-download-source]");
      if (source) source.textContent = sourceLabel;
    } catch (_) {
      const source = document.querySelector("[data-download-source]");
      if (source) source.textContent = "当前使用 GitHub Release 备用下载源";
    }
  }

  function applyTheme(value) {
    const next = value === "light" ? "light" : "dark";
    root.dataset.theme = next;
    root.style.colorScheme = next;
    document.querySelector('meta[name="theme-color"]')?.setAttribute("content", next === "dark" ? "#0e1014" : "#f5f7fb");
    themeButtons.forEach((button) => {
      button.setAttribute("aria-label", next === "dark" ? "切换为浅色主题" : "切换为暗色主题");
      const label = button.querySelector(".theme-toggle__label");
      if (label) label.textContent = next === "dark" ? "暗色" : "浅色";
    });
  }

  applyTheme(localStorage.getItem(THEME_KEY) || "dark");
  themeButtons.forEach((button) => button.addEventListener("click", () => {
    const next = root.dataset.theme === "dark" ? "light" : "dark";
    localStorage.setItem(THEME_KEY, next);
    applyTheme(next);
  }));

  navToggle?.addEventListener("click", () => {
    const open = !nav?.classList.contains("is-open");
    nav?.classList.toggle("is-open", open);
    navToggle.setAttribute("aria-expanded", String(open));
    navToggle.setAttribute("aria-label", open ? "收起导航" : "展开导航");
  });
  nav?.querySelectorAll("a").forEach((link) => link.addEventListener("click", () => {
    nav.classList.remove("is-open");
    navToggle?.setAttribute("aria-expanded", "false");
  }));

  function setModal(modal, open, trigger = null) {
    if (!modal) return;
    if (open) {
      if (activeModal && activeModal !== modal) setModal(activeModal, false);
      lastFocused = trigger || document.activeElement;
      modal.hidden = false;
      activeModal = modal;
      body.classList.add("is-modal-open");
      window.setTimeout(() => modal.querySelector(".modal__close")?.focus(), 0);
    } else {
      modal.hidden = true;
      if (activeModal === modal) activeModal = null;
      if (!activeModal) body.classList.remove("is-modal-open");
      if (lastFocused?.isConnected) lastFocused.focus();
      lastFocused = null;
    }
  }

  function revealSupportForm(shouldFocus = true) {
    if (!modalSupportForm) return;
    modalSupportForm.hidden = false;
    document.querySelector("[data-reveal-support]")?.setAttribute("aria-expanded", "true");
    modalSupportForm.scrollIntoView({ behavior: "smooth", block: "nearest" });
    if (shouldFocus) window.setTimeout(() => modalSupportForm.querySelector('input[name="displayName"]')?.focus(), 260);
  }

  function openImageModal(image) {
    if (!imageModal || !imagePreview || !image) return;
    imageModalParent = activeModal;
    imageModalTrigger = image;
    imagePreview.src = image.currentSrc || image.src;
    imagePreview.alt = image.alt || "二维码预览";
    if (imageTitle) imageTitle.textContent = image.alt || "二维码预览";
    imageModal.hidden = false;
    activeModal = imageModal;
    body.classList.add("is-modal-open");
    window.setTimeout(() => imageModal.querySelector(".modal__close")?.focus(), 0);
  }

  function closeImageModal() {
    if (!imageModal) return;
    imageModal.hidden = true;
    imagePreview?.removeAttribute("src");
    activeModal = imageModalParent && !imageModalParent.hidden ? imageModalParent : null;
    imageModalParent = null;
    if (!activeModal) body.classList.remove("is-modal-open");
    if (imageModalTrigger?.isConnected) imageModalTrigger.focus();
    imageModalTrigger = null;
  }

  document.querySelectorAll("[data-open-download]").forEach((button) => button.addEventListener("click", () => setModal(downloadModal, true, button)));
  document.querySelectorAll("[data-close-download]").forEach((button) => button.addEventListener("click", () => setModal(downloadModal, false)));
  document.querySelectorAll("[data-open-support]").forEach((button) => button.addEventListener("click", () => {
    setModal(downloadModal, true, button);
    window.setTimeout(() => revealSupportForm(false), 80);
  }));
  document.querySelector("[data-reveal-support]")?.addEventListener("click", () => revealSupportForm(true));
  document.querySelectorAll("[data-close-image]").forEach((button) => button.addEventListener("click", closeImageModal));
  document.querySelectorAll("[data-zoomable]").forEach((image) => {
    image.addEventListener("click", () => openImageModal(image));
    image.addEventListener("keydown", (event) => {
      if (event.key !== "Enter" && event.key !== " ") return;
      event.preventDefault();
      openImageModal(image);
    });
  });

  document.querySelectorAll("[data-copy-value]").forEach((button) => button.addEventListener("click", async () => {
    const value = button.dataset.copyValue || "";
    const label = button.querySelector("strong");
    const original = label?.textContent || "";
    try {
      if (navigator.clipboard?.writeText) await navigator.clipboard.writeText(value);
      else {
        const input = document.createElement("textarea");
        input.value = value; input.style.position = "fixed"; input.style.opacity = "0";
        body.appendChild(input); input.select(); document.execCommand("copy"); input.remove();
      }
      button.classList.add("is-copied");
      if (label) label.textContent = "已复制";
      window.setTimeout(() => { button.classList.remove("is-copied"); if (label) label.textContent = original; }, 1300);
    } catch (_) {
      if (label) label.textContent = "复制失败";
      window.setTimeout(() => { if (label) label.textContent = original; }, 1300);
    }
  }));

  modalSupportForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!modalSupportForm.reportValidity()) return;
    const submit = modalSupportForm.querySelector('button[type="submit"]');
    submit.disabled = true;
    modalSupportStatus.className = "form-status";
    modalSupportStatus.textContent = "正在提交赞助记录…";
    try {
      await support.submitClaim(modalSupportForm);
      modalSupportForm.reset();
      modalSupportStatus.className = "form-status is-success";
      modalSupportStatus.textContent = "提交成功！管理员核实到账后会公开显示在赞助榜。";
    } catch (error) {
      modalSupportStatus.className = "form-status is-error";
      modalSupportStatus.textContent = error.message || "提交失败，请稍后再试。";
    } finally {
      submit.disabled = false;
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && activeModal) {
      if (activeModal === imageModal) closeImageModal();
      else setModal(activeModal, false);
    }
    if (event.key !== "Tab" || !activeModal) return;
    const focusable = [...activeModal.querySelectorAll('button:not([disabled]), a[href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])')];
    if (!focusable.length) return;
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
    if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
  });

  document.querySelector("[data-launch-demo]")?.addEventListener("click", () => {
    if (!demoFrame) return;
    demoFrame.src = demoFrame.dataset.src || "demo/";
    demoPoster?.setAttribute("hidden", "");
    demoClose?.removeAttribute("hidden");
    if (demoStatus) demoStatus.textContent = "正在加载安全演示…";
  });
  demoFrame?.addEventListener("load", () => {
    if (demoFrame.src && demoStatus) demoStatus.textContent = "演示已加载 · 使用模拟数据";
  });
  demoClose?.addEventListener("click", () => {
    demoFrame?.removeAttribute("src");
    demoPoster?.removeAttribute("hidden");
    demoClose.hidden = true;
    if (demoStatus) demoStatus.textContent = "演示已关闭，不再占用运行资源";
  });

  const revealItems = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("is-visible");
        observer.unobserve(entry.target);
      });
    }, { rootMargin: "0px 0px -8%", threshold: 0.08 });
    revealItems.forEach((item) => observer.observe(item));
  } else {
    revealItems.forEach((item) => item.classList.add("is-visible"));
  }

  document.querySelectorAll("[data-year]").forEach((item) => { item.textContent = String(new Date().getFullYear()); });
  const updateHeader = () => header?.classList.toggle("is-scrolled", window.scrollY > 8);
  updateHeader();
  window.addEventListener("scroll", updateHeader, { passive: true });
  const pageParams = new URLSearchParams(location.search);
  if (pageParams.get("support") === "1" || pageParams.get("download") === "1") {
    setModal(downloadModal, true);
    if (pageParams.get("support") === "1") revealSupportForm(false);
    history.replaceState({}, "", `${location.pathname}${location.hash}`);
  }
  loadRelease();
})();
