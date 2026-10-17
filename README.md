# Hyperloop Main

Control and monitoring software for the hyperloop pod. It has two parts:

- **Web server** (`index.js`): a Node.js/Express app that serves the operator dashboard and runs a WebSocket server that relays messages between connected clients.
- **CAN bridge** (`updated_main_can.c`): a C program that connects to the WebSocket server as a client, receives pod state commands from the dashboard, and talks to the pod over CAN FD. Every 10 seconds it sends sensor data (temperature and pressure) to the dashboard as JSON.

## Project structure

```
index.js              Express + WebSocket server
public/               Dashboard pages (home, pod interface, error log, settings), CSS and images
updated_main_can.c    Current CAN <-> WebSocket bridge
old_main_can.c        Previous version of the bridge (kept for reference)
notes.txt             Environment setup notes
```

## Requirements

- Node.js
- For the CAN bridge (Linux, with SocketCAN):
  - libwebsockets
  - json-c
  - OpenSSL (may only be needed on Windows)

## Running the server

```bash
npm install
node index.js
```

The server listens on `http://localhost:3000` (set the `PORT` environment variable to change it).

Pages:

| Route                 | Page                     |
|-----------------------|--------------------------|
| `/`                   | Home screen              |
| `/pod-interface`      | Pod interface            |
| `/error-log-warnings` | Error log and warnings   |
| `/settings`           | Settings                 |

## Building the CAN bridge

On Linux with the libraries above installed:

```bash
gcc updated_main_can.c -o main_can -lwebsockets -ljson-c
./main_can
```

The bridge connects to the WebSocket server at `localhost:3000` and opens three CAN interfaces:

- `can0`: pod states
- `can1`: sensor data
- `can2`: internal

### Pod states

| State              | CAN extended ID |
|--------------------|-----------------|
| `INIT`             | `0x00001000`    |
| `FAULT`            | `0x00001100`    |
| `SAFE_TO_APPROACH` | `0x00001200`    |
| `COAST`            | `0x00001300`    |
| `BRAKE`            | `0x00001400`    |
| `CRAWL`            | `0x00001500`    |
| `TRACK`            | `0x00001600`    |
| `READY`            | `0x00001700`    |
| `LAUNCH`           | `0x0000FF00`    |
