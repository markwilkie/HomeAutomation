#!/usr/bin/env bash
#
# setup-microsoft-todo-mcp.sh
# Builds and deploys jordanburke/microsoft-todo-mcp-server as a Docker image
# on wilkie-home-server, for Claude Desktop to invoke over SSH.
#
# Unlike the other services in this directory, this is NOT a long-running
# network daemon -- it's a stdio-transport MCP server, spawned fresh
# (docker run --rm -i) once per Claude Desktop session via SSH, then exits.
# So there's no docker-compose / restart policy here; "deploy" just means
# "build the image and write the run.sh wrapper Claude Desktop's SSH command
# points at."
#
# One-time manual prerequisites (secrets, so never committed to this repo):
#   1. Azure App Registration (portal.azure.com -> Entra ID -> App
#      registrations): single tenant, redirect URI
#      http://localhost:3000/callback, delegated Graph permissions
#      Tasks.Read + Tasks.ReadWrite + User.Read, admin consent granted.
#   2. On a machine with a browser (NOT this headless box): clone
#      https://github.com/jordanburke/microsoft-todo-mcp-server, put the
#      Azure app's CLIENT_ID/CLIENT_SECRET/TENANT_ID in a .env file, run
#      `pnpm run auth` and complete the browser login. This produces
#      tokens.json.
#   3. Copy those two files here, then run this script:
#        scp .env tokens.json mwilkie@<host>:/mnt/data/appdata/microsoft-todo-mcp/config/
#
# After that, token refresh is automatic and self-persisting (the server
# writes refreshed tokens back into config/tokens.json on every run) --
# re-running step 2 is only needed if the refresh token itself is revoked
# or expires from long inactivity.
#
# Usage:
#   ./setup-microsoft-todo-mcp.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/microsoft-todo-mcp"
APP_DIR="${APPDATA_ROOT}/app"
CONFIG_DIR="${APPDATA_ROOT}/config"
IMAGE_TAG="microsoft-todo-mcp:latest"
REPO_URL="https://github.com/jordanburke/microsoft-todo-mcp-server.git"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${CONFIG_DIR}"

if [ -d "${APP_DIR}/.git" ]; then
  echo "==> ${APP_DIR} already cloned, pulling latest"
  git -C "${APP_DIR}" pull
else
  echo "==> Cloning ${REPO_URL}"
  git clone "${REPO_URL}" "${APP_DIR}"
fi

if [ ! -f "${CONFIG_DIR}/.env" ] || [ ! -f "${CONFIG_DIR}/tokens.json" ]; then
  echo "!! Missing ${CONFIG_DIR}/.env and/or ${CONFIG_DIR}/tokens.json"
  echo "   These are secrets and are never committed to this repo -- see the"
  echo "   prerequisites in this script's header comment, then copy both"
  echo "   files into ${CONFIG_DIR} and re-run."
  exit 1
fi

# ---- Dockerfile (written into the cloned app dir, not upstream) -----------
echo "==> Writing ${APP_DIR}/Dockerfile"
tee "${APP_DIR}/Dockerfile" > /dev/null <<'EOF'
FROM node:22-alpine
WORKDIR /app
RUN npm install -g pnpm
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
RUN pnpm install --frozen-lockfile
COPY . .
RUN pnpm run build
ENTRYPOINT ["node", "dist/cli.js"]
EOF

echo "==> Building ${IMAGE_TAG}"
docker build -t "${IMAGE_TAG}" "${APP_DIR}"

# ---- run.sh: what Claude Desktop's "ssh" mcpServers command invokes -------
# HOME=/data + mounting CONFIG_DIR there means the server's token file
# (normally ~/.config/microsoft-todo-mcp/tokens.json) resolves to
# CONFIG_DIR/tokens.json, so refreshed tokens persist across container runs.
echo "==> Writing ${APPDATA_ROOT}/run.sh"
tee "${APPDATA_ROOT}/run.sh" > /dev/null <<EOF
#!/usr/bin/env bash
exec docker run --rm -i \\
  --env-file "${CONFIG_DIR}/.env" \\
  -e HOME=/data \\
  -v "${CONFIG_DIR}:/data/.config/microsoft-todo-mcp" \\
  "${IMAGE_TAG}"
EOF
chmod +x "${APPDATA_ROOT}/run.sh"

echo ""
echo "==> Done."
echo "    Image:   ${IMAGE_TAG}"
echo "    Config:  ${CONFIG_DIR}"
echo "    Run via: ${APPDATA_ROOT}/run.sh"
echo ""
echo "    Point Claude Desktop's mcpServers config at this over SSH:"
echo '      "microsoft-todo": {'
echo '        "command": "ssh",'
echo "        \"args\": [\"mwilkie@<this-host>\", \"${APPDATA_ROOT}/run.sh\"]"
echo '      }'
