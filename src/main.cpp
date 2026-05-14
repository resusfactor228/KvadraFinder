#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Supported extensions ────────────────────────────────────────────────────

static const std::set<std::string> AUDIO_EXT = {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma", ".opus", ".ape", ".alac"
};
static const std::set<std::string> VIDEO_EXT = {
    ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".mpg", ".mpeg", ".m4v",
    ".webm", ".3gp", ".vob", ".ogv"
};
static const std::set<std::string> IMAGE_EXT = {
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".tif",
    ".webp", ".svg", ".ico", ".heic", ".heif", ".raw", ".cr2", ".nef"
};

// ─── Shared state ─────────────────────────────────────────────────────────────

static std::mutex      g_mutex;
static std::string     g_json;
static std::atomic<bool> g_running{true};

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// ─── JSON builder ─────────────────────────────────────────────────────────────

static std::string build_json(const std::vector<std::string>& audio,
                               const std::vector<std::string>& video,
                               const std::vector<std::string>& images)
{
    auto array = [](const std::vector<std::string>& v) -> std::string {
        std::string s = "[\n";
        for (size_t i = 0; i < v.size(); ++i) {
            s += "    \"" + json_escape(v[i]) + "\"";
            if (i + 1 < v.size()) s += ',';
            s += '\n';
        }
        s += "  ]";
        return s;
    };

    return "{\n"
           "  \"audio\": "  + array(audio)  + ",\n"
           "  \"video\": "  + array(video)  + ",\n"
           "  \"images\": " + array(images) + "\n"
           "}\n";
}

// ─── Directory scanner ────────────────────────────────────────────────────────

static void do_scan(const fs::path& dir, const fs::path& output_file) {
    std::vector<std::string> audio, video, images;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                dir, fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;
            const std::string ext = to_lower(entry.path().extension().string());
            const std::string name = entry.path().filename().string();

            if (AUDIO_EXT.count(ext))      audio.push_back(name);
            else if (VIDEO_EXT.count(ext)) video.push_back(name);
            else if (IMAGE_EXT.count(ext)) images.push_back(name);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[scanner] " << e.what() << '\n';
    }

    std::sort(audio.begin(),  audio.end());
    std::sort(video.begin(),  video.end());
    std::sort(images.begin(), images.end());

    const std::string json = build_json(audio, video, images);

    // Write to file
    std::ofstream out(output_file);
    if (out) {
        out << json;
        std::cout << "[scanner] Written to " << output_file
                  << "  (audio=" << audio.size()
                  << " video=" << video.size()
                  << " images=" << images.size() << ")\n";
    } else {
        std::cerr << "[scanner] Cannot write " << output_file
                  << ": " << std::strerror(errno) << '\n';
    }

    // Publish for HTTP
    std::lock_guard<std::mutex> lk(g_mutex);
    g_json = json;
}

static void scanner_loop(const fs::path& dir,
                          const fs::path& output_file,
                          int interval_sec)
{
    while (g_running) {
        do_scan(dir, output_file);

        // Sleep in 1-second ticks so shutdown is prompt
        for (int i = 0; i < interval_sec && g_running; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ─── HTTP server ──────────────────────────────────────────────────────────────

static void handle_client(int fd) {
    char buf[4096]{};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        std::string_view req(buf, static_cast<size_t>(n));
        bool ok = req.substr(0, 16) == "GET /media_files";

        std::string body;
        std::string status;

        if (ok) {
            std::lock_guard<std::mutex> lk(g_mutex);
            body   = g_json;
            status = "200 OK";
        } else {
            body   = "Not Found\n";
            status = "404 Not Found";
        }

        const std::string content_type =
            ok ? "application/json" : "text/plain";

        std::string response =
            "HTTP/1.1 " + status + "\r\n"
            "Content-Type: " + content_type + "\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(fd, response.data(), response.size(), MSG_NOSIGNAL);
    }
    close(fd);
}

static void http_server(int port) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set accept timeout so we can check g_running periodically
    struct timeval tv{};
    tv.tv_sec = 1;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(srv);
        return;
    }
    if (listen(srv, 16) < 0) {
        perror("listen");
        close(srv);
        return;
    }

    std::cout << "[http] Listening on http://localhost:" << port
              << "/media_files\n";

    while (g_running) {
        sockaddr_in peer{};
        socklen_t   plen = sizeof(peer);
        int cli = accept(srv, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (cli < 0) {
            // EAGAIN / EWOULDBLOCK → timeout, loop back and check g_running
            continue;
        }
        std::thread(handle_client, cli).detach();
    }
    close(srv);
}

// ─── Signal handling ──────────────────────────────────────────────────────────

static void on_signal(int) { g_running = false; }

// ─── Usage ────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::cout <<
        "Usage: " << prog << " [OPTIONS]\n\n"
        "Options:\n"
        "  --dir <path>       Directory to scan          (default: $HOME)\n"
        "  --interval <sec>   Scan interval in seconds   (default: 60)\n"
        "  --port <num>       HTTP port                  (default: 1234)\n"
        "  --output <path>    Output JSON file path      (default: $HOME/.media_files)\n"
        "  -h, --help         Show this help\n\n"
        "Endpoints:\n"
        "  GET http://localhost:<port>/media_files\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* home = getenv("HOME");
    if (!home) home = ".";

    fs::path    scan_dir    = home;
    fs::path    output_file = fs::path(home) / ".media_files";
    int         interval    = 60;
    int         port        = 1234;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing argument for " << flag << '\n';
                std::exit(1);
            }
            return argv[++i];
        };

        if      (a == "--dir")      scan_dir    = need("--dir");
        else if (a == "--interval") interval    = std::stoi(need("--interval"));
        else if (a == "--port")     port        = std::stoi(need("--port"));
        else if (a == "--output")   output_file = need("--output");
        else if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << a << '\n'; return 1; }
    }

    if (!fs::is_directory(scan_dir)) {
        std::cerr << "Not a directory: " << scan_dir << '\n';
        return 1;
    }
    if (interval < 1) {
        std::cerr << "Interval must be >= 1 second\n";
        return 1;
    }
    if (port < 1 || port > 65535) {
        std::cerr << "Invalid port: " << port << '\n';
        return 1;
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::cout << "KvadraFinder\n"
              << "  Scan dir : " << scan_dir    << '\n'
              << "  Interval : " << interval    << "s\n"
              << "  Output   : " << output_file << '\n'
              << "  HTTP port: " << port        << '\n'
              ;

    std::thread scanner(scanner_loop, scan_dir, output_file, interval);
    http_server(port);   // runs in main thread until g_running = false

    g_running = false;
    scanner.join();

    return 0;
}
