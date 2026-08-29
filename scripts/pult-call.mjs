#!/usr/bin/env node
// Talk to a running the-pult station over its WebSocket API.
//   scripts/pult-call.mjs call device.adopt '{"serial":"0c181818"}'
//   scripts/pult-call.mjs call device.identify '{"serial":"0c181818"}'
//   scripts/pult-call.mjs get devices
//   scripts/pult-call.mjs set fixtures/<uuid>/... '<json>'
const [, , op, a1, a2, ...rest] = process.argv;
const url = process.env.PULT_URL || "ws://localhost:7700/ws";
const ws = new WebSocket(url);
const id = "cli-" + Date.now();
const seg = (s) => (/^\d+$/.test(s) ? { Index: Number(s) } : { Key: s });
ws.onopen = () => {
  let msg;
  if (op === "call") msg = { type: "Call", payload: { method: a1, args: JSON.parse(a2 || "{}"), request_id: id } };
  else if (op === "get") msg = { type: "Get", payload: { path: a1.split("/").filter(Boolean).map(seg), request_id: id } };
  else if (op === "set") msg = { type: "Set", payload: { path: a1.split("/").filter(Boolean).map(seg), value: JSON.parse(a2), request_id: id } };
  else { console.error("usage: call <method> [args] | get <path> | set <path> <json>"); process.exit(2); }
  ws.send(JSON.stringify(msg));
};
ws.onmessage = (ev) => {
  const m = JSON.parse(ev.data);
  if (m.payload && m.payload.request_id && m.payload.request_id !== id) return;
  if (m.type === "CallResult" || m.type === "GetResult" || m.type === "SetAck" || m.type === "Error") {
    console.log(JSON.stringify(m.payload ?? m, null, 1));
    ws.close();
  }
};
ws.onerror = (e) => { console.error("ws error", e.message || e); process.exit(1); };
setTimeout(() => { console.error("timeout"); process.exit(1); }, 8000);
