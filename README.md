# WebSocket
WebSocket v13 implementation in C++

#### Usage
```c++
#include "ws.h"

...

nc::WebSocket ws;

ws.on_open = [](nc::WebSocket* sender) {
  sender->Send("hello world");
}

ws.Connect("ws://127.0.0.1:443");

ws.Run();

```
