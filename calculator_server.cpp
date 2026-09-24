#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cctype>

using namespace std;

const int DEFAULT_PORT = 8080;
const int BUFFER_SIZE = 4096;

// ============================================================
// Send all bytes
// ============================================================

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


// ============================================================
// Create an HTTP response
// ============================================================

string makeResponse(
    int status_code,
    const string& reason,
    const string& body
)
{
    stringstream response;

    response << "HTTP/1.1 "
             << status_code
             << " "
             << reason
             << "\r\n";

    response << "Content-Length: "
             << body.size()
             << "\r\n";

    response << "Content-Type: text/plain\r\n";

    response << "Connection: keep-alive\r\n";

    response << "\r\n";

    response << body;

    return response.str();
}


// ============================================================
// Trim whitespace
// ============================================================

string trim(const string& str)
{
    size_t start = str.find_first_not_of(" \t\r\n");

    if (start == string::npos)
    {
        return "";
    }

    size_t end = str.find_last_not_of(" \t\r\n");

    return str.substr(start, end - start + 1);
}


// ============================================================
// Parsing URL query parameters
// ============================================================

unordered_map<string, string> parseQuery(const string& query)
{
    unordered_map<string, string> params;

    size_t start = 0;

    while (start < query.size())
    {
        size_t ampersand = query.find('&', start);

        string pair;

        if (ampersand == string::npos)
        {
            pair = query.substr(start);
        }
        else
        {
            pair = query.substr(start, ampersand - start);
        }

        size_t equals = pair.find('=');

        if (equals != string::npos)
        {
            string key = pair.substr(0, equals);
            string value = pair.substr(equals + 1);

            params[key] = value;
        }

        if (ampersand == string::npos)
        {
            break;
        }

        start = ampersand + 1;
    }

    return params;
}


// ============================================================
// Reading ONE complete HTTP request
//
// IMPORTANT:
// TCP is a byte stream.
//
// One recv() can contain:
//   - half a request
//   - exactly one request
//   - multiple requests
//
// receive_buffer stores bytes that have arrived but have not
// yet been consumed.
// ============================================================

bool readRequest(
    int client_socket,
    string& request,
    string& receive_buffer
)
{
    request.clear();

    while (true)
    {
        // Check whether we already have a complete HTTP header.
        size_t header_end =
            receive_buffer.find("\r\n\r\n");

        if (header_end != string::npos)
        {
            // Include the four bytes:
            // \r\n\r\n
            size_t request_end = header_end + 4;

            // Take ONLY the first request.
            request =
                receive_buffer.substr(0, request_end);

            // Remove the request we just consumed.
            //
            // If another request was already received,
            // it remains inside receive_buffer.
            receive_buffer.erase(0, request_end);

            return true;
        }

        // We don't have a complete request yet.
        char buffer[BUFFER_SIZE];

        ssize_t bytes_read = recv(
            client_socket,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytes_read < 0)
        {
            perror("recv");
            return false;
        }

        if (bytes_read == 0)
        {
            // Client closed the connection.
            return false;
        }

        // Add newly received bytes to our persistent buffer.
        receive_buffer.append(
            buffer,
            bytes_read
        );

        // Prevent an endlessly large request.
        if (receive_buffer.size() > 64 * 1024)
        {
            cerr << "Request too large\n";
            return false;
        }
    }
}


// ============================================================
// Process one HTTP request
// ============================================================

string handleRequest(const string& request)
{
    // Find end of HTTP headers.
    size_t header_end =
        request.find("\r\n\r\n");

    if (header_end == string::npos)
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    string headers =
        request.substr(0, header_end);

    // Split headers into lines.
    istringstream stream(headers);

    string request_line;

    if (!getline(stream, request_line))
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // getline removes '\n' but leaves '\r'.
    if (!request_line.empty() &&
        request_line.back() == '\r')
    {
        request_line.pop_back();
    }

    // Parse request line.
    istringstream request_stream(request_line);

    string method;
    string target;
    string version;

    request_stream
        >> method
        >> target
        >> version;

    if (method.empty() ||
        target.empty() ||
        version.empty())
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Only HTTP/1.1 is supported.
    if (version != "HTTP/1.1")
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Parse headers.
    bool has_host = false;

    string line;

    while (getline(stream, line))
    {
        if (!line.empty() &&
            line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

        size_t colon = line.find(':');

        if (colon == string::npos)
        {
            return makeResponse(
                400,
                "Bad Request",
                ""
            );
        }

        string header_name =
            trim(line.substr(0, colon));

        string header_value =
            trim(line.substr(colon + 1));

        // Convert header name to lowercase.
        for (char& c : header_name)
        {
            c = static_cast<char>(
                tolower(static_cast<unsigned char>(c))
            );
        }

        if (header_name == "host")
        {
            has_host = true;
        }
    }

    // HTTP/1.1 requires Host.
    if (!has_host)
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Only GET is supported.
    if (method != "GET")
    {
        return makeResponse(
            405,
            "Method Not Allowed",
            ""
        );
    }

    // Separate path and query string.
    size_t question_mark =
        target.find('?');

    string path;
    string query;

    if (question_mark == string::npos)
    {
        path = target;
        query = "";
    }
    else
    {
        path =
            target.substr(
                0,
                question_mark
            );

        query =
            target.substr(
                question_mark + 1
            );
    }

    // Check supported operations.
    if (path != "/add" &&
        path != "/sub" &&
        path != "/mul" &&
        path != "/div")
    {
        return makeResponse(
            404,
            "Not Found",
            ""
        );
    }

    // Parse a and b.
    auto params = parseQuery(query);

    if (params.find("a") == params.end() ||
        params.find("b") == params.end())
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    int a;
    int b;

    try
    {
        size_t pos1;
        size_t pos2;

        a = stoi(
            params["a"],
            &pos1
        );

        b = stoi(
            params["b"],
            &pos2
        );

        // Make sure the ENTIRE value is a number.
        if (pos1 != params["a"].size() ||
            pos2 != params["b"].size())
        {
            return makeResponse(
                400,
                "Bad Request",
                ""
            );
        }
    }
    catch (...)
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Perform calculation.
    long long result;

    if (path == "/add")
    {
        result =
            static_cast<long long>(a) + b;
    }
    else if (path == "/sub")
    {
        result =
            static_cast<long long>(a) - b;
    }
    else if (path == "/mul")
    {
        result =
            static_cast<long long>(a) * b;
    }
    else
    {
        // /div
        if (b == 0)
        {
            return makeResponse(
                400,
                "Bad Request",
                ""
            );
        }

        result = a / b;
    }

    return makeResponse(
        200,
        "OK",
        to_string(result)
    );
}


// ============================================================
// Handling ONE TCP connection
// ============================================================

void handleConnection(
    int client_socket,
    const sockaddr_in& client_address
)
{
    char client_ip[INET_ADDRSTRLEN];

    inet_ntop(
        AF_INET,
        &client_address.sin_addr,
        client_ip,
        sizeof(client_ip)
    );

    cout << "\n[+] New connection from "
         << client_ip
         << ":"
         << ntohs(client_address.sin_port)
         << endl;

    // IMPORTANT:
    // This buffer belongs to THIS TCP connection.
    //
    // It must stay alive while we process multiple
    // HTTP requests on the same socket.
    string receive_buffer;

    while (true)
    {
        string request;

        // Read exactly ONE request.
        bool success =
            readRequest(
                client_socket,
                request,
                receive_buffer
            );

        if (!success)
        {
            break;
        }

        cout << "\n========== REQUEST ==========\n";
        cout << request;
        cout << "=============================\n";

        // Generate response.
        string response =
            handleRequest(request);

        // Send response.
        if (!sendAll(
                client_socket,
                response
            ))
        {
            break;
        }

        cout << "Response sent.\n";
    }

    close(client_socket);

    cout << "[-] Connection closed.\n";
}


// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[])
{
    int port = DEFAULT_PORT;

    if (argc >= 2)
    {
        port = stoi(argv[1]);
    }

    // Create TCP socket.
    int server_socket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_socket < 0)
    {
        perror("socket");
        return 1;
    }

    // Allow quick restart.
    int opt = 1;

    setsockopt(
        server_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    // Server address.
    sockaddr_in server_address{};

    server_address.sin_family =
        AF_INET;

    server_address.sin_addr.s_addr =
        INADDR_ANY;

    server_address.sin_port =
        htons(port);

    // Bind.
    if (bind(
            server_socket,
            reinterpret_cast<sockaddr*>(
                &server_address
            ),
            sizeof(server_address)
        ) < 0)
    {
        perror("bind");

        close(server_socket);

        return 1;
    }

    // Listen.
    if (listen(
            server_socket,
            10
        ) < 0)
    {
        perror("listen");

        close(server_socket);

        return 1;
    }

    cout << "=====================================\n";
    cout << " Calculator HTTP Server\n";
    cout << "=====================================\n";
    cout << "Listening on port "
         << port
         << "\n";
    cout << "Waiting for connections...\n";

    // Accept connections forever.
    while (true)
    {
        sockaddr_in client_address{};

        socklen_t client_length =
            sizeof(client_address);

        int client_socket =
            accept(
                server_socket,
                reinterpret_cast<sockaddr*>(
                    &client_address
                ),
                &client_length
            );

        if (client_socket < 0)
        {
            perror("accept");
            continue;
        }

        handleConnection(
            client_socket,
            client_address
        );
    }

    close(server_socket);

    return 0;
}