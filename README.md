# Client-server application

Backend application for broadcasting news received from UDP clients to TCP clients, using I/O multiplexing


## Project structure

* `common.h` -> necessary macros for server and client
* `helpers.h` -> definition of DIE macro
* `queue.h` -> queue implementation
* `common.cpp` -> functions used by server and client
* `server.cpp` -> server implementation
* `subscriber.cpp` -> TCP client implementation
* `Makefile` -> contains server, subscriber and clean rules
* `udp_client/` -> contains an UDP client implemented in python for testing purposes


## Run the tests

Clone the repository on your local machine with Linux (required for use of epoll).

```bash
git clone https://github.com/alexandruvidu/client-server-app
```

Go to the project directory.

```bash
cd client-server-app
```

Run the test script.
```bash
python3 test.py
```


## Message format

Every message sent between client and server uses the following header.

```
+--------+----------------+
| Byte 1 |    Bytes 2-3   |
+--------+----------------+
|  Type  | Payload length |
+--------+----------------+
```

To set and check type of message, bit operations are used.

```
+-------------+-------------+--------------+---------+-------+----------+
|    Bit 0    |    Bit 1    |    Bit 2     |  Bit 3  | Bit 4 | Bits 5-7 |
+-------------+-------------+--------------+---------+-------+----------+
| UNSUBSCRIBE |  SUBSCRIBE  | NOTIFICATION | ID_COMM |  SF   |  unused  |
+-------------+-------------+--------------+---------+-------+----------+

UNSUBSCRIBE     -> message sent by client to server to unsubscribe from the topic included

SUBSCRIBE       -> message sent by client to server to subscribe from the topic included

NOTIFICATION    -> message sent by server to TCP clients with content from a UDP client

ID_COMM         -> if sent by client to server, the message contains the client's ID
                -> if sent by server to client, it means that a user is already connected with the same name

SF              -> used with subscribe command

```

For messages received from UDP clients that need to be sent to TCP clients, the following header is used:

```
+------+--------------+--------------+--------------+--------------+
| Word |      1       |      2       |      3       |      4       |
+---------------------+--------------+--------------+--------------+
|    0 |                          SRC_IP                           |
+------+--------------+--------------+--------------+--------------+
|    4 |          SRC_PORT           |  TOPIC_LEN   | PAYLOAD_TYPE |
+------+--------------+--------------+--------------+--------------+

SRC_IP          -> IP address of UDP client
SRC_PORT        -> Port of UDP client
TOPIC_LEN_      -> Topic length in bytes
PAYLOAD TYPE    -> Message type
```


## Documentation

[Detailed implementation](https://github.com/alexandruvidu/client-server-app/blob/master/Implementation.md)
