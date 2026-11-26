#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

constexpr int STORRE_TORRENT_PORT = 8788;
constexpr int STORRE_INSTALLER_PORT = 8285;

enum class TaskState {
    QUEUED,
    DOWNLOADING,
    PAUSED,
    INSTALLING,
    SEEDING,        // Установлено, раздаётся в фоне (цель: Ratio 2.0 или 7 дней)
    COMPLETED,      // Завершено (лимит раздачи выполнен, файлы PKG удалены)
    ERROR_STATE
};

struct DownloadTask {
    std::string id;
    std::string cid;
    std::string title;
    std::string torrent_hash;
    uint32_t size_mb = 0;
    size_t catalog_index = 0;
    TaskState state = TaskState::QUEUED;
    float download_progress = 0.0f;
    std::string speed = "0 KB/s";
    float install_progress = 0.0f;
    std::string status_text = "В очереди";

    // Раздельное владение жизненным циклом торрента и установленной игры
    bool torrent_active = true;   // отображается во вкладке «Загрузки»
    bool is_installed = false;    // отображается во вкладке «Установленные»
    time_t install_time = 0;
    float seed_ratio = 0.0f;
    uint64_t bytes_downloaded = 0;
    uint64_t bytes_uploaded = 0;
};

struct TorrentConfig {
    int max_parallel = 1;
    int upload_kb = 32;
    bool upload_enabled = true;
    std::string save_to = "internal";
    int listen_port = 0;
    bool autostart = true;
    bool resume_session = true;
};

class QueueManager {
public:
    QueueManager();
    ~QueueManager();

    void start();
    void stop();

    bool add_task(size_t catalog_index, const std::string& id, const std::string& cid, const std::string& title,
                  const std::string& torrent_hash = "", uint32_t size_mb = 0);
    bool has_task(const std::string& id);
    bool is_torrent_active(const std::string& id);
    bool is_game_installed(const std::string& id);
    std::vector<DownloadTask> get_tasks();

    bool toggle_pause(size_t index);
    bool pause_task(size_t index);
    bool resume_task(size_t index);
    bool remove_torrent(size_t index);
    bool uninstall_game(size_t index);

    bool is_torrent_service_alive() const { return torrent_alive_; }
    bool is_installer_service_alive() const { return installer_alive_; }

    std::string get_web_ui_url() const;
    std::string get_lan_ip() const;

    TorrentConfig get_torrent_config() const;
    int get_seeding_speed_index() const;
    void apply_max_parallel(int max_p);
    void apply_seeding_limit(int speed_idx);
    void apply_storage_location(int storage_idx);
    void apply_autostart(bool enabled);
    void apply_resume_session(bool enabled);
    void apply_listen_port(int port);

private:
    std::vector<DownloadTask> tasks_;
    mutable std::mutex mutex_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    std::atomic<bool> torrent_alive_{false};
    std::atomic<bool> installer_alive_{false};
    std::string lan_ip_{"127.0.0.1"};
    TorrentConfig torrent_cfg_;

    void worker_loop();
};
