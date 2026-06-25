import json
import sys
import paho.mqtt.publish as publish

# Usage: python mqtt_pub.py [group] [action]
#   group: endblinds | sideblinds   (default: endblinds)
#   action: up | down               (default: down)
group = sys.argv[1] if len(sys.argv) > 1 else "endblinds"
action = sys.argv[2] if len(sys.argv) > 2 else "down"

payload = json.dumps({"group": group, "action": action})
publish.single(
    "blinds/control",
    payload,
    hostname="192.168.15.10",
    port=1883,
    auth={"username": "mwilkie", "password": "123lauve"},
)
print("published:", payload)
