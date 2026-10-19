"""Privacy and request-boundary checks. No real account or network access."""

import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import probe_lossless as probe
import oauth_control


class ProbeTests(unittest.TestCase):
    def args(self):
        return SimpleNamespace(adb="adb", serial="test-device", track="123")

    def protocol(self):
        return patch.object(probe, "public_protocol", return_value=(
            "TESTKEY0000000000", "YandexMusicDesktopAppWindows/1.0", "source-hash"
        ))

    def test_summary_excludes_account_secrets_and_signed_audio_urls(self):
        account = {"result": {"plus": {"hasPlus": True},
                              "account": {"login": "PRIVATE_LOGIN"}}}
        audio = {"result": {"downloadInfo": {
            "codec": "aac-mp4", "quality": "nq", "transport": "encraw",
            "bitrate": 192, "url": "https://media.example/PRIVATE_SIGNED_URL",
            "key": "PRIVATE_FILE_KEY",
        }}}
        with self.protocol(), patch.object(probe, "device_token", return_value="PRIVATE_TOKEN"), \
                patch.object(probe.time, "sleep"), \
                patch.object(probe, "request", side_effect=[(200, account)] + [(200, audio)] * 3) as request:
            summary = probe.probe(self.args())
        text = json.dumps(summary)
        self.assertNotIn("PRIVATE_", text)
        self.assertEqual(request.call_count, 4)
        self.assertEqual(len(summary["requests"]), 3)
        self.assertTrue(summary["has_plus"])
        self.assertTrue(summary["requests"][0]["file_key_present"])

    def test_rate_limit_or_auth_failure_stops_remaining_variants(self):
        for status in (0, 401, 403, 429, 302):
            with self.subTest(status=status), self.protocol(), \
                    patch.object(probe, "device_token", return_value="PRIVATE_TOKEN"), \
                    patch.object(probe.time, "sleep"), \
                    patch.object(probe, "request", side_effect=[(200, {}), (status, {})]) as request:
                summary = probe.probe(self.args())
            self.assertEqual(request.call_count, 2)
            self.assertEqual(len(summary["requests"]), 1)

    def test_unknown_endpoint_is_rejected_before_credentials_are_sent(self):
        for endpoint in ("/users/private-user/playlists/1", "https://other.example", "/get-file-info/../account"):
            with self.subTest(endpoint=endpoint), self.assertRaises(ValueError):
                probe.request(None, "PRIVATE_TOKEN", "test-client", endpoint)

    def test_foreign_api_origin_is_rejected(self):
        with self.assertRaises(ValueError):
            probe.request(None, "PRIVATE_TOKEN", "test-client", "/account/status",
                          api_base="https://other.example")

    def test_oauth_form_accepts_one_code_and_rejects_invalid_forms(self):
        self.assertEqual(oauth_control.parse_code(b"code=LOCAL_TEST_CODE"), "LOCAL_TEST_CODE")
        for body in (b"", b"other=x", b"code=", b"code=a&code=b", b"code=\xff"):
            with self.subTest(body=body), self.assertRaises((ValueError, UnicodeError)):
                oauth_control.parse_code(body)

    def test_redirects_are_not_followed(self):
        self.assertIsNone(probe.NoRedirect().redirect_request(
            None, None, 302, "", {}, "https://other.example"
        ))

    def test_legacy_adb_shell_reads_token_without_putting_it_in_command(self):
        result = SimpleNamespace(stdout=b"access_token=PRIVATE_TOKEN\r\nuid=123\r\n")
        with patch.object(probe.subprocess, "run", return_value=result) as run:
            self.assertEqual(probe.device_token("adb", "test"), "PRIVATE_TOKEN")
        command = run.call_args.args[0]
        self.assertIn("shell", command)
        self.assertNotIn("exec-out", command)
        self.assertNotIn("PRIVATE_TOKEN", " ".join(command))


if __name__ == "__main__":
    unittest.main()
