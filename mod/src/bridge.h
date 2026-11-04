#pragma once

// Command channel of the mod. Text protocol, one command per line.
//
// The built-in Archipelago client sends its commands through it (Inject) and gets the replies
// and broadcast events through its sink. With debug=1 in BioShockAP.ini, the same commands are
// also open to external tools on 127.0.0.1:38300 (several clients at once).
//
// Networking runs on its own thread; the game thread fetches the commands every frame
// (PopCommand) and replies with Send, so the game never waits for the network.
//
// Routing: while a command runs (SetCurrentClient), Send replies to the client that sent it;
// outside of that ("event ..." lines), Send broadcasts to everyone.

#include <functional>
#include <string>

namespace bsap::bridge {

constexpr unsigned short kPort = 38300;

struct Command {
    int client;
    std::string line;
};

// Opens the TCP listener for external debug tools.
void Start();
bool PopCommand(Command* out);
void SetCurrentClient(int client);  // -1 = broadcast
void Send(const std::string& line);

// Internal client: the Archipelago client built into the DLL. Its commands go through the same
// queue as the network ones (Inject); its replies, and the events broadcast to everyone, are
// handed to `sink`, on the game thread.
constexpr int kInternalClient = 0;
void SetInternalSink(std::function<void(const std::string&)> sink);
void Inject(const std::string& line);

}  // namespace bsap::bridge
