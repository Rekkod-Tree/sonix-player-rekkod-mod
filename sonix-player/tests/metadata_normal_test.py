"""Normal media regression fixtures for the upstream metadata integration.
Generate with --generate DIRECTORY (requires ffmpeg). After make host, link
with --build-reader CHECKOUT --reader OUTPUT_BINARY. Verify with
--verify DIRECTORY --reader OUTPUT_BINARY. No damaged media,
fuzzing, network, or audio output is used. Optional --adb ADB --serial SERIAL
--device-root REMOTE_DIRECTORY reads fixtures already staged on the device;
--reader then names the remote CLI. This mode never pushes or deletes files.
"""
import argparse,json,math,random,shlex,shutil,struct,subprocess,wave,zlib
from pathlib import Path, PurePosixPath

def be32(n): return struct.pack('>I',n)
def le32(n): return struct.pack('<I',n)
def syncsafe(n): return bytes([(n>>21)&127,(n>>14)&127,(n>>7)&127,n&127])
def id3(frames,version=4):
    body=b''.join(name.encode()+ (syncsafe(len(data)) if version==4 else be32(len(data))) + struct.pack('>H',flags)+data for name,data,flags in frames)
    return b'ID3'+bytes([version,0,0])+syncsafe(len(body))+body

def png():
    rng=random.Random(42)
    raw=b''.join(b'\0'+rng.randbytes(512*3) for _ in range(512))
    def chunk(name,data):return be32(len(data))+name+data+be32(zlib.crc32(name+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',512,512,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

def with_picture(data):
    assert data[:4]==b'fLaC';blocks=[];at=4
    while True:
        typ=data[at];size=int.from_bytes(data[at+1:at+4],'big');blocks.append((typ&127,data[at+4:at+4+size]));at+=4+size
        if typ&128:break
    picture=png();mime=b'image/png'
    payload=be32(3)+be32(len(mime))+mime+be32(0)+be32(512)+be32(512)+be32(24)+be32(0)+be32(len(picture))+picture
    blocks.insert(1,(6,payload))
    return b'fLaC'+b''.join(bytes([typ|(128 if i==len(blocks)-1 else 0)])+len(body).to_bytes(3,'big')+body for i,(typ,body) in enumerate(blocks))+data[at:]

def ape_tag(items):
    body=b''.join(le32(len(value.encode()))+le32(0)+key.encode()+b'\0'+value.encode() for key,value in items)
    return body+b'APETAGEX'+le32(2000)+le32(len(body)+32)+le32(len(items))+le32(0)+b'\0'*8

def strip_ape(data):
    if data[-32:-24]==b'APETAGEX':
        size=int.from_bytes(data[-20:-16],'little');data=data[:-size]
        if data[-32:-24]==b'APETAGEX':data=data[:-32]
    return data

def generate(root):
    root.mkdir(parents=True,exist_ok=False)
    ffmpeg=shutil.which('ffmpeg');assert ffmpeg,'ffmpeg is required'
    for bits in (16,24):
        with wave.open(str(root/f'source{bits}.wav'),'wb') as out:
            out.setparams((2,bits//8,48000,12000,'NONE','not compressed'))
            frames=bytearray()
            for i in range(12000):
                v=int(math.sin(2*math.pi*431*i/48000)*((1<<(bits-1))-1)*0.2)
                frames.extend(v.to_bytes(bits//8,'little',signed=True)*2)
            out.writeframes(frames)
    def encode(name,codec,bits=16,tags=(),extra=()):
        cmd=[ffmpeg,'-nostdin','-v','error','-i',str(root/f'source{bits}.wav'),'-map_metadata','-1','-c:a',codec]
        for key,value in tags:cmd+=['-metadata',f'{key}={value}']
        subprocess.run(cmd+list(extra)+[str(root/name)],check=True)
    meta=[];pairs=[]
    def expect(name,**fields):meta.append({'file':name,'expected':fields})
    encode('native.flac','flac',24,[('title','Vorbis title'),('artist','Artist'),('ALBUM_ARTIST','Album artist'),('TRACK','7'),('YEAR','2004')])
    expect('native.flac',title='Vorbis title',album_artist='Album artist',track=7,year=2004,has_stream=1,rate=48000,bits=24,duration_ms=250)
    data=(root/'native.flac').read_bytes();(root/'picture.flac').write_bytes(with_picture(data));expect('picture.flac',title='Vorbis title',has_stream=1,rate=48000,bits=24,duration_ms=250)
    prefix=id3([('TIT2',b'\3ID3 fallback title',0),('TALB',b'\3Fallback album',0)])
    (root/'prefixed.flac').write_bytes(prefix+data);expect('prefixed.flac',title='Vorbis title',album='Fallback album',has_stream=1,rate=48000,bits=24,duration_ms=250)
    (root/'renamed.mp3').write_bytes(data);expect('renamed.mp3',title='Vorbis title',rate=48000,bits=24,format=2)
    pairs += [('native.flac','prefixed.flac'),('native.flac','picture.flac'),('native.flac','renamed.mp3')]
    shutil.copyfile(root/'native.flac',root/'sidecar.flac');(root/'sidecar.flac.tags').write_text('title=Service title\nalbum=Service album\n',encoding='utf-8');expect('sidecar.flac',title='Service title',album='Service album',has_stream=1,rate=48000,bits=24)
    encode('ogg.flac','flac',16,[('title','Ogg FLAC title')],['-f','ogg']);expect('ogg.flac',title='Ogg FLAC title')
    encode('bare.mp3','libmp3lame',16,extra=['-id3v2_version','0','-write_id3v1','0'])
    mp3=(root/'bare.mp3').read_bytes()
    unicode_text='  Привет мир  '
    (root/'unicode.mp3').write_bytes(id3([('TIT2',b'\0'+unicode_text.encode(),0),('TPE1',b'\3Artist\nName',0)])+mp3);expect('unicode.mp3',title='Привет мир',artist='Artist Name')
    (root/'chain.mp3').write_bytes(id3([('TIT2',b'\3First title',0)])+id3([('TIT2',b'\3Second title',0),('TALB',b'\3Second album',0)])+mp3);expect('chain.mp3',title='First title',album='Second album')
    text=b'\0Compressed title';compressed=be32(len(text))+zlib.compress(text)
    (root/'compressed.mp3').write_bytes(id3([('TIT2',compressed,0x80)],3)+mp3);expect('compressed.mp3',title='Compressed title')
    text=b'\3DLI title';(root/'dli.mp3').write_bytes(id3([('TIT2',syncsafe(len(text))+text,1)])+mp3);expect('dli.mp3',title='DLI title')
    txxx=b'\1'+('SERIES\0Series name').encode('utf-16')
    (root/'series.mp3').write_bytes(id3([('TXXX',txxx,0)],3)+mp3);expect('series.mp3',series='Series name')
    (root/'ape-mp3.mp3').write_bytes(mp3+ape_tag([('Title','APE title'),('DiscNumber','2'),('TrackNumber','4')]));expect('ape-mp3.mp3',title='APE title',disc=2,track=4)
    encode('opus.opus','libopus',16,[('title','Opus title'),('R128_TRACK_GAIN','-1280'),('R128_ALBUM_GAIN','-2560')]);expect('opus.opus',title='Opus title',has_track_gain=1,has_album_gain=1,track_gain=0.0,album_gain=-5.0)
    encode('opus-rg.opus','libopus',16,[('R128_TRACK_GAIN','-1280'),('REPLAYGAIN_TRACK_GAIN','-2.5 dB')]);expect('opus-rg.opus',has_track_gain=1,track_gain=-2.5)
    encode('vorbis.ogg','libvorbis',16,[('title','Vorbis song')]);(root/'id3-vorbis.ogg').write_bytes(prefix+(root/'vorbis.ogg').read_bytes());expect('id3-vorbis.ogg',title='Vorbis song');pairs += [('vorbis.ogg','id3-vorbis.ogg')]
    encode('alac.m4a','alac',24,[('title','ALAC song')]);expect('alac.m4a',title='ALAC song',has_stream=1,alac=1,rate=48000,bits=24,duration_ms=250)
    shutil.copyfile(root/'alac.m4a',root/'alac-renamed.flac');expect('alac-renamed.flac',title='ALAC song',has_stream=1,alac=1,rate=48000,bits=24)
    shutil.copyfile(root/'opus.opus',root/'opus-renamed.ogg');expect('opus-renamed.ogg',title='Opus title',has_track_gain=1,track_gain=0.0)
    pairs += [('alac.m4a','alac-renamed.flac'),('opus.opus','opus-renamed.ogg')]
    encode('aac.m4a','aac',16,[('title','AAC song')]);expect('aac.m4a',title='AAC song',has_stream=1,alac=0,rate=48000,bits=0)
    encode('wavepack.wv','wavpack');wv=strip_ape((root/'wavepack.wv').read_bytes());(root/'wavepack.wv').write_bytes(wv+ape_tag([('Title','WavPack title'),('Disc','3'),('Track','8')]));expect('wavepack.wv',title='WavPack title',disc=3,track=8)
    wav=(root/'source16.wav').read_bytes();value='Русский WAV'.encode()+b'\0';chunk=b'INAM'+le32(len(value))+value+(b'\0' if len(value)&1 else b'');body=b'INFO'+chunk;wav+=b'LIST'+le32(len(body))+body;wav=wav[:4]+le32(len(wav)-8)+wav[8:];(root/'info.wav').write_bytes(wav);expect('info.wav',title='Русский WAV')
    encode('native.aiff','pcm_s16be');aiff=(root/'native.aiff').read_bytes()
    for key,value in [(b'NAME',b'AIFF title'),(b'AUTH',b'AIFF artist')]:aiff+=key+be32(len(value))+value+(b'\0' if len(value)&1 else b'')
    aiff=aiff[:4]+be32(len(aiff)-8)+aiff[8:];(root/'native.aiff').write_bytes(aiff);expect('native.aiff',title='AIFF title',artist='AIFF artist')
    encode('native.caf','pcm_s16le',16,[('title','CAF title'),('artist','CAF artist')]);expect('native.caf',title='CAF title',artist='CAF artist')
    shutil.copyfile(root/'native.flac',root/'converted.flac')
    (root/'converted.cue').write_text('PERFORMER "CUE artist"\nTITLE "CUE album"\nFILE "converted.wav" WAVE\n TRACK 01 AUDIO\n  TITLE "CUE track"\n  INDEX 01 00:00:00\n')
    (root/'manifest.json').write_text(json.dumps({'metadata':meta,'pcm_pairs':pairs,'cue':'converted.cue'},indent=2,ensure_ascii=False),encoding='utf-8')
    print(f'Generated {len(meta)} normal metadata cases and {len(pairs)} decode/seek pairs in {root}')

def build_reader(mirror,reader):
    # Reuse the completed production host build, including its normal main.o
    # (other display helpers live there). Only the startup main reference is
    # wrapped to the read-only test CLI; no player/UI initialization is called.
    mirror=mirror.resolve();reader=reader.resolve()
    def variable(name):
        target='metadata_print_'+name.lower()
        rule=f'{target}:; @printf "%s\\n" "$({name})"'
        return subprocess.check_output(['make','--no-print-directory','-s','--eval',rule,target],cwd=mirror,text=True).strip()
    objects=[mirror/name for name in shlex.split(variable('HOST_OBJS'))]
    assert objects and all(p.is_file() for p in objects),'Complete make host before linking the test reader'
    obj=reader.with_suffix('.o')
    source=Path(__file__).with_suffix('.c').resolve()
    subprocess.run(['cc',*shlex.split(variable('HOST_CFLAGS')),'-Dmain=__wrap_main','-c',str(source),'-o',str(obj)],cwd=mirror,check=True)
    subprocess.run(['c++','-o',str(reader),str(obj),*map(str,objects),'-Wl,--wrap=main',*shlex.split(variable('HOST_LDFLAGS'))],cwd=mirror,check=True)
    print('Linked normal-file reader against completed host production objects')

def verify(root,reader,adb=None,device_root=None,serial=None):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'));checks=0
    if adb:
        assert device_root and PurePosixPath(device_root).is_absolute(),'--adb requires an absolute --device-root'
    def run(*args):
        if adb:
            # Fixtures and reader must already be staged by the caller. This
            # mode only executes the read-only CLI; it never pushes or deletes.
            remote=[reader.as_posix()]
            for arg in args:
                if isinstance(arg,Path):
                    remote.append(str(PurePosixPath(device_root)/arg.relative_to(root).as_posix()))
                else:remote.append(str(arg))
            command=[str(adb)]+(['-s',serial] if serial else [])+['shell',' '.join(shlex.quote(arg) for arg in remote)]
        else:command=[str(reader),*map(str,args)]
        p=subprocess.run(command,text=True,encoding='utf-8',stdout=subprocess.PIPE,stderr=subprocess.PIPE)

        assert p.returncode==0,(args,p.returncode,p.stdout,p.stderr)
        records=[line[7:] for line in p.stdout.splitlines() if line.startswith('RESULT ')]
        assert len(records)==1,(args,p.stdout,p.stderr)
        return json.loads(records[0])
    for case in manifest['metadata']:
        got=run('meta',root/case['file'])
        for key,expected in case['expected'].items():assert got[key]==expected,(case['file'],key,got[key],expected,got)
        print('PASS metadata',case['file']);checks+=1
    for a,b in manifest['pcm_pairs']:
        for at in (0,1024):
            x=run('pcm',root/a,at);y=run('pcm',root/b,at);assert x==y,(a,b,at,x,y)
            print('PASS PCM/seek',a,b,at);checks+=1
    cue=run('cue',root/manifest['cue']);assert Path(cue['audio']).name=='converted.flac' and cue['tracks']==1;checks+=1
    print(f'PASS: {checks} normal-file metadata, source-format, PCM/seek and CUE checks')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--generate',type=Path);p.add_argument('--verify',type=Path);p.add_argument('--reader',type=Path);p.add_argument('--build-reader',type=Path);p.add_argument('--adb',type=Path);p.add_argument('--device-root');p.add_argument('--serial');a=p.parse_args()
    if a.build_reader:assert a.reader;build_reader(a.build_reader,a.reader)
    if a.generate:generate(a.generate)
    if a.verify:assert a.reader;verify(a.verify,a.reader,a.adb,a.device_root,a.serial)
