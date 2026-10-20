# 15 — Brokers, and a console that fails over

A node publishes to one MQTT broker, and doc 08 has a controller tell it which with
`POST /api/v1/config` (`mqtt.broker`). That broker is the controller's own. A console that runs
as a session of several stations, one leading and the rest standing by, needs its nodes to
follow the lead when it moves: when the leading machine dies, whoever leads next has to end up
with every node, including nodes it has never seen. Discovery can't be relied on to tell it
about them. mDNS stops at a router, a crowded wifi drops it, and the station taking over may
simply not have been looking at that moment.

So a node is told **every** broker it may use, in order, and finds the next one itself.

Like docs 13 and 14, this is an extension to doc 08. A node that knows nothing of lists reads
`mqtt.broker` and works as before; a controller that sends no list is a list of one.

## The list

An ordered list of `"host:port"` strings, **leader first**, at most **four**. Entries after the
fourth are dropped, not refused: a session bigger than that still has a spare for every node.
An entry is a host, a colon and a port 1..65535, with no comma or space in it. A list with one
bad entry is refused whole.

It arrives two ways, and a node treats them the same.

**Over HTTP**, in the `mqtt` object of `POST /api/v1/config`:

```json
{ "mqtt": { "broker": "10.0.0.5:1883", "brokers": ["10.0.0.5:1883", "10.0.0.6:1883"] } }
```

`brokers` wins where it is present. A controller sends `broker` as well, set to the first
entry, for nodes from before lists. `broker` alone is a list of one, and `"broker": ""` is
how a node is told to forget its broker. `GET /api/v1/config` returns both.

**Over MQTT**, retained, on one topic for every node:

```
openhaunt/brokers     {"brokers": ["10.0.0.5:1883", "10.0.0.6:1883"]}
```

A node subscribes to it on every connection. This is how the list reaches a node that walked to
the new leader by itself, since that leader may have no idea where the node is on the network.
It publishes the list with the retain flag set, so the node gets it the moment it subscribes.

Either way, the node **keeps the list in NVS**. A node that reboots while its leader is dead
still knows where the others are.

## Which broker a node is on

A node is connected to at most one broker, and **it takes commands from the broker it is
connected to and from nowhere else**. That needs no rule of its own: one client, one
connection.

- **Starting**, the node tries the list from the top.
- **Losing** the broker it is on, or being refused by one, it tries the **next entry** 200 ms
  later. After a whole pass of the list has failed, it starts again from the top after **1 s**,
  doubling each pass up to **8 s**. A successful connection resets the backoff.
- **Told a new list** whose first entry is not the broker it is on, it **moves**: it disconnects,
  saying `offline` on its status topic, and tries the new list from the top. A new list with the
  same first entry, such as a longer tail or a reordered tail, moves nothing.
- Input events that happen while it is on no broker are **queued**, the most recent 32 in order,
  and published on connecting, with their timestamps. A PIR that fires during a failover arrives
  late rather than not at all.

A connection attempt to an address nobody answers gives up after 3 s.

## Which broker takes a node

**Only the leading station's broker accepts nodes.** Every station of a session may run a
broker, but the port nodes dial is open exactly while that station leads. A station that stops
leading closes the port, and every node on it drops and walks its list.

This rule is what stops two stations from both believing they own a node. A follower never holds
a node it doesn't command, so it can never have a node whose inputs nobody acts on. And a
station that led and then stopped, after a network split heals for instance, hands its nodes back
by closing the door, without anybody having to tell them. The cost is the gap: between the
leader dying and the next station taking over, a node can reach no broker, and its inputs wait
in its queue. For a killed process that gap is a moment. For a machine that lost power it is
however long the session takes to notice, 16 s on the console this was written for.

Running a broker only on the leader, started on promotion, was the other design, and it fails at
the same place this one is careful about: a broker can't always be stopped, and one left
listening on a station that no longer leads would hold every node that walked to it.

## What a controller does

- **Starting to lead**, it opens its port, publishes the list retained with itself first and every
  other station of the session after it, and sends the list over HTTP to every node it can
  reach.
- **When the session changes** (a station joins, leaves, or says a different address), it does
  the same, and only if the list has actually changed.
- **Stopping leading**, it closes its port.

The list's order after the first entry is the controller's to choose; it should be stable, so the
same session produces the same list each time. The console this was written for sorts the
followers by station id.

## Topics, collected

```
openhaunt/brokers      retained, published by the leading controller (doc 15)
openhaunt/clock        retained, the controller's clock (doc 08)
openhaunt/<sn>/...     as doc 08, 13 and 14
```
