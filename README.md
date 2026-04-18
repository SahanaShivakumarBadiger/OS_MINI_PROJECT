# Multi-Container Runtime 

---

## 📌 Project Summary

This project implements a lightweight Linux container runtime in C with:

* A user-space supervisor (`engine.c`) to manage multiple containers
* A kernel-space monitor (`monitor.c`) for memory tracking
* Multi-container execution, CLI control, logging, and scheduling experiments

---

## 🏗️ Architecture Overview

### User-Space Runtime (engine.c)

* Manages multiple containers
* Supports CLI commands: `start`, `run`, `ps`, `logs`, `stop`
* Maintains container metadata
* Captures logs using bounded-buffer system

### Kernel-Space Monitor (monitor.c)

* Linux Kernel Module (LKM)
* Tracks container processes
* Enforces:

  * Soft memory limit (warning)
  * Hard memory limit (kill)

---

## ⚙️ Environment Setup

```
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

* Ubuntu 22.04 / 24.04 (VM)
* Secure Boot OFF
*  WSL

---

## 📦 Root Filesystem Setup

```
mkdir rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base
```

Create container rootfs:

```
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
```

---

## 🚀 Build & Run

### Build

```
make
```

### Load Kernel Module

```
sudo insmod monitor.ko
ls -l /dev/container_monitor
```

### Start Supervisor

```
sudo ./engine supervisor ./rootfs-base
```

### Start Containers

```
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96
```

### View Containers

```
sudo ./engine ps
```

### View Logs

```
sudo ./engine logs alpha
```

### Stop Containers

```
sudo ./engine stop alpha
sudo ./engine stop beta
```

### Cleanup

```
sudo rmmod monitor
dmesg | tail
```

---

## 🖥️ CLI Commands

```
engine supervisor <base-rootfs>
engine start <id> <rootfs> <command>
engine run <id> <rootfs> <command>
engine ps
engine logs <id>
engine stop <id>
```

---

## 🧠 Key Features

* Multi-container execution
* Namespace isolation (PID, UTS, mount)
* Bounded-buffer logging
* IPC (pipes + control channel)
* Kernel-level memory monitoring
* Scheduling experiments

---

## 🔬 Scheduling Experiments

* CPU-bound vs I/O-bound workloads
* Different nice values tested
* Observed CPU sharing and response differences

---

## 🧹 Cleanup

* No zombie processes
* Threads terminate cleanly
* File descriptors closed
* Kernel memory freed on unload

---
