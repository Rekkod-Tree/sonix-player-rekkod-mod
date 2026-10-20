// Protocol transport only: Sonix owns decoding, the queue, DSP and the DAC.
mod catalog;
mod transfer;
mod lifecycle;

use std::{fs, os::unix::fs::{FileTypeExt, PermissionsExt}, path::PathBuf, sync::Arc, time::Duration};
use std::os::fd::{FromRawFd, OwnedFd};
use tokio::io::unix::AsyncFd;
use librespot_core::{cache::Cache, config::SessionConfig, Session};
use serde::Deserialize;
use serde_json::{Value, json};
use tokio::{io::{AsyncReadExt, AsyncWriteExt}, net::{UnixListener, UnixStream}, sync::{Mutex, Semaphore}};

const PROTOCOL: u32 = 1;
const REQUEST_MAX: usize = 8192;
const RESPONSE_MAX: usize = 128 * 1024;
const REQUEST_TIMEOUT: Duration = Duration::from_secs(20);

#[derive(Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct Request {
    op: String,
    #[serde(default)] id: String,
    #[serde(default)] query: String,
    #[serde(default)] offset: usize,
    #[serde(default = "default_bitrate")] bitrate: u16,
    #[serde(default)] cursor: Option<catalog::Cursor>,
}
fn default_bitrate() -> u16 { 160 }
struct Args { socket: PathBuf, credentials: PathBuf, parent: Option<OwnedFd> }
struct App {
    args: Args,
    session: Mutex<Option<Session>>,
    catalog: Mutex<catalog::Cursor>,
    browsing: Mutex<()>,
    streams: Arc<Semaphore>,
    activity: Arc<lifecycle::Activity>,
    exiting: tokio::sync::Notify,
}
fn failure(reason: &'static str) -> Value { json!({"ok":false,"protocol":PROTOCOL,"error":reason}) }

impl App {
    async fn session(&self) -> Result<Session, &'static str> {
        let mut current = self.session.lock().await;
        if let Some(session) = current.as_ref() { if !session.is_invalid() { return Ok(session.clone()); } }
        if let Some(old) = current.take() { old.shutdown(); }
        let directory = fs::symlink_metadata(&self.args.credentials).map_err(|_| "auth_required")?;
        let metadata = fs::symlink_metadata(self.args.credentials.join("credentials.json")).map_err(|_| "auth_required")?;
        if !directory.is_dir() || directory.permissions().mode() & 0o077 != 0 || !metadata.is_file() || metadata.permissions().mode() & 0o077 != 0 { return Err("private_credentials_required"); }
        let cache = Cache::new(Some(&self.args.credentials),None::<&PathBuf>,None::<&PathBuf>,None).map_err(|_| "auth_required")?;
        let credentials = cache.credentials().ok_or("auth_required")?;
        let session = Session::new(SessionConfig { autoplay:Some(false), ..Default::default() },Some(cache));
        if !matches!(tokio::time::timeout(Duration::from_secs(15),session.connect(credentials,false)).await,Ok(Ok(()))) {
            session.shutdown(); return Err("connection_failed");
        }
        if matches!(session.get_user_attribute("type").as_deref(),Some("free"|"open")) { session.shutdown(); return Err("premium_required"); }
        *current = Some(session.clone()); Ok(session)
    }

    async fn request(&self, request: Request) -> Value {
        if request.op == "probe" { return json!({"ok":true,"protocol":PROTOCOL,"version":env!("CARGO_PKG_VERSION")}); }
        if request.op == "status" {
            let connected = self.session.try_lock().ok().and_then(|s| s.clone()).is_some_and(|s| !s.is_invalid());
            return json!({"ok":true,"protocol":PROTOCOL,"connected":connected});
        }
        if request.op == "keepalive" { let _busy = self.activity.begin(); return json!({"ok":true,"protocol":PROTOCOL}); }
        if !matches!(request.op.as_str(),"authenticate"|"track"|"playlists"|"liked"|"playlist"|"album"|"search") { return failure("invalid_request"); }
        let _busy = self.activity.begin();
        let _browse = match self.browsing.try_lock() { Ok(b) => b, Err(_) => return failure("busy") };
        let session = match self.session().await { Ok(s) => s, Err(e) => return failure(e) };
        if request.op == "authenticate" { return json!({"ok":true,"protocol":PROTOCOL}); }
        if request.op == "track" {
            return match catalog::track(&session,&request.id).await { Ok(item) => json!({"ok":true,"protocol":PROTOCOL,"item":item}), Err(e) => failure(e) };
        }
        if request.op == "playlists" {
            return match catalog::playlists(&session,request.offset).await {
                Ok((items,next,more)) => json!({"ok":true,"protocol":PROTOCOL,"items":items,"next_offset":next,"more":more}), Err(e) => failure(e)
            };
        }
        let uri = match request.op.as_str() {
            "liked" => format!("spotify:user:{}:collection",session.username()),
            "playlist"|"album" if catalog::valid_id(&request.id) => format!("spotify:{}:{}",request.op,request.id),
            "search" if !request.query.trim().is_empty() && request.query.len() <= 512 => {
                let query: String = request.query.bytes().map(|b| if b.is_ascii_alphanumeric() { (b as char).to_string() } else { format!("%{b:02X}") }).collect();
                format!("spotify:search:{query}")
            },
            _ => return failure("invalid_request"),
        };
        let mut cursor = self.catalog.lock().await;
        if let Some(checkpoint) = request.cursor {
            if !checkpoint.valid_for(&uri,request.offset) { return failure("invalid_cursor"); }
            *cursor = checkpoint;
        }
        match cursor.page(&session,&uri,request.offset).await {
            Ok((items,next,more)) => json!({"ok":true,"protocol":PROTOCOL,"items":items,"next_offset":next,"more":more,"cursor":cursor.checkpoint()}), Err(e) => failure(e)
        }
    }
}

async fn reply(stream: &mut UnixStream, response: &Value) {
    let mut bytes = serde_json::to_vec(response).unwrap_or_default();
    if bytes.len() > RESPONSE_MAX { bytes = serde_json::to_vec(&failure("response_limit")).unwrap(); }
    bytes.push(b'\n'); let _ = tokio::time::timeout(Duration::from_secs(2),stream.write_all(&bytes)).await;
}

async fn serve(mut stream: UnixStream, app: Arc<App>) {
    let mut bytes = Vec::with_capacity(1024);
    let read = async {
        let mut chunk = [0u8;1024];
        loop {
            let count = stream.read(&mut chunk).await?;
            if count == 0 { return Err(std::io::ErrorKind::UnexpectedEof.into()); }
            let end = chunk[..count].iter().position(|b| *b == b'\n').map(|i| i+1);
            bytes.extend_from_slice(&chunk[..end.unwrap_or(count)]);
            if bytes.len() > REQUEST_MAX { return Err(std::io::ErrorKind::InvalidData.into()); }
            if end.is_some() { return Ok::<_,std::io::Error>(()); }
        }
    };
    if !matches!(tokio::time::timeout(Duration::from_secs(2),read).await,Ok(Ok(()))) { return; }
    let request = match serde_json::from_slice::<Request>(&bytes) {
        Ok(request) => request, Err(_) => { reply(&mut stream,&failure("invalid_request")).await; return; }
    };
    if request.op == "quit" {
        reply(&mut stream,&json!({"ok":true,"protocol":PROTOCOL})).await;
        app.exiting.notify_one();
        return;
    }
    if request.op == "stream" {
        if !catalog::valid_id(&request.id) || !matches!(request.bitrate,96|160|320) || request.offset > i32::MAX as usize { reply(&mut stream,&failure("invalid_request")).await; return; }
        let _permit = match app.streams.clone().try_acquire_owned() { Ok(p) => p, Err(_) => { reply(&mut stream,&failure("busy")).await; return; } };
        let _busy = app.activity.begin();
        let session = match tokio::time::timeout(REQUEST_TIMEOUT,app.session()).await {
            Ok(Ok(session)) => session, Ok(Err(error)) => { reply(&mut stream,&failure(error)).await; return; },
            Err(_) => { reply(&mut stream,&failure("timeout")).await; return; }
        };
        transfer::serve(session,request.id,request.bitrate,request.offset,stream).await;
        return;
    }
    let response = match tokio::time::timeout(REQUEST_TIMEOUT,app.request(request)).await { Ok(r) => r, Err(_) => failure("timeout") };
    reply(&mut stream,&response).await;
}

async fn parent_closed(parent: &Option<AsyncFd<OwnedFd>>) {
    match parent {
        Some(parent) => { let _ = parent.readable().await; },
        None => std::future::pending::<()>().await,
    }
}

async fn run(mut args: Args) -> Result<(), &'static str> {
    // EOF belongs to the whole Sonix process, not the short-lived RPC thread
    // which created us. No periodic parent-liveness polling is needed.
    let parent = args.parent.take().map(AsyncFd::new).transpose().map_err(|_| "invalid_parent_fd")?;
    let metadata = fs::symlink_metadata(args.socket.parent().ok_or("invalid_socket")?).map_err(|_| "invalid_socket")?;
    if !metadata.is_dir() || metadata.permissions().mode() & 0o077 != 0 { return Err("private_socket_required"); }
    if let Ok(metadata) = fs::symlink_metadata(&args.socket) {
        if !metadata.file_type().is_socket() { return Err("socket_in_use"); }
        match tokio::time::timeout(Duration::from_millis(500),UnixStream::connect(&args.socket)).await {
            Ok(Err(error)) if error.kind() == std::io::ErrorKind::ConnectionRefused => {}, _ => return Err("socket_in_use")
        }
        fs::remove_file(&args.socket).map_err(|_| "socket_in_use")?;
    }
    let listener = UnixListener::bind(&args.socket).map_err(|_| "socket_failed")?;
    fs::set_permissions(&args.socket,fs::Permissions::from_mode(0o600)).map_err(|_| "socket_failed")?;
    let app = Arc::new(App { args, session:Mutex::new(None), catalog:Mutex::new(catalog::Cursor::default()), browsing:Mutex::new(()),
        streams:Arc::new(Semaphore::new(2)), activity:lifecycle::Activity::new(), exiting:tokio::sync::Notify::new() });
    let clients = Arc::new(Semaphore::new(4));
    let mut tasks = tokio::task::JoinSet::new();
    let mut terminate = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()).map_err(|_| "signal_failed")?;
    loop {
        tokio::select! {
            accepted = listener.accept() => {
                let (stream,_) = accepted.map_err(|_| "socket_failed")?;
                if let Ok(permit) = clients.clone().try_acquire_owned() { let app = app.clone(); tasks.spawn(async move { let _permit = permit; serve(stream,app).await; }); }
            },
            _ = tasks.join_next(), if !tasks.is_empty() => {},
            _ = terminate.recv() => break,
            _ = parent_closed(&parent) => break,
            _ = app.exiting.notified() => break,
            _ = app.activity.expired(Duration::from_secs(30)) => break,
        }
    }
    // Cancel network requests before waiting for their session lock. Sleep or
    // source changes must not wait out a pending authentication timeout.
    tasks.abort_all();
    while tasks.join_next().await.is_some() {}
    if let Some(session) = app.session.lock().await.take() { session.shutdown(); }
    let _ = fs::remove_file(&app.args.socket); Ok(())
}

fn arguments() -> Result<Option<Args>, &'static str> {
    let mut socket = None; let mut credentials = None; let mut parent = None; let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--self-test" => {
                let a = SessionConfig::default().device_id;
                let b = SessionConfig::default().device_id;
                let mut map = std::collections::HashMap::new(); map.insert("probe",true);
                let ready = !a.is_empty() && a!=b && map.get("probe")==Some(&true);
                println!("{}",json!({"protocol":PROTOCOL,"os_random_ready":ready}));
                if !ready { std::process::exit(2); } return Ok(None);
            },
            "--version"|"--probe" => { println!("{}",json!({"protocol":PROTOCOL,"version":env!("CARGO_PKG_VERSION")})); return Ok(None); },
            "--socket" => socket = args.next().map(PathBuf::from),
            "--credentials-dir" => credentials = args.next().map(PathBuf::from),
            "--parent-fd" if parent.is_none() => {
                let fd = args.next().and_then(|value| value.parse::<i32>().ok()).filter(|fd| *fd > 2).ok_or("invalid_parent_fd")?;
                // The launcher transfers this descriptor; this process owns it.
                parent = Some(unsafe { OwnedFd::from_raw_fd(fd) });
            },
            _ => return Err("invalid_arguments"),
        }
    }
    Ok(Some(Args { socket:socket.ok_or("invalid_arguments")?, credentials:credentials.ok_or("invalid_arguments")?, parent }))
}
fn main() {
    let result = match arguments() {
        Ok(None) => return,
        Ok(Some(args)) => { let runtime = tokio::runtime::Builder::new_current_thread().enable_all().max_blocking_threads(2).build().unwrap();
            let result = runtime.block_on(run(args)); runtime.shutdown_timeout(Duration::from_secs(2)); result },
        Err(error) => Err(error)
    };
    if let Err(error) = result { eprintln!("sonix-spotify: {error}"); std::process::exit(1); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn parent_channel_eof_exits_before_idle_deadline() {
        let directory = std::env::temp_dir().join(format!("sonix-spotify-owner-test-{}",std::process::id()));
        fs::create_dir(&directory).unwrap(); fs::set_permissions(&directory,fs::Permissions::from_mode(0o700)).unwrap();
        let socket = directory.join("control.sock");
        let (owner, child) = std::os::unix::net::UnixStream::pair().unwrap();
        child.set_nonblocking(true).unwrap();
        let server = tokio::spawn(run(Args { socket:socket.clone(),credentials:directory.join("credentials"),parent:Some(child.into()) }));
        for _ in 0..50 { if socket.exists() { break; } tokio::time::sleep(Duration::from_millis(10)).await; }
        assert!(socket.exists());
        drop(owner);
        tokio::time::timeout(Duration::from_secs(1),server).await.unwrap().unwrap().unwrap();
        assert!(!socket.exists()); fs::remove_dir(directory).unwrap();
    }
    #[tokio::test]
    async fn private_ipc_rejects_missing_auth_and_invalid_streams_without_network() {
        let directory = std::env::temp_dir().join(format!("sonix-spotify-ipc-test-{}",std::process::id()));
        fs::create_dir(&directory).unwrap(); fs::set_permissions(&directory,fs::Permissions::from_mode(0o700)).unwrap();
        let socket = directory.join("control.sock");
        let server = tokio::spawn(run(Args { socket:socket.clone(), credentials:directory.join("credentials"), parent:None }));
        for _ in 0..50 { if socket.exists() { break; } tokio::time::sleep(Duration::from_millis(10)).await; }
        async fn rpc(path: &PathBuf, body: &[u8]) -> Value {
            let mut stream = UnixStream::connect(path).await.unwrap(); stream.write_all(body).await.unwrap();
            let mut reply = Vec::new(); stream.read_to_end(&mut reply).await.unwrap(); serde_json::from_slice(&reply).unwrap()
        }
        assert_eq!(rpc(&socket,b"{\"op\":\"probe\"}\n").await["protocol"],PROTOCOL);
        assert_eq!(rpc(&socket,b"{\"op\":\"liked\"}\n").await["error"],"auth_required");
        assert_eq!(rpc(&socket,b"{\"op\":\"unknown\"}\n").await["error"],"invalid_request");
        assert_eq!(rpc(&socket,b"{\"op\":\"stream\",\"id\":\"../credentials\"}\n").await["error"],"invalid_request");
        let mut partial = UnixStream::connect(&socket).await.unwrap();
        partial.write_all(b"{\"op\":").await.unwrap();
        let mut quit = UnixStream::connect(&socket).await.unwrap(); quit.write_all(b"{\"op\":\"quit\"}\n").await.unwrap();
        tokio::time::timeout(Duration::from_secs(1),server).await.unwrap().unwrap().unwrap();
        let mut tail = Vec::new();
        assert_eq!(partial.read_to_end(&mut tail).await.unwrap(),0);
        assert!(!socket.exists()); fs::remove_dir(directory).unwrap();
    }
}
