#pragma once

#include <algorithm>
#include <stdint.h>
#include <string.h>

namespace lab {

// A short, single Text record has a one-byte payload length and four header bytes.
constexpr uint16_t kNfcNdefCapacity = 259;
struct NfcText {
    char text[256]{};
    const char* error = "not_started";
};

inline bool nfcUtf8(const uint8_t* bytes, size_t size) {
    for (size_t i = 0; i < size;) {
        const uint8_t first = bytes[i++];
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7F) return false;
            continue;
        }
        unsigned extra;
        uint32_t value, minimum;
        if (first >= 0xC2 && first <= 0xDF) { extra = 1; value = first & 31; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { extra = 2; value = first & 15; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { extra = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (size - i < extra) return false;
        while (extra--) {
            const uint8_t next = bytes[i++];
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ||
            (value >= 0x80 && value <= 0x9F)) return false;
    }
    return true;
}

inline const char* decodeNfcText(const uint8_t* data, uint16_t size, NfcText& output) {
    output.text[0] = '\0';
    if (size < 5 || data[0] != 0xD1 || data[1] != 1 || data[3] != 'T' ||
        size != static_cast<uint16_t>(4 + data[2])) return "ndef_record_invalid";
    const uint8_t status = data[4];
    const uint8_t language = status & 63;
    if ((status & 0xC0) || !language || data[2] <= 1 + language) return "ndef_text_invalid";
    for (uint8_t i = 0; i < language; ++i) {
        const uint8_t c = data[5 + i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return "ndef_language_invalid";
    }
    const uint16_t offset = 5 + language;
    const uint16_t length = size - offset;
    if (length >= sizeof(output.text) || !nfcUtf8(data + offset, length)) return "ndef_utf8_invalid";
    memcpy(output.text, data + offset, length);
    output.text[length] = '\0';
    return nullptr;
}

// Transport supplies the pinned ISO-DEP APDU exchange and cancellation state.
template<class Transport>
NfcText readNfcText(Transport& transport) {
    NfcText result;
    uint8_t rx[kNfcNdefCapacity + 2]{};
    const auto exchange = [&](const uint8_t* command, uint16_t size, uint16_t expected) {
        if (transport.cancelled()) { result.error = "nfc_cancelled"; return false; }
        uint16_t received = sizeof(rx);
        if (!transport.apdu(rx, received, command, size)) {
            result.error = transport.cancelled() ? "nfc_cancelled" : "nfc_transport_failed";
            return false;
        }
        if (transport.cancelled()) { result.error = "nfc_cancelled"; return false; }
        if (received < 2 || received > sizeof(rx)) { result.error = "apdu_length_invalid"; return false; }
        if (rx[received - 2] != 0x90 || rx[received - 1] != 0) {
            result.error = "apdu_status_failed";
            return false;
        }
        if (received != expected + 2) { result.error = "apdu_length_invalid"; return false; }
        return true;
    };
    const uint8_t selectApp[] = {0, 0xA4, 4, 0, 7, 0xD2, 0x76, 0, 0, 0x85, 1, 1, 0};
    const uint8_t selectCc[] = {0, 0xA4, 0, 0x0C, 2, 0xE1, 3};
    const uint8_t readCc[] = {0, 0xB0, 0, 0, 15};
    if (!exchange(selectApp, sizeof(selectApp), 0) ||
        !exchange(selectCc, sizeof(selectCc), 0) ||
        !exchange(readCc, sizeof(readCc), 15)) return result;
    const uint16_t mle = static_cast<uint16_t>((rx[3] << 8) | rx[4]);
    const uint16_t mlc = static_cast<uint16_t>((rx[5] << 8) | rx[6]);
    const uint16_t file = static_cast<uint16_t>((rx[9] << 8) | rx[10]);
    const uint16_t fileSize = static_cast<uint16_t>((rx[11] << 8) | rx[12]);
    if (rx[0] != 0 || rx[1] != 15 || rx[2] != 0x20 || mle < 15 || !mlc ||
        rx[7] != 4 || rx[8] != 6 || !file || file == 0xE102 || file == 0xE103 ||
        file == 0x3F00 || file == 0xFFFF || fileSize < 2 || rx[13] != 0 ||
        (rx[14] != 0 && rx[14] != 0xFF)) {
        result.error = "cc_unsupported";
        return result;
    }
    const uint8_t selectFile[] = {0, 0xA4, 0, 0x0C, 2, rx[9], rx[10]};
    const uint8_t readLength[] = {0, 0xB0, 0, 0, 2};
    if (!exchange(selectFile, sizeof(selectFile), 0) ||
        !exchange(readLength, sizeof(readLength), 2)) return result;
    const uint16_t length = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
    if (!length || length > kNfcNdefCapacity || length > fileSize - 2) {
        result.error = "ndef_length_invalid";
        return result;
    }
    uint8_t ndef[kNfcNdefCapacity]{};
    for (uint16_t offset = 0; offset < length;) {
        const uint16_t count = std::min<uint16_t>(length - offset, std::min<uint16_t>(mle, 256));
        const uint16_t position = offset + 2;
        const uint8_t read[] = {0, 0xB0, static_cast<uint8_t>(position >> 8),
            static_cast<uint8_t>(position), static_cast<uint8_t>(count)};
        if (!exchange(read, sizeof(read), count)) return result;
        memcpy(ndef + offset, rx, count);
        offset += count;
    }
    result.error = decodeNfcText(ndef, length, result);
    return result;
}
} // namespace lab
