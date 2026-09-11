# push_to_github.ps1 - Windows 原生版，与 push_to_github.sh 功能一致
# 用法: powershell -ExecutionPolicy Bypass -File scripts\push_to_github.ps1 -GitHubUser yuaddwa -RepoName uno
#       不带参则交互式输入

param(
    [string]$GitHubUser = "",
    [string]$Token = "",
    [string]$RepoName = "uno"
)

$ErrorActionPreference = "Stop"

function Write-Color($msg, $color) {
    Write-Host $msg -ForegroundColor $color
}

Write-Color "=== FoloToy UNO DUEL - 一键推送 GitHub ===" "Cyan"

# 检查 git
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Color "错误：未检测到 git，请先安装 Git for Windows" "Red"
    exit 1
}

# 切换到项目根
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Split-Path -Parent $ScriptDir
Set-Location $ProjectRoot

Write-Color "项目目录：$ProjectRoot" "Cyan"

# 询问用户名
if (-not $GitHubUser) {
    $GitHubUser = Read-Host "GitHub 用户名"
}

if (-not $GitHubUser) {
    Write-Color "错误：必须提供 GitHub 用户名" "Red"
    exit 1
}

# 询问 PAT
if (-not $Token) {
    $secureToken = Read-Host "GitHub Personal Access Token (ghp_xxx...)" -AsSecureString
    $bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
    $Token = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr)
    [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
}

if (-not $Token) {
    Write-Color "错误：必须提供 PAT" "Red"
    exit 1
}

# 验证 PAT 格式
if ($Token -notmatch "^(ghp_|github_pat_|ghu_|gho_|ghs_)") {
    Write-Color "警告：PAT 格式不像标准 token" "Yellow"
    $confirm = Read-Host "是否继续？(y/N)"
    if ($confirm -notmatch "^[Yy]$") { exit 1 }
}

$RepoUrl = "https://github.com/$GitHubUser/$RepoName.git"
$AuthUrl = "https://x-access-token:${Token}@github.com/$GitHubUser/$RepoName.git"

# 检查仓库是否存在
Write-Color "检查仓库 $GitHubUser/$RepoName 是否存在..." "Cyan"
try {
    $headers = @{
        "Authorization" = "Bearer $Token"
        "Accept" = "application/vnd.github+json"
    }
    $response = Invoke-RestMethod -Uri "https://api.github.com/repos/$GitHubUser/$RepoName" `
        -Headers $headers -Method Get -ErrorAction Stop
    Write-Color "仓库已存在" "Green"
} catch {
    $statusCode = $_.Exception.Response.StatusCode.value__
    if ($statusCode -eq 404) {
        Write-Color "仓库 $GitHubUser/$RepoName 不存在" "Yellow"
        Write-Color "请先到 https://github.com/new 创建空仓库：" "Cyan"
        Write-Host "  - Repository name: $RepoName"
        Write-Host "  - Visibility: Public 或 Private 均可"
        Write-Host "  - 不要勾选 'Add a README'"
        Write-Host "  - 不要勾选 'Add .gitignore'"
        Write-Host "  - 不要勾选 'Choose a license'"
        Write-Host ""
        $created = Read-Host "仓库创建好了吗？(y/N)"
        if ($created -notmatch "^[Yy]$") {
            Write-Color "请先创建仓库再运行此脚本" "Red"
            exit 1
        }
    } else {
        Write-Color "访问仓库返回 HTTP $statusCode，请检查 PAT 权限" "Red"
        exit 1
    }
}

# git init
if (-not (Test-Path ".git")) {
    Write-Color "初始化 git 仓库..." "Cyan"
    git init
    git config user.name "FoloToy UNO DUEL Builder"
    git config user.email "builder@local.todo"
}

Write-Color "添加并提交所有文件..." "Cyan"
git checkout -B main 2>$null | Out-Null
git add .

$diff = git diff --staged --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Color "没有新内容需要提交（可能已提交过）" "Yellow"
} else {
    git commit -m "Initial commit: FoloToy UNO DUEL firmware + CI" 2>$null
}

# 配置远程
git remote remove origin 2>$null | Out-Null
git remote add origin $AuthUrl

# 推送
Write-Color "推送到 GitHub..." "Cyan"
$pushResult = git push -u origin main --force 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Color "=== 推送成功 ===" "Green"
    Write-Host ""
    Write-Color "下一步：" "Cyan"
    Write-Host "1. 打开 https://github.com/$GitHubUser/$RepoName/actions"
    Write-Host "2. 等待 'Build ESP32-C3 Firmware' 跑完（约 6-10 分钟）"
    Write-Host "3. 在完成的工作流页面底部下载 'uno-firmware' artifact"
    Write-Host "   里面包含:"
    Write-Host "     - bootloader.bin"
    Write-Host "     - partition-table.bin"
    Write-Host "     - folotoy_uno_duel.bin"
    Write-Host "     - merged.bin (合并烧录用)"
    Write-Host ""

    git remote remove origin | Out-Null
    git remote add origin $RepoUrl
    Write-Color "远程地址已恢复为公开 URL（不含凭据）" "Green"
} else {
    Write-Host $pushResult
    Write-Color "=== 推送失败 ===" "Red"
    Write-Host "常见原因："
    Write-Host "  - PAT 无效或过期"
    Write-Host "  - PAT 没有 'repo' scope"
    Write-Host "  - 仓库不存在或用户名拼错"
    exit 1
}

Write-Host ""
Write-Color "安全提示:" "Yellow"
Write-Host "  - 建议去 https://github.com/settings/tokens 撤销刚用过的 PAT（如果是一次性的）"
Write-Host "  - 本脚本不会把 PAT 写入任何文件"