(() => {
  "use strict";

  const app = document.querySelector("[data-app]");
  const viewport = document.querySelector("[data-viewport]");
  const main = document.querySelector("[data-main]");
  const projectChip = document.querySelector("[data-project-chip]");
  const globalSearch = document.querySelector("[data-global-search]");
  const settingsDialog = document.querySelector("[data-settings-dialog]");
  const toast = document.querySelector("[data-toast]");
  const jobTimeline = document.querySelector("[data-job-timeline]");
  const jobBadge = document.querySelector("[data-job-badge]");

  const projects = [
    { id: "city", name: "城市纪录片", path: "D:\\CineVault\\城市纪录片", created: "2026-07-12 09:28", assets: 1284, sources: 3 },
    { id: "brand", name: "品牌宣传片", path: "E:\\Projects\\品牌宣传片", created: "2026-07-08 14:06", assets: 768, sources: 2 },
    { id: "drone", name: "航拍素材库", path: "F:\\Media\\Drone", created: "2026-06-26 18:42", assets: 2406, sources: 5 },
    { id: "interview", name: "人物访谈", path: "D:\\CineVault\\人物访谈", created: "2026-06-18 11:20", assets: 392, sources: 1 }
  ];
  const sources = [
    { id: "all", name: "全部素材源", path: "当前项目全部目录", count: 1284 },
    { id: "cam-a", name: "A 机素材", path: "D:\\Footage\\CAM_A", count: 524 },
    { id: "cam-b", name: "B 机素材", path: "D:\\Footage\\CAM_B", count: 436 },
    { id: "audio", name: "现场录音", path: "D:\\Audio\\Location", count: 118 },
    { id: "stills", name: "剧照与文档", path: "D:\\Production\\Stills", count: 206 }
  ];
  const assets = [
    { id: 1, file: "A001_C004_0713QK.mp4", type: "视频", ext: "MP4", size: "1.86 GB", detail: "3840×2160 · 25fps · H.264", source: "cam-a", favorite: true, thumb: "city", summary: "清晨城市高架上的车流与远处天际线，画面由冷蓝逐渐过渡到暖色。", keywords: ["城市", "清晨", "车流", "航拍"], state: "已解析", confirmed: "待确认" },
    { id: 2, file: "A001_C011_0713QK.mp4", type: "视频", ext: "MP4", size: "2.24 GB", detail: "3840×2160 · 25fps · H.264", source: "cam-a", favorite: false, thumb: "people", summary: "纪录片人物在室内窗边接受采访，中近景，暖色自然光。", keywords: ["人物", "采访", "室内", "暖光"], state: "已解析", confirmed: "已确认" },
    { id: 3, file: "B003_C002_0713LK.mov", type: "视频", ext: "MOV", size: "4.12 GB", detail: "4096×2160 · 50fps · ProRes", source: "cam-b", favorite: true, thumb: "mountain", summary: "无人机掠过山谷与云海，远景层次清晰，适合作为环境建立镜头。", keywords: ["山谷", "云海", "无人机", "远景"], state: "已解析", confirmed: "待确认" },
    { id: 4, file: "B003_C008_0713LK.mov", type: "视频", ext: "MOV", size: "3.48 GB", detail: "4096×2160 · 50fps · ProRes", source: "cam-b", favorite: false, thumb: "night", summary: "夜间街道霓虹与行人剪影，镜头缓慢向前推进。", keywords: ["夜景", "街道", "霓虹", "行人"], state: "待解析", confirmed: "未确认" },
    { id: 5, file: "OFFICE_BROLL_03.mp4", type: "视频", ext: "MP4", size: "986 MB", detail: "3840×2160 · 25fps · H.265", source: "cam-a", favorite: false, thumb: "office", summary: "现代办公室内团队协作与屏幕操作的补充镜头。", keywords: ["办公室", "团队", "工作", "科技"], state: "已解析", confirmed: "待确认" },
    { id: 6, file: "LOCATION_SOUND_012.wav", type: "音频", ext: "WAV", size: "384 MB", detail: "96kHz · 24bit · 2ch", source: "audio", favorite: false, thumb: "audio", summary: "现场环境声与采访主轨。", keywords: ["录音", "采访"], state: "不适用", confirmed: "—" },
    { id: 7, file: "场记单_20260713.pdf", type: "文档", ext: "PDF", size: "2.8 MB", detail: "PDF · 8 页", source: "stills", favorite: true, thumb: "document", summary: "7 月 13 日拍摄场记单与镜号记录。", keywords: ["场记", "拍摄记录"], state: "已索引", confirmed: "—" },
    { id: 8, file: "剧照_采访现场_001.jpg", type: "图片", ext: "JPG", size: "12.6 MB", detail: "6240×4160 · JPEG", source: "stills", favorite: false, thumb: "people", summary: "采访现场工作照，包含摄影机位与灯光布置。", keywords: ["剧照", "采访", "灯光"], state: "已解析", confirmed: "已确认" }
  ];

  const state = {
    workspace: "projects",
    project: null,
    source: "all",
    selectedAsset: 1,
    viewMode: "grid",
    favoritesOnly: false,
    search: "",
    job: null,
    theme: "dark",
    reportPage: 1
  };
  let toastTimer = 0;
  let jobTimer = 0;

  function scaleApp() {
    const scale = Math.min(viewport.clientWidth / 1600, viewport.clientHeight / 980);
    app.style.transform = `translate(-50%, -50%) scale(${scale})`;
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[char]));
  }

  function showToast(message) {
    window.clearTimeout(toastTimer);
    toast.textContent = message;
    toast.hidden = false;
    toastTimer = window.setTimeout(() => { toast.hidden = true; }, 2600);
  }

  function thumb(asset, compact = false) {
    return `<div class="media-thumb media-thumb--${asset.thumb}" data-ext="${asset.ext}">${asset.favorite && !compact ? '<span class="asset-favorite">★</span>' : ""}</div>`;
  }

  function currentAssets() {
    const term = state.search.trim().toLowerCase();
    return assets.filter((asset) => (state.source === "all" || asset.source === state.source)
      && (!state.favoritesOnly || asset.favorite)
      && (!term || `${asset.file} ${asset.type} ${asset.summary} ${asset.keywords.join(" ")}`.toLowerCase().includes(term)));
  }

  function renderTopbar() {
    projectChip.textContent = state.project ? state.project.name : "未选择项目";
    document.querySelectorAll("[data-workspace]").forEach((button) => {
      const target = button.dataset.workspace;
      button.classList.toggle("is-active", target === state.workspace);
      button.disabled = !state.project && ["library", "center", "report", "jobs"].includes(target);
    });
    globalSearch.placeholder = state.workspace === "projects" ? "搜索项目..." : "搜索素材...";
    globalSearch.value = state.search;
    globalSearch.closest("label").hidden = state.workspace === "feedback";
  }

  function renderProjectLibrary() {
    const term = state.search.trim().toLowerCase();
    const list = projects.filter((project) => !term || `${project.name} ${project.path}`.toLowerCase().includes(term));
    main.innerHTML = `<section class="workspace project-workspace"><div class="workspace-scroll"><header class="workspace-header"><div class="workspace-title"><h1>项目库</h1><p>${list.length} 个项目可用 · 演示项目均为模拟数据</p></div><div class="workspace-actions"><button class="action-button action-button--primary" type="button" data-action="new-project">新建项目</button><button class="action-button" type="button" data-action="open-project">打开项目</button></div></header><div class="project-grid">${list.map((project) => `<button class="project-card ${state.project?.id === project.id ? "is-current" : ""}" type="button" data-project="${project.id}"><span class="project-card__top"><strong>${project.name}</strong><i class="status-chip">可用</i></span><span class="project-card__path">${project.path}</span><i class="project-card__rule"></i><span class="project-card__meta">创建时间：${project.created}</span><span class="project-card__entry">点击进入素材库</span><span class="project-card__stats"><span>${project.assets} 项素材</span><span>${project.sources} 个素材源</span></span></button>`).join("")}</div></div></section>`;
  }

  function sourceRail() {
    return `<aside class="source-rail"><header class="source-rail__header"><div><strong>源素材</strong><span>${state.project?.name || "未选择项目"}</span></div><button class="action-button" type="button" data-action="collapse-rail">‹</button></header><div class="source-list">${sources.map((source) => `<button class="source-card ${state.source === source.id ? "is-active" : ""}" type="button" data-source="${source.id}"><strong>${source.name}</strong><p>${source.path}</p><footer><span>${source.count} 个文件</span><em>在线</em></footer></button>`).join("")}</div><footer class="source-rail__footer"><button class="action-button" type="button" data-action="add-source">添加素材源</button></footer></aside>`;
  }

  function assetCard(asset) {
    return `<button class="asset-card ${state.selectedAsset === asset.id ? "is-selected" : ""}" type="button" data-asset="${asset.id}">${thumb(asset)}<span class="asset-card__copy"><strong>${asset.file}</strong><p>${asset.type} · ${asset.size}</p><footer><span>${asset.detail}</span><span>${asset.favorite ? "★" : asset.ext}</span></footer></span></button>`;
  }

  function assetTable(items) {
    return `<table class="asset-table"><thead><tr><th>文件名</th><th>类型</th><th>大小</th><th>技术信息</th><th>素材源</th></tr></thead><tbody>${items.map((asset) => `<tr class="${state.selectedAsset === asset.id ? "is-selected" : ""}" data-asset="${asset.id}"><td>${asset.favorite ? "★ " : ""}${asset.file}</td><td>${asset.type}</td><td>${asset.size}</td><td>${asset.detail}</td><td>${sources.find((source) => source.id === asset.source)?.name || ""}</td></tr>`).join("")}</tbody></table>`;
  }

  function inspector(asset) {
    if (!asset) return `<aside class="inspector"><h2>未选择素材</h2><p>点击素材卡片查看详情</p><div class="inspector-preview"></div></aside>`;
    return `<aside class="inspector"><h2>${asset.file}</h2><p>${asset.type} · ${asset.size}</p><div class="inspector-preview">${thumb(asset, true)}</div><section class="inspector-section"><h3>技术元数据</h3><div class="detail-row"><span>文件类型</span><strong>${asset.type} / ${asset.ext}</strong></div><div class="detail-row"><span>技术信息</span><strong>${asset.detail}</strong></div><div class="detail-row"><span>文件大小</span><strong>${asset.size}</strong></div><div class="detail-row"><span>素材源</span><strong>${sources.find((source) => source.id === asset.source)?.name}</strong></div></section><section class="inspector-section"><h3>内容信息</h3><div class="detail-row"><span>解析状态</span><strong>${asset.state}</strong></div><p style="color:var(--muted);font-size:11px;line-height:1.7">${asset.summary}</p><div class="keyword-list">${asset.keywords.map((keyword) => `<span>${keyword}</span>`).join("")}</div></section><section class="inspector-section"><button class="action-button action-button--primary" type="button" data-action="preview-asset">点击预览</button> <button class="action-button" type="button" data-action="toggle-favorite">${asset.favorite ? "取消收藏" : "收藏"}</button></section></aside>`;
  }

  function renderLibrary() {
    const items = currentAssets();
    const selected = assets.find((asset) => asset.id === state.selectedAsset);
    main.innerHTML = `${sourceRail()}<section class="workspace library-workspace has-jobbar"><div class="workspace-scroll"><header class="workspace-header"><div class="workspace-title"><h1>素材库</h1><p>已显示 ${items.length} 项素材 · ${sources.find((source) => source.id === state.source)?.name}</p></div><div class="workspace-actions"><button class="action-button" type="button" data-action="toggle-sort">修改时间倒序</button><label class="check-label"><input type="checkbox" data-favorites ${state.favoritesOnly ? "checked" : ""}>仅收藏</label><button class="action-button ${state.viewMode === "grid" ? "action-button--primary" : ""}" type="button" data-view="grid">大图卡片</button><button class="action-button ${state.viewMode === "table" ? "action-button--primary" : ""}" type="button" data-view="table">技术表格</button></div></header>${items.length ? (state.viewMode === "grid" ? `<div class="asset-grid">${items.map(assetCard).join("")}</div>` : assetTable(items)) : '<div class="empty-state"><div><h1>当前筛选下没有素材</h1><p>请更换素材源、取消“仅收藏”或调整搜索词。</p></div></div>'}</div></section>${inspector(selected)}`;
  }

  function centerResult(asset) {
    return `<button class="center-result ${state.selectedAsset === asset.id ? "is-selected" : ""}" type="button" data-asset="${asset.id}">${thumb(asset, true)}<span class="center-result__copy"><strong>${asset.file}</strong><span>${asset.type} · ${asset.ext} · ${state.project.name} · ${sources.find((source) => source.id === asset.source)?.name}</span><p>${asset.summary}</p><footer><span>${asset.keywords.join("、")}</span><span>${asset.confirmed}</span></footer></span><i class="center-result__state">${asset.state}</i></button>`;
  }

  function centerDetail(asset) {
    if (!asset) return `<aside class="center-detail"><h2>选择左侧素材查看详情</h2></aside>`;
    return `<aside class="center-detail"><h2>${asset.file}</h2><div class="inspector-preview">${thumb(asset, true)}</div><p class="center-detail__meta">${state.project.name} · ${sources.find((source) => source.id === asset.source)?.name}<br>类型：${asset.type} · ${asset.ext}</p><div class="center-detail__actions"><button class="action-button action-button--primary" type="button" data-action="analyze-selected">${asset.state === "已解析" ? "重新解析" : "开始解析"}</button><button class="action-button" type="button" data-action="confirm-selected">${asset.confirmed === "已确认" ? "已确认" : "确认结果"}</button><button class="action-button" type="button" data-action="demo-only">打开所属项目</button><button class="action-button" type="button" data-action="demo-only">定位文件夹</button></div><section class="analysis-card"><h3>内容摘要</h3><p>${asset.summary}</p></section><section class="analysis-card"><h3>关键词与状态</h3><p>解析状态：${asset.state}<br>确认状态：${asset.confirmed}</p><div class="keyword-list">${asset.keywords.map((keyword) => `<span>${keyword}</span>`).join("")}</div></section><section class="analysis-card"><h3>路径与素材信息</h3><p>源文件：D:\\Footage\\${asset.file}<br>技术摘要：${asset.detail}<br>解析图片目录：C:\\Users\\Demo\\CineVault\\frames</p></section></aside>`;
  }

  function renderCenter() {
    const items = currentAssets();
    const selected = assets.find((asset) => asset.id === state.selectedAsset);
    main.innerHTML = `<section class="workspace center-workspace has-jobbar"><div class="workspace-scroll"><header class="center-hero"><div class="workspace-title"><h1>素材管理中心</h1><p>全局索引已就绪 · ${assets.length} 项演示素材 · 使用模拟数据</p></div><div class="workspace-actions"><button class="action-button" type="button" data-action="demo-only">同步当前项目</button><button class="action-button" type="button" data-action="batch-analyze">批量解析</button><button class="action-button action-button--primary" type="button" data-action="batch-analyze">全局多维度解析</button><button class="action-button" type="button" data-action="demo-only">重建全局索引</button><button class="action-button" type="button" data-action="confirm-all">全部确认</button></div></header><div class="center-filter"><input class="text-field" type="search" value="${escapeHtml(state.search)}" placeholder="搜索文件名、路径、摘要、关键词或文本内容" data-center-search><select class="filter-control"><option>全部项目</option><option>${state.project.name}</option></select><select class="filter-control"><option>全部素材源</option></select><select class="filter-control"><option>全部类型</option><option>视频</option><option>图片</option></select><select class="filter-control"><option>全部解析状态</option><option>已解析</option><option>待解析</option></select></div><div class="center-content"><div class="center-results">${items.map(centerResult).join("") || '<div class="empty-state"><div><h1>当前筛选条件下没有素材</h1></div></div>'}</div>${centerDetail(selected)}</div></div></section>`;
  }

  function renderJobs() {
    const progress = state.job?.progress || 0;
    const running = Boolean(state.job && progress < 100);
    main.innerHTML = `<section class="workspace jobs-layout"><div class="jobs-main"><header class="workspace-header"><div class="workspace-title"><h1>任务</h1><p>${state.job ? "当前有 1 个解析任务" : "当前还没有任务"}</p></div><button class="action-button" type="button" data-action="clear-jobs" ${state.job && progress >= 100 ? "" : "disabled"}>清理已完成</button></header><section class="batch-panel section-panel"><div class="batch-panel__head"><div><h2>视频解析总量进度</h2><p>${state.job ? `演示批次 · ${progress}%` : "暂无批次"}</p></div><span class="batch-percent">${progress}%</span></div><div class="progress-track" style="--progress:${progress}%"><i></i></div><div class="batch-stats"><span><strong>${progress >= 100 ? "1/1" : "0/1"}</strong>已处理</span><span><strong>${progress >= 100 ? 1 : 0}</strong>成功</span><span><strong>0</strong>失败</span><span><strong>${running ? 1 : 0}</strong>排队中</span></div></section><div class="job-list">${state.job ? `<article class="job-row"><div><strong>${state.job.file}</strong><p>${running ? "正在按固定帧间隔提取并分析画面" : "解析完成，等待确认结果"}</p></div><span class="job-state">${running ? "执行中" : "已完成"}</span><span class="job-percent">${progress}%</span></article>` : '<div class="empty-state" style="height:380px"><div><h1>当前还没有任务。</h1><p>在素材管理中心启动一次模拟解析即可观察任务进度。</p><button class="action-button action-button--primary" type="button" data-workspace="center">前往素材管理中心</button></div></div>'}</div></div><aside class="job-inspector"><h2>任务详情</h2><p>${state.job ? `${state.job.file}<br><br>任务类型：视觉解析<br>帧采样：固定 10 秒间隔<br>当前进度：${progress}%<br><br>${running ? "演示任务在浏览器内模拟，不会调用任何视觉接口。" : "任务已完成，结果仅保留在本次浏览器页面中。"}` : "选择任务后可查看素材缩略图、任务类型、进度和错误信息。"}</p></aside></section>`;
  }

  function renderReport() {
    main.innerHTML = `<section class="workspace report-layout has-jobbar"><aside class="report-form"><h1>数据报表</h1><p>配置表头信息并在右侧预览 PDF 内容。</p><div class="report-fields"><label>项目名称<input class="text-field" value="${state.project.name}"></label><label>客户名称<input class="text-field" value="城市影像工作室"></label><label>拍摄日期<input class="text-field" value="2026-07-13"></label><label>备注<textarea class="text-field" style="height:90px;padding-top:10px">演示项目素材归档报告</textarea></label><button class="action-button action-button--primary" type="button" data-action="demo-only">导出 PDF</button><button class="action-button" type="button" data-action="demo-only">打开导出目录</button></div></aside><div class="report-preview"><div class="report-preview__toolbar"><h2>报表预览</h2><button class="action-button" type="button" data-action="report-prev">上一页</button><button class="action-button" type="button" data-action="report-next">下一页</button><span>${state.reportPage} / 3</span><button class="action-button" type="button" data-action="demo-only">−</button><button class="action-button" type="button" data-action="demo-only">+</button><button class="action-button" type="button" data-action="demo-only">重置</button></div><article class="paper"><small>影资管家 · 项目数据报表</small><h1>${state.project.name}</h1><p>素材归档与技术元数据摘要</p><div class="paper-rule"></div><p>客户：城市影像工作室<br>拍摄日期：2026-07-13<br>生成时间：2026-07-14</p><div class="paper-grid"><span><strong>1,284</strong>素材总数</span><span><strong>960</strong>视频素材</span><span><strong>3</strong>素材源</span></div></article></div></section>`;
  }

  function renderFeedback() {
    main.innerHTML = `<section class="workspace feedback-layout"><aside class="conversation-list"><h1>使用反馈</h1><article class="conversation-card"><strong>欢迎使用影资管家</strong><p>演示会话 · 刚刚</p></article></aside><div class="chat-panel"><header class="chat-header"><strong>影资管家开发者</strong><span>● 演示模式</span></header><div class="messages"><article class="message"><strong>开发者</strong><div>你好！这里是软件内的使用反馈页面。正式版支持文字与附件反馈，并可持续查看回复。</div><small>10:32</small></article><article class="message is-user"><strong>我</strong><div>素材管理中心的逐帧解析很直观。</div><small>10:34</small></article><article class="message"><strong>开发者</strong><div>谢谢反馈。在线 Demo 不会真正发送消息，你可以放心体验布局与交互。</div><small>10:35</small></article></div><form class="chat-compose" data-feedback-form><input class="text-field" placeholder="输入反馈内容（演示不会发送）"><button class="action-button" type="button" data-action="demo-only">选择附件</button><button class="action-button action-button--primary" type="submit">发送</button></form></div></section>`;
  }

  function renderJobbar() {
    const visible = Boolean(state.project && !["projects", "feedback"].includes(state.workspace));
    jobTimeline.classList.toggle("is-visible", visible);
    const progress = state.job?.progress || 0;
    jobTimeline.innerHTML = visible ? `<div class="job-timeline__title"><strong>${state.job ? state.job.file : "当前无运行任务"}</strong><span>${state.job ? (progress >= 100 ? "解析完成" : "固定帧间隔解析中") : "任务队列空闲"}</span></div><div class="progress-track" style="--progress:${progress}%"><i></i></div><span class="job-timeline__percent">${progress}%</span><button class="action-button" type="button" data-workspace="jobs">查看任务</button>` : "";
    jobBadge.hidden = !state.job;
    if (state.job) jobBadge.textContent = progress >= 100 ? "✓" : "1";
  }

  function render() {
    renderTopbar();
    if (state.workspace === "projects") renderProjectLibrary();
    if (state.workspace === "library") renderLibrary();
    if (state.workspace === "center") renderCenter();
    if (state.workspace === "jobs") renderJobs();
    if (state.workspace === "report") renderReport();
    if (state.workspace === "feedback") renderFeedback();
    renderJobbar();
  }

  function switchWorkspace(workspace) {
    if (!state.project && ["library", "center", "report", "jobs"].includes(workspace)) {
      showToast("请先从项目库进入一个演示项目。");
      return;
    }
    state.workspace = workspace;
    if (workspace === "projects") state.search = "";
    render();
  }

  function startJob(asset) {
    window.clearInterval(jobTimer);
    state.job = { file: asset.file, progress: 8 };
    render();
    showToast("已创建模拟解析任务。可在“任务”页查看进度。");
    jobTimer = window.setInterval(() => {
      if (!state.job) return window.clearInterval(jobTimer);
      state.job.progress = Math.min(100, state.job.progress + Math.ceil(Math.random() * 9));
      if (state.job.progress >= 100) {
        asset.state = "已解析";
        window.clearInterval(jobTimer);
        showToast("模拟解析已完成，没有调用外部接口。");
      }
      render();
    }, 900);
  }

  document.addEventListener("click", (event) => {
    const workspaceButton = event.target.closest("[data-workspace]");
    if (workspaceButton && !workspaceButton.disabled) return switchWorkspace(workspaceButton.dataset.workspace);

    const projectCard = event.target.closest("[data-project]");
    if (projectCard) {
      state.project = projects.find((project) => project.id === projectCard.dataset.project);
      state.workspace = "library";
      state.search = "";
      render();
      showToast(`已进入“${state.project.name}”。所有内容均为演示数据。`);
      return;
    }

    const sourceCard = event.target.closest("[data-source]");
    if (sourceCard) { state.source = sourceCard.dataset.source; render(); return; }
    const assetTarget = event.target.closest("[data-asset]");
    if (assetTarget) { state.selectedAsset = Number(assetTarget.dataset.asset); render(); return; }
    const viewButton = event.target.closest("[data-view]");
    if (viewButton) { state.viewMode = viewButton.dataset.view; render(); return; }
    const themeChoice = event.target.closest("[data-theme-choice]");
    if (themeChoice) {
      document.querySelectorAll("[data-theme-choice]").forEach((button) => button.classList.remove("is-active"));
      themeChoice.classList.add("is-active");
      const choice = themeChoice.dataset.themeChoice;
      state.theme = choice === "system" && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : choice === "light" ? "light" : "dark";
      app.dataset.appTheme = state.theme;
      return;
    }

    const action = event.target.closest("[data-action]")?.dataset.action;
    if (!action) return;
    if (action === "open-settings") settingsDialog.hidden = false;
    if (action === "close-settings") settingsDialog.hidden = true;
    if (action === "save-settings") { settingsDialog.hidden = true; showToast("演示设置已应用，本次刷新后会恢复默认值。"); }
    if (action === "toggle-switch") event.target.closest(".switch")?.classList.toggle("is-on");
    if (["new-project", "open-project", "add-source", "demo-only", "preview-asset", "collapse-rail", "toggle-sort"].includes(action)) showToast("这是安全演示：已展示操作反馈，不会打开本机文件或保存数据。");
    if (action === "toggle-favorite") { const asset = assets.find((item) => item.id === state.selectedAsset); asset.favorite = !asset.favorite; render(); showToast(asset.favorite ? "已在演示中收藏素材。" : "已取消演示收藏。"); }
    if (action === "analyze-selected") startJob(assets.find((asset) => asset.id === state.selectedAsset));
    if (action === "batch-analyze") startJob(assets.find((asset) => asset.state === "待解析") || assets[0]);
    if (action === "confirm-selected") { assets.find((asset) => asset.id === state.selectedAsset).confirmed = "已确认"; render(); showToast("已在演示中确认当前结果。"); }
    if (action === "confirm-all") { assets.forEach((asset) => { if (asset.state === "已解析") asset.confirmed = "已确认"; }); render(); showToast("已在演示中确认全部解析结果。"); }
    if (action === "clear-jobs") { state.job = null; render(); showToast("已清理演示任务。"); }
    if (action === "report-prev") { state.reportPage = Math.max(1, state.reportPage - 1); render(); }
    if (action === "report-next") { state.reportPage = Math.min(3, state.reportPage + 1); render(); }
  });

  document.addEventListener("change", (event) => {
    if (event.target.matches("[data-favorites]")) { state.favoritesOnly = event.target.checked; render(); }
  });
  globalSearch.addEventListener("input", () => { state.search = globalSearch.value; render(); globalSearch.focus(); globalSearch.setSelectionRange(state.search.length, state.search.length); });
  document.addEventListener("input", (event) => {
    if (!event.target.matches("[data-center-search]")) return;
    state.search = event.target.value;
    render();
    const field = document.querySelector("[data-center-search]");
    field?.focus(); field?.setSelectionRange(state.search.length, state.search.length);
  });
  document.addEventListener("submit", (event) => {
    if (!event.target.matches("[data-feedback-form]")) return;
    event.preventDefault();
    showToast("演示消息未发送，不会向服务器提交任何内容。");
    event.target.reset();
  });
  document.addEventListener("keydown", (event) => { if (event.key === "Escape" && !settingsDialog.hidden) settingsDialog.hidden = true; });
  window.addEventListener("resize", scaleApp, { passive: true });

  scaleApp();
  render();
})();
