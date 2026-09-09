#!/usr/bin/env bash
#
# setup-mcp-gateway-spotify.sh
# Deploys a persistent, network-reachable Spotify MCP server, for remote
# clients (e.g. Claude mobile, via Caddy) that can't spawn a local stdio
# process the way Claude Desktop does.
#
# Upstream: marcelmarais/spotify-mcp-server (github.com/marcelmarais/spotify-mcp-server)
# -- TypeScript/Node, stdio-only. Chosen over several other Spotify MCP
# forks for being by far the most active/used (445 stars, pushed within the
# last two weeks, vs. single-digit stars and months-old-or-more for every
# alternative found) -- same selection reasoning already used for the
# Monarch gateway (see setup-mcp-gateway-monarch.sh's header comment).
# Wrapped here with `supergateway` (github.com/supercorp-ai/supergateway) to
# expose it over Streamable HTTP, same as the other three gateways.
#
# Genre patch: upstream has NO tool that resolves genre at all (confirmed by
# reading its full source tree/tool list before choosing it) -- Spotify's
# API attaches genre to artists, not tracks, and this repo never calls the
# Get Artist(s) endpoint. This script adds one new file, src/genres.ts,
# defining a getTrackGenres tool (resolves track -> artist(s) -> genres,
# meant to be used with upstream's own getPlaylistTracks output), and
# idempotently patches src/index.ts to register it. Applied fresh after
# every clone/pull below so a future `git pull` of upstream doesn't need
# manual reconciliation -- the patch step just re-applies.
#
# Auth: standard Spotify OAuth (authorization code + refresh token), NOT
# email/password like Monarch or an Azure app grant like the Todo gateway.
# Needs a one-time interactive browser login, which can't happen on this
# headless host -- same "run it on a machine with a browser, copy the
# resulting file here" pattern as the other two gateways' prerequisites.
#
# One-time manual prerequisite (secrets, never committed to this repo):
#   1. Create a Spotify app at https://developer.spotify.com/dashboard
#      (any Spotify account). Add redirect URI: http://127.0.0.1:8888/callback
#      Note the Client ID and Client Secret.
#   2. On a machine with a browser (NOT this headless box):
#        git clone https://github.com/marcelmarais/spotify-mcp-server.git
#        cd spotify-mcp-server && npm install
#        cp spotify-config.example.json spotify-config.json
#        # edit spotify-config.json: fill in clientId, clientSecret,
#        # redirectUri (http://127.0.0.1:8888/callback)
#        npm run build && npm run auth
#      Completes the browser authorization; writes accessToken/refreshToken
#      into spotify-config.json.
#   3. Copy that file here:
#        scp spotify-config.json mwilkie@<host>:/mnt/data/appdata/mcp-gateway-spotify/config/
#      Re-run this script afterward to start the gateway for real.
#
#   Spotify access tokens expire hourly; index.ts already proactively
#   refreshes every 45 minutes using the stored refresh token, so no
#   recurring manual step -- only re-run step 2/3 if the refresh token
#   itself is ever revoked (Spotify returns invalid_grant, which
#   utils.ts's refreshAccessToken() detects and discards the stored tokens
#   for, logging clearly rather than retrying a dead token forever).
#
# Network exposure: this container listens on 127.0.0.1 only (see the
# "127.0.0.1:" prefix on the port mapping below) -- it is NOT reachable from
# the LAN or WAN directly, only from Caddy running on this same host. Note
# supergateway's HTTP server mode has no built-in inbound auth of its own;
# Caddy is expected to be the thing enforcing access control in front of it.
#
# Usage:
#   ./setup-mcp-gateway-spotify.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/mcp-gateway-spotify"
APP_DIR="${APPDATA_ROOT}/app"
CONFIG_DIR="${APPDATA_ROOT}/config"
CONTAINER_NAME="mcp-gateway-spotify"
REPO_URL="https://github.com/marcelmarais/spotify-mcp-server.git"
LISTEN_PORT="8603"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${CONFIG_DIR}"

if [ -d "${APP_DIR}/.git" ]; then
  echo "==> ${APP_DIR} already cloned, pulling latest"
  git -C "${APP_DIR}" pull
else
  echo "==> Cloning ${REPO_URL}"
  git clone "${REPO_URL}" "${APP_DIR}"
fi

# ---- Genre patch: new tool file (always rewritten, safe -- ours, not upstream's) ----
echo "==> Writing ${APP_DIR}/src/genres.ts (genre-lookup tool, not present upstream)"
tee "${APP_DIR}/src/genres.ts" > /dev/null <<'EOF'
// Local addition, not upstream. Spotify's API attaches genre to artists,
// not individual tracks -- upstream has no tool that resolves genre at
// all. This one takes track IDs (e.g. straight from upstream's own
// getPlaylistTracks output), batch-resolves each track's artist(s), then
// batch-resolves those artists' genres. Re-applied by
// setup-mcp-gateway-spotify.sh on every deploy; if upstream ever adds its
// own genre tool, this can be retired.
import { z } from 'zod';
import type { SpotifyTrack, tool } from './types.js';
import { spotifyFetch } from './utils.js';

interface SpotifyArtistWithGenres {
  id: string;
  name: string;
  genres: string[];
}

// Spotify's batch limit for both GET /tracks and GET /artists.
const MAX_IDS_PER_REQUEST = 50;

function chunk<T>(items: T[], size: number): T[][] {
  const out: T[][] = [];
  for (let i = 0; i < items.length; i += size) {
    out.push(items.slice(i, i + size));
  }
  return out;
}

const getTrackGenres: tool<{
  trackIds: z.ZodArray<z.ZodString>;
}> = {
  name: 'getTrackGenres',
  description:
    "Look up genre for one or more Spotify tracks. Spotify doesn't attach " +
    "genre to individual tracks -- this resolves each track's artist(s) " +
    'and returns their genres instead, which is the closest equivalent ' +
    'Spotify exposes. Pair with getPlaylistTracks: pass its returned track ' +
    'IDs here to see genres for an entire playlist.',
  schema: {
    trackIds: z
      .array(z.string())
      .min(1)
      .max(100)
      .describe('Spotify track IDs to resolve genres for (up to 100)'),
  },
  handler: async (args, _extra) => {
    const { trackIds } = args;

    try {
      const tracks: SpotifyTrack[] = [];
      for (const batch of chunk(trackIds, MAX_IDS_PER_REQUEST)) {
        const result = await spotifyFetch<{
          tracks: Array<SpotifyTrack | null>;
        }>('tracks', { query: { ids: batch.join(',') } });
        tracks.push(
          ...result.tracks.filter((t): t is SpotifyTrack => t !== null),
        );
      }

      if (tracks.length === 0) {
        return {
          content: [
            { type: 'text', text: 'No tracks found for the given IDs' },
          ],
        };
      }

      const uniqueArtistIds = Array.from(
        new Set(tracks.flatMap((t) => t.artists.map((a) => a.id))),
      );
      const genresByArtistId = new Map<string, string[]>();
      for (const batch of chunk(uniqueArtistIds, MAX_IDS_PER_REQUEST)) {
        const result = await spotifyFetch<{
          artists: Array<SpotifyArtistWithGenres | null>;
        }>('artists', { query: { ids: batch.join(',') } });
        for (const artist of result.artists) {
          if (artist) genresByArtistId.set(artist.id, artist.genres);
        }
      }

      const formatted = tracks
        .map((track, i) => {
          const artistNames = track.artists.map((a) => a.name).join(', ');
          const genres = Array.from(
            new Set(
              track.artists.flatMap((a) => genresByArtistId.get(a.id) ?? []),
            ),
          );
          const genreText =
            genres.length > 0
              ? genres.join(', ')
              : '(no genres listed on Spotify for this artist)';
          return `${i + 1}. "${track.name}" by ${artistNames} - Genres: ${genreText} - ID: ${track.id}`;
        })
        .join('\n');

      return {
        content: [{ type: 'text', text: `# Genres by Track\n\n${formatted}` }],
      };
    } catch (error) {
      return {
        content: [
          {
            type: 'text',
            text: `Error resolving genres: ${error instanceof Error ? error.message : String(error)}`,
          },
        ],
      };
    }
  },
};

export const genreTools = [getTrackGenres];
EOF

# ---- Genre patch: register the new tool in index.ts, idempotently --------
INDEX_FILE="${APP_DIR}/src/index.ts"
if ! grep -q "genres.js" "${INDEX_FILE}"; then
  echo "==> Patching ${INDEX_FILE} to register genreTools"
  sed -i "s|import { readTools } from './read.js';|import { readTools } from './read.js';\nimport { genreTools } from './genres.js';|" "${INDEX_FILE}"
  sed -i "s|\[...readTools, ...playTools, ...albumTools, ...playlistTools\]|[...readTools, ...playTools, ...albumTools, ...playlistTools, ...genreTools]|" "${INDEX_FILE}"
else
  echo "==> ${INDEX_FILE} already patched for genreTools"
fi

if [ ! -f "${CONFIG_DIR}/spotify-config.json" ]; then
  echo ""
  echo "!! Missing ${CONFIG_DIR}/spotify-config.json"
  echo "   This is a secret and is never committed to this repo -- see the"
  echo "   prerequisites in this script's header comment (register a Spotify"
  echo "   app, run the interactive browser login on a machine with a"
  echo "   browser), then copy the resulting file into ${CONFIG_DIR} and"
  echo "   re-run this script."
  exit 1
fi

# ---- Dockerfile: upstream server (Node) + supergateway on top -------------
echo "==> Writing ${APP_DIR}/Dockerfile"
tee "${APP_DIR}/Dockerfile" > /dev/null <<'EOF'
FROM node:22-alpine
WORKDIR /app
RUN npm install -g supergateway
COPY package.json ./
RUN npm install
COPY . .
RUN npm run build
ENTRYPOINT ["npx", "supergateway", \
  "--stdio", "node build/index.js", \
  "--outputTransport", "streamableHttp", \
  "--stateful", \
  "--sessionTimeout", "3600000", \
  "--streamableHttpPath", "/mcp", \
  "--port", "8603"]
EOF

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    build: ${APP_DIR}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    volumes:
      - ${CONFIG_DIR}/spotify-config.json:/app/spotify-config.json
    ports:
      - "127.0.0.1:${LISTEN_PORT}:${LISTEN_PORT}"
EOF

echo "==> Building and starting ${CONTAINER_NAME}"
cd "${APPDATA_ROOT}"
docker compose up -d --build

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 30

echo ""
echo "==> Done."
echo "    Local-only endpoint: http://127.0.0.1:${LISTEN_PORT}/mcp"
echo "    (Not reachable from the LAN/WAN -- Caddy proxies to this from here.)"
