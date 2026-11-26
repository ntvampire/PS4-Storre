#include "orchestrator.h"
#include "http_client.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

static std::string detect_lan_ip() {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return "127.0.0.1";
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8");
    serv.sin_port = htons(53);
    if (connect(s, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return "127.0.0.1";
    }
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(s, (struct sockaddr*)&name, &namelen) < 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return "127.0.0.1";
    }
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf));
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    std::string res = buf;
    return res.empty() ? "127.0.0.1" : res;
}

QueueManager::QueueManager() = default;

QueueManager::~QueueManager() {
    stop();
}

void QueueManager::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&QueueManager::worker_loop, this);
}

void QueueManager::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool QueueManager::add_task(size_t catalog_index, const std::string& id, const std::string& cid, const std::string& title,
                            const std::string& torrent_hash, uint32_t size_mb) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            if (t.torrent_active) return false; // торрент уже в очереди/раздаётся
            // Игра была установлена ранее, торрент удалён — запускаем торрент заново
            t.torrent_active = true;
            t.torrent_hash = torrent_hash.empty() ? t.torrent_hash : torrent_hash;
            t.size_mb = size_mb > 0 ? size_mb : t.size_mb;
            t.state = TaskState::QUEUED;
            t.download_progress = 0.0f;
            t.install_progress = t.is_installed ? 100.0f : 0.0f;
            t.seed_ratio = 0.0f;
            t.bytes_uploaded = 0;
            t.bytes_downloaded = (t.size_mb > 0) ? ((uint64_t)t.size_mb * 1024ULL * 1024ULL) : 0ULL;
            t.speed = "0 KB/s";
            t.status_text = "Ожидание в очереди";
            return true;
        }
    }

    DownloadTask task;
    task.id = id;
    task.cid = cid;
    task.title = title;
    task.torrent_hash = torrent_hash;
    task.size_mb = size_mb;
    task.catalog_index = catalog_index;
    task.state = TaskState::QUEUED;
    task.torrent_active = true;
    task.is_installed = false;
    task.status_text = "Ожидание в очереди";
    if (size_mb > 0) {
        task.bytes_downloaded = (uint64_t)size_mb * 1024ULL * 1024ULL;
    }
    tasks_.push_back(task);
    return true;
}

bool QueueManager::has_task(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : tasks_) {
        if (t.id == id && (t.torrent_active || t.is_installed)) return true;
    }
    return false;
}

bool QueueManager::is_torrent_active(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : tasks_) {
        if (t.id == id && t.torrent_active) return true;
    }
    return false;
}

bool QueueManager::is_game_installed(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : tasks_) {
        if (t.id == id && t.is_installed) return true;
    }
    return false;
}

std::vector<DownloadTask> QueueManager::get_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_;
}

bool QueueManager::toggle_pause(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= tasks_.size()) return false;
    auto& task = tasks_[index];
    if (task.state == TaskState::DOWNLOADING || task.state == TaskState::SEEDING) {
        task.state = TaskState::PAUSED;
        task.speed = "0 KB/s";
        task.status_text = task.is_installed ? "Раздача приостановлена" : "Скачивание приостановлено";
        if (torrent_alive_) {
            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/pause_all", "", 500);
        }
        return true;
    } else if (task.state == TaskState::PAUSED) {
        task.state = task.is_installed ? TaskState::SEEDING : TaskState::DOWNLOADING;
        task.status_text = task.is_installed ? "Возобновление раздачи..." : "Возобновление скачивания...";
        if (torrent_alive_) {
            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/resume_all", "", 500);
        }
        return true;
    }
    return false;
}

bool QueueManager::pause_task(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= tasks_.size()) return false;
    auto& task = tasks_[index];
    if (task.state == TaskState::DOWNLOADING || task.state == TaskState::SEEDING) {
        task.state = TaskState::PAUSED;
        task.speed = "0 KB/s";
        task.status_text = task.is_installed ? "Раздача приостановлена" : "Скачивание приостановлено";
        if (torrent_alive_) {
            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/pause_all", "", 500);
        }
        return true;
    }
    return false;
}

bool QueueManager::resume_task(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= tasks_.size()) return false;
    auto& task = tasks_[index];
    if (task.state == TaskState::PAUSED) {
        task.state = task.is_installed ? TaskState::SEEDING : TaskState::DOWNLOADING;
        task.status_text = task.is_installed ? "Возобновление раздачи..." : "Возобновление скачивания...";
        if (torrent_alive_) {
            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/resume_all", "", 500);
        }
        return true;
    }
    return false;
}

bool QueueManager::remove_torrent(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= tasks_.size()) return false;
    auto& task = tasks_[index];
    if (torrent_alive_) {
        std::string hash_or_id = task.torrent_hash.empty() ? task.id : task.torrent_hash;
        HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/delete?hash=" + hash_or_id, "", 500);
        HttpClient::post("127.0.0.1", STORRE_INSTALLER_PORT, "/delete_files", "{\"paths\":[\"/data/pkg/" + task.id + ".pkg\"]}", 500);
    }
    if (task.is_installed) {
        // Игра остаётся установленной на консоли — убираем только торрент из «Загрузки»
        task.torrent_active = false;
        task.state = TaskState::COMPLETED;
        task.speed = "0 KB/s";
        task.status_text = "Установлено (торрент удалён)";
    } else {
        tasks_.erase(tasks_.begin() + index);
    }
    return true;
}

bool QueueManager::uninstall_game(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= tasks_.size()) return false;
    auto& task = tasks_[index];
    task.is_installed = false;

    // Примечание: торрент и .pkg файлы в «Загрузки» НЕ трогаем —
    // торрент остаётся в очереди до срабатывания правил раздачи или ручного удаления.
    if (!task.torrent_active) {
        tasks_.erase(tasks_.begin() + index);
    }
    return true;
}

std::string QueueManager::get_web_ui_url() const {
    return "http://" + get_lan_ip() + ":" + std::to_string(STORRE_TORRENT_PORT) + "/";
}

std::string QueueManager::get_lan_ip() const {
    return lan_ip_.empty() ? "127.0.0.1" : lan_ip_;
}

TorrentConfig QueueManager::get_torrent_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return torrent_cfg_;
}

int QueueManager::get_seeding_speed_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int kb = torrent_cfg_.upload_kb;
    if (kb <= 32) return 0;
    if (kb <= 128) return 1;
    if (kb <= 256) return 2;
    if (kb <= 1024) return 3;
    return 4;
}

void QueueManager::apply_max_parallel(int max_p) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.max_parallel = max_p;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?max_parallel=" + std::to_string(max_p), 500);
    }
}

void QueueManager::apply_seeding_limit(int speed_idx) {
    int kb = 32;
    if (speed_idx == 1) kb = 128;
    else if (speed_idx == 2) kb = 256;
    else if (speed_idx >= 3) kb = 1024;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.upload_kb = kb;
        torrent_cfg_.upload_enabled = true;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?upload=1&upload_kb=" + std::to_string(kb), 500);
    }
}

void QueueManager::apply_storage_location(int storage_idx) {
    std::string loc = (storage_idx == 0) ? "internal" : "usb";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.save_to = loc;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?save_to=" + loc, 500);
    }
}

void QueueManager::apply_autostart(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.autostart = enabled;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?autostart=" + std::string(enabled ? "1" : "0"), 500);
    }
}

void QueueManager::apply_resume_session(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.resume_session = enabled;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?resume_session=" + std::string(enabled ? "1" : "0"), 500);
    }
}

void QueueManager::apply_listen_port(int port) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        torrent_cfg_.listen_port = port;
    }
    if (torrent_alive_) {
        HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/set?listen_port=" + std::to_string(port), 500);
    }
}

static std::string find_daemon_elf(const std::string& name) {
    std::vector<std::string> paths = {
        "/app0/assets/misc/" + name,
        "/app0/assets/daemons/" + name,
        "assets/misc/" + name,
        "assets/daemons/" + name,
        "../assets/misc/" + name,
        "../assets/daemons/" + name,
        "ps4_storre/assets/misc/" + name,
        "ps4_storre/assets/daemons/" + name,
        "d:/Projects/PS4/ps4_storre/assets/misc/" + name,
        "d:/Projects/PS4/ps4_storre/assets/daemons/" + name
    };
    for (const auto& p : paths) {
        FILE* f = fopen(p.c_str(), "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return "";
}

static bool send_elf_to_binloader(const std::string& elf_path, int port = 9090) {
    if (elf_path.empty()) return false;
    FILE* f = fopen(elf_path.c_str(), "rb");
    if (!f) return false;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        fclose(f);
        return false;
    }

#ifdef _WIN32
    DWORD tv = 2000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv.sin_port = htons(port);

    if (connect(s, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        fclose(f);
        return false;
    }

    char buffer[65536];
    size_t bytes_read = 0;
    bool success = true;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        size_t total_sent = 0;
        while (total_sent < bytes_read) {
            int sent = send(s, buffer + total_sent, (int)(bytes_read - total_sent), 0);
            if (sent <= 0) {
                success = false;
                break;
            }
            total_sent += sent;
        }
        if (!success) break;
    }

    fclose(f);
#ifdef _WIN32
    shutdown(s, SD_BOTH);
    closesocket(s);
#else
    shutdown(s, SHUT_RDWR);
    close(s);
#endif

    std::cout << "[Orchestrator] Dispatched " << elf_path << " to GoldHEN BinLoader (port " << port << "): "
              << (success ? "OK" : "FAILED") << std::endl;
    return success;
}

void QueueManager::worker_loop() {
    lan_ip_ = detect_lan_ip();
    int cfg_poll_counter = 0;
    auto last_torrent_attempt = std::chrono::steady_clock::time_point{};
    auto last_installer_attempt = std::chrono::steady_clock::time_point{};
    auto last_any_binloader = std::chrono::steady_clock::time_point{};
    int torrent_attempts = 0;
    int installer_attempts = 0;
    constexpr int MAX_BINLOADER_ATTEMPTS = 2;

    while (running_) {
        // 1. Check health of background daemons
        auto resp_t = HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/status", 300);
        torrent_alive_ = resp_t.ok;
#if !defined(_WIN32)
        if (!torrent_alive_ && torrent_attempts < MAX_BINLOADER_ATTEMPTS) {
            auto now = std::chrono::steady_clock::now();
            bool cooldown_own = (last_torrent_attempt == std::chrono::steady_clock::time_point{}) ||
                                (std::chrono::duration_cast<std::chrono::seconds>(now - last_torrent_attempt).count() >= 15);
            bool cooldown_global = (last_any_binloader == std::chrono::steady_clock::time_point{}) ||
                                   (std::chrono::duration_cast<std::chrono::seconds>(now - last_any_binloader).count() >= 4);
            if (cooldown_own && cooldown_global) {
                last_torrent_attempt = now;
                last_any_binloader = now;
                torrent_attempts++;
                std::string elf = find_daemon_elf("storre_torrentd.elf");
                if (!elf.empty()) {
                    send_elf_to_binloader(elf, 9090);
                }
            }
        }
#else
        (void)last_torrent_attempt;
        (void)torrent_attempts;
#endif
        if (resp_t.ok) {
            size_t pos = resp_t.body.find("\"net_ip\":\"");
            if (pos != std::string::npos) {
                size_t start = pos + 10;
                size_t end = resp_t.body.find("\"", start);
                if (end != std::string::npos) {
                    std::string parsed = resp_t.body.substr(start, end - start);
                    if (!parsed.empty() && parsed != "127.0.0.1") {
                        lan_ip_ = parsed;
                    }
                }
            }

            // Sync settings from storre_torrentd daemon every ~3s
            if (++cfg_poll_counter % 12 == 0) {
                auto resp_cfg = HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/api/config", 300);
                if (resp_cfg.ok) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    size_t p_mp = resp_cfg.body.find("\"max_parallel\":");
                    if (p_mp != std::string::npos) {
                        int mp = atoi(resp_cfg.body.c_str() + p_mp + 15);
                        if (mp >= 1) torrent_cfg_.max_parallel = mp;
                    }
                    size_t p_lp = resp_cfg.body.find("\"listen_port\":");
                    if (p_lp != std::string::npos) {
                        torrent_cfg_.listen_port = atoi(resp_cfg.body.c_str() + p_lp + 14);
                    }
                    size_t p_as = resp_cfg.body.find("\"autostart\":");
                    if (p_as != std::string::npos) {
                        torrent_cfg_.autostart = (resp_cfg.body.compare(p_as + 12, 4, "true") == 0);
                    }
                    size_t p_rs = resp_cfg.body.find("\"resume_session\":");
                    if (p_rs != std::string::npos) {
                        torrent_cfg_.resume_session = (resp_cfg.body.compare(p_rs + 17, 4, "true") == 0);
                    }
                    size_t p_ul = resp_cfg.body.find("\"upload\":");
                    if (p_ul != std::string::npos) {
                        torrent_cfg_.upload_enabled = (resp_cfg.body.compare(p_ul + 9, 4, "true") == 0);
                    }
                    size_t p_ukb = resp_cfg.body.find("\"upload_kb\":");
                    if (p_ukb != std::string::npos) {
                        int ukb = atoi(resp_cfg.body.c_str() + p_ukb + 12);
                        if (ukb > 0) torrent_cfg_.upload_kb = ukb;
                    }
                    size_t p_sv = resp_cfg.body.find("\"save_to\":\"");
                    if (p_sv != std::string::npos) {
                        size_t end_sv = resp_cfg.body.find("\"", p_sv + 11);
                        if (end_sv != std::string::npos) {
                            torrent_cfg_.save_to = resp_cfg.body.substr(p_sv + 11, end_sv - (p_sv + 11));
                        }
                    }
                }
            }
        }
        if (lan_ip_ == "127.0.0.1") {
            lan_ip_ = detect_lan_ip();
        }

        auto resp_i = HttpClient::get("127.0.0.1", STORRE_INSTALLER_PORT, "/status", 300);
        installer_alive_ = resp_i.ok;
#if !defined(_WIN32)
        if (!installer_alive_ && installer_attempts < MAX_BINLOADER_ATTEMPTS) {
            auto now = std::chrono::steady_clock::now();
            bool cooldown_own = (last_installer_attempt == std::chrono::steady_clock::time_point{}) ||
                                (std::chrono::duration_cast<std::chrono::seconds>(now - last_installer_attempt).count() >= 15);
            bool cooldown_global = (last_any_binloader == std::chrono::steady_clock::time_point{}) ||
                                   (std::chrono::duration_cast<std::chrono::seconds>(now - last_any_binloader).count() >= 4);
            if (cooldown_own && cooldown_global) {
                last_installer_attempt = now;
                last_any_binloader = now;
                installer_attempts++;
                std::string elf = find_daemon_elf("storre_installerd.elf");
                if (!elf.empty()) {
                    send_elf_to_binloader(elf, 9090);
                }
            }
        }
#else
        (void)last_installer_attempt;
        (void)last_any_binloader;
        (void)installer_attempts;
        (void)MAX_BINLOADER_ATTEMPTS;
#endif

        // 2. Process tasks with mutex lock
        {
            std::lock_guard<std::mutex> lock(mutex_);

            bool busy_download_or_install = false;
            time_t now = time(nullptr);

            for (auto& task : tasks_) {
                if (!task.torrent_active || task.state == TaskState::COMPLETED || task.state == TaskState::ERROR_STATE || task.state == TaskState::PAUSED) {
                    continue;
                }

                // 1. Обслуживание фоновой раздачи (SEEDING) для уже установленных игр
                if (task.state == TaskState::SEEDING) {
                    if (torrent_alive_) {
                        // Реальный PS4: опрос 8788
                        auto st = HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/status", 500);
                        // Вычисляем ratio
                        if (task.bytes_downloaded > 0 && task.bytes_uploaded > 0) {
                            task.seed_ratio = (float)task.bytes_uploaded / (float)task.bytes_downloaded;
                        }
                    } else {
                        // Симулятор для ПК: используем реальную настройку upload_kb и 250мс интервал тика
                        if (task.bytes_downloaded == 0) {
                            task.bytes_downloaded = (uint64_t)std::max<uint32_t>(task.size_mb, 1000) * 1024ULL * 1024ULL;
                        }
                        if (torrent_cfg_.upload_enabled) {
                            int eff_kb = std::max(torrent_cfg_.upload_kb, 32);
                            uint64_t bytes_per_tick = ((uint64_t)eff_kb * 1024ULL) / 4ULL; // 4 тика в секунду (250мс)
                            task.bytes_uploaded += bytes_per_tick;
                            task.speed = (eff_kb >= 1024) ? "1.0 MB/s" : (std::to_string(eff_kb) + " KB/s");
                        } else {
                            task.speed = "0 KB/s";
                        }
                        task.seed_ratio = (float)task.bytes_uploaded / (float)task.bytes_downloaded;
                    }

                    int elapsed_days = (task.install_time > 0) ? (int)((now - task.install_time) / 86400) : 0;
                    char r_buf[96];
                    snprintf(r_buf, sizeof(r_buf), "%s Раздача: %.2fx (%s) | %d/7 дн.",
                             task.is_installed ? "Установлено!" : "Удалено с PS4!",
                             task.seed_ratio, task.speed.c_str(), elapsed_days);
                    task.status_text = r_buf;

                    // Критерий завершения: Ratio >= 2.0 ИЛИ прошло 7 дней раздачи
                    if (task.seed_ratio >= 2.0f || (task.install_time > 0 && (now - task.install_time) >= 7 * 86400)) {
                        task.torrent_active = false;
                        task.state = TaskState::COMPLETED;
                        task.speed = "0 KB/s";
                        task.status_text = "Завершено! Раздача выполнена. Файлы PKG очищены с диска.";
                        if (torrent_alive_) {
                            std::string hash_or_id = task.torrent_hash.empty() ? task.id : task.torrent_hash;
                            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/delete?hash=" + hash_or_id, "", 500);
                            HttpClient::post("127.0.0.1", STORRE_INSTALLER_PORT, "/delete_files", "{\"paths\":[\"/data/pkg/" + task.id + ".pkg\"]}", 500);
                        }
                    }
                    continue; // раздача не блокирует очередь скачивания следующих игр
                }

                // 2. Очередь скачивания и установки (строго по одной игре для защиты диска)
                if (busy_download_or_install) {
                    continue;
                }

                if (task.state == TaskState::QUEUED) {
                    task.state = TaskState::DOWNLOADING;
                    task.status_text = "Скачивание торрента...";
                    busy_download_or_install = true;

                    if (torrent_alive_) {
                        std::string magnet = task.torrent_hash.empty()
                            ? "http://bt.t-ru.org/ann?magnet"
                            : ("magnet:?xt=urn:btih:" + task.torrent_hash + "&tr=http%3A%2F%2Fbt.t-ru.org%2Fann%3Fmagnet");
                        HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/add_magnet", "{\"magnet\":\"" + magnet + "\"}", 1000);
                    }
                }
                else if (task.state == TaskState::DOWNLOADING) {
                    busy_download_or_install = true;
                    if (torrent_alive_) {
                        auto st = HttpClient::get("127.0.0.1", STORRE_TORRENT_PORT, "/status", 500);
                        if (task.download_progress >= 100.0f) {
                            // Пауза торрента на время записи PKG на HDD
                            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/pause_all", "", 1000);

                            task.state = TaskState::INSTALLING;
                            task.status_text = "Торренты на паузе. Идет установка bgft...";
                            HttpClient::get("127.0.0.1", STORRE_INSTALLER_PORT, "/install?path=/data/pkg/" + task.id + ".pkg", 1000);
                        }
                    } else {
                        // Симулятор ПК
                        task.download_progress += 2.5f;
                        task.speed = "18.4 MB/s";
                        task.status_text = "Скачивание: " + std::to_string((int)task.download_progress) + "% (" + task.speed + ")";

                        if (task.download_progress >= 100.0f) {
                            task.download_progress = 100.0f;
                            task.state = TaskState::INSTALLING;
                            task.speed = "0 KB/s";
                            task.status_text = "Диск свободен. Установка пакета (bgft)...";
                        }
                    }
                }
                else if (task.state == TaskState::INSTALLING) {
                    busy_download_or_install = true;
                    if (installer_alive_) {
                        auto st = HttpClient::get("127.0.0.1", STORRE_INSTALLER_PORT, "/status", 500);
                        if (task.install_progress >= 100.0f) {
                            task.install_progress = 100.0f;
                            task.is_installed = true;
                            task.install_time = now;
                            task.state = TaskState::SEEDING;
                            task.status_text = "Установлено! Старт сидирования (Цель: 2.0x / 7 дн.)";
                            // Возобновляем раздачу
                            HttpClient::post("127.0.0.1", STORRE_TORRENT_PORT, "/api/resume_all", "", 1000);
                        }
                    } else {
                        // Симулятор ПК
                        task.install_progress += 4.0f;
                        task.status_text = "Установка в систему: " + std::to_string((int)task.install_progress) + "%";

                        if (task.install_progress >= 100.0f) {
                            task.install_progress = 100.0f;
                            task.is_installed = true;
                            task.install_time = now;
                            task.state = TaskState::SEEDING;
                            task.status_text = "Установлено! Старт сидирования (Цель: 2.0x / 7 дн.)";
                        }
                    }
                }
            }

            // Очистка записей, у которых уже нет ни активного торрента, ни установленной игры
            tasks_.erase(
                std::remove_if(tasks_.begin(), tasks_.end(),
                    [](const DownloadTask& t) { return !t.torrent_active && !t.is_installed; }),
                tasks_.end()
            );
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
