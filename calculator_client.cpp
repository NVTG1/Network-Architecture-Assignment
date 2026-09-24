#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

using namespace std;

const char* HOST = "127.0.0.1";
const int PORT = 8080;
const int BUFFER_SIZE = 4096;


// ===================
// Sending all bytes
// ===================

bool sendAll(int socket_fd, const string& data)
{
    size_t total_sent = 0;

    while (total_sent < data.size())
    {
        ssize_t sent = send(
            socket_fd,
            data.data() + total_sent,
            data.size() - total_sent,
            0
        );

        if (sent <= 0)
        {
            return false;
        }

        total_sent += sent;
    }

    return true;
}


// ===================================
// Receive one complete HTTP response
// ===================================

bool receiveResponse(int socket_fd)
{
    string response;

    char buffer[BUFFER_SIZE];

    // Receive until HTTP headers are complete.
    while (response.find("\r\n\r\n") == string::npos)
    {
        ssize_t received = recv(
            socket_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (received == 0)
        {
            cerr << "Server closed connection.\n";
            return false;
        }

        if (received < 0)
        {
            perror("recv");
            return false;
        }

        response.append(buffer, received);
    }

    // Separate headers from body.
    size_t header_end =
        response.find("\r\n\r\n");

    string headers =
        response.substr(0, header_end);

    string body =
        response.substr(header_end + 4);

    // Print response headers.
    cout << "\n---------------- RESPONSE ----------------\n";
    cout << headers << "\n";

    // Find Content-Length.
    size_t content_length = 0;

    string key = "Content-Length:";

    size_t position =
        headers.find(key);

    if (position != string::npos)
    {
        position += key.size();

        while (
            position < headers.size() &&
            headers[position] == ' '
        )
        {
            position++;
        }

        size_t end =
            headers.find("\r\n", position);

        string length_string =
            headers.substr(
                position,
                end - position
            );

        content_length =
            stoul(length_string);
    }

    // Receive the rest of the body.
    while (body.size() < content_length)
    {
        ssize_t received = recv(
            socket_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (received <= 0)
        {
            cerr << "Connection closed before "
                 << "complete response.\n";

            return false;
        }

        body.append(buffer, received);
    }

    // Print only the expected body.
    body.resize(content_length);

    cout << "BODY: ";

    if (body.empty())
    {
        cout << "(empty)";
    }
    else
    {
        cout << body;
    }

    cout << "\n";
    cout << "-------------------------------------------\n";

    return true;
}


// =========================
// Sending one HTTP request
// =========================

bool sendRequest(
    int socket_fd,
    const string& request
)
{
    cout << "\n===========================================\n";
    cout << "REQUEST\n";
    cout << "===========================================\n";
    cout << request;

    if (!sendAll(socket_fd, request))
    {
        cerr << "Failed to send request.\n";
        return false;
    }

    return receiveResponse(socket_fd);
}


// ======
// MAIN
// ======

int main()
{
    // Create TCP socket
    int client_socket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (client_socket < 0)
    {
        perror("socket");
        return 1;
    }

    // Server address
    sockaddr_in server_address{};

    server_address.sin_family =
        AF_INET;

    server_address.sin_port =
        htons(PORT);

    if (inet_pton(
            AF_INET,
            HOST,
            &server_address.sin_addr
        ) <= 0)
    {
        cerr << "Invalid server address.\n";

        close(client_socket);

        return 1;
    }

    // Connect to server
    cout << "Connecting to "
         << HOST
         << ":"
         << PORT
         << "...\n";

    if (connect(
            client_socket,
            reinterpret_cast<sockaddr*>(
                &server_address
            ),
            sizeof(server_address)
        ) < 0)
    {
        perror("connect");

        close(client_socket);

        return 1;
    }

    cout << "Connected!\n";

    // ==========
    // REQUEST 1
    // ==========

    if (!sendRequest(
            client_socket,

            "GET /add?a=2&b=3 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }


    // ==========
    // REQUEST 2
    // ==========

    if (!sendRequest(
            client_socket,

            "GET /sub?a=10&b=4 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }


    // ==========
    // REQUEST 3
    // ==========

    if (!sendRequest(
            client_socket,

            "GET /mul?a=6&b=7 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }


    // ==================
    // REQUEST 4
    // Division by zero
    // Expected: 400
    // ==================

    if (!sendRequest(
            client_socket,

            "GET /div?a=1&b=0 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }


    // =======================
    // REQUEST 5
    // Unsupported operation
    // Expected: 404
    // =======================

    if (!sendRequest(
            client_socket,

            "GET /pow?a=2&b=8 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }


    // ===================
    // REQUEST 6
    // Unsupported method
    // Expected: 405
    // ====================

    if (!sendRequest(
            client_socket,

            "POST /add HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ))
    {
        close(client_socket);
        return 1;
    }
    close(client_socket);
    return 0;
}