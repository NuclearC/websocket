/*
    WebSocket v13 Implemented in C/C++ 
    Version 0.0.18.3
    Copyright (c) 2017-2018 NuclearC
*/

#include "websocket.hpp"

namespace nc {
    namespace websocket {
        size_t WebSocket::decode_frame(char * buf, size_t len)
        {
            auto frame = WebSocketExtractFrameHeader(buf, len);
            if (!frame.payload_length || frame.payload_length > len)
                return 0;

            if (frame.is_masked) {
                xor_buffer(buf, frame.payload_length, frame.mask);
            }

            if (on_message)
                on_message(this, buf + frame.frame_length, frame.payload_length, frame.opcode);

            return frame.payload_length + frame.frame_length;
        }

        void WebSocket::decode_fragments()
        {
            for (size_t i = fragment_offset;;) {
                auto len = decode_frame(fragment_buffer.data() + i, fragment_buffer.size() - i);
                if (!len) {
                    fragment_offset = i;
                    break;
                }
                else {
                    i += len;

                    if (i == fragment_buffer.size()) {
                        fragment_buffer.clear();
                        fragment_offset = 0;
                        break;
                    }
                }
            }
        }

        void WebSocket::enque_fragment(const char * buf, size_t len)
        {
            fragment_buffer.append(buf, len);
        }

        void WebSocket::handle_packet(char * buf, size_t len)
        {
            if (fragment_buffer.size() > 0) {
                enque_fragment(buf, len);
                decode_fragments();
            }
            else {
                for (size_t i = 0; i < len;) {
                    auto frame = decode_frame(buf + i, len - i);
                    if (!frame) {
                        fragment_offset = 0;
                        enque_fragment(buf + i, len - i);
                        break;
                    }
                    else {
                        i += frame;
                    }
                }
            }
        }

        WebSocket::WebSocket(uv_loop_t * loop, uv_tcp_t * socket)
            : loop(loop), socket(socket), state(WebSocketState::kClosed), fragment_offset(0)
        {
        }
        WebSocket::WebSocket(const WebSocket & other)
            : loop(other.loop), socket(other.socket), state(other.state), 
            fragment_offset(other.fragment_offset)
        {
        }

        WebSocket::~WebSocket()
        {
        }

        uv_loop_t * WebSocket::get_loop()
        {
            return loop;
        }

        void WebSocket::send(std::string message, WebSocketOpcode op)
        {
            return send(message.c_str(), message.size(), op);
        }

        void WebSocket::send(const char * str, WebSocketOpcode op)
        {
            return send(str, std::strlen(str), op);
        }

        void WebSocket::send(const char* buf, size_t length, WebSocketOpcode op)
        {
            if (state != WebSocketState::kOpen)
                return;

            std::string frame(length + 14, 0);
            size_t header_length = WebSocketCreateFrameHeader(frame.data(), frame.length(),
                op, length, (char*)&masking_key);

            memcpy(frame.data() + header_length, buf, length);
            if (masking_key) {
                xor_buffer(frame.data() + header_length, length, masking_key);
            }

            send_raw(frame.data(), header_length + length);
        }

        void WebSocket::send_raw(char * str, size_t len)
        {
            auto write_req = new uv_write_t{};
            write_req->data = this;

            uv_buf_t bufs[1];
            bufs[0].base = str;
            bufs[0].len = len;

            if (int res = uv_write(write_req, (uv_stream_t*)socket, bufs, 1, on_write)) {
                throw WebSocketException("failed to write into socket");
            }
        }

        void WebSocket::close() {
            if (state != WebSocketState::kOpen)
                return;

            state = WebSocketState::kClosing;

            auto sd_req = new uv_shutdown_t{};
            sd_req->data = this;

            if (int res = uv_shutdown(sd_req, (uv_stream_t*)socket, on_shutdown)) {
                throw WebSocketException("failed to shutdown the socket");
            }
        }

        void WebSocket::destroy()
        {
            uv_close((uv_handle_t*)socket, on_handle_close);
        }

        size_t WebSocketCreateFrameHeader(char * dest, size_t dest_len, 
            WebSocketOpcode opcode, size_t payload_length, char * mask)
        {
            size_t off = 0;
            write_uint8(dest, dest_len, 0x80 | opcode, off);

            if (payload_length < 126) {
                dest[off++] = mask == nullptr ? payload_length : payload_length | 0x80;
            }
            else if (payload_length < 65536) {
                dest[off++] = mask == nullptr ? 126 : 126 | 0x80;
                dest[off++] = (payload_length >> 8) & 0xff;
                dest[off++] = (payload_length) & 0xff;
            }
            else {
                dest[off++] = mask == nullptr ? 127 : 127 | 0x80;
                dest[off++] = (payload_length >> 56) & 0xff;
                dest[off++] = (payload_length >> 48) & 0xff;
                dest[off++] = (payload_length >> 40) & 0xff;
                dest[off++] = (payload_length >> 32) & 0xff;
                dest[off++] = (payload_length >> 24) & 0xff;
                dest[off++] = (payload_length >> 16) & 0xff;
                dest[off++] = (payload_length >> 8) & 0xff;
                dest[off++] = (payload_length) & 0xff;
            }

            if (mask) {
                memcpy(dest + off, mask, 4);
                off += 4;
            }

            return off;
        }

        WebSocketFrame WebSocketExtractFrameHeader(const char * source, size_t len)
        {
            WebSocketFrame res = { 0 };
            if (len < 2)
                return { 0 };

            size_t off = 0;
            uint8_t b = source[off++];
            if ((b & 112) != 0) {
                return { 0 };
            }
            res.opcode = (WebSocketOpcode)(b & 0xf);

            b = source[off++];
            res.is_masked = b & 0x80;

            b &= ~0x80;

            if (b < 126) {
                res.payload_length = b;
            }
            else if (b == 126 && (off + 2 < len)) {
                res.payload_length = 0;
                res.payload_length |= (source[off++] & 0xff) << 8;
                res.payload_length |= source[off++] & 0xff;
            }
            else if (b == 127 && (off + 8 < len)) {
                res.payload_length = 0;
                res.payload_length |= (source[off++] & 0xff) << 56;
                res.payload_length |= (source[off++] & 0xff) << 48;
                res.payload_length |= (source[off++] & 0xff) << 40;
                res.payload_length |= (source[off++] & 0xff) << 32;
                res.payload_length |= (source[off++] & 0xff) << 24;
                res.payload_length |= (source[off++] & 0xff) << 16;
                res.payload_length |= (source[off++] & 0xff) << 8;
                res.payload_length |= source[off++] & 0xff;
            }
            else {
                return { 0 };
            }

            if (res.is_masked) {
                if (off + 4 >= len)
                    return WebSocketFrame{ 0 };
                res.mask = read_uint32(source, len, off);
            }

            res.frame_length = off;

            return res;
        }

        WebSocketServer::WebSocketServer(uv_loop_t * loop)
            : loop(loop)
        {
        }

        WebSocketServer::~WebSocketServer()
        {
        }

        void WebSocketServer::listen(uint16_t port, bool ipv6)
        {
            listener = new uv_tcp_t();
            listener->data = this;

            if (int res = uv_tcp_init(loop, listener)) {
                throw WebSocketException("failed to init the socket");
            }

            if (ipv6) {
                sockaddr_in6 address = {};
                auto res = uv_ip6_addr(nullptr, port, &address);

                if (int res = uv_tcp_bind(listener, (sockaddr*)&address, 0)) {
                    throw WebSocketException("failed to bind the socket");
                }
            }
            else {
                sockaddr_in address = {};
                auto res = uv_ip4_addr(nullptr, port, &address);

                if (int res = uv_tcp_bind(listener, (sockaddr*)&address, 0)) {
                    throw WebSocketException("failed to bind the socket");
                }
            }

            if (int res = uv_listen((uv_stream_t*)listener, 128, on_connection_callback)) {
                throw WebSocketException("failed to listen");
            }
        }

        void WebSocketServer::on_connection_callback(uv_stream_t * server, int status)
        {
            auto client = new uv_tcp_t();
            auto sender = (WebSocketServer*)server->data;

            if (int res = uv_tcp_init(sender->loop, client)) {
                throw WebSocketException("failed to init the socket");
            }

            auto result = uv_accept(server, (uv_stream_t*)client);

            if (result) {
                uv_close((uv_handle_t*)client, nullptr);
            }
            else {
                client->data = new WebSocketServerNode(sender->loop, client, sender);
                if (int res = uv_read_start((uv_stream_t*)client, on_alloc_callback, on_read_callback)) {
                    throw WebSocketException("failed to start reading the socket");
                }
            }
        }

        void WebSocketServer::on_alloc_callback(uv_handle_t * handle, size_t suggested_size, uv_buf_t * buf)
        {
            buf->base = new char[suggested_size];
            buf->len = suggested_size;
        }

        void WebSocketServer::on_read_callback(uv_stream_t * client, ssize_t nbuf, const uv_buf_t * buf)
        {
            auto sender = (WebSocketServerNode*)client->data;

            if (nbuf > 0) {
                sender->on_tcp_packet(buf->base, nbuf);
                delete[] buf->base;
            }
            else {
                sender->on_tcp_close(nbuf);
            }
        }

        WebSocketServerNode::WebSocketServerNode(uv_loop_t * _loop, uv_tcp_t * _socket, WebSocketServer * _owner)
            : WebSocket(_loop, _socket), owner(_owner)
        {
        }

        void WebSocketServerNode::on_request_end(HttpRequest* req)
        {
            auto key = req->headers["Sec-WebSocket-Key"] + ws_magic;
            unsigned char res[20];
            SHA1((const unsigned char*)key.c_str(), key.length(), res);
            send_http_response(cppcodec::base64_rfc4648::encode(res, 20));

            http_handshake_done = true;

            delete req;
        }

        void WebSocketServerNode::on_tcp_packet(char * packet, ssize_t len)
        {
            if (http_handshake_done) {
                handle_packet(packet, len);
            }
            else {
                auto req = parse_http_request(packet, len);
                if (req->res) {
                    req->end = std::bind(&WebSocketServerNode::on_request_end, this, req);
                    if (req->res < len) {
                        enque_fragment(packet + req->res, len - req->res);
                    }

                    owner->on_connection(this, req);
                }
            }
        }

        void WebSocketServerNode::on_tcp_close(int status)
        {
            close();
        }

        HttpRequest* WebSocket::parse_http_request(const char * str, ssize_t len)
        {
            HttpParser p;
            HttpRequest* req = new HttpRequest{};
            bool completed = false;

            p.on_header_field = [&req](const char* cstr, size_t len) {
                req->header = std::string(cstr, len);
            };
            p.on_header_value = [&req](const char* cstr, size_t len) {
                std::string str(cstr, len);
                req->headers[req->header] = str;
            };
            p.on_url = [&req](const char* cstr, size_t len) {
                req->url = std::string(cstr, len);
            };
            p.on_status = [&req](const char* cstr, size_t len) {
                req->status = std::string(cstr, len);
            };
            p.on_message_complete = [&completed]() {
                completed = true;
            };

            auto parsed = p.parse_request(std::string(str, len));
            req->res = completed ? parsed : 0;
            
            return req;
        }

        HttpResponse* WebSocket::parse_http_response(const char * str, ssize_t len)
        {
            HttpParser p;
            HttpResponse* req = new HttpResponse{};
            bool completed = false;

            p.on_header_field = [&req](const char* cstr, size_t len) {
                req->header = std::string(cstr, len);
            };
            p.on_header_value = [&req](const char* cstr, size_t len) {
                std::string str(cstr, len);
                req->headers[req->header] = str;
            };
            p.on_url = [&req](const char* cstr, size_t len) {
                req->url = std::string(cstr, len);
            };
            p.on_status = [&req](const char* cstr, size_t len) {
                req->status = std::string(cstr, len);
            };
            p.on_message_complete = [&completed]() {
                completed = true;
            };

            auto parsed = p.parse_response(std::string(str, len));
            req->res = completed ? parsed : 0;

            return req;
        }

        void WebSocket::send_http_response(std::string accept)
        {
            std::string response = 
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

            send_raw(response.data(), response.length());
        }

        void WebSocket::send_http_request(std::string path,
            std::string host, const WebSocketHeaders& custom_headers)
        {
            if (path.empty())
                path = "/";
            std::string request =
                "GET " + path + " HTTP/1.1\r\n"
                "Host: " + host + "\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: websocket\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n";

            for (auto&& header : custom_headers) {
                request += header.first + ": " + header.second + "\r\n";
            }

            request += "\r\n";
            send_raw(request.data(), request.length());
        }

        void WebSocket::on_write(uv_write_t * req, int status)
        {
            auto _this = (WebSocket*)req->data;

            if (status) {
                _this->close();
            }

            delete req;
        }

        void WebSocket::on_shutdown(uv_shutdown_t * req, int status)
        {
            auto _this = (WebSocket*)req->data;
            delete req;

            if (status) {
                _this->state = WebSocketState::kClosed;
                if (_this->on_error)
                    _this->on_error(_this, uv_err_name(status), uv_strerror(status));
                else
                    throw WebSocketException("failed to shutdown the socket");
                return;
            }

            uv_close((uv_handle_t*)_this->socket, on_handle_close_with_callback);
        }

        void WebSocket::on_handle_close(uv_handle_t * handle)
        {
            auto _this = (WebSocket*)handle->data;
            _this->state = WebSocketState::kClosed;
            delete handle;
            _this->socket = nullptr;
        }

        void WebSocket::on_handle_close_with_callback(uv_handle_t * handle)
        {
            auto _this = (WebSocket*)handle->data;
            _this->state = WebSocketState::kClosed;
            delete handle;
            _this->socket = nullptr;
            if (_this->on_close)
                _this->on_close(_this, "", "");
        }

        HttpParser::HttpParser() : parser{}, settings{}
        {
            settings.on_body = [](http_parser* p, const char* at,
                size_t len) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_body)
                    _this->on_body(at, len);
                return 0;
            };
            settings.on_header_field = [](http_parser* p, const char* at, 
                size_t len) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_header_field)
                    _this->on_header_field(at, len);
                return 0;
            };
            settings.on_header_value = [](http_parser* p, const char* at, 
                size_t len) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_header_value)
                    _this->on_header_value(at, len);
                return 0;
            };
            settings.on_status = [](http_parser* p, const char* at, 
                size_t len) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_status)
                    _this->on_status(at, len);
                return 0;
            };
            settings.on_url = [](http_parser* p, const char* at, 
                size_t len) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_url)
                    _this->on_url(at, len);
                return 0;
            };
            settings.on_chunk_complete = [](http_parser* p) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_chunk_complete)
                    _this->on_chunk_complete();
                return 0;
            };
            settings.on_chunk_header = [](http_parser* p) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_chunk_header)
                    _this->on_chunk_header();
                return 0;
            };
            settings.on_headers_complete = [](http_parser* p) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_headers_complete)
                    _this->on_headers_complete();
                return 0;
            };
            settings.on_message_begin = [](http_parser* p) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_message_begin)
                    _this->on_message_begin();
                return 0;
            };
            settings.on_message_complete = [](http_parser* p) -> int {
                auto _this = (HttpParser*)p->data;
                if (_this->on_message_complete)
                    _this->on_message_complete();
                return 0;
            };
        }

        HttpParser::~HttpParser()
        {
        }

        size_t HttpParser::parse_request(std::string input)
        {
            parser = {};
            parser.data = this;
            http_parser_init(&parser, http_parser_type::HTTP_REQUEST);
            return http_parser_execute(&parser, &settings, 
                input.data(), input.size());
        }

        size_t HttpParser::parse_response(std::string input)
        {
            parser = {};
            parser.data = this;
            http_parser_init(&parser, http_parser_type::HTTP_RESPONSE);
            return http_parser_execute(&parser, &settings,
                input.data(), input.size());
        }

        WebSocketClient::WebSocketClient()
            : WebSocket(nullptr, nullptr)
        {
        }

        WebSocketClient::WebSocketClient(uv_loop_t* loop, uv_tcp_t* socket)
            : WebSocket(loop, socket)
        {
            masking_key = rand() * 0xffffffff;
        }

        void WebSocketClient::connect(std::string uri)
        {
            Url u(uri);
            host = u.host();
            path = u.path();

            if (!socket) {
                socket = new uv_tcp_t{};
                socket->data = this;

                if (int res = uv_tcp_init(loop, socket)) {
                    throw WebSocketException("failed to init the socket");
                }
            }

            auto getaddrinfo_req = new uv_getaddrinfo_t{};
            getaddrinfo_req->data = this;

            addrinfo hints = {};
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_family = u.ip_version() == 6 ? AF_INET6 : AF_INET;

            if (int res = uv_getaddrinfo(loop,
                getaddrinfo_req,
                on_getaddrinfo_end,
                u.host().c_str(),
                u.port().c_str(),
                &hints)) {
                throw WebSocketException("failed to call getaddrinfo");
            }
        }

        void WebSocketClient::connect(addrinfo * addr)
        {
            if (state != WebSocketState::kClosed)
                return;

            state = WebSocketState::kOpening;

            auto connect_req = new uv_connect_t{};
            connect_req->data = this;

            if (int res = uv_tcp_connect(connect_req,
                socket, addr->ai_addr, on_connect_end)) {
                printf("connection error: %s\n", uv_strerror(res));
                throw WebSocketException("failed to connect");
            }
        }

        void WebSocketClient::on_response_end(HttpResponse * res)
        {
            http_handshake_done = true;

            state = WebSocketState::kOpen;

            delete res;
        }

        void WebSocketClient::on_getaddrinfo_end(uv_getaddrinfo_t * req,
            int status, addrinfo * res)
        {
            auto _this = (WebSocketClient*)req->data;
            delete req;

            if (status) {
                _this->state = WebSocketState::kClosed;
                if (_this->on_error)
                    _this->on_error(_this, uv_err_name(status), uv_strerror(status));
                else
                    throw WebSocketException("failed to getaddrinfo");
                return;
            }

            _this->connect(res);
        }

        void WebSocketClient::on_connect_end(uv_connect_t * req, int status)
        {
            auto _this = (WebSocketClient*)req->data;
            delete req;

            if (status) {
                _this->state = WebSocketState::kClosed;
                if (_this->on_error)
                    _this->on_error(_this, uv_err_name(status), uv_strerror(status));
                else
                    throw WebSocketException("failed to connect");
                return;
            }

            _this->on_tcp_connect();
        }

        void WebSocketClient::on_alloc_callback(uv_handle_t * handle, 
            size_t suggested_size, uv_buf_t * buf)
        {
            buf->base = new char[suggested_size];
            buf->len = suggested_size;
        }

        void WebSocketClient::on_read_callback(uv_stream_t * client, 
            ssize_t nbuf, const uv_buf_t * buf)
        {
            auto sender = (WebSocketClient*)client->data;

            if (nbuf > 0) {
                sender->on_tcp_packet(buf->base, nbuf);
                delete[] buf->base;
            }
            else {
                sender->on_tcp_close(nbuf);
            }
        }

        void WebSocketClient::on_tcp_packet(char * packet, ssize_t len)
        {
            if (http_handshake_done) {
                handle_packet(packet, len);
            }
            else {
                auto res = parse_http_response(packet, len);
                if (res->res) {
                    res->end = std::bind(&WebSocketClient::on_response_end, this, res);
                    if (res->res < len) {
                        enque_fragment(packet + res->res, len - res->res);
                    }

                    if (on_connection)
                        on_connection(this, res);
                }
            }
        }

        void WebSocketClient::on_tcp_close(int status)
        {
            close();
        }

        void WebSocketClient::on_tcp_connect()
        {
            send_http_request(path, host, custom_headers);

            if (int res = uv_read_start((uv_stream_t*)socket,
                on_alloc_callback, on_read_callback)) {
                throw WebSocketException("failed to start reading");
            }
        }
} // namespace websocket
} // namespace nc
