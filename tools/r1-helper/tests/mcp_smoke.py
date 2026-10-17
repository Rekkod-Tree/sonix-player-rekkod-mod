"""Real stdio handshake; --device additionally performs read-only R1 checks."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main(device=False):
    server = Path(__file__).resolve().parents[1] / "server.py"
    params = StdioServerParameters(command=sys.executable, args=[str(server)], env=dict(os.environ))
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            names = [tool.name for tool in tools.tools]
            assert len(names) == 10 and "r1_observe" in names and "r1_type_search" in names
            result = {"handshake": "PASS", "tools": names, "device_checks": False}
            if device:
                for name in ("r1_status", "r1_now_playing", "r1_observe"):
                    call = await session.call_tool(name, {})
                    assert not call.isError, str(call)
                    data = json.loads(next(c.text for c in call.content if c.type == "text"))
                    if name == "r1_observe":
                        assert data["stable_pair"] and data["lines"], data
                        result["observation"] = {k: data[k] for k in ("snapshot_id", "stable_pair", "display_available", "text")}
                        picture = await session.call_tool("r1_screenshot", {"snapshot_id": data["snapshot_id"]})
                        assert not picture.isError and any(c.type == "image" for c in picture.content)
                    elif name == "r1_now_playing":
                        result["sonixlink_available"] = data["available"]
                result["device_checks"] = True
                result["touches_sent"] = 0
            print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", action="store_true")
    asyncio.run(main(parser.parse_args().device))
