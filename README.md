# Calculator HTTP Server

## Overview

A simple HTTP calculator server built on a raw TCP socket.

The server accepts HTTP/1.1 requests and performs basic arithmetic:

- Addition
- Subtraction
- Multiplication
- Division

It uses a **persistent TCP connection**, so multiple HTTP requests can be sent over the same connection without opening a new one each time.

No external framework is used. Everything is plain C++ socket programming.

---

## Requirements

- Linux / Unix-like operating system
- C++ compiler with C++17 support
- `nc` (Netcat) for manual testing

Check that `g++` is installed:

```bash
g++ --version
```

---

## Files

```
.
├── calculator_server.cpp
├── calculator_client.cpp
├── .gitignore
└── README.md
```

### `calculator_server.cpp`

The HTTP calculator server. It:

1. Creates a TCP socket.
2. Binds it to a port (default `8080`).
3. Listens for incoming connections.
4. Accepts client connections.
5. Reads HTTP requests.
6. Processes calculator operations.
7. Sends HTTP responses.
8. Keeps the TCP connection open so more requests can be processed.

### `calculator_client.cpp`

A simple C++ client for testing the server. It opens one TCP connection and sends multiple HTTP requests over it.

---

## Supported Operations

| Method | Endpoint | Example         | Result |
|--------|----------|-----------------|--------|
| GET    | `/add`   | `/add?a=2&b=3`  | `5`    |
| GET    | `/sub`   | `/sub?a=10&b=4` | `6`    |
| GET    | `/mul`   | `/mul?a=6&b=7`  | `42`   |
| GET    | `/div`   | `/div?a=9&b=3`  | `3`    |

The parameters `a` and `b` must be integers.

---

## HTTP Response Codes

### 200 OK

Valid request, calculation succeeded.

```http
GET /add?a=2&b=3 HTTP/1.1
Host: localhost
```

Response:

```
HTTP/1.1 200 OK

5
```

### 400 Bad Request

The request is invalid. For example:

- Missing `a`
- Missing `b`
- Non-numeric values
- Division by zero
- Missing `Host` header

```http
GET /div?a=10&b=0 HTTP/1.1
Host: localhost
```

Response:

```
HTTP/1.1 400 Bad Request
```

### 404 Not Found

Unsupported endpoint.

```http
GET /pow?a=2&b=8 HTTP/1.1
Host: localhost
```

Response:

```
HTTP/1.1 404 Not Found
```

### 405 Method Not Allowed

Unsupported HTTP method.

```http
POST /add HTTP/1.1
Host: localhost
```

Response:

```
HTTP/1.1 405 Method Not Allowed
```

---

## Compilation

Server:

```bash
g++ -std=c++17 -Wall -Wextra calculator_server.cpp -o server
```

Client:

```bash
g++ -std=c++17 -Wall -Wextra calculator_client.cpp -o client
```

---

## Running the Server

Start the server on port 8080:

```bash
./server 8080
```

Expected output:

```
=====================================
 Calculator HTTP Server
=====================================
Listening on port 8080
Waiting for connections...
```

The server is now ready to accept connections.

---

## Testing with Netcat

The server can be tested manually with `nc`. Since it runs on the same machine, use `localhost`.

### Addition

```bash
printf 'GET /add?a=10&b=20 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `200 OK`, body `30`

### Subtraction

```bash
printf 'GET /sub?a=20&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `13`

### Multiplication

```bash
printf 'GET /mul?a=6&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `42`

### Division

```bash
printf 'GET /div?a=20&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `5`

---

## Testing Error Conditions

### Division by zero

```bash
printf 'GET /div?a=10&b=0 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `400 Bad Request`

### Non-numeric input

```bash
printf 'GET /add?a=hello&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `400 Bad Request`

### Missing parameter

```bash
printf 'GET /add?b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `400 Bad Request`

### Unsupported operation

```bash
printf 'GET /pow?a=2&b=8 HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `404 Not Found`

### Unsupported HTTP method

```bash
printf 'POST /add HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Expected: `405 Method Not Allowed`

### Missing Host header

```bash
printf 'GET /add?a=2&b=3 HTTP/1.1\r\n\r\n' | nc localhost 8080
```

Expected: `400 Bad Request`

---

## Persistent Connection

The server keeps the TCP connection open and handles multiple HTTP requests over it.

Start Netcat in CRLF mode (HTTP needs `\r\n` line endings, and plain `nc` sends `\n`):

```bash
nc -C localhost 8080
```

Then type:

```
GET /add?a=10&b=20 HTTP/1.1
Host: localhost

```

(press Enter on the empty line at the end). The server returns `30`.

Without closing Netcat, send another:

```
GET /sub?a=20&b=5 HTTP/1.1
Host: localhost

```

Returns `15`.

And another:

```
GET /mul?a=6&b=7 HTTP/1.1
Host: localhost

```

Returns `42`.

Three requests, one TCP connection.

---

## Testing with the C++ Client

Start the server:

```bash
./server 8080
```

In another terminal:

```bash
./client
```

The client opens one TCP connection and sends several requests over it. Expected responses:

```
200 OK   -> 5
200 OK   -> 6
200 OK   -> 42
400 Bad Request
404 Not Found
405 Method Not Allowed
```

---

## Connection Flow

```
Client
   |
   | TCP connection
   v
Server
   |
   | HTTP request
   v
Request parsing
   |
   v
Validate method, path and parameters
   |
   v
Perform calculation
   |
   v
HTTP response
   |
   v
Client
```

With a persistent connection, the flow repeats on the same socket:

```
TCP Connection
      |
      +--> HTTP Request 1 --> HTTP Response 1
      |
      +--> HTTP Request 2 --> HTTP Response 2
      |
      +--> HTTP Request 3 --> HTTP Response 3
      |
      +--> ...
      |
      +--> Connection closed
```

---

## Technologies Used

- C++17
- TCP sockets
- HTTP/1.1
- POSIX socket APIs
- Netcat (`nc`)

---

## Key Concepts Demonstrated

- TCP socket creation
- `bind()`, `listen()`, `accept()`
- `recv()` and `send()`
- HTTP request parsing
- HTTP response construction
- Query parameter parsing
- HTTP status codes
- Persistent TCP connections
- Client-server communication

---

## How to Stop the Server

Press `Ctrl+C` in the terminal where the server is running.