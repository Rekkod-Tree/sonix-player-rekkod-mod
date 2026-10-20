use std::collections::VecDeque;
use librespot_core::{Session, SpotifyUri, spotify_id::SpotifyId};
use librespot_metadata::{Metadata, Playlist, Track};
use librespot_protocol::playlist4_external::SelectedListContent;
use protobuf::Message;
use serde::{Deserialize, Serialize};
use serde_json::Value;

pub const PAGE_SIZE: usize = 20;

#[derive(Clone, Default, Serialize)]
pub struct Item {
    pub kind: &'static str,
    pub id: String,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub album_id: String,
    pub cover: String,
    pub duration_ms: u32,
}

fn short(text: &str) -> String { text.chars().filter(|c| !c.is_control()).take(160).collect() }
pub fn valid_id(id: &str) -> bool { id.len() == 22 && id.bytes().all(|b| b.is_ascii_alphanumeric()) }

pub async fn track(session: &Session, id: &str) -> Result<Item, &'static str> {
    if !valid_id(id) { return Err("invalid_id"); }
    let uri = SpotifyUri::Track { id: SpotifyId::from_base62(id).map_err(|_| "invalid_id")? };
    let info = Track::get(session, &uri).await.map_err(|_| "metadata_failed")?;
    let cover = info.album.covers.0.iter().filter(|image| image.width > 0)
        .min_by_key(|image| (image.width - 320).abs())
        .or_else(|| info.album.covers.0.first())
        .and_then(|image| image.id.to_base16().ok())
        .map(|id| format!("https://i.scdn.co/image/{id}")).unwrap_or_default();
    Ok(Item {
        kind: "track", id: id.into(), title: short(&info.name),
        artist: short(&info.artists.0.iter().map(|a| a.name.as_str()).collect::<Vec<_>>().join(", ")),
        album: short(&info.album.name), album_id: SpotifyId::try_from(&info.album.id).ok().and_then(|id| id.to_base62().ok()).unwrap_or_default(),
        cover, duration_ms: info.duration.max(0) as u32,
    })
}

// Only the current browse cursor is retained, never a mirror of the queue.
#[derive(Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Cursor {
    uri: String,
    offset: usize,
    ids: VecDeque<String>,
    next: String,
}

impl Cursor {
    pub fn valid_for(&self, uri: &str, offset: usize) -> bool {
        self.uri == uri && self.offset == offset && self.ids.len() <= 128
            && self.ids.iter().all(|id| valid_id(id)) && self.next.len() <= 2048
            && !self.next.chars().any(char::is_control)
    }

    pub fn checkpoint(&self) -> Value {
        let value = serde_json::to_value(self).unwrap_or(Value::Null);
        if self.ids.len() <= 128 && serde_json::to_vec(&value).is_ok_and(|bytes| bytes.len() <= 4096) { value } else { Value::Null }
    }
    pub async fn page(&mut self, session: &Session, uri: &str, offset: usize) -> Result<(Vec<Item>, usize, bool), &'static str> {
        if self.uri != uri || self.offset != offset {
            let context = session.spclient().get_context(uri).await.map_err(|_| "catalog_failed")?;
            self.uri = uri.into(); self.offset = 0; self.ids.clear(); self.next.clear();
            for page in context.pages {
                self.ids.extend(page.tracks.into_iter().filter_map(|t| t.uri).filter_map(|u| u.strip_prefix("spotify:track:").map(str::to_owned)));
                if let Some(next) = page.next_page_url { if !next.is_empty() { self.next = next; } }
            }
        }
        let mut out = Vec::with_capacity(PAGE_SIZE);
        let mut fetched = 0;
        while out.len() < PAGE_SIZE {
            if let Some(id) = self.ids.pop_front() {
                let index = self.offset; self.offset += 1;
                if index < offset { continue; }
                out.push(track(session, &id).await?);
                continue;
            }
            if self.next.is_empty() { break; }
            // Bound work per request; subsequent sequential pages reuse this cursor.
            if fetched >= 8 { return Err("catalog_failed"); }
            fetched += 1;
            let response = session.spclient().get_next_page(&self.next).await.map_err(|_| "catalog_failed")?;
            let page: Value = serde_json::from_slice(response.as_ref()).map_err(|_| "catalog_failed")?;
            let next = page.get("next_page_url").or_else(|| page.get("nextPageUrl")).and_then(Value::as_str).unwrap_or_default();
            if next == self.next { return Err("catalog_failed"); }
            self.next = next.into();
            if let Some(tracks) = page.get("tracks").and_then(Value::as_array) {
                self.ids.extend(tracks.iter().filter_map(|t| t.get("uri").and_then(Value::as_str))
                    .filter_map(|u| u.strip_prefix("spotify:track:").map(str::to_owned)));
            }
        }
        Ok((out, self.offset, !self.ids.is_empty() || !self.next.is_empty()))
    }
}

fn playlist_id(uri: &str) -> Option<String> {
    match SpotifyUri::from_uri(uri).ok()? {
        SpotifyUri::Playlist { id, .. } => id.to_base62().ok(),
        _ => None,
    }
}

pub async fn playlists(session: &Session, offset: usize) -> Result<(Vec<Item>, usize, bool), &'static str> {
    let data = session.spclient().get_rootlist(offset, Some(PAGE_SIZE)).await.map_err(|_| "catalog_failed")?;
    let mut list = SelectedListContent::parse_from_bytes(data.as_ref()).map_err(|_| "catalog_failed")?;
    let Some(contents) = list.contents.take() else { return Err("catalog_failed"); };
    let count = contents.items.len();
    let mut out = Vec::with_capacity(count.min(PAGE_SIZE));
    for (index, entry) in contents.items.into_iter().take(PAGE_SIZE).enumerate() {
        let Some(uri) = playlist_id(entry.uri()) else { continue; };
        let mut title = contents.meta_items.get(index).and_then(|m| m.attributes.as_ref()).map(|a| short(a.name())).unwrap_or_default();
        if title.is_empty() {
            if let Ok(id) = SpotifyId::from_base62(&uri) {
                if let Ok(playlist) = Playlist::get(session, &SpotifyUri::Playlist { id, user: None }).await { title = short(playlist.name()); }
            }
        }
        if title.is_empty() { title = "Spotify playlist".into(); }
        out.push(Item { kind: "playlist", id: uri, title, ..Default::default() });
    }
    Ok((out, offset + count, count >= PAGE_SIZE))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn legacy_and_current_playlist_uris_keep_the_same_identity() {
        let id = "0123456789abcdefghijkl";
        assert_eq!(playlist_id(&format!("spotify:playlist:{id}")),Some(id.into()));
        assert_eq!(playlist_id(&format!("spotify:user:listener:playlist:{id}")),Some(id.into()));
        assert_eq!(playlist_id(&format!("spotify:track:{id}")),None);
    }
    #[test] fn cursor_can_resume_after_transport_exit_without_restarting_the_catalog() {
        let text = r#"{"uri":"spotify:playlist:0123456789abcdefghijkl","offset":240,"ids":["0123456789abcdefghijkl"],"next":"hm://playlist/v1/page/next"}"#;
        let cursor: Cursor = serde_json::from_str(text).unwrap();
        assert!(cursor.valid_for("spotify:playlist:0123456789abcdefghijkl",240));
        assert!(!cursor.valid_for("spotify:playlist:0123456789abcdefghijkl",220));
        assert!(!cursor.valid_for("spotify:search:other",240));
        let restored: Cursor = serde_json::from_value(cursor.checkpoint()).unwrap();
        assert_eq!(restored.offset,240);
        assert_eq!(restored.ids,cursor.ids);
        assert_eq!(restored.next,cursor.next);
    }
    #[test] fn ids_cannot_escape_the_protocol_namespace() {
        assert!(valid_id("0123456789abcdefghijkl"));
        assert!(!valid_id("../../credentials.json"));
        assert!(!valid_id("spotify:track:abc"));
        assert_eq!(short("title\n\0next"), "titlenext");
    }
}
