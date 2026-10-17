"""Use the same helper without an MCP client; read-only except explicit prepare/cleanup."""
import argparse
import json
from r1 import R1

parser = argparse.ArgumentParser()
parser.add_argument("command", choices=("prepare", "cleanup", "status", "observe", "now-playing"))
parser.add_argument("--language", default="ru", choices=("ru", "en-US"))
args = parser.parse_args()
device = R1()
if args.command == "observe":
    result = device.observe(args.language)
else:
    result = getattr(device, args.command.replace("-", "_"))()
print(json.dumps(result, ensure_ascii=False, indent=2))
