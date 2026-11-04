#include "ap_net.h"

#include "log.h"

#include <windows.h>
#include <winhttp.h>

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace bsap::apnet {

namespace {

// The handles of one attempt. Close() may close them from the game thread while the network
// thread is blocked on them: WinHTTP then cancels the pending call.
struct Connection {
    std::mutex m;
    HINTERNET session = nullptr, connect = nullptr, request = nullptr, ws = nullptr;
    bool cancelled = false;
    int generation = 0;

    void CloseHandlesLocked() {
        for (HINTERNET* h : {&ws, &request, &connect, &session}) {
            if (*h) WinHttpCloseHandle(*h);
            *h = nullptr;
        }
    }
};

std::mutex g_mutex;  // guards everything below
std::shared_ptr<Connection> g_current;
std::deque<std::pair<int, Event>> g_events;
int g_generation = 0;

void Push(const Connection& c, EventType type, std::string text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back({c.generation, Event{type, std::move(text)}});
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// true = c.ws is open.
bool Handshake(Connection& c, const std::wstring& host, int port, bool secure, std::string* error) {
    HINTERNET request = nullptr;
    {
        std::lock_guard<std::mutex> lock(c.m);
        if (c.cancelled) return false;
        c.session = WinHttpOpen(L"BioShockAP/" BSAP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
        if (c.session) {
            WinHttpSetTimeouts(c.session, 5000, 8000, 8000, 8000);
            c.connect = WinHttpConnect(c.session, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
        }
        if (c.connect) {
            c.request = WinHttpOpenRequest(c.connect, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        }
        request = c.request;
    }
    DWORD status = 0, size = sizeof(status);
    const bool ok = request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
                    WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, nullptr) &&
                    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    const DWORD err = ok ? 0 : GetLastError();
    std::lock_guard<std::mutex> lock(c.m);
    if (c.cancelled) return false;
    if (ok && status == 101) {
        c.ws = WinHttpWebSocketCompleteUpgrade(c.request, 0);
    }
    if (!c.ws) {
        *error = ok ? "HTTP response " + std::to_string(status) : "WinHTTP error " + std::to_string(err);
        c.CloseHandlesLocked();
        return false;
    }
    WinHttpCloseHandle(c.request);
    c.request = nullptr;
    return true;
}

void ReceiveLoop(Connection& c) {
    HINTERNET ws;
    {
        std::lock_guard<std::mutex> lock(c.m);
        ws = c.ws;
    }
    std::vector<char> buffer(64 * 1024);
    std::string message;
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        const DWORD err = WinHttpWebSocketReceive(ws, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
        if (err == ERROR_WINHTTP_TIMEOUT) continue;  // quiet server: nothing wrong
        if (err != NO_ERROR) {
            Push(c, EventType::Close, "receive: error " + std::to_string(err));
            return;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            Push(c, EventType::Close, "closed by the server");
            return;
        }
        message.append(buffer.data(), read);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            Push(c, EventType::Message, std::move(message));
            message.clear();
        }
    }
}

struct Target {
    std::shared_ptr<Connection> connection;
    std::wstring host;
    int port;
    Scheme scheme;
};

DWORD WINAPI NetworkThread(LPVOID param) {
    std::unique_ptr<Target> t(static_cast<Target*>(param));
    Connection& c = *t->connection;
    const bool local = t->host == L"localhost" || t->host == L"127.0.0.1" || t->host == L"::1";
    std::vector<bool> attempts;
    if (t->scheme == Scheme::Secure)
        attempts = {true};
    else if (t->scheme == Scheme::Plain)
        attempts = {false};
    else
        attempts = local ? std::vector<bool>{false, true} : std::vector<bool>{true, false};

    std::string error;
    for (const bool secure : attempts) {
        std::string why;
        if (Handshake(c, t->host, t->port, secure, &why)) {
            Log("[apnet] connected to %s://%s:%d", secure ? "wss" : "ws", Utf8(t->host).c_str(), t->port);
            Push(c, EventType::Open, secure ? "wss" : "ws");
            ReceiveLoop(c);
            std::lock_guard<std::mutex> lock(c.m);
            c.CloseHandlesLocked();
            return 0;
        }
        Log("[apnet] %s://%s:%d : %s", secure ? "wss" : "ws", Utf8(t->host).c_str(), t->port, why.c_str());
        error += (error.empty() ? "" : "; ") + why;
        std::lock_guard<std::mutex> lock(c.m);
        if (c.cancelled) return 0;
    }
    Push(c, EventType::Close, "cannot connect (" + error + ")");
    return 0;
}

}  // namespace

void Open(const std::wstring& host, int port, Scheme scheme) {
    Close();
    auto connection = std::make_shared<Connection>();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        connection->generation = ++g_generation;
        g_current = connection;
    }
    auto* target = new Target{connection, host, port, scheme};
    const HANDLE thread = CreateThread(nullptr, 0, &NetworkThread, target, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        delete target;
        Push(*connection, EventType::Close, "cannot start the network thread");
    }
}

void Close() {
    std::shared_ptr<Connection> previous;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        previous = std::move(g_current);
        ++g_generation;  // the old connection's events will be ignored
        g_events.clear();
    }
    if (previous) {
        std::lock_guard<std::mutex> lock(previous->m);
        previous->cancelled = true;
        // Hard close, without waiting for the server: Archipelago treats it as a normal disconnect.
        previous->CloseHandlesLocked();
    }
}

bool Send(const std::string& text) {
    std::shared_ptr<Connection> c;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        c = g_current;
    }
    if (!c) return false;
    std::lock_guard<std::mutex> lock(c->m);
    return c->ws && WinHttpWebSocketSend(c->ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                         const_cast<char*>(text.data()), static_cast<DWORD>(text.size())) == NO_ERROR;
}

bool Poll(Event* out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    while (!g_events.empty()) {
        auto [generation, event] = std::move(g_events.front());
        g_events.pop_front();
        if (generation == g_generation) {
            *out = std::move(event);
            return true;
        }
    }
    return false;
}

}  // namespace bsap::apnet
