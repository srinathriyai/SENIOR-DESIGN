#include <iostream>
#include <string>

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include "external/httplib.h"

// Minimal JSON escaping for prompt strings (handles \ and ")
static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

int main() {
    // llama-server default host/port
    httplib::Client cli("127.0.0.1", 8081);
    cli.set_read_timeout(60, 0);

    std::string prompt = "Say hi in one sentence.";

    std::string body =
        std::string("{\"prompt\":\"") + json_escape(prompt) +
        "\",\"n_predict\":64,\"temperature\":0.7}";

    auto res = cli.Post("/completion", body, "application/json");

    if (!res) {
        std::cerr << "Request failed: could not connect to llama-server at 127.0.0.1:8081\n";
        return 1;
    }

    std::cout << "HTTP " << res->status << "\n";
    std::cout << res->body << "\n";
    return 0;
}
