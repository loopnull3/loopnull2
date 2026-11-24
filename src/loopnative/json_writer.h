// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace LoopNative {

/// A compact JSON writer for LoopNative's reports (no reading, no pretty printing).
class JsonWriter {
public:
    JsonWriter& BeginObject() {
        Separator();
        out += '{';
        first.push_back(true);
        return *this;
    }
    JsonWriter& EndObject() {
        out += '}';
        first.pop_back();
        return *this;
    }
    JsonWriter& BeginArray() {
        Separator();
        out += '[';
        first.push_back(true);
        return *this;
    }
    JsonWriter& EndArray() {
        out += ']';
        first.pop_back();
        return *this;
    }
    JsonWriter& Key(std::string_view key) {
        Separator();
        Quoted(key);
        out += ':';
        after_key = true;
        return *this;
    }
    JsonWriter& String(std::string_view value) {
        Separator();
        Quoted(value);
        return *this;
    }
    JsonWriter& Number(std::uint64_t value) {
        Separator();
        out += std::to_string(value);
        return *this;
    }
    JsonWriter& Number(std::int64_t value) {
        Separator();
        out += std::to_string(value);
        return *this;
    }
    JsonWriter& Number(int value) {
        return Number(static_cast<std::int64_t>(value));
    }
    JsonWriter& Number(std::uint32_t value) {
        return Number(static_cast<std::uint64_t>(value));
    }
    JsonWriter& Number(double value) {
        Separator();
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        out += buffer;
        return *this;
    }
    JsonWriter& Bool(bool value) {
        Separator();
        out += value ? "true" : "false";
        return *this;
    }
    JsonWriter& Null() {
        Separator();
        out += "null";
        return *this;
    }

    [[nodiscard]] const std::string& Str() const {
        return out;
    }

private:
    void Separator() {
        if (after_key) {
            after_key = false;
            return;
        }
        if (!first.empty()) {
            if (!first.back()) {
                out += ',';
            }
            first.back() = false;
        }
    }

    void Quoted(std::string_view text) {
        out += '"';
        for (const char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += c;
                }
                break;
            }
        }
        out += '"';
    }

    std::string out;
    std::vector<bool> first;
    bool after_key = false;
};

} // namespace LoopNative
