#ifndef _IPC__SOCKET__UNIX_H
#define _IPC__SOCKET__UNIX_H

#include <ipc/socket.h>

struct socket *socket_create_unix(int type, int protocol);

#endif
