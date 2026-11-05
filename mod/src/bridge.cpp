#include "bridge.h"

#include "log.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <vector>

namespace bsap::bridge {

namespace {

constexpr int kMaxClients = 8;

struct Client {
    SOCKET socket = INVALID_SOCKET;
    std::string pending;
    std::deque<std::string> outbox;
};

std::mutex g_mutex;
std::map<int, Client> g_clients;                 // guarded by g_mutex
std::deque<Command> g_inbox;                     // guarded by g_mutex
int g_nextId = 1;                                // 0 = internal client
int g_current = -1;                              // game thread only
std::function<void(const std::string&)> g_sink;  // game thread only

void Disconnect(int id) {
    closesocket(g_clients[id].socket);
    g_clients.erase(id);
    Log("[bridge] client %d disconnected", id);
}

DWORD WINAPI NetworkThread(LPVOID) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("[bridge] WSAStartup failed");
        return 0;
    }
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (server == INVALID_SOCKET || bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(server, 4) == SOCKET_ERROR) {
        Log("[bridge] cannot listen on 127.0.0.1:%u (error %d)", kPort, WSAGetLastError());
        return 0;
    }
    Log("[bridge] debug bridge listening on 127.0.0.1:%u", kPort);

    char buf[4096];
    for (;;) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server, &readable);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& [id, c] : g_clients) FD_SET(c.socket, &readable);
        }
        timeval timeout{0, 15 * 1000};
        select(0, &readable, nullptr, nullptr, &timeout);

        std::lock_guard<std::mutex> lock(g_mutex);
        if (FD_ISSET(server, &readable)) {
            SOCKET s = accept(server, nullptr, nullptr);
            if (s != INVALID_SOCKET && static_cast<int>(g_clients.size()) < kMaxClients) {
                const int id = g_nextId++;
                g_clients[id].socket = s;
                g_inbox.push_back({id, "hello"});  // the game introduces itself on the next frame
                Log("[bridge] client %d connected", id);
            } else if (s != INVALID_SOCKET) {
                closesocket(s);
            }
        }
        std::vector<int> dead;
        for (auto& [id, c] : g_clients) {
            if (FD_ISSET(c.socket, &readable)) {
                const int n = recv(c.socket, buf, sizeof(buf), 0);
                if (n <= 0) {
                    dead.push_back(id);
                    continue;
                }
                c.pending.append(buf, n);
                std::size_t eol;
                while ((eol = c.pending.find('\n')) != std::string::npos) {
                    std::string line = c.pending.substr(0, eol);
                    c.pending.erase(0, eol + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) g_inbox.push_back({id, line});
                }
            }
            while (!c.outbox.empty()) {
                const std::string data = c.outbox.front() + "\n";
                if (send(c.socket, data.c_str(), static_cast<int>(data.size()), 0) == SOCKET_ERROR) {
                    dead.push_back(id);
                    break;
                }
                c.outbox.pop_front();
            }
        }
        for (int id : dead) Disconnect(id);
    }
}

}  // namespace

void Start() {
    const HANDLE t = CreateThread(nullptr, 0, &NetworkThread, nullptr, 0, nullptr);
    if (t) {
        CloseHandle(t);
    }
}

bool PopCommand(Command* out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_inbox.empty()) {
        return false;
    }
    *out = std::move(g_inbox.front());
    g_inbox.pop_front();
    return true;
}

void SetCurrentClient(int client) {
    g_current = client;
}

void Send(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_current > 0) {
            auto it = g_clients.find(g_current);
            if (it != g_clients.end()) it->second.outbox.push_back(line);
            return;
        }
        if (g_current < 0) {
            for (auto& [id, c] : g_clients) c.outbox.push_back(line);
        }
    }
    // Outside the lock: the internal client may send a command back. Its errors stop here: an
    // exception must never unwind into the game's frame.
    if (g_sink) {
        try {
            g_sink(line);
        } catch (const std::exception& e) {
            Log("[bridge] the client failed on \"%s\": %s", line.c_str(), e.what());
        }
    }
}

void SetInternalSink(std::function<void(const std::string&)> sink) {
    g_sink = std::move(sink);
}

void Inject(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_inbox.push_back({kInternalClient, line});
}

}  // namespace bsap::bridge
