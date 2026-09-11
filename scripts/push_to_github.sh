#!/usr/bin/env bash
# push_to_github.sh - 把 uno-duel 推到 GitHub 触发 CI 自动编译
# 用法: bash scripts/push_to_github.sh [你的GitHub用户名] [PAT] [仓库名]
#       PAT 留空时会交互式提示输入
#
# 示例: bash scripts/push_to_github.sh yuaddwa
#       (然后输入 ghp_xxxxxxxxxxxxxxxxxxxx)

set -euo pipefail

REPO_OWNER="${1:-}"
TOKEN="${2:-}"
REPO_NAME="${3:-uno}"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== FoloToy UNO DUEL - 一键推送 GitHub ===${NC}"

# 检查 git
if ! command -v git >/dev/null 2>&1; then
    echo -e "${RED}错误：未检测到 git，请先安装 Git for Windows${NC}"
    exit 1
fi

# 切换到项目根目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${CYAN}项目目录：${NC}$PROJECT_ROOT"

# 询问用户名
if [ -z "$REPO_OWNER" ]; then
    echo -n "GitHub 用户名: "
    read -r REPO_OWNER
fi

if [ -z "$REPO_OWNER" ]; then
    echo -e "${RED}错误：必须提供 GitHub 用户名${NC}"
    exit 1
fi

# 询问 PAT
if [ -z "$TOKEN" ]; then
    echo -n "GitHub Personal Access Token (ghp_xxx...): "
    read -rs TOKEN
    echo ""
fi

if [ -z "$TOKEN" ]; then
    echo -e "${RED}错误：必须提供 PAT${NC}"
    exit 1
fi

# 验证 PAT 格式
if [[ ! "$TOKEN" =~ ^(ghp_|github_pat_|ghu_|gho_|ghs_) ]]; then
    echo -e "${YELLOW}警告：PAT 格式不像标准 token（应该以 ghp_/github_pat_/ghu_ 等开头）${NC}"
    echo -n "是否继续？(y/N): "
    read -r CONFIRM
    if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# 检查仓库是否存在
REPO_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}.git"
AUTH_URL="https://x-access-token:${TOKEN}@github.com/${REPO_OWNER}/${REPO_NAME}.git"

echo -e "${CYAN}检查仓库 ${REPO_OWNER}/${REPO_NAME} 是否存在...${NC}"
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}" 2>/dev/null || echo "000")

if [ "$HTTP_STATUS" = "404" ]; then
    echo -e "${YELLOW}仓库 ${REPO_OWNER}/${REPO_NAME} 不存在${NC}"
    echo -e "${CYAN}请先到 https://github.com/new 创建空仓库：${NC}"
    echo "  - Repository name: ${REPO_NAME}"
    echo "  - Visibility: Public 或 Private 均可"
    echo "  - 不要勾选 'Add a README'"
    echo "  - 不要勾选 'Add .gitignore'"
    echo "  - 不要勾选 'Choose a license'"
    echo ""
    echo -n "仓库创建好了吗？(y/N): "
    read -r CREATED
    if [[ ! "$CREATED" =~ ^[Yy]$ ]]; then
        echo -e "${RED}请先创建仓库再运行此脚本${NC}"
        exit 1
    fi
elif [ "$HTTP_STATUS" = "200" ]; then
    echo -e "${GREEN}仓库已存在${NC}"
elif [ "$HTTP_STATUS" = "000" ]; then
    echo -e "${YELLOW}无法连接 GitHub API（可能是网络问题），尝试继续${NC}"
else
    echo -e "${RED}访问仓库返回 HTTP ${HTTP_STATUS}，请检查 PAT 权限${NC}"
    exit 1
fi

# git init (如果还没有)
if [ ! -d ".git" ]; then
    echo -e "${CYAN}初始化 git 仓库...${NC}"
    git init
    git config user.name "FoloToy UNO DUEL Builder"
    git config user.email "builder@local.todo"
fi

# 添加并提交
echo -e "${CYAN}添加并提交所有文件...${NC}"
git checkout -B main 2>/dev/null || git branch -M main
git add .

# 检查是否需要提交
if git diff --staged --quiet; then
    echo -e "${YELLOW}没有新内容需要提交（可能已提交过）${NC}"
else
    git commit -m "Initial commit: FoloToy UNO DUEL firmware + CI" || {
        echo -e "${YELLOW}提交失败，可能是 pre-commit hook 或配置问题，继续尝试 push${NC}"
    }
fi

# 配置远程
git remote remove origin 2>/dev/null || true
git remote add origin "$AUTH_URL"

# 推送
echo -e "${CYAN}推送到 GitHub...${NC}"
if git push -u origin main --force 2>&1 | tee /tmp/git_push.log; then
    echo ""
    echo -e "${GREEN}=== 推送成功 ===${NC}"
    echo ""
    echo -e "${CYAN}下一步：${NC}"
    echo "1. 打开 https://github.com/${REPO_OWNER}/${REPO_NAME}/actions"
    echo "2. 等待 'Build ESP32-C3 Firmware' 跑完（约 6-10 分钟）"
    echo "3. 在完成的工作流页面底部下载 'uno-firmware' artifact"
    echo "   里面包含:"
    echo "     - bootloader.bin"
    echo "     - partition-table.bin"
    echo "     - folotoy_uno_duel.bin"
    echo "     - merged.bin (合并烧录用)"
    echo ""
    # 清理临时凭据
    git remote remove origin
    git remote add origin "$REPO_URL"
    echo -e "${GREEN}远程地址已恢复为公开 URL（不含凭据）${NC}"
else
    echo ""
    echo -e "${RED}=== 推送失败 ===${NC}"
    echo "查看上方错误信息，常见原因："
    echo "  - PAT 无效或过期（去 https://github.com/settings/tokens 重新生成）"
    echo "  - PAT 没有 'repo' scope"
    echo "  - 仓库不存在或用户名拼错"
    exit 1
fi

# 安全提示
echo ""
echo -e "${YELLOW}安全提示:${NC}"
echo "  - 建议去 https://github.com/settings/tokens 撤销刚用过的 PAT（如果是一次性的）"
echo "  - 本脚本不会把 PAT 写入任何文件"