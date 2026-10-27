# private-chat-c

A private one-to-one chat application written in C, with a multi-threaded TCP server and a command-line client. Messages are encrypted on the client with libsodium before they are sent, so the server only relays and stores ciphertext it cannot read. Users log in with hashed passwords, and each conversation's history is kept in SQLite and is visible only to the two people in it.

*Work in progress.*
