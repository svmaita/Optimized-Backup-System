# Optimized Backup System

A performance-driven Windows backup tool that thinks before it copies. Built with C++ and Windows APIs, the Optimized Backup System performs full and incremental backups, skips unchanged files using metadata checks, and uses the NTFS USN journal to detect changes efficiently. It blends systems programming, filesystem optimization, and practical engineering into a faster, smarter backup workflow.

## Features

- Real source and destination folder selection
- Regular/full backup mode
- Optimized/incremental backup mode
- Metadata comparison using file size and last-write time
- Real file copy using Windows file APIs
- Backup statistics and performance timing
- Native Windows GUI with folder pickers, progress, and cancellation
- NTFS USN journal fast path for repeated unchanged incremental backups
- Safe handling of deleted source files without automatic deletion from backup

## Video Demonstration Link

[Project Demonstration](https://drive.google.com/file/d/1q6ctk9-PJ3kOZtmBBMfEBDTwelRtyvkr/view?usp=sharing)
