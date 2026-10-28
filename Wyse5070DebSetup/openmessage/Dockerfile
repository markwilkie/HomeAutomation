# OpenMessage plus its Linux Google-cookie refresher (see setup-openmessage.sh).
#
# BASE is the upstream image setup-openmessage.sh builds from the pinned
# commit; the "upstream" build context is that same checkout, for the
# refresher script that the upstream image doesn't ship.
ARG BASE
FROM ${BASE}

USER root
RUN apk add --no-cache python3=3.12.13-r0 py3-cryptography=42.0.7-r0
COPY --from=upstream scripts/refresh-google-session-cookies-linux.py /opt/openmessage/
COPY --chmod=755 refresh-cookies.py /opt/openmessage/refresh-cookies
USER openmessage
