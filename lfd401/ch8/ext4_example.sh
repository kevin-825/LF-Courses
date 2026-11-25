# 1. Create a 32 MiB raw disk image and format it as ext4
dd if=/dev/zero of=/tmp/test_ext4.img bs=1M count=32 status=progress
mkfs.ext4 -L SYSWATCH_FS /tmp/test_ext4.img

# 2. Inspect the Superblock, Features (extents, has_journal, flex_bg), and Block Groups
dumpe2fs -h /tmp/test_ext4.img

# 3. Use debugfs to inspect special inode 8 (the hidden jbd2 journal!) and root inode 2
debugfs -R "stat <8>" /tmp/test_ext4.img
debugfs -R "stat <2>" /tmp/test_ext4.img
rm -f /tmp/test_ext4.img