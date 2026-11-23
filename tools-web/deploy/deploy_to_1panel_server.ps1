param(
    [string]$HostName = '64.90.17.178',
    [int]$SshPort = 419,
    [string]$UserName = 'root',
    [string]$KeyPath,
    [string]$Domain = 'dit.ee2x.cn',
    [string]$ContactEmail = '419773176@qq.com',
    [string]$RemoteSiteRoot = '/opt/1panel/www/sites/dit.ee2x.cn',
    [string]$RemoteBackendRoot = '/opt/ee2x/cinevault_support_site',
    [string]$ServiceName = 'cinevault-support-site.service',
    [string]$ActiveNginxConfig = '/opt/1panel/www/conf.d/dit.ee2x.cn.conf',
    [int]$PortStart = 3413,
    [int]$PortEnd = 3499
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$siteRoot = [System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$repoRoot = [System.IO.Path]::GetFullPath((Split-Path $siteRoot -Parent))
$releasePath = Join-Path $siteRoot 'downloads\release.json'
$remoteScriptSource = Join-Path $PSScriptRoot 'deploy_to_1panel_server_remote.sh'
$nginxTemplate = Join-Path $PSScriptRoot 'dit.ee2x.cn.conf'

if ([string]::IsNullOrWhiteSpace($KeyPath)) {
    $KeyPath = $env:DIT_1PANEL_SSH_KEY
}
if ([string]::IsNullOrWhiteSpace($KeyPath)) {
    $workspaceParent = Split-Path $repoRoot -Parent
    $keyCandidates = Get-ChildItem -LiteralPath $workspaceParent -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName '公钥\id_ed25519_1panel' } |
        Where-Object { Test-Path -LiteralPath $_ }
    $KeyPath = $keyCandidates | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($KeyPath)) {
    throw '未找到 1Panel SSH 私钥，请设置 DIT_1PANEL_SSH_KEY 或传入 -KeyPath。'
}
$KeyPath = [System.IO.Path]::GetFullPath($KeyPath)

if ($Domain -notmatch '^[A-Za-z0-9.-]+$') { throw "域名格式无效：$Domain" }
if ($ContactEmail -notmatch '^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+$') { throw "证书邮箱格式无效" }
if ($PortStart -lt 1024 -or $PortEnd -gt 65535 -or $PortStart -gt $PortEnd) { throw "端口范围无效：$PortStart-$PortEnd" }

$release = Get-Content -LiteralPath $releasePath -Raw -Encoding UTF8 | ConvertFrom-Json
$installerPath = Join-Path $repoRoot ("output\{0}\{1}" -f $release.version, $release.fileName)
$required = @(
    (Join-Path $siteRoot 'index.html'),
    (Join-Path $siteRoot 'sponsors.html'),
    (Join-Path $siteRoot 'sponsors-admin.html'),
    (Join-Path $siteRoot 'support-shared.js'),
    (Join-Path $siteRoot 'demo\index.html'),
    (Join-Path $siteRoot 'server\app\main.py'),
    (Join-Path $siteRoot 'server\requirements.txt'),
    $releasePath,
    $installerPath,
    $remoteScriptSource,
    $nginxTemplate,
    $KeyPath
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "部署所需文件不存在：$path" }
}

$actualSha = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha -ne ([string]$release.sha256).ToLowerInvariant()) {
    throw "安装包 SHA-256 与 release.json 不一致"
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$tempRoot = Join-Path $env:TEMP "cinevault-web-deploy-$stamp"
$tempKey = Join-Path $tempRoot 'id_ed25519_1panel'
$siteStage = Join-Path $tempRoot 'site'
$siteArchive = Join-Path $tempRoot 'cinevault-site.tar.gz'
$backendArchive = Join-Path $tempRoot 'cinevault-support-backend.tar.gz'
$remotePrefix = "/tmp/cinevault-web-$stamp"
$remoteSiteArchive = "$remotePrefix-site.tar.gz"
$remoteBackendArchive = "$remotePrefix-backend.tar.gz"
$remoteNginxTemplate = "$remotePrefix-nginx.conf"
$remoteScript = "$remotePrefix-deploy.sh"

New-Item -ItemType Directory -Path $siteStage -Force | Out-Null
Copy-Item -LiteralPath $KeyPath -Destination $tempKey -Force
icacls $tempKey /inheritance:r | Out-Null
icacls $tempKey /grant:r "$($env:USERNAME):(R)" | Out-Null

$staticDirectories = @('assets', 'demo', 'downloads')
foreach ($name in $staticDirectories) {
    Copy-Item -LiteralPath (Join-Path $siteRoot $name) -Destination (Join-Path $siteStage $name) -Recurse -Force
}
$staticFiles = @(
    'index.html', 'styles.css', 'script.js', 'support-shared.js',
    'sponsors.html', 'sponsors.css', 'sponsors.js',
    'sponsors-admin.html', 'sponsors-admin.css', 'sponsors-admin.js'
)
foreach ($name in $staticFiles) {
    Copy-Item -LiteralPath (Join-Path $siteRoot $name) -Destination (Join-Path $siteStage $name) -Force
}
Copy-Item -LiteralPath $installerPath -Destination (Join-Path $siteStage "downloads\$($release.fileName)") -Force

& tar.exe -czf $siteArchive -C $siteStage .
if ($LASTEXITCODE -ne 0) { throw "网站部署包创建失败，退出码：$LASTEXITCODE" }
& tar.exe -czf $backendArchive `
    '--exclude=./.env' `
    '--exclude=./.venv' `
    '--exclude=./data' `
    '--exclude=./.pytest_cache' `
    '--exclude=__pycache__' `
    '--exclude=*.pyc' `
    -C (Join-Path $siteRoot 'server') .
if ($LASTEXITCODE -ne 0) { throw "赞助后端部署包创建失败，退出码：$LASTEXITCODE" }

$sshArgs = @(
    '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=20', '-o', 'ServerAliveInterval=15',
    '-o', 'StrictHostKeyChecking=accept-new', '-p', "$SshPort", '-i', $tempKey,
    "$UserName@$HostName"
)
$scpArgs = @(
    '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=20', '-o', 'ServerAliveInterval=15',
    '-o', 'StrictHostKeyChecking=accept-new', '-P', "$SshPort", '-i', $tempKey
)

try {
    & scp.exe @scpArgs $siteArchive "$UserName@$HostName`:$remoteSiteArchive"
    if ($LASTEXITCODE -ne 0) { throw "网站部署包上传失败，退出码：$LASTEXITCODE" }
    & scp.exe @scpArgs $backendArchive "$UserName@$HostName`:$remoteBackendArchive"
    if ($LASTEXITCODE -ne 0) { throw "后端部署包上传失败，退出码：$LASTEXITCODE" }
    & scp.exe @scpArgs $nginxTemplate "$UserName@$HostName`:$remoteNginxTemplate"
    if ($LASTEXITCODE -ne 0) { throw "Nginx 模板上传失败，退出码：$LASTEXITCODE" }
    & scp.exe @scpArgs $remoteScriptSource "$UserName@$HostName`:$remoteScript"
    if ($LASTEXITCODE -ne 0) { throw "远端部署脚本上传失败，退出码：$LASTEXITCODE" }

    $remoteCommand = @(
        'bash', $remoteScript, $remoteSiteArchive, $remoteBackendArchive, $remoteNginxTemplate,
        $RemoteSiteRoot, $RemoteBackendRoot, $ServiceName, $ActiveNginxConfig,
        $Domain, $ContactEmail, "$PortStart", "$PortEnd"
    ) -join ' '
    & ssh.exe @sshArgs $remoteCommand
    if ($LASTEXITCODE -ne 0) { throw "远端部署失败，退出码：$LASTEXITCODE" }
}
finally {
    if (Test-Path -LiteralPath $tempKey) { icacls $tempKey /grant:r "$($env:USERNAME):(F)" | Out-Null }
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}

Write-Output "影资管家官网部署完成：https://$Domain"
