#!/usr/bin/env bash
# check_build.sh - 轮询 GitHub Actions 构建状态并下载产物
# 用法: bash scripts/check_build.sh [用户名] [仓库名] [--download]
#       不带 --download 只查看状态，带则下载最新 artifacts

set -euo pipefail

REPO_OWNER="${1:-}"
REPO_NAME="${2:-uno}"
DOWNLOAD_FLAG="${3:-}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

if [ -z "$REPO_OWNER" ]; then
    echo -n "GitHub 用户名: "
    read -r REPO_OWNER
fi

if [ -z "$REPO_OWNER" ]; then
    echo -e "${RED}必须提供用户名${NC}"
    exit 1
fi

echo -e "${CYAN}查询 ${REPO_OWNER}/${REPO_NAME} 的构建状态...${NC}"

# 用 gh CLI 或 curl 查询
WORKFLOW_RUNS=$(curl -s \
    -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/actions/runs?per_page=5")

# 解析 JSON (依赖 python3)
echo "$WORKFLOW_RUNS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
runs = data.get('workflow_runs', [])
if not runs:
    print('暂无构建记录')
    sys.exit(0)
for r in runs[:5]:
    status = r['status']
    conclusion = r.get('conclusion') or '-'
    name = r['name']
    branch = r['head_branch']
    url = r['html_url']
    print(f\"[{status}] {conclusion:10s} {branch:8s} {name}\")
    print(f\"            {url}\")
" || echo "查询失败"

if [[ "$DOWNLOAD_FLAG" == "--download" ]]; then
    echo ""
    echo -e "${CYAN}下载最新成功构建的产物...${NC}"
    LATEST_SUCCESS=$(echo "$WORKFLOW_RUNS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for r in data.get('workflow_runs', []):
    if r['status'] == 'completed' and r['conclusion'] == 'success':
        print(r['id'])
        break
")
    if [ -z "$LATEST_SUCCESS" ]; then
        echo -e "${RED}没有成功的构建${NC}"
        exit 1
    fi
    echo "构建 ID: $LATEST_SUCCESS"
    echo "去这里下载: https://github.com/${REPO_OWNER}/${REPO_NAME}/actions/runs/${LATEST_SUCCESS}"
fi