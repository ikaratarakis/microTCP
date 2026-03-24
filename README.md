# microTCP — Reliable Transport Protocol over UDP

A custom reliable transport protocol (microTCP) built on top of UDP sockets in C, featuring TCP-like connection management and an iperf-inspired network performance measurement tool.

Developed as part of the **CS-435 Computer Networks** course at the University of Crete.

## Features

- **3-Way Handshake**: Full SYN / SYN-ACK / ACK connection establishment
- **Reliable Data Transfer**: Sequence numbers, acknowledgments, and retransmissions
- **Congestion Control**: Slow start and congestion avoidance (AIMD) with configurable `cwnd` and `ssthresh`
- **Flow Control**: Sliding window with receiver-advertised window size
- **CRC-32 Integrity Checks**: Per-packet checksum validation with duplicate ACK handling
- **Graceful Shutdown**: FIN-ACK based 4-step connection teardown
- **iperf-like Benchmarking Tool**: Client/server modes with throughput, goodput, packet loss, and one-way delay measurements

## Architecture

| File | Description |
|---|---|
| `udp.h` | microTCP API — socket structures, header format, state machine |
| `udp.c` | Protocol implementation — handshake, send/recv, congestion & flow control, shutdown |
| `crc32.h` | CRC-32 checksum utility using lookup tables |
| `iperf.c` | Client/server benchmarking tool with multi-threaded stream support |

## Build

```bash
make
```

## Usage

**Server:**
```bash
./iperf -s -p <port>
```

**Client:**
```bash
./iperf -c -a <server_ip> -p <port> [options]
```

### Options

| Flag | Description |
|---|---|
| `-s` | Run in server mode |
| `-c` | Run in client mode |
| `-a <ip>` | IP address to bind/connect to |
| `-p <port>` | Port number |
| `-t <sec>` | Experiment duration in seconds |
| `-l <bytes>` | UDP packet size (client only) |
| `-b <bps>` | Bandwidth limit in bits/sec (client only) |
| `-n <num>` | Number of parallel streams (client only, max 10) |
| `-d` | Measure one-way delay instead of throughput |
| `-w <sec>` | Wait before starting transmission |

## Protocol Details

### Header Format (52 bytes)

```
┌──────────────────┬──────────────────┐
│  Sequence Number │    ACK Number    │
├──────────────────┼──────────────────┤
│   Control Bits   │   Window Size    │
├──────────────────┴──────────────────┤
│            Data Length              │
├─────────────────────────────────────┤
│            Timestamp                │
├──────────────────┬──────────────────┤
│   Measure Delay  │    Reserved      │
├──────────────────┴──────────────────┤
│           CRC-32 Checksum           │
└─────────────────────────────────────┘
```

### State Machine

```
UNKNOWN → LISTEN → ESTABLISHED → CLOSING_BY_HOST/CLOSING_BY_PEER → CLOSED
```

### Congestion Control

- **Slow Start**: `cwnd` increases by MSS per ACK when `cwnd < ssthresh`
- **Congestion Avoidance**: `cwnd` increases by 1 per ACK when `cwnd >= ssthresh`
- **Triple Duplicate ACK**: `ssthresh = cwnd/2`, `cwnd = ssthresh + 3*MSS` (fast recovery)
- **Timeout**: `ssthresh = cwnd/2`, `cwnd = MSS` (reset to slow start)
