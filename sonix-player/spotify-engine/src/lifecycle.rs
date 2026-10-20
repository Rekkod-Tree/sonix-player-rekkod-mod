use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::{sync::Notify, time::Instant};

// Transport lifetime is independent of the Sonix queue and browse state.
pub struct Activity {
    state: Mutex<(Instant, usize)>,
    changed: Notify,
}

pub struct Busy(Arc<Activity>);

impl Activity {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { state: Mutex::new((Instant::now(), 0)), changed: Notify::new() })
    }

    pub fn begin(self: &Arc<Self>) -> Busy {
        let mut state = self.state.lock().unwrap();
        state.0 = Instant::now();
        state.1 += 1;
        drop(state);
        self.changed.notify_one();
        Busy(self.clone())
    }

    // A single deadline replaces polling. Active requests cannot expire.
    pub async fn expired(&self, grace: Duration) {
        loop {
            let changed = self.changed.notified();
            tokio::pin!(changed);
            changed.as_mut().enable();
            let (last, active) = *self.state.lock().unwrap();
            if active != 0 {
                changed.await;
            } else {
                tokio::select! {
                    _ = &mut changed => {},
                    _ = tokio::time::sleep_until(last + grace) => {
                        let (last, active) = *self.state.lock().unwrap();
                        if active == 0 && last.elapsed() >= grace { return; }
                    }
                }
            }
        }
    }
}

impl Drop for Busy {
    fn drop(&mut self) {
        let mut state = self.0.state.lock().unwrap();
        state.0 = Instant::now();
        state.1 -= 1;
        drop(state);
        self.0.changed.notify_one();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test(start_paused = true)]
    async fn idle_expiry_waits_for_all_work_then_a_full_grace_period() {
        let activity = Activity::new();
        let waiting = activity.clone();
        let expiry = tokio::spawn(async move { waiting.expired(Duration::from_secs(30)).await });
        let first = activity.begin();
        let second = activity.begin();
        tokio::time::advance(Duration::from_secs(90)).await;
        tokio::task::yield_now().await;
        assert!(!expiry.is_finished());
        drop(first);
        tokio::time::advance(Duration::from_secs(90)).await;
        tokio::task::yield_now().await;
        assert!(!expiry.is_finished());
        drop(second);
        tokio::time::advance(Duration::from_secs(29)).await;
        tokio::task::yield_now().await;
        assert!(!expiry.is_finished());
        tokio::time::advance(Duration::from_secs(1)).await;
        expiry.await.unwrap();
    }

    #[tokio::test(start_paused = true)]
    async fn new_work_extends_deadline_and_cancelled_work_releases_it() {
        let activity = Activity::new();
        let waiting = activity.clone();
        let expiry = tokio::spawn(async move { waiting.expired(Duration::from_secs(30)).await });
        tokio::time::advance(Duration::from_secs(29)).await;
        let busy = activity.begin();
        let worker = tokio::spawn(async move { let _busy = busy; std::future::pending::<()>().await });
        tokio::time::advance(Duration::from_secs(60)).await;
        tokio::task::yield_now().await;
        assert!(!expiry.is_finished());
        worker.abort();
        let _ = worker.await;
        tokio::time::advance(Duration::from_secs(29)).await;
        tokio::task::yield_now().await;
        assert!(!expiry.is_finished());
        tokio::time::advance(Duration::from_secs(1)).await;
        expiry.await.unwrap();
    }
}
