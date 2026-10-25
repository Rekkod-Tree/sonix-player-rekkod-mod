"""Test the packer's actual daemon/receipt gate on temporary files only."""
from pathlib import Path
import hashlib,os,stat,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
text=(root/'sonix-packer/sonix_firmware_packer.sh').read_text()
start=text.index('\t\t# Never silently ship the original crashing A2DP daemon')
end=text.index("\t\t# This overlay's modules",start)
block=text[start:end]
with tempfile.TemporaryDirectory(prefix='sonix-packer-bt-') as tmp:
 tmp=Path(tmp)
 binary=tmp/'bluetoothd';binary.write_bytes(b'not-an-ELF-hash-gate-fixture\0')
 receipt=tmp/'bluetoothd.sha256';receipt.write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'  bluetoothd\n')
 for case in ('valid','tampered','missing','malformed'):
  dest=tmp/case;dest.mkdir()
  env=dict(os.environ,SCRIPT_DIR=str(tmp),SQUASH_DIR=str(dest),R1_BLUETOOTHD=str(binary),R1_BLUETOOTHD_SHA256_FILE=str(receipt))
  if case=='tampered':receipt.write_text('0'*64+'  bluetoothd\n')
  elif case=='missing':env['R1_BLUETOOTHD']=str(tmp/'absent')
  elif case=='malformed':receipt.write_text('not-a-digest\n')
  r=subprocess.run(['bash','-c','set -eu\ndie() { echo "$*" >&2; exit 9; }\n'+block],env=env,capture_output=True)
  target=dest/'usr/libexec/bluetooth/bluetoothd'
  if case=='valid':
   assert r.returncode==0 and target.read_bytes()==binary.read_bytes(),r.stderr
   assert stat.S_IMODE(target.stat().st_mode)==0o755
  else:assert r.returncode!=0 and not target.exists(),case
print('PASS: actual packer accepts matching receipt and rejects missing/tampered/malformed input; no image/device mutation')
