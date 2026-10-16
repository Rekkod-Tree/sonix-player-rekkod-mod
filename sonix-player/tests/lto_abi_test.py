#!/usr/bin/env python3
"""Cross-link the LTO math ABI regression; never execute a MIPS binary.

Run from any directory:
  python3 tests/lto_abi_test.py --cc /path/to/bin/mipsel-rockbox-linux-gnu-gcc
  ROCKBOX_TOOLCHAIN_DIR=/path/to/toolchain python3 tests/lto_abi_test.py

The matching g++ and readelf are inferred beside gcc; --cxx and --readelf can
override them. --diagnose-max additionally checks many LTRANS partitions, but
does not require that a future compiler reproduce GCC 9's broken symbol binding.
All generated sources, objects and binaries live in a temporary directory.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def parse_arguments():
    root = os.environ.get("ROCKBOX_TOOLCHAIN_DIR")
    default_cc = str(Path(root) / "bin/mipsel-rockbox-linux-gnu-gcc") if root else None
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cc", default=default_cc, help="MIPS GCC path; alternatively set ROCKBOX_TOOLCHAIN_DIR")
    parser.add_argument("--cxx", help="matching MIPS g++ path")
    parser.add_argument("--readelf", help="matching MIPS readelf path")
    parser.add_argument("--diagnose-max", action="store_true", help="also report the optional many-partition diagnostic")
    args = parser.parse_args()
    if not args.cc:
        parser.error("provide --cc or ROCKBOX_TOOLCHAIN_DIR")
    cc = Path(args.cc).absolute()
    if not cc.name.endswith("gcc") and (not args.cxx or not args.readelf):
        parser.error("this compiler name requires explicit --cxx and --readelf")
    prefix = cc.name[:-3]
    cxx = Path(args.cxx).absolute() if args.cxx else cc.with_name(prefix + "g++")
    readelf = Path(args.readelf).absolute() if args.readelf else cc.with_name(prefix + "readelf")
    for tool in (cc, cxx, readelf):
        if not tool.is_file() or not os.access(tool, os.X_OK):
            parser.error(f"tool is not executable: {tool}")
    return cc, cxx, readelf, args.diagnose_max


def run(command):
    return subprocess.check_output(list(map(str, command)), text=True, stderr=subprocess.STDOUT, timeout=120)


def inspect_abi(readelf, binary):
    symbols = run([readelf, "--dyn-syms", "-W", binary])
    powf = sorted(set(re.findall(r"\bpowf@GLIBC_(\d+(?:\.\d+)+)", symbols)))
    versions = [tuple(map(int, version.split("."))) for version in re.findall(r"\bGLIBC_(\d+(?:\.\d+)+)\b", symbols)]
    newest = max((version + (0,) * (3 - len(version)) for version in versions), default=())
    return powf, newest


def main():
    cc, cxx, readelf, diagnose_max = parse_arguments()
    repo = Path(__file__).resolve().parents[1]
    compat = repo / "src/system/core/glibc_compat.h"
    if not compat.is_file():
        raise RuntimeError(f"missing production compatibility header: {compat}")
    print(run([cc, "--version"]).splitlines()[0], flush=True)
    machine = ["-march=mips32r2", "-mno-madd4", "-mplt"]
    common = ["-g", "-Wall", "-Wextra", "-Werror", "-flto=4", *machine,
              "-ffunction-sections", "-fdata-sections", "-include", compat]
    with tempfile.TemporaryDirectory(prefix="sonix-lto-abi-") as directory:
        temp = Path(directory)
        objects = []
        # Distinct noinline functions stay live in separate translation units.
        # A two-function smoke test can stay in one partition and miss this bug.
        for index in range(8):
            source, obj = temp / f"math_{index}.c", temp / f"math_{index}.o"
            source.write_text(
                "#include <math.h>\nextern volatile float exponent;\n"
                f"__attribute__((noinline,noclone)) float math_{index}(float x) {{\n"
                f"    return powf(x, exponent) + {index}.0f;\n}}\n",
                encoding="ascii",
            )
            run([cc, *common, "-O3" if index % 2 == 0 else "-O2", "-c", source, "-o", obj])
            objects.append(obj)
        source, obj = temp / "caller.cpp", temp / "caller.o"
        source.write_text(
            'extern "C" { volatile float exponent = 1.37f; volatile float input = 2.0f; }\n'
            + "".join(f'extern "C" float math_{index}(float);\n' for index in range(8))
            + "int main() { float result = 0;\n"
            + "".join(f"    result += math_{index}(input);\n" for index in range(8))
            + "    return (int)result;\n}\n",
            encoding="ascii",
        )
        run([cxx, *common, "-O2", "-fno-exceptions", "-fno-rtti", "-c", source, "-o", obj])
        objects.append(obj)

        def link(partition):
            binary = temp / f"lto-{partition}"
            run([cxx, "-O2", "-flto=4", f"-flto-partition={partition}", *machine, *objects,
                 "-static-libstdc++", "-static-libgcc", "-Wl,--gc-sections", "-lm", "-o", binary])
            return inspect_abi(readelf, binary)

        powf, newest = link("one")
        if powf != ["2.0"] or not newest or newest > (2, 22, 0):
            raise RuntimeError(f"single-partition ABI regression: powf={powf}, newest GLIBC={newest}")
        print(f"PASS one: powf@GLIBC_2.0, newest GLIBC={newest}; binary not executed", flush=True)
        if diagnose_max:
            try:
                powf, newest = link("max")
                safe = powf == ["2.0"] and bool(newest) and newest <= (2, 22, 0)
                print(f"DIAGNOSTIC max: powf={powf}, newest GLIBC={newest}, ABI_OK={safe}; not required to fail")
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
                print(f"DIAGNOSTIC max unavailable: {error}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        if error.output:
            print(error.output, end="")
        raise
