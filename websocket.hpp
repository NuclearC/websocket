/*
    WebSocket v13 Implemented in C/C++ 
    Version 0.0.18.3
    Copyright (c) 2017-2018 NuclearC
*/

#ifndef WEBSOCKET_HPP_
#define WEBSOCKET_HPP_

#include <map>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <functional>
#include <cstring>

#include <http_parser.h>
#include <uv.h>
#include <base64_default_rfc4648.hpp>
#include <openssl/sha.h>
#include "url.hpp"

#include <binary.h>

namespace nc {
    namespace websocket {
        static constexpr char* ws_magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        typedef std::map<std::string, std::string> WebSocketHeaders;

        enum WebSocketState {
            kClosed = 0,
            kClosing = 1,
            kOpen = 2,
            kOpening = 3,
        };

        enum WebSocketOpcode {
            kContinuation = 0,
            kTextFrame = 1,
            kBinaryFrame = 2,
            kClose = 8,
            kPing = 9,
            kPong = 10,
        };

        struct WebSocketFrame {
            size_t payload_length;
            WebSocketOpcode opcode;
            bool is_masked;
            uint32_t mask;
            size_t frame_length;
        };

        static size_t WebSocketCreateFrameHeader(
            char* dest,
            size_t dest_len,
            WebSocketOpcode opcode,
            size_t payload_length,
            char* mask = nullptr);

        static WebSocketFrame WebSocketExtractFrameHeader(
            const char* source,
            size_t source_length
        );

        static inline void xor_buffer(char* src, size_t len, uint32_t key) {
            for (size_t i = 0; i < len; i++) {
                src[i] ^= key >> i % 4 * 8 & 255;
            }
        }

        class HttpParser {
        private:
            http_parser parser;
            http_parser_settings settings;
        public:
            HttpParser();
            ~HttpParser();

            size_t parse_request(std::string input);
            size_t parse_response(std::string input);

            std::function<void()>      on_message_begin;
            std::function<void(const char*, size_t)> on_url;
            std::function<void(const char*, size_t)> on_status;
            std::function<void(const char*, size_t)> on_header_field;
            std::function<void(const char*, size_t)> on_header_value;
            std::function<void()>      on_headers_complete;
            std::function<void(const char*, size_t)> on_body;
            std::function<void()>      on_message_complete;
            std::function<void()>      on_chunk_header;
            std::function<void()>      on_chunk_complete;
        };

        class HttpRequest {
        public:
            std::map<std::string, std::string> headers;
            std::string header, url, status;
            size_t res = 0;

            std::function<void()> end;
        };

        class HttpResponse {
        public:
            std::map<std::string, std::string> headers;
            std::string header, url, status;
            size_t res = 0;

            std::function<void()> end;
        };

        class WebSocketException
            : public std::exception
        {
        private:
            std::string message;
        public:
            WebSocketException() 
                : std::exception() {}
            WebSocketException(std::string _What)
                : message(_What), std::exception() {}
        };

        class WebSocket {
        protected:
            uv_tcp_t* socket;
            uv_loop_t* loop;

            WebSocketState state;

            HttpRequest* parse_http_request(const char* str, ssize_t len);
            HttpResponse* parse_http_response(const char* str, ssize_t len);
            void send_http_response(std::string accept);
            void send_http_request(std::string path,
                std::string host, const WebSocketHeaders& custom_headers);

            static void on_write(uv_write_t* req, int status);
            static void on_shutdown(uv_shutdown_t* req, int status);
            static void on_handle_close(uv_handle_t* handle);
            static void on_handle_close_with_callback(uv_handle_t* handle);

            bool http_handshake_done = false;

            size_t decode_frame(char* buf, size_t len);
            void decode_fragments();
            void enque_fragment(const char* buf, size_t len);

            void handle_packet(char* buf, size_t len);

            std::string fragment_buffer;
            size_t fragment_offset;

            int masking_key = 0;
        public:
            WebSocket(uv_loop_t* loop, uv_tcp_t* socket);
            WebSocket(const WebSocket& other);
            ~WebSocket();

            uv_loop_t* get_loop();

            virtual void close();

            virtual void destroy();

            virtual void send(std::string message, WebSocketOpcode op = kTextFrame);
            virtual void send(const char* str, WebSocketOpcode op = kTextFrame);
            virtual void send(const char* buf, size_t length, WebSocketOpcode op);

            template <typename T>
            inline void send(std::initializer_list<T> message, WebSocketOpcode op = kBinaryFrame) {
                std::vector<T> buf(message.begin(), message.end());
                send((char*)buf.data(), buf.size() * sizeof(T), op);
            };

            virtual void send_raw(char* str, size_t len);

            std::function<void(WebSocket*, const char*, const char*)> on_close;
            std::function<void(WebSocket*, const char*, const char*)> on_error;
            std::function<void(WebSocket*, char*, size_t, WebSocketOpcode)> on_message;
        };

        class WebSocketClient
            : public WebSocket {
            friend class WebSocket;
        public:
            WebSocketClient();
            WebSocketClient(uv_loop_t* loop, uv_tcp_t* socket = nullptr);

            void connect(std::string uri);
            void connect(addrinfo* addr);

            std::function<void(WebSocket*, const HttpResponse*)> on_connection;

            WebSocketHeaders custom_headers;
        private:
            void on_response_end(HttpResponse* res);
            static void on_getaddrinfo_end(uv_getaddrinfo_t* req, int status,
                addrinfo* res);
            static void on_connect_end(uv_connect_t* req, int status);
            static void on_alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
            static void on_read_callback(uv_stream_t* client, ssize_t nbuf, const uv_buf_t* buf);
            void on_tcp_packet(char* packet, ssize_t len);
            void on_tcp_close(int status);

            void on_tcp_connect();
            std::string path, host;
        };

        class WebSocketServer;

        class WebSocketServerNode
            : public WebSocket {
            friend class WebSocket;
            friend class WebSocketServer;
        public:
            WebSocketServerNode(uv_loop_t* loop, uv_tcp_t* socket, WebSocketServer* owner);

        private:
            void on_request_end(HttpRequest* req);
            void on_tcp_packet(char* packet, ssize_t len);
            void on_tcp_close(int status);

            WebSocketServer * owner;
        };

        class WebSocketServer {
            friend class WebSocket;
            friend class WebSocketServerNode;

        public:
            WebSocketServer(uv_loop_t* loop);
            ~WebSocketServer();

            void listen(uint16_t port, bool ipv6 = false);

            std::function<void(WebSocketServerNode*, const HttpRequest*)> on_connection;
        private:
            uv_loop_t * loop;
            uv_tcp_t* listener;

            static void on_connection_callback(uv_stream_t* server, int status);
            static void on_alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
            static void on_read_callback(uv_stream_t* client, ssize_t nbuf, const uv_buf_t* buf);
        };
    } // namespace websocket
} // namespace nc

#endif // WEBSOCKET_HPP_
