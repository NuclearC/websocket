/*
    WebSocket v13 Implementation
    Copyright (C) 2017 by NuclearC
*/

#ifndef NC_WEBSOCKET_H_
#define NC_WEBSOCKET_H_

enum WebSocketOpcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

static void WebSocketCreateFrame(
    const char*           _Source,
    unsigned long long    _SourceLength,
    char*                 _Destination,
    unsigned long long&   _DestinationLength,
    WebSocketOpcode       _Opcode,
    int                   _Key = 0) {

    char mask[4];
    mask[0] = _Key & 0xff;
    mask[1] = (_Key >> 8) & 0xff;
    mask[2] = (_Key >> 16) & 0xff;
    mask[3] = (_Key >> 24) & 0xff;

    unsigned long long off = 0;

    _Destination[off++] = 0x80 | _Opcode;

    if (_SourceLength <= 125) {
        _Destination[off++] = _SourceLength | (_Key != 0 ? 0x80 : 0);
    }
    else if (_SourceLength >= 126 && _SourceLength <= 65535) {
        _Destination[off++] = 126 | (_Key != 0 ? 0x80 : 0);
        _Destination[off++] = (_SourceLength >> 8) & 0xff;
        _Destination[off++] = (_SourceLength) & 0xff;
    }
    else {
        _Destination[off++] = 127 | (_Key != 0 ? 0x80 : 0);
        _Destination[off++] = (_SourceLength >> 56) & 0xff;
        _Destination[off++] = (_SourceLength >> 48) & 0xff;
        _Destination[off++] = (_SourceLength >> 40) & 0xff;
        _Destination[off++] = (_SourceLength >> 32) & 0xff;
        _Destination[off++] = (_SourceLength >> 24) & 0xff;
        _Destination[off++] = (_SourceLength >> 16) & 0xff;
        _Destination[off++] = (_SourceLength >> 8) & 0xff;
        _Destination[off++] = (_SourceLength) & 0xff;
    }

    if (_Key) {
        for (int i = 0; i < 4; i++) {
            _Destination[off++] = mask[i];
        }
    }

    for (unsigned long long i = 0; i < _SourceLength; i++) {
        _Destination[off++] = _Source[i] ^ mask[i % 4];
    }

    _DestinationLength = off;

    return;
}

static void WebSocketExtractFrame(
    const char*           _Source,
    unsigned long long    _SourceLength,
    char*                 _Destination,
    unsigned long long&   _DestinationLength,
    WebSocketOpcode&      _Opcode) {

    unsigned long long off = 0;

    _DestinationLength = 0;
    _Opcode = (WebSocketOpcode)(_Source[0] & 0x0f);

    bool mask = (_Source[1] & 0x80) == 0x80;
    char mask_key[4];
    int length = (_Source[1] & 0x7f);

    off = 2;

    _DestinationLength = 0;

    if (length < 126) {
        _DestinationLength = length;
    }
    else if (length == 126) {
        _DestinationLength = 0;
        _DestinationLength |= (_Source[off++] & 0xff) << 8;
        _DestinationLength |= _Source[off++] & 0xff;
    }
    else if (length == 127) {
        _DestinationLength = 0;
        _DestinationLength |= (_Source[off++] & 0xff) << 56;
        _DestinationLength |= (_Source[off++] & 0xff) << 48;
        _DestinationLength |= (_Source[off++] & 0xff) << 40;
        _DestinationLength |= (_Source[off++] & 0xff) << 32;
        _DestinationLength |= (_Source[off++] & 0xff) << 24;
        _DestinationLength |= (_Source[off++] & 0xff) << 16;
        _DestinationLength |= (_Source[off++] & 0xff) << 8;
        _DestinationLength |= _Source[off++] & 0xff;
    }

    if (mask) {
        mask_key[0] = _Source[off++];
        mask_key[1] = _Source[off++];
        mask_key[2] = _Source[off++];
        mask_key[3] = _Source[off++];
    }

    for (unsigned long long i = 0; i < _DestinationLength; i++) {
        if (mask)
            _Destination[i] = _Source[off++] ^ mask_key[i % 4];
        else
            _Destination[i] = _Source[off++];
    }
}

#endif // NC_WEBSOCKET_H_
