# Next Steps: Commands to Run on Camera

These commands should be run from the PMCA service shell.
**Always be careful with writable partitions. Don't write to /system or raw block devices.**

## Step 1: Dump Backup.bin for Analysis
```sh
# Copy settings file to /tmp for extraction
cp /setting/Backup.bin /tmp/Backup.bin
# You can then pull it via: pmca-console serviceshell > pull /tmp/Backup.bin Backup.bin
```

## Step 2: Check for ld.so.preload
```sh
# Does it exist?
ls -la /etc/ld.so.preload
cat /etc/ld.so.preload

# What's in /etc?
ls -la /etc/

# Is /etc writable?
touch /etc/test_write
ls -la /etc/test_write
rm /etc/test_write
```

## Step 3: Map Running Processes
```sh
ps
# If ps is limited, try:
ls /proc/ | grep '^[0-9]'
# For each PID:
cat /proc/<PID>/cmdline
cat /proc/<PID>/maps | head -20
```

## Step 4: Check What References Writable Paths
```sh
# From /usr/bin, find scripts that reference writable paths
grep -r '/tmp' /usr/bin/*.sh 2>/dev/null
grep -r '/etc' /usr/bin/*.sh 2>/dev/null
grep -r '/setting' /usr/bin/*.sh 2>/dev/null
```

## Step 5: Test /tmp Autoexec (After Reboot)
```sh
# Place a test script in /tmp
cat > /tmp/autoexec.sh << 'EOF'
#!/bin/sh
date > /tmp/autoexec_ran.txt
EOF
chmod 755 /tmp/autoexec.sh

# Reboot camera (power cycle), reconnect shell, check:
ls -la /tmp/autoexec_ran.txt
```

## Step 6: Explore Android Partitions
```sh
ls -la /android/system/ 2>/dev/null
ls -la /android/data/ 2>/dev/null
mount | grep android
```

## Step 7: Check Jiritsu Partition
```sh
# nflasha6 is the Jiritsu partition (16MB)
ls -la /tmp/jiritsu/ 2>/dev/null
# Or wherever it's mounted
mount | grep jiritsu
```
