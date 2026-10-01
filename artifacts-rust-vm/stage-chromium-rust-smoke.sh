set -e
debugfs -w -R "write /artifacts/chromium-rust-smoke /bin/chromium-rust-smoke" /artifacts/test.disk
debugfs -R "dump /bin/chromium-rust-smoke /artifacts/verify-chromium-rust-smoke" /artifacts/test.disk
cmp /artifacts/chromium-rust-smoke /artifacts/verify-chromium-rust-smoke
