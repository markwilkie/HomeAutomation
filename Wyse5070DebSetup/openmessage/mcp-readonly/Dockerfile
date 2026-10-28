# Read-only MCP front for OpenMessage (see server.py, setup-openmessage.sh).
FROM python:3.13.9-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir --root-user-action=ignore -r requirements.txt
COPY server.py .
USER nobody
CMD ["python", "server.py"]
