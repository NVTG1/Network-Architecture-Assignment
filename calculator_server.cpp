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

using namespace std;

const int DEFAULT_PORT = 8080;
const int BUFFER_SIZE = 4096;

// Sending all bytes
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


// ========================
// Create an HTTP response
// ========================

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


// ================
// Trim whitespace
// ================

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


// =============================
// Parsing URL query parameters
// =============================

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


// ===================================
// Reading one complete HTTP request
// ===================================

bool readRequest(int client_socket, std::string& request) {
    request.clear();

    char buffer[4096];

    while (true) {
        ssize_t bytes_read = recv(
            client_socket,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytes_read < 0) {
            perror("recv");
            return false;
        }

        if (bytes_read == 0) {
            // Client closed the connection
            return false;
        }

        request.append(buffer, bytes_read);

        // We have received the end of the HTTP headers.
        if (request.find("\r\n\r\n") != std::string::npos) {
            return true;
        }

        // Prevent an endlessly large request.
        if (request.size() > 64 * 1024) {
            std::cerr << "Request too large\n";
            return false;
        }
    }
}


// =========================
// Process one HTTP request
// =========================

string handleRequest(const string& request)
{
    // Finding end of headers

    size_t header_end = request.find("\r\n\r\n");

    if (header_end == string::npos)
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    string headers = request.substr(
        0,
        header_end
    );

    // Splitting headers into lines

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

    // getline removes '\n' but leaves '\r'
    if (!request_line.empty() &&
        request_line.back() == '\r')
    {
        request_line.pop_back();
    }

    // Parsing request line
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

    // Only HTTP/1.1
    if (version != "HTTP/1.1")
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Parse headers
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
            c = tolower(c);
        }

        if (header_name == "host")
        {
            has_host = true;
        }
    }

    // HTTP/1.1 requires Host
    if (!has_host)
    {
        return makeResponse(
            400,
            "Bad Request",
            ""
        );
    }

    // Only GET is supported
    if (method != "GET")
    {
        return makeResponse(
            405,
            "Method Not Allowed",
            ""
        );
    }

    // Separate path and query string
    size_t question_mark = target.find('?');

    string path;
    string query;

    if (question_mark == string::npos)
    {
        path = target;
        query = "";
    }
    else
    {
        path = target.substr(
            0,
            question_mark
        );

        query = target.substr(
            question_mark + 1
        );
    }

    // Check supported operations
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

    // Parse a and b
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

        a = stoi(params["a"], &pos1);
        b = stoi(params["b"], &pos2);

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

    // Perform calculation
    long long result;

    if (path == "/add")
    {
        result = static_cast<long long>(a) + b;
    }
    else if (path == "/sub")
    {
        result = static_cast<long long>(a) - b;
    }
    else if (path == "/mul")
    {
        result = static_cast<long long>(a) * b;
    }
    else
    {
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


// ============================
// Handling one TCP connection
// ============================

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

    while (true)
    {
        string request;

        // Read one request
        bool success =
            readRequest(
                client_socket,
                request
            );

        if (!success)
        {
            break;
        }

        cout << "\n========== REQUEST ==========\n";
        cout << request;
        cout << "=============================\n";

        // Generate response
        string response =
            handleRequest(request);

        // Send response
        if (!sendAll(
                client_socket,
                response))
        {
            break;
        }

        cout << "Response sent.\n";
    }

    close(client_socket);

    cout << "[-] Connection closed.\n";
}


// =====
// MAIN
// =====

int main(int argc, char* argv[])
{
    int port = DEFAULT_PORT;

    if (argc >= 2)
    {
        port = stoi(argv[1]);
    }

    // Create TCP socket
    int server_socket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_socket < 0)
    {
        perror("socket");
        return 1;
    }

    // Allow quick restart
    int opt = 1;

    setsockopt(
        server_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    // Server address
    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;

    server_address.sin_addr.s_addr =
        INADDR_ANY;

    server_address.sin_port =
        htons(port);

    // Bind
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

    // Listen
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

    // Accept connections forever
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