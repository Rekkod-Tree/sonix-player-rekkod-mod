use std::{collections::VecDeque, io::{self, Read, Seek, SeekFrom, Write}, os::unix::net::UnixStream as Output, time::Duration};
use bytes::Bytes;
use futures_util::StreamExt;
use http_body_util::BodyExt;
use librespot_audio::AudioDecrypt;
use librespot_core::{Session, SpotifyUri, audio_key::AudioKey, cdn_url::CdnUrl, spotify_id::SpotifyId};
use librespot_metadata::audio::{AudioFileFormat, AudioItem};
use serde_json::json;
use tokio::{net::UnixStream, sync::mpsc};

const HEADER_BYTES: usize = 167;
const CHUNK: usize = 64 * 1024;
const RANGE_BYTES: usize = 1024 * 1024;

#[derive(Clone,Copy,Debug,PartialEq)]
struct Span { start: usize, end: usize, total: usize }
fn span(value: &str) -> Option<Span> {
    let (range,total) = value.strip_prefix("bytes ")?.split_once('/')?;
    let (start,end) = range.split_once('-')?;
    let s = Span { start:start.parse().ok()?, end:end.parse().ok()?, total:total.parse().ok()? };
    (s.start <= s.end && s.end < s.total && s.total <= i32::MAX as usize).then_some(s)
}

// A bounded, read-only segment. Seeking to its absolute origin initializes CTR
// at the same byte as the HTTP range; Sonix seeks in its own growing file.
struct Segment {
    receiver: mpsc::Receiver<Bytes>,
    pending: Bytes,
    position: u64,
}
impl Read for Segment {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        if out.is_empty() { return Ok(0); }
        while self.pending.is_empty() {
            match self.receiver.blocking_recv() { Some(bytes) => self.pending = bytes, None => return Ok(0) }
        }
        let count = out.len().min(self.pending.len());
        out[..count].copy_from_slice(&self.pending.split_to(count)); self.position += count as u64; Ok(count)
    }
}
impl Seek for Segment {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        match from {
            SeekFrom::Start(at) if at == self.position => Ok(at),
            SeekFrom::Current(0) => Ok(self.position),
            _ => Err(io::ErrorKind::Unsupported.into()),
        }
    }
}

fn finite(header: &[u8], offset: usize) -> Option<f32> {
    let value = f32::from_le_bytes(header.get(offset..offset+4)?.try_into().ok()?);
    value.is_finite().then_some(value)
}

fn write_audio(key: AudioKey, receiver: mpsc::Receiver<Bytes>, mut output: Output, total: usize, offset: usize, bitrate: u16) -> io::Result<()> {
    output.set_nonblocking(false)?; output.set_write_timeout(Some(Duration::from_secs(2)))?;
    let origin = if offset == 0 { 0 } else { offset + HEADER_BYTES };
    let mut input = AudioDecrypt::new(Some(key),Segment { receiver, pending:Bytes::new(), position:origin as u64 });
    input.seek(SeekFrom::Start(origin as u64))?;
    let mut started = false;
    let result = (|| {
        let mut header = [0u8;HEADER_BYTES];
        let mut magic = [0u8;4];
        if offset == 0 {
            input.read_exact(&mut header)?; input.read_exact(&mut magic)?;
            if &magic != b"OggS" { return Err(io::ErrorKind::InvalidData.into()); }
        }
        let response = json!({"ok":true,"protocol":1,"total":total,"offset":offset,"bitrate":bitrate,
            "track_gain_db":if offset==0 {finite(&header,144)} else {None},
            "track_peak":if offset==0 {finite(&header,148)} else {None},
            "album_gain_db":if offset==0 {finite(&header,152)} else {None},
            "album_peak":if offset==0 {finite(&header,156)} else {None}});
        writeln!(output,"{response}")?; started = true;
        let prefix = if offset == 0 { output.write_all(&magic)?; 4 } else { 0 };
        let expected = total.checked_sub(offset + prefix).ok_or(io::ErrorKind::InvalidData)?;
        let count = io::copy(&mut input.take(expected as u64),&mut output)?;
        if count != expected as u64 { return Err(io::ErrorKind::UnexpectedEof.into()); }
        Ok(())
    })();
    if result.is_err() && !started { let _ = writeln!(output,"{}",crate::failure("invalid_audio")); }
    result
}

async fn range(session: &Session, url: &str, at: usize, count: usize) -> Result<(hyper::Response<hyper::body::Incoming>,Span), &'static str> {
    if !url.starts_with("https://") || count == 0 { return Err("stream_failed"); }
    let mut request = session.spclient().stream_from_cdn(url,at,count).map_err(|_| "stream_failed")?;
    let response = tokio::time::timeout(Duration::from_secs(10),request.next()).await
        .map_err(|_| "timeout")?.ok_or("stream_failed")?.map_err(|_| "stream_failed")?;
    if response.status().as_u16() != 206 { return Err("stream_failed"); }
    let header = response.headers().get("content-range").and_then(|h|h.to_str().ok()).and_then(span).ok_or("stream_failed")?;
    if header.start != at || header.end >= at.checked_add(count).ok_or("stream_failed")? { return Err("stream_failed"); }
    Ok((response,header))
}

async fn source(session: &Session, id: &str, bitrate: u16) -> Result<(AudioKey,String,u16), &'static str> {
    let id = SpotifyId::from_base62(id).map_err(|_| "invalid_id")?;
    let mut candidates = VecDeque::from([SpotifyUri::Track { id }]);
    let formats = [(AudioFileFormat::OGG_VORBIS_320,320),(AudioFileFormat::OGG_VORBIS_160,160),(AudioFileFormat::OGG_VORBIS_96,96)];
    for _ in 0..8 {
        let Some(uri) = candidates.pop_front() else { break; };
        let item = AudioItem::get_file(session,uri).await.map_err(|_| "track_unavailable")?;
        if let Some(alternatives) = item.alternatives { candidates.extend(alternatives.0.into_iter().take(8)); }
        if item.availability.is_err() { continue; }
        for (format,quality) in formats.iter().filter(|(_,quality)| *quality <= bitrate) {
            let Some(file) = item.files.get(format).copied() else { continue; };
            let track = SpotifyId::try_from(&item.track_id).map_err(|_| "track_unavailable")?;
            let key = session.audio_key().request(track,file).await.map_err(|_| "stream_failed")?;
            let urls = CdnUrl::new(file).resolve_audio(session).await.map_err(|_| "stream_failed")?;
            let url = urls.try_get_urls().map_err(|_| "stream_failed")?.into_iter().find(|url|url.starts_with("https://")).ok_or("stream_failed")?.to_owned();
            return Ok((key,url,*quality));
        }
    }
    Err("track_unavailable")
}

pub async fn serve(session: Session, id: String, bitrate: u16, offset: usize, mut output: UnixStream) {
    let setup = async {
        let (key,url,quality) = source(&session,&id,bitrate).await?;
        let origin = if offset == 0 { 0 } else { offset.checked_add(HEADER_BYTES).ok_or("invalid_request")? };
        let (response,range) = range(&session,&url,origin,CHUNK).await?;
        if range.total < HEADER_BYTES + 4 || offset >= range.total - HEADER_BYTES { return Err("invalid_request"); }
        Ok::<_,&'static str>((key,url,quality,response,range))
    };
    let (key,url,quality,mut response,mut current) = match tokio::time::timeout(Duration::from_secs(20),setup).await {
        Ok(Ok(value)) => value,
        Ok(Err(error)) => { crate::reply(&mut output,&crate::failure(error)).await; return; },
        Err(_) => { crate::reply(&mut output,&crate::failure("timeout")).await; return; }
    };
    let total = current.total;
    let output = match output.into_std() { Ok(output) => output, Err(_) => return };
    let (sender,receiver) = mpsc::channel(2);
    let writer = tokio::task::spawn_blocking(move || write_audio(key,receiver,output,total-HEADER_BYTES,offset,quality));
    let producer = async {
        loop {
            let expected = current.end - current.start + 1;
            let mut received = 0usize;
            loop {
                let frame = tokio::time::timeout(Duration::from_secs(10),response.body_mut().frame()).await.map_err(|_| ())?;
                let Some(frame) = frame else { break; };
                let frame = frame.map_err(|_| ())?;
                let Ok(mut bytes) = frame.into_data() else { continue; };
                received = received.checked_add(bytes.len()).ok_or(())?;
                if received > expected { return Err(()); }
                while !bytes.is_empty() {
                    let count = bytes.len().min(CHUNK);
                    sender.send(bytes.split_to(count)).await.map_err(|_| ())?;
                }
            }
            if received != expected { return Err(()); }
            if current.end + 1 == total { break; }
            let next = current.end + 1;
            let (next_response,next_range) = range(&session,&url,next,(total-next).min(RANGE_BYTES)).await.map_err(|_| ())?;
            if next_range.total != total { return Err(()); }
            response = next_response; current = next_range;
        }
        Ok::<_,()>(())
    };
    let _ = producer.await;
    drop(sender);
    let _ = writer.await;
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{io::Cursor, os::unix::net::UnixListener};
    #[test]
    fn range_headers_are_strict_and_bounded() {
        assert_eq!(span("bytes 0-99/100"),Some(Span {start:0,end:99,total:100}));
        for invalid in ["bytes 0-100/100","bytes 9-0/100","bytes 0-9/*","bytes 0-9/4294967295"] { assert!(span(invalid).is_none()); }
    }
    fn decode(offset: usize, truncated: bool) -> (io::Result<()>,Vec<u8>,Vec<u8>) {
        let mut plain = vec![0;HEADER_BYTES];
        plain[144..148].copy_from_slice(&(-3.5f32).to_le_bytes());
        let mut audio = b"OggS".to_vec(); audio.extend((0..50000).map(|v|v as u8)); plain.extend(&audio);
        let key = AudioKey([7;16]);
        let mut encrypted = Vec::new(); AudioDecrypt::new(Some(key),Cursor::new(plain)).read_to_end(&mut encrypted).unwrap();
        let origin = if offset == 0 {0} else {offset+HEADER_BYTES};
        let end = if truncated {encrypted.len()-500} else {encrypted.len()};
        let data = encrypted[origin..end].to_vec();
        let (sender,receiver) = mpsc::channel(2);
        let producer = std::thread::spawn(move || { for chunk in data.chunks(113) { if sender.blocking_send(Bytes::copy_from_slice(chunk)).is_err() { break; } } });
        let path = std::env::temp_dir().join(format!("sonix-spotify-transfer-{}-{offset}-{truncated}.sock",std::process::id()));
        let listener = UnixListener::bind(&path).unwrap(); let output = Output::connect(&path).unwrap();
        let (mut input,_) = listener.accept().unwrap();
        let reader = std::thread::spawn(move || { let mut bytes=Vec::new(); input.read_to_end(&mut bytes).unwrap(); bytes });
        let result = write_audio(key,receiver,output,audio.len(),offset,160);
        producer.join().unwrap(); let reply = reader.join().unwrap(); fs_remove(path); (result,reply,audio)
    }
    fn fs_remove(path: std::path::PathBuf) { std::fs::remove_file(path).unwrap(); }
    #[test]
    fn decrypts_once_and_resumes_at_a_non_block_aligned_offset() {
        for offset in [0,37] {
            let (result,reply,audio) = decode(offset,false); result.unwrap();
            let end = reply.iter().position(|b|*b==b'\n').unwrap();
            let header: serde_json::Value = serde_json::from_slice(&reply[..end]).unwrap();
            assert_eq!(header["offset"],offset); assert_eq!(&reply[end+1..],&audio[offset..]);
            if offset==0 { assert_eq!(header["track_gain_db"],-3.5); }
        }
    }
    #[test]
    fn truncated_transfer_cannot_be_reported_as_complete() { assert!(decode(0,true).0.is_err()); }
}
