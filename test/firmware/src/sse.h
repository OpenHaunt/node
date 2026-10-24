// Server-Sent Events: keeps a few HTTP connections open and pushes "data: ..." events to them.
// Sending never blocks the caller for long; a client that can't keep up or went away is dropped
// (browsers' EventSource reconnects by itself).
#pragma once

#include <WiFiClient.h>

constexpr int SSE_MAX_CLIENTS = 4;

// Takes over an accepted connection whose request was GET /api/events and sends the response header.
// When all slots are in use, the oldest client is dropped.
void sseAdd(WiFiClient &client);

// Sends one event with the given payload (single line, e.g. JSON) to every client.
void sseBroadcast(const char *data);

int sseClients();

// Closes all event streams cleanly (FIN), so browsers reconnect at once instead of waiting for a
// TCP timeout. Call before a planned restart.
void sseCloseAll();
