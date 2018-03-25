# WebSocket
WebSocket v13 implementation in C++

#### Usage
```c++
#include "websocket.hpp"
...
    using namespace nc;

    loop = uv_default_loop();

    websocket::WebSocketClient ws(loop);

    ws.on_connection = [](websocket::WebSocket* ws, const websocket::HttpResponse* res) {
        ws->send<char>({16, 0, 15, 0});
        ws->send("echo hello");

        res->end();
    };

    ws.connect("ws://localhost:1501");

```

#### Dependencies
* libuv: https://github.com/libuv/libuv
* Binary Writer/Reader: https://github.com/NuclearC/binary-writer-reader
* cppcodec: https://github.com/tplgy/cppcodec
* http-parser: https://github.com/nodejs/http-parser
* CxxUrl: https://github.com/chmike/CxxUrl/blob/master/url.cpp
* OpenSSL
