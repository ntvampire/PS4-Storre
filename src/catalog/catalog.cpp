#include "catalog.h"
#include <cJSON.h>
#include <SDL_image.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>

CatalogManager::CatalogManager() = default;

CatalogManager::~CatalogManager() {
    close();
}

void CatalogManager::clear_cache() {
    for (auto& node : lru_list_) {
        if (node.texture) {
            SDL_DestroyTexture(node.texture);
        }
    }
    lru_list_.clear();
    texture_cache_.clear();
}

void CatalogManager::close() {
    clear_cache();
    if (pak_file_) {
        fclose(pak_file_);
        pak_file_ = nullptr;
    }
    games_.clear();
    all_indices_.clear();
    num_indices_.clear();
    for (int i = 0; i < 26; ++i) {
        alpha_indices_[i].clear();
    }
}

static char get_clean_first_letter(const std::string& title) {
    size_t i = 0;
    while (i < title.size()) {
        unsigned char c = (unsigned char)title[i];
        if (c <= ' ' || c == '"' || c == '\'' || c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '.' || c == '#' || c == '!' || c == '?' || c == '-' || c == '+' || c == '>' || c == '<' ||
            c == '_' || c == '~' || c == '@' || c == '$' || c == '%' || c == '^' || c == '&' || c == '*') {
            i++;
        } else {
            break;
        }
    }
    if (i >= title.size()) return '#';

    unsigned char c = (unsigned char)title[i];
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return (char)c;
    return '#';
}

static FILE* try_open_file(const std::string& primary_path, const std::vector<std::string>& fallbacks, const char* mode) {
    FILE* f = fopen(primary_path.c_str(), mode);
    if (f) return f;
    for (const auto& fb : fallbacks) {
        f = fopen(fb.c_str(), mode);
        if (f) return f;
    }
    return nullptr;
}

bool CatalogManager::init(const std::string& json_path, const std::string& pak_path) {
    close();

    std::vector<std::string> json_fallbacks = {
        "assets/misc/catalog.json",
        "../build_pkgtool_store/assets/misc/catalog.json",
        "d:/Projects/PS4/build_pkgtool_store/assets/misc/catalog.json",
        "/app0/assets/misc/catalog.json"
    };

    FILE* jf = try_open_file(json_path, json_fallbacks, "rb");
    if (!jf) {
        std::cerr << "Failed to open catalog.json\n";
        return false;
    }

    fseek(jf, 0, SEEK_END);
    long fsize = ftell(jf);
    fseek(jf, 0, SEEK_SET);

    std::vector<char> json_buf(fsize + 1, 0);
    fread(json_buf.data(), 1, fsize, jf);
    fclose(jf);

    cJSON* root = cJSON_Parse(json_buf.data());
    if (!root) {
        std::cerr << "cJSON parsing error for catalog.json\n";
        return false;
    }

    cJSON* games_array = cJSON_GetObjectItem(root, "games");
    if (!games_array || !cJSON_IsArray(games_array)) {
        std::cerr << "Invalid catalog.json schema: 'games' array not found\n";
        cJSON_Delete(root);
        return false;
    }

    int count = cJSON_GetArraySize(games_array);
    std::vector<GameItem> parsed;
    parsed.reserve(count);
    bool has_any_torrent = false;

    for (int i = 0; i < count; ++i) {
        cJSON* g = cJSON_GetArrayItem(games_array, i);
        if (!g) continue;

        GameItem item;
        cJSON* j_id = cJSON_GetObjectItem(g, "id");
        cJSON* j_cid = cJSON_GetObjectItem(g, "cid");
        cJSON* j_n = cJSON_GetObjectItem(g, "n");
        cJSON* j_c = cJSON_GetObjectItem(g, "c");
        cJSON* j_d = cJSON_GetObjectItem(g, "d");

        if (j_id && j_id->valuestring) item.id = j_id->valuestring;
        if (j_cid && j_cid->valuestring) item.cid = j_cid->valuestring;
        if (j_n && j_n->valuestring) item.title = j_n->valuestring;

        if (j_c && cJSON_GetArraySize(j_c) >= 2) {
            item.cover_offset = (uint32_t)cJSON_GetArrayItem(j_c, 0)->valuedouble;
            item.cover_size = (uint32_t)cJSON_GetArrayItem(j_c, 1)->valuedouble;
        }

        if (j_d && cJSON_GetArraySize(j_d) >= 2) {
            item.desc_offset = (uint32_t)cJSON_GetArrayItem(j_d, 0)->valuedouble;
            item.desc_size = (uint32_t)cJSON_GetArrayItem(j_d, 1)->valuedouble;
        }

        cJSON* j_th = cJSON_GetObjectItem(g, "th");
        cJSON* j_ts = cJSON_GetObjectItem(g, "ts");
        cJSON* j_ty = cJSON_GetObjectItem(g, "ty");
        cJSON* j_tu = cJSON_GetObjectItem(g, "tu");
        cJSON* j_tv = cJSON_GetObjectItem(g, "tv");
        cJSON* j_td = cJSON_GetObjectItem(g, "td");
        cJSON* j_tf = cJSON_GetObjectItem(g, "tf");
        cJSON* j_tb = cJSON_GetObjectItem(g, "tb");

        if (j_th && j_th->valuestring) item.torrent_hash = j_th->valuestring;
        if (!item.torrent_hash.empty()) has_any_torrent = true;
        if (j_ts && cJSON_IsNumber(j_ts)) item.size_mb = (uint32_t)j_ts->valuedouble;
        if (j_ty && cJSON_IsNumber(j_ty)) item.year = (uint16_t)j_ty->valuedouble;
        if (j_tu && cJSON_IsNumber(j_tu)) item.has_rus_ui = (j_tu->valueint != 0);
        if (j_tv && cJSON_IsNumber(j_tv)) item.has_rus_voice = (j_tv->valueint != 0);
        if (j_td && cJSON_IsNumber(j_td)) item.has_dlc = (j_td->valueint != 0);
        if (j_tf && cJSON_IsNumber(j_tf)) item.has_fix = (j_tf->valueint != 0);
        if (j_tb && cJSON_IsNumber(j_tb)) item.is_backport = (j_tb->valueint != 0);

        parsed.push_back(std::move(item));
    }

    cJSON_Delete(root);

    games_.clear();
    if (has_any_torrent) {
        games_.reserve(parsed.size());
        for (auto& item : parsed) {
            if (!item.torrent_hash.empty()) {
                games_.push_back(std::move(item));
            }
        }
    } else {
        games_ = std::move(parsed);
    }

    std::cout << "Catalog loaded: " << games_.size() << " games parsed"
              << (has_any_torrent ? " (filtered to downloadable releases)." : " (unfiltered).") << "\n";

    all_indices_.clear();
    num_indices_.clear();
    for (int i = 0; i < 26; ++i) {
        alpha_indices_[i].clear();
    }

    all_indices_.resize(games_.size());
    for (size_t i = 0; i < games_.size(); ++i) {
        all_indices_[i] = i;
        char cat = get_clean_first_letter(games_[i].title);
        if (cat >= 'A' && cat <= 'Z') {
            alpha_indices_[cat - 'A'].push_back(i);
        } else {
            num_indices_.push_back(i);
        }
    }

    // Open catalog.pak
    std::vector<std::string> pak_fallbacks = {
        "assets/misc/catalog.pak",
        "../build_pkgtool_store/assets/misc/catalog.pak",
        "d:/Projects/PS4/build_pkgtool_store/assets/misc/catalog.pak",
        "/app0/assets/misc/catalog.pak"
    };

    pak_file_ = try_open_file(pak_path, pak_fallbacks, "rb");
    if (!pak_file_) {
        std::cerr << "Warning: catalog.pak not found. Covers will be unavailable.\n";
    } else {
        std::cout << "Catalog media archive opened successfully.\n";
    }

    return true;
}

const GameItem* CatalogManager::get_game(size_t index) const {
    if (index >= games_.size()) return nullptr;
    return &games_[index];
}

std::string CatalogManager::get_description(size_t index) {
    if (!pak_file_ || index >= games_.size()) return "";
    const auto& g = games_[index];
    if (g.desc_size == 0) return "";

    fseek(pak_file_, g.desc_offset, SEEK_SET);
    std::vector<char> buf(g.desc_size + 1, 0);
    fread(buf.data(), 1, g.desc_size, pak_file_);
    return std::string(buf.data());
}

SDL_Texture* CatalogManager::get_cover_texture(SDL_Renderer* renderer, size_t index) {
    if (!pak_file_ || !renderer || index >= games_.size()) return nullptr;

    auto it = texture_cache_.find(index);
    if (it != texture_cache_.end()) {
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return it->second->texture;
    }

    const auto& g = games_[index];
    if (g.cover_size == 0) return nullptr;

    fseek(pak_file_, g.cover_offset, SEEK_SET);
    std::vector<uint8_t> buf(g.cover_size);
    if (fread(buf.data(), 1, g.cover_size, pak_file_) != g.cover_size) {
        return nullptr;
    }

    SDL_RWops* rw = SDL_RWFromMem(buf.data(), (int)buf.size());
    if (!rw) return nullptr;

    SDL_Surface* surf = IMG_Load_RW(rw, 1);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);

    if (!tex) return nullptr;

    if (lru_list_.size() >= MAX_CACHE_SIZE) {
        auto oldest = lru_list_.back();
        if (oldest.texture) {
            SDL_DestroyTexture(oldest.texture);
        }
        texture_cache_.erase(oldest.index);
        lru_list_.pop_back();
    }

    lru_list_.push_front({ index, tex });
    texture_cache_[index] = lru_list_.begin();

    return tex;
}

static std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = (char)std::tolower((unsigned char)c);
    }
    return s;
}

std::vector<size_t> CatalogManager::search(const std::string& query) const {
    std::vector<size_t> results;
    if (query.empty()) {
        results.resize(games_.size());
        for (size_t i = 0; i < games_.size(); ++i) results[i] = i;
        return results;
    }

    std::string lquery = to_lower_ascii(query);
    results.reserve(256);

    for (size_t i = 0; i < games_.size(); ++i) {
        std::string ltitle = to_lower_ascii(games_[i].title);
        std::string lid = to_lower_ascii(games_[i].id);

        if (ltitle.find(lquery) != std::string::npos || lid.find(lquery) != std::string::npos) {
            results.push_back(i);
        }
    }

    return results;
}

const std::vector<size_t>& CatalogManager::get_items_by_letter(char letter) const {
    char u = (char)std::toupper((unsigned char)letter);
    if (u >= 'A' && u <= 'Z') {
        return alpha_indices_[u - 'A'];
    }
    if (u == '#') {
        return num_indices_;
    }
    return all_indices_;
}

size_t CatalogManager::find_first_by_letter(char letter, const std::vector<size_t>& active_subset) const {
    char target = (char)std::toupper((unsigned char)letter);
    size_t fallback_pos = 0;
    bool has_fallback = false;

    for (size_t pos = 0; pos < active_subset.size(); ++pos) {
        size_t idx = active_subset[pos];
        if (idx >= games_.size()) continue;

        char c = get_clean_first_letter(games_[idx].title);
        if (target == '*' || target == '#') {
            if (c == '#') return pos;
        } else if (c == target) {
            return pos;
        } else if (!has_fallback && c > target && c != '#') {
            fallback_pos = pos;
            has_fallback = true;
        }
    }

    return fallback_pos;
}
