# bounce-notify (Bouncer C Client)

A lightweight, zero-dependency C implementation of the `bouncer-client`. This tool is designed to be used as a Postfix
pipe transport for efficient mail ingestion into the Bouncer system.

## Overview

The `bounce-notify` is a port of the original Rust `bouncer-client`. It reads an email from `stdin`, wraps it in a
binary frame with a JSON header, and sends it to a `bouncer-server` over TCP.

Key features:

- **Zero dependencies**: Uses only standard C libraries (POSIX sockets).
- **Extremely lightweight**: Ideal for high-throughput Postfix pipe environments.
- **Postfix compatible**: Returns standard exit codes (`EX_USAGE`, `EX_TEMPFAIL`).
- **Timeouts**: Configurable connection and I/O timeouts.

## Build

The project uses CMake for building.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j

cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
```

After initial configure, rebuild with:

```bash
cmake --build build-release -j
cmake --build build-debug -j
```

This repo uses separate build directories per profile (`build-release`, `build-debug`) to avoid mode switching confusion.
Project version is defined in `CMakeLists.txt` via `project(... VERSION x.y.z)`.

## Deploy

`scripts/deploy.sh` installs the built binary to `/usr/local/bin` using `sudo`.
By default it builds `Release` first, then deploys.

```bash
chmod +x scripts/deploy.sh

# Build (Release) + local install
./scripts/deploy.sh

# Build (Release) + remote install
./scripts/deploy.sh --host user@server

# Remote install with short host (uses your current SSH user)
./scripts/deploy.sh --host server

# Remote install (default: <local-hostname>, current SSH user)
./scripts/deploy.sh --host

# Use an already built binary (skip build)
./scripts/deploy.sh --no-build --binary ./build-release/bounce-notify --host server
```

Binary install name is fixed as `bounce-notify`.

## Test With Mock Server

`tests/client-test.sh` runs an end-to-end check:
- builds `bounce-notify` and `mock-bouncer-server`
- feeds `tests/bounce_notify.eml` to the client via stdin
- verifies server receives a valid frame and returns `OK\n`

```bash
bash tests/client-test.sh
```

## Usage

```bash
./bounce-notify --server host:port --from sender@example.com --to recipient@example.com [options]
```

### Arguments

- `--server`: The `host:port` of the `bouncer-server` (e.g., `127.0.0.1:2147`).
- `--from`: The envelope sender address.
- `--to`: The envelope recipient address.

### Options

- `--timeout-secs`: Network timeout for connect, read, and write operations (default: `10`).
- `-h`, `--help`: Show usage information.
- `-V`, `--version`: Show binary version from CMake project version.


### Postfix Bounce Integration

To use this client for handling Postfix bounce notifications, configure your Postfix as follows:

1. Add the transport to `master.cf`:

```conf
bounce-notify unix  -       n       n       -       -       pipe
  flags=RQ user=bouncer argv=/path/to/bounce-notify 
  --server 127.0.0.1:2147 
  --from ${sender} 
  --to ${recipient}
```

2. Configure bounce notices in `main.cf`:

```conf
notify_classes = bounce, ...
bounce_notice_recipient = bounces@domain.com
bounce_size_limit = 51200
```

3. Map the recipient to the transport in your `transport` file:

```text
bounces@domain.com    bounce-notify:
```

## Protocol Details

The client implements the Bouncer binary protocol:

1. **Magic**: `BNCE` (4 bytes).
2. **Header Length**: 32-bit big-endian integer.
3. **Body Length**: 64-bit big-endian integer.
4. **Header**: JSON-encoded metadata (`{"from":"...","to":"...","kind":null,"source":null}`).
5. **Body**: The raw RFC822 mail content from `stdin` (max 50 KiB = 51200 bytes).
6. **ACK**: The client waits for `OK\n` from the server before exiting successfully.

## Architecture

```text
[Postfix] -> [bounce-notify] -> (TCP 2147) -> [bouncer-server]
```

This client acts as the entry point for raw mail ingest in the Bouncer ecosystem, as described in the
main [Bouncer README](https://github.com/estagent/bouncer/blob/master/README.md).
