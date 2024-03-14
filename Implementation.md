# Detailed implementation

## Server

For a server implementation that is fast in storing and accessing customer information, I chose to use three maps (std::map) \
with the following meaning:

* `clients` -> the key is the TCP client ID, and the value is a structure that contains the client's socket (or -1 if it is disconnected) \
and a queue to add the pending messages in case the client is subscribed to one or more topics with the SF option active
* `sockets` -> the key is the TCP client socket, and the value is its ID
* `topics` -> the key is the name of the topic, and the value is a list (std::list) of structures containing the subscriber \
ID and the type of subscription (with / without SF)

For I/O multiplexing, I used the epoll interface. If the server receives an event, it calls a handler function:

* for commands received from the keyboard, check if exit is requested
* for messages received from a UDP client, the `new_udp_message` function is called
* for messages received from a TCP client, the `new_tcp_message` function is called

Main functions (additional documentation in the source code):

* `new_udp_message` -> receives a message from a UDP client, builds the packet for TCP clients and sends the package to \
clients connected and subscribed to the respective topic or puts it in the queue  of customers who are disconnected but \
have the SF option active
* `new_tcp_message` -> receives a packet from a TCP client, and processes the message according to its type:
     * `ID_COMM` -> the package contains the ID of the newly connected client. It is checked if it is a new client, if a \
    client reconnects, or if a client is already connected with the respective ID and the corresponding function is called
     * `SUBSCRIBE` -> the package contains the topic to which the client subscribes. If the client was already subscribed \
     * to the topic, the SF option is updated, if not, it is added to the table of topics and subscribers
     * `UNSUBSCRIBE` -> the package contains the topic from which the client unsubscribes. The table of topics is scrolled \
    and subscribers, the entry corresponding to the customer is deleted if he is a subscriber

**Mention**:
In order not to limit the maximum number of TCP clients, the `listen` function was called with the `SOMAXCONN` macro as size. \
In the case of the epoll instance, according to the manual, starting with Linux 2.6.8, the size parameter in the `epoll_create` \
function is ignored, but it must be greater than 0.\
**References**:
[listen](https://man7.org/linux/man-pages/man2/listen.2.html),
[epoll_create](https://man7.org/linux/man-pages/man2/epoll_create.2.html)


## Client

For I/O multiplexing, I used the epoll interface. When the client starts, it sends an `ID_COMM` type message to the server containing the \
client ID. If the client receives an event, it calls the function corresponding to the treatment of this event:
* for messages received from the keyboard, the `handle_cmd` function is called
* for messages received from the server, the `print_notification` function is called

Main functions (additional documentation in the source code):
* `handle_cmd` -> checks the command type and processes it
     * `subscribe <topic> <SF>` -> sends a SUBSCRIBE message to the server with the topic
     * `unsubscribe <topic>` -> sends an UNSUBSCRIBE message to the server with the topic
     * `exit` -> frees memory and closes the client
* `print_notification` -> processes a NOTIFICATION type message, and displays it in the console