set -e
truncate -s 64M /artifacts/test.disk
mkfs.ext2 -q -F -d fs_root /artifacts/test.disk
debugfs -w -R "rm /config/startup.sh" /artifacts/test.disk
debugfs -w -R "write /artifacts/startup.sh /config/startup.sh" /artifacts/test.disk
for name in wide-io-valid wide-io-output wide-io-invalid wide-io-incomplete fd-io-data fd-io-zero; do
  debugfs -w -R "write /artifacts/$name /$name" /artifacts/test.disk
done
debugfs -w -f /artifacts/metadata.commands /artifacts/test.disk
