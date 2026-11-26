#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <SDL.h>

struct GameItem {
    std::string id;
    std::string cid;
    std::string title;
    uint32_t cover_offset = 0;
    uint32_t cover_size = 0;
    uint32_t desc_offset = 0;
    uint32_t desc_size = 0;

    // Torrent & release metadata (Rutracker merge)
    std::string torrent_hash;   // "th"
    uint32_t size_mb = 0;       // "ts"
    uint16_t year = 0;          // "ty"
    bool has_rus_ui = false;    // "tu"
    bool has_rus_voice = false; // "tv"
    bool has_dlc = false;       // "td"
    bool has_fix = false;       // "tf"
    bool is_backport = false;   // "tb"
};

class CatalogManager {
public:
    CatalogManager();
    ~CatalogManager();

    bool init(const std::string& json_path, const std::string& pak_path);
    void close();

    size_t get_count() const { return games_.size(); }
    const GameItem* get_game(size_t index) const;

    // Media & details on-demand
    std::string get_description(size_t index);
    SDL_Texture* get_cover_texture(SDL_Renderer* renderer, size_t index);

    // Filter & Search
    std::vector<size_t> search(const std::string& query) const;
    const std::vector<size_t>& get_items_by_letter(char letter) const;
    size_t find_first_by_letter(char letter, const std::vector<size_t>& active_subset) const;

private:
    std::vector<GameItem> games_;
    FILE* pak_file_ = nullptr;
    std::string pak_path_;

    // Letter buckets for instant O(1) letter switching
    std::vector<size_t> all_indices_;
    std::vector<size_t> num_indices_; // '#'
    std::vector<size_t> alpha_indices_[26]; // 'A'..'Z'

    // Simple Texture Cache (LRU)
    static constexpr size_t MAX_CACHE_SIZE = 48;
    struct CacheNode {
        size_t index;
        SDL_Texture* texture;
    };
    std::list<CacheNode> lru_list_;
    std::unordered_map<size_t, std::list<CacheNode>::iterator> texture_cache_;

    void clear_cache();
};
