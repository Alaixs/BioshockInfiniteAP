#pragma once

// WebSocket connection to the Archipelago server, with WinHTTP (part of Windows: ws:// and
// wss://, system proxy and certificates, no dependency).
//
// All networking runs on its own thread: the game never blocks. The game thread reads events
// with Poll and sends with Send (short messages).
//
// Without an explicit scheme, wss:// is tried then ws:// (ws:// first for localhost), like the
// official Archipelago clients.

#include <string>

namespace bsap::apnet {

enum class Scheme { Auto, Secure, Plain };

enum class EventType {
    Open,     // connected (text = "ws" or "wss")
    Message,  // one complete text message (JSON)
    Close,    // connection lost or impossible (text = reason, for the log)
};

struct Event {
    EventType type;
    std::string text;
};

// Closes the current connection, then connects (in the background).
void Open(const std::wstring& host, int port, Scheme scheme);
void Close();
bool Send(const std::string& text);
// Events of the most recent connection only.
bool Poll(Event* out);

}  // namespace bsap::apnet
