#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "catalog/catalog.h"
#include "network/orchestrator.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif

constexpr int CANVAS_WIDTH = 1920;
constexpr int CANVAS_HEIGHT = 1080;

// Grid layout parameters
constexpr int GRID_COLS = 6;
constexpr int GRID_ROWS = 2;
constexpr int CARD_WIDTH = 250;
constexpr int CARD_HEIGHT = 368;
constexpr int CARD_GAP_X = 40;
constexpr int CARD_GAP_Y = 22;
constexpr int GRID_START_X = 110;
constexpr int GRID_START_Y = 178;

// Theme Colors
const SDL_Color COLOR_BG          = { 12, 16, 24, 255 };      // Deep dark
const SDL_Color COLOR_HEADER      = { 19, 24, 36, 255 };      // Top panel
const SDL_Color COLOR_ACCENT      = { 0, 114, 206, 255 };     // PlayStation Blue
const SDL_Color COLOR_CARD_BG     = { 24, 30, 44, 255 };      // Card item
const SDL_Color COLOR_CARD_SELECT = { 0, 150, 255, 255 };     // Focused outline
const SDL_Color COLOR_WHITE       = { 245, 247, 250, 255 };
const SDL_Color COLOR_MUTED       = { 135, 145, 165, 255 };
const SDL_Color COLOR_GREEN       = { 46, 204, 113, 255 };
const SDL_Color COLOR_YELLOW      = { 241, 196, 15, 255 };
const SDL_Color COLOR_RED         = { 235, 77, 75, 255 };
const SDL_Color COLOR_STRIP_BG    = { 25, 32, 48, 255 };

inline uint64_t get_hdd_free_bytes() {
#if defined(_WIN32)
    ULARGE_INTEGER free_bytes_avail, total_bytes, total_free;
    if (GetDiskFreeSpaceExA("C:\\", &free_bytes_avail, &total_bytes, &total_free)) {
        return free_bytes_avail.QuadPart;
    }
    return 142ULL * 1024 * 1024 * 1024ULL;
#else
    struct statvfs st;
    if (statvfs("/user", &st) == 0) {
        return (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
    }
    return 0;
#endif
}

const std::vector<std::string> ALPHABET_LABELS = {
    "ВСЕ", "#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z"
};
const std::string ALPHABET_KEYS = "*#ABCDEFGHIJKLMNOPQRSTUVWXYZ";

enum class FocusArea {
    GRID,
    ALPHABET_STRIP,
    SEARCH_INPUT
};

enum class ConfirmType {
    NONE,
    EXIT_APP,
    DELETE_TORRENT,
    UNINSTALL_GAME
};

struct AppState {
    int active_tab = 0;
    std::vector<std::string> tabs = {
        "Каталог",
        "Загрузки",
        "Установленные",
        "Настройки"
    };

    FocusArea focus = FocusArea::GRID;

    // Alphabet navigation
    int alphabet_idx = 0;

    // Search filter & On-Screen Keyboard (OSK)
    bool search_mode = false;
    std::string search_query = "";
    bool osk_ru = false;
    int osk_row = 1;
    int osk_col = 0;

    // Catalog navigation
    int selected_item = 0; // index inside filtered list
    int scroll_row = 0;
    bool show_details = false;

    // Downloads tab navigation
    int selected_download = 0;
    int downloads_scroll_offset = 0;

    // Installed tab navigation
    int selected_installed = 0;
    int installed_scroll_offset = 0;

    // Confirmation modal
    ConfirmType confirm = ConfirmType::NONE;
    int confirm_target_idx = -1;
    std::string confirm_title = "";

    // Settings tab
    int selected_setting = 0;
    int settings_scroll_offset = 0;
    int setting_parallel_count = 1; // 1 (по умолчанию для HDD PS4), 2, 3, 4
    int setting_seeding_idx = 0; // 0: "32 КБ/с", 1: "128 КБ/с", 2: "256 КБ/с", 3: "1024 КБ/с (1 МБ/с)", 4: "Без ограничений"
    int setting_storage_idx = 0; // 0: "/data/storre (Внутренний HDD)", 1: "/mnt/usb0", 2: "/mnt/usb1"
    bool setting_autostart = true;
    bool setting_resume_session = true;
    int setting_listen_port_idx = 0; // 0: "Отключен (0)", 1: "6881 (Стандартный)", 2: "6882", 3: "6889"
    bool setting_auto_delete_pkg = true;
    std::string setting_ntp_status = "Синхронизировано (pool.ntp.org)";

    // Trigger state latches for L2 / R2 and details scroll
    bool l2_latched = false;
    bool r2_latched = false;
    int desc_scroll_lines = 0;
    int16_t right_stick_y = 0;
    uint32_t last_stick_scroll_ms = 0;

    std::string status_notice = "";
    uint32_t status_notice_until = 0;

    std::string gamepad_name = "Клавиатура";
    bool running = true;
};

struct ButtonIcons {
    SDL_Texture* app_icon = nullptr;
    SDL_Texture* cross = nullptr;
    SDL_Texture* circle = nullptr;
    SDL_Texture* square = nullptr;
    SDL_Texture* triangle = nullptr;
    SDL_Texture* l1 = nullptr;
    SDL_Texture* r1 = nullptr;
    SDL_Texture* l2 = nullptr;
    SDL_Texture* r2 = nullptr;
    SDL_Texture* options = nullptr;
};

std::string format_size_mb(uint32_t mb) {
    if (mb == 0) return "нет данных";
    if (mb < 1024) return std::to_string(mb) + " МБ";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f ГБ", (double)mb / 1024.0);
    return std::string(buf);
}

struct FooterHint {
    SDL_Texture* icon1 = nullptr;
    int w1 = 28;
    int h1 = 28;
    SDL_Texture* icon2 = nullptr;
    int w2 = 0;
    int h2 = 0;
    std::string label;
};

void render_icon(SDL_Renderer* ren, SDL_Texture* tex, int x, int y, int w, int h) {
    if (!tex) return;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(ren, tex, nullptr, &dst);
}

struct CachedTextEntry {
    SDL_Texture* tex = nullptr;
    int w = 0;
    int h = 0;
};

static std::unordered_map<std::string, CachedTextEntry> g_text_cache;
static constexpr size_t MAX_TEXT_CACHE_ENTRIES = 256;

void clear_text_cache() {
    for (auto& kv : g_text_cache) {
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    }
    g_text_cache.clear();
}

void render_text(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font || text.empty()) return;

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%p:%02x%02x%02x%02x:", (const void*)font, color.r, color.g, color.b, color.a);
    std::string key = std::string(hdr) + text;

    auto it = g_text_cache.find(key);
    if (it != g_text_cache.end()) {
        SDL_Rect dst = { x, y, it->second.w, it->second.h };
        SDL_RenderCopy(ren, it->second.tex, nullptr, &dst);
        return;
    }

    if (g_text_cache.size() >= MAX_TEXT_CACHE_ENTRIES) {
        clear_text_cache();
    }

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex) {
        SDL_Rect dst = { x, y, surf->w, surf->h };
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        g_text_cache.emplace(std::move(key), CachedTextEntry{ tex, surf->w, surf->h });
    }
    SDL_FreeSurface(surf);
}

void render_text_wrapped(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, Uint32 wrap_width, SDL_Color color, int max_height = 0) {
    if (!font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, wrap_width);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex) {
        int render_h = surf->h;
        SDL_Rect src = { 0, 0, surf->w, surf->h };
        if (max_height > 0 && render_h > max_height) {
            render_h = max_height;
            src.h = max_height;
        }
        SDL_Rect dst = { x, y, surf->w, render_h };
        SDL_RenderCopy(ren, tex, &src, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void render_footer_hints(SDL_Renderer* ren, TTF_Font* font, const std::vector<FooterHint>& hints, int min_x, int max_x, int y_center) {
    if (!font || hints.empty()) return;

    const int item_gap = 34;
    const int icon_text_gap = 10;
    const int pair_gap = 5;

    int total_w = 0;
    std::vector<int> text_widths(hints.size(), 0);
    int text_h = 0;

    for (size_t i = 0; i < hints.size(); ++i) {
        int tw = 0, th = 0;
        TTF_SizeUTF8(font, hints[i].label.c_str(), &tw, &th);
        text_widths[i] = tw;
        if (th > text_h) text_h = th;

        int item_w = 0;
        if (hints[i].icon1) item_w += hints[i].w1;
        if (hints[i].icon2) item_w += pair_gap + hints[i].w2;
        if (hints[i].icon1 || hints[i].icon2) item_w += icon_text_gap;
        item_w += tw;
        total_w += item_w;
        if (i + 1 < hints.size()) total_w += item_gap;
    }

    int avail = max_x - min_x;
    int cx = (total_w < avail) ? (min_x + (avail - total_w) / 2) : min_x;

    for (size_t i = 0; i < hints.size(); ++i) {
        const auto& h = hints[i];
        if (h.icon1) {
            render_icon(ren, h.icon1, cx, y_center - h.h1 / 2, h.w1, h.h1);
            cx += h.w1;
        }
        if (h.icon2) {
            cx += pair_gap;
            render_icon(ren, h.icon2, cx, y_center - h.h2 / 2, h.w2, h.h2);
            cx += h.w2;
        }
        if (h.icon1 || h.icon2) {
            cx += icon_text_gap;
        }
        render_text(ren, font, h.label, cx, y_center - text_h / 2 - 1, COLOR_WHITE);
        cx += text_widths[i] + item_gap;
    }
}

ButtonIcons load_button_icons(SDL_Renderer* ren, std::string (*resolver)(const std::string&)) {
    auto load_tex = [&](const char* rel) -> SDL_Texture* {
        std::string p = resolver(rel);
        SDL_Texture* t = IMG_LoadTexture(ren, p.c_str());
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        return t;
    };
    ButtonIcons icons;
    icons.app_icon = load_tex("assets/images/logo_header.png");
    icons.cross    = load_tex("assets/images/btn_cross.png");
    icons.circle   = load_tex("assets/images/btn_circle.png");
    icons.square   = load_tex("assets/images/btn_square.png");
    icons.triangle = load_tex("assets/images/btn_triangle.png");
    icons.l1       = load_tex("assets/images/btn_l1.png");
    icons.r1       = load_tex("assets/images/btn_r1.png");
    icons.l2       = load_tex("assets/images/btn_l2.png");
    icons.r2       = load_tex("assets/images/btn_r2.png");
    icons.options  = load_tex("assets/images/btn_options.png");
    return icons;
}

void destroy_button_icons(ButtonIcons& icons) {
    if (icons.app_icon) SDL_DestroyTexture(icons.app_icon);
    if (icons.cross)    SDL_DestroyTexture(icons.cross);
    if (icons.circle)   SDL_DestroyTexture(icons.circle);
    if (icons.square)   SDL_DestroyTexture(icons.square);
    if (icons.triangle) SDL_DestroyTexture(icons.triangle);
    if (icons.l1)       SDL_DestroyTexture(icons.l1);
    if (icons.r1)       SDL_DestroyTexture(icons.r1);
    if (icons.l2)       SDL_DestroyTexture(icons.l2);
    if (icons.r2)       SDL_DestroyTexture(icons.r2);
    if (icons.options)  SDL_DestroyTexture(icons.options);
}

void render_badge(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, SDL_Color bg_color, SDL_Color text_color) {
    if (!font || text.empty()) return;
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text.c_str(), &tw, &th);
    SDL_Rect badge = { x, y, tw + 20, th + 8 };
    SDL_SetRenderDrawColor(ren, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderFillRect(ren, &badge);
    render_text(ren, font, text, x + 10, y + 4, text_color);
}

void utf8_pop_back(std::string& s) {
    while (!s.empty()) {
        unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break; // stop at start byte (ASCII or multi-byte lead)
    }
}

std::string truncate_utf8(const std::string& str, size_t max_chars) {
    size_t chars = 0;
    size_t i = 0;
    while (i < str.size()) {
        if (chars >= max_chars) {
            return str.substr(0, i) + "...";
        }
        unsigned char c = (unsigned char)str[i];
        size_t adv = 1;
        if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) adv = 4;
        if (i + adv > str.size()) break;
        i += adv;
        chars++;
    }
    return str;
}

void wrap_two_lines(TTF_Font* font, const std::string& text, int max_w, std::string& line1, std::string& line2) {
    line1.clear();
    line2.clear();
    if (!font || text.empty()) return;

    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text.c_str(), &tw, &th);
    if (tw <= max_w) {
        line1 = text;
        return;
    }

    // Build words or cut by UTF-8 characters
    std::vector<std::string> words;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && text[start] == ' ') start++;
        if (start >= text.size()) break;
        size_t end = start;
        while (end < text.size() && text[end] != ' ') end++;
        words.push_back(text.substr(start, end - start));
        start = end;
    }

    size_t wi = 0;
    while (wi < words.size()) {
        std::string cand = line1.empty() ? words[wi] : (line1 + " " + words[wi]);
        TTF_SizeUTF8(font, cand.c_str(), &tw, &th);
        if (tw <= max_w) {
            line1 = std::move(cand);
            wi++;
        } else {
            if (line1.empty()) {
                // Single super-long word: trim by UTF-8 characters
                size_t i = 0, last_ok = 0;
                while (i < words[wi].size()) {
                    unsigned char c = (unsigned char)words[wi][i];
                    size_t adv = ((c & 0xE0) == 0xC0) ? 2 : (((c & 0xF0) == 0xE0) ? 3 : (((c & 0xF8) == 0xF0) ? 4 : 1));
                    if (i + adv > words[wi].size()) break;
                    std::string sub = words[wi].substr(0, i + adv);
                    TTF_SizeUTF8(font, sub.c_str(), &tw, &th);
                    if (tw > max_w) break;
                    last_ok = i + adv;
                    i += adv;
                }
                if (last_ok == 0) last_ok = 1;
                line1 = words[wi].substr(0, last_ok);
                words[wi] = words[wi].substr(last_ok);
            }
            break;
        }
    }

    if (wi >= words.size()) return;

    std::string rem;
    for (size_t k = wi; k < words.size(); ++k) {
        if (!rem.empty()) rem += " ";
        rem += words[k];
    }

    TTF_SizeUTF8(font, rem.c_str(), &tw, &th);
    if (tw <= max_w) {
        line2 = rem;
        return;
    }

    // Fit line2 + "..."
    size_t i = 0, last_ok = 0;
    while (i < rem.size()) {
        unsigned char c = (unsigned char)rem[i];
        size_t adv = ((c & 0xE0) == 0xC0) ? 2 : (((c & 0xF0) == 0xE0) ? 3 : (((c & 0xF8) == 0xF0) ? 4 : 1));
        if (i + adv > rem.size()) break;
        std::string cand = rem.substr(0, i + adv) + "...";
        TTF_SizeUTF8(font, cand.c_str(), &tw, &th);
        if (tw > max_w) break;
        last_ok = i + adv;
        i += adv;
    }
    line2 = rem.substr(0, last_ok) + "...";
}

std::string resolve_asset_path(const std::string& rel_path) {
    char* base = SDL_GetBasePath();
    if (base) {
        std::string cand = std::string(base) + rel_path;
        SDL_free(base);
        FILE* f = fopen(cand.c_str(), "rb");
        if (f) {
            fclose(f);
            return cand;
        }
    }

    std::vector<std::string> prefixes = {
        "",
        "assets/",
        "ps4_storre/",
        "ps4_storre/build_win/",
        "ps4_storre/assets/",
        "../",
        "d:/Projects/PS4/ps4_storre/",
        "d:/Projects/PS4/ps4_storre/build_win/",
        "d:/Projects/PS4/build_pkgtool_store/",
        "/app0/"
    };

    for (const auto& p : prefixes) {
        std::string cand = p + rel_path;
        FILE* f = fopen(cand.c_str(), "rb");
        if (f) {
            fclose(f);
            return cand;
        }
    }

    return rel_path;
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
        return 1;
    }

    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init Error: " << TTF_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_Window* window = SDL_CreateWindow(
        "PS4 Storre - All-in-One Store & Torrent Client",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) return 1;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    SDL_RenderSetLogicalSize(renderer, CANVAS_WIDTH, CANVAS_HEIGHT);

    // Fonts with robust path resolution (NotoSans Regular & SemiBold across entire UI)
    std::string font_reg_path  = resolve_asset_path("assets/fonts/NotoSans-Regular.ttf");
    std::string font_semi_path = resolve_asset_path("assets/fonts/NotoSans-SemiBold.ttf");

    TTF_Font* font_title = TTF_OpenFont(font_semi_path.c_str(), 34);
    TTF_Font* font_ui    = TTF_OpenFont(font_semi_path.c_str(), 22);
    TTF_Font* font_card  = TTF_OpenFont(font_reg_path.c_str(), 18);
    TTF_Font* font_desc  = TTF_OpenFont(font_reg_path.c_str(), 20);
    TTF_Font* font_alpha = TTF_OpenFont(font_semi_path.c_str(), 18);

    if (!font_title || !font_ui || !font_card || !font_desc || !font_alpha) {
        std::cerr << "[Storre] Font load warning: " << TTF_GetError() << "\n";
    }

    // Initialize Catalog with robust path resolution
    CatalogManager catalog;
    std::string cat_json = resolve_asset_path("assets/misc/catalog.json");
    std::string cat_pak  = resolve_asset_path("assets/misc/catalog.pak");
    catalog.init(cat_json, cat_pak);
    std::vector<size_t> active_items = catalog.get_items_by_letter('*');

    // Initialize Queue
    QueueManager queue_mgr;
    queue_mgr.start();

    // Controller
    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    AppState state;
    if (controller) {
        state.gamepad_name = std::string("DualShock 4: ") + SDL_GameControllerName(controller);
    }

    ButtonIcons icons = load_button_icons(renderer, resolve_asset_path);

    size_t cached_desc_game_idx = (size_t)-1;
    SDL_Texture* cached_desc_tex = nullptr;
    int cached_desc_w = 0;
    int cached_desc_h = 0;
    int cached_desc_line_h = 24;

    SDL_Event ev;
    uint32_t frame_start = 0;

    auto get_selected_download_task_idx = [&]() -> int {
        auto tasks = queue_mgr.get_tasks();
        int idx = 0;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].torrent_active) {
                if (idx == state.selected_download) return (int)i;
                idx++;
            }
        }
        return -1;
    };

    auto count_download_tasks = [&]() -> int {
        auto tasks = queue_mgr.get_tasks();
        int cnt = 0;
        for (const auto& t : tasks) if (t.torrent_active) cnt++;
        return cnt;
    };

    auto get_selected_installed_task_idx = [&]() -> int {
        auto tasks = queue_mgr.get_tasks();
        int idx = 0;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].is_installed) {
                if (idx == state.selected_installed) return (int)i;
                idx++;
            }
        }
        return -1;
    };

    auto count_installed_tasks = [&]() -> int {
        auto tasks = queue_mgr.get_tasks();
        int cnt = 0;
        for (const auto& t : tasks) if (t.is_installed) cnt++;
        return cnt;
    };

    auto select_letter = [&](int alpha_idx) {
        if (alpha_idx < 0) alpha_idx = 0;
        if (alpha_idx >= (int)ALPHABET_LABELS.size()) alpha_idx = (int)ALPHABET_LABELS.size() - 1;
        state.alphabet_idx = alpha_idx;
        state.search_query.clear();
        char key = ALPHABET_KEYS[state.alphabet_idx];
        active_items = catalog.get_items_by_letter(key);
        state.selected_item = 0;
        state.scroll_row = 0;
    };

    auto update_search = [&](const std::string& query) {
        state.search_query = query;
        if (query.empty()) {
            char key = ALPHABET_KEYS[state.alphabet_idx];
            active_items = catalog.get_items_by_letter(key);
        } else {
            active_items = catalog.search(query);
        }
        state.selected_item = 0;
        state.scroll_row = 0;
    };

    const std::vector<std::vector<std::string>> osk_en = {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-" },
        { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", ":" },
        { "A", "S", "D", "F", "G", "H", "J", "K", "L", "'", "." },
        { "Z", "X", "C", "V", "B", "N", "M", ",", "/", "&", "!" },
        { "Пробел", "Удалить", "RU", "Очистить", "Готово" }
    };
    const std::vector<std::vector<std::string>> osk_ru = {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-" },
        { "Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х" },
        { "Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э" },
        { "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", "Ъ", "Ё" },
        { "Пробел", "Удалить", "EN", "Очистить", "Готово" }
    };

    auto osk_move = [&](int dr, int dc) {
        const auto& layout = state.osk_ru ? osk_ru : osk_en;
        int prev_row = state.osk_row;
        state.osk_row = (state.osk_row + dr + (int)layout.size()) % (int)layout.size();
        int new_len = (int)layout[state.osk_row].size();
        if (dr != 0) {
            if (prev_row < 4 && state.osk_row == 4) {
                state.osk_col = (state.osk_col * new_len) / 11;
            } else if (prev_row == 4 && state.osk_row < 4) {
                state.osk_col = (state.osk_col * 11 + 5) / 5;
            }
        }
        state.osk_col = std::clamp(state.osk_col, 0, new_len - 1);
        if (dc != 0) {
            state.osk_col = (state.osk_col + dc + new_len) % new_len;
        }
    };

    auto osk_activate = [&]() {
        const auto& layout = state.osk_ru ? osk_ru : osk_en;
        const std::string& key = layout[state.osk_row][state.osk_col];
        if (state.osk_row < 4) {
            state.search_query += key;
            update_search(state.search_query);
        } else {
            if (key == "Пробел") {
                if (!state.search_query.empty() && state.search_query.back() != ' ') {
                    state.search_query += ' ';
                    update_search(state.search_query);
                }
            } else if (key == "Удалить") {
                utf8_pop_back(state.search_query);
                update_search(state.search_query);
            } else if (key == "RU" || key == "EN") {
                state.osk_ru = !state.osk_ru;
            } else if (key == "Очистить") {
                state.search_query.clear();
                update_search("");
            } else if (key == "Готово") {
                state.focus = FocusArea::GRID;
                SDL_StopTextInput();
            }
        }
    };

    auto execute_confirm = [&]() {
        if (state.confirm == ConfirmType::EXIT_APP) {
            state.running = false;
        } else if (state.confirm == ConfirmType::DELETE_TORRENT && state.confirm_target_idx >= 0) {
            queue_mgr.remove_torrent((size_t)state.confirm_target_idx);
            int new_cnt = count_download_tasks();
            if (state.selected_download >= new_cnt && state.selected_download > 0) {
                state.selected_download = new_cnt - 1;
            }
        } else if (state.confirm == ConfirmType::UNINSTALL_GAME && state.confirm_target_idx >= 0) {
            queue_mgr.uninstall_game((size_t)state.confirm_target_idx);
            int new_cnt = count_installed_tasks();
            if (state.selected_installed >= new_cnt && state.selected_installed > 0) {
                state.selected_installed = new_cnt - 1;
            }
        }
        state.confirm = ConfirmType::NONE;
        state.confirm_target_idx = -1;
        state.confirm_title.clear();
    };

    while (state.running) {
        frame_start = SDL_GetTicks();

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                state.running = false;
            }
            // Text Input for Search (hardware keyboard on PC)
            else if (ev.type == SDL_TEXTINPUT && state.focus == FocusArea::SEARCH_INPUT && state.confirm == ConfirmType::NONE) {
                state.search_query += ev.text.text;
                update_search(state.search_query);
            }
            // Analog Triggers (L2 / R2) and Right Stick scroll
            else if (ev.type == SDL_CONTROLLERAXISMOTION) {
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                    state.right_stick_y = ev.caxis.value;
                }
                if (state.confirm == ConfirmType::NONE && state.active_tab == 0 && !state.show_details) {
                    // L2 Trigger (Left Trigger)
                    if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                        if (ev.caxis.value > 16000 && !state.l2_latched) {
                            state.l2_latched = true;
                            if (state.focus == FocusArea::SEARCH_INPUT) state.osk_ru = !state.osk_ru;
                            else select_letter(state.alphabet_idx - 1);
                        } else if (ev.caxis.value < 8000) {
                            state.l2_latched = false;
                        }
                    }
                    // R2 Trigger (Right Trigger)
                    else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                        if (ev.caxis.value > 16000 && !state.r2_latched) {
                            state.r2_latched = true;
                            if (state.focus == FocusArea::SEARCH_INPUT) state.osk_ru = !state.osk_ru;
                            else select_letter(state.alphabet_idx + 1);
                        } else if (ev.caxis.value < 8000) {
                            state.r2_latched = false;
                        }
                    }
                }
            }
            // Gamepad Buttons
            else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                // Confirm dialog modal has highest priority
                if (state.confirm != ConfirmType::NONE) {
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        execute_confirm();
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        state.confirm = ConfirmType::NONE;
                        state.confirm_target_idx = -1;
                        state.confirm_title.clear();
                    }
                }
                // Details popup
                else if (state.show_details) {
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        state.show_details = false;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        state.desc_scroll_lines = std::max(0, state.desc_scroll_lines - 2);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        state.desc_scroll_lines += 2;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        if (state.selected_item < (int)active_items.size()) {
                            size_t real_idx = active_items[state.selected_item];
                            const GameItem* sel = catalog.get_game(real_idx);
                            if (sel) {
                                queue_mgr.add_task(real_idx, sel->id, sel->cid, sel->title, sel->torrent_hash, sel->size_mb);
                            }
                        }
                    }
                }
                // Search Input mode (Built-in On-Screen Keyboard)
                else if (state.focus == FocusArea::SEARCH_INPUT) {
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        osk_move(-1, 0);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        osk_move(1, 0);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        osk_move(0, -1);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                        osk_move(0, 1);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        osk_activate();
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_X) { // Square = Backspace
                        utf8_pop_back(state.search_query);
                        update_search(state.search_query);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_Y) { // Triangle = Space
                        if (!state.search_query.empty() && state.search_query.back() != ' ') {
                            state.search_query += ' ';
                            update_search(state.search_query);
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                               ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                        state.osk_ru = !state.osk_ru;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                               ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                        state.focus = FocusArea::GRID;
                        SDL_StopTextInput();
                    }
                }
                // Global Tab Switching via L1 / R1
                else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    state.active_tab = (state.active_tab + 1) % state.tabs.size();
                    state.focus = FocusArea::GRID;
                } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    state.active_tab = (state.active_tab - 1 + (int)state.tabs.size()) % state.tabs.size();
                    state.focus = FocusArea::GRID;
                }
                // Tab 0: Catalog
                else if (state.active_tab == 0) {
                    // Triangle (△) to Search, Square (□) to Clear Search
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_Y) { // Triangle
                        state.focus = FocusArea::SEARCH_INPUT;
                        state.osk_row = 1;
                        state.osk_col = 0;
                        SDL_StartTextInput();
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_X) { // Square
                        state.search_query.clear();
                        update_search("");
                        state.focus = FocusArea::GRID;
                        SDL_StopTextInput();
                    }
                    // In Alphabet Strip
                    else if (state.focus == FocusArea::ALPHABET_STRIP) {
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                            select_letter(state.alphabet_idx + 1);
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                            select_letter(state.alphabet_idx - 1);
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                                   ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                            state.focus = FocusArea::GRID;
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                            state.focus = FocusArea::GRID;
                        }
                    }
                    // In Game Grid
                    else if (state.focus == FocusArea::GRID) {
                        int count = (int)active_items.size();
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                            if (state.selected_item < count - 1) state.selected_item++;
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                            if (state.selected_item > 0) state.selected_item--;
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                            if (state.selected_item + GRID_COLS < count) state.selected_item += GRID_COLS;
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                            if (state.selected_item - GRID_COLS >= 0) {
                                state.selected_item -= GRID_COLS;
                            } else {
                                state.focus = FocusArea::ALPHABET_STRIP;
                            }
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                            if (count > 0) {
                                state.show_details = true;
                                state.desc_scroll_lines = 0;
                            }
                        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                            state.confirm = ConfirmType::EXIT_APP;
                            state.confirm_target_idx = -1;
                            state.confirm_title = "Выйти из PS4 Storre?";
                        }
                    }
                }
                // Tab 1: Downloads / Queue
                else if (state.active_tab == 1) {
                    int count = count_download_tasks();
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        if (state.selected_download > 0) state.selected_download--;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        if (state.selected_download < count - 1) state.selected_download++;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        int tidx = get_selected_download_task_idx();
                        if (tidx >= 0) queue_mgr.toggle_pause((size_t)tidx);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                        int tidx = get_selected_download_task_idx();
                        auto tasks = queue_mgr.get_tasks();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
                            state.confirm = ConfirmType::DELETE_TORRENT;
                            state.confirm_target_idx = tidx;
                            state.confirm_title = tasks[tidx].title;
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        state.active_tab = 0;
                    }
                }
                // Tab 2: Installed Games
                else if (state.active_tab == 2) {
                    auto tasks = queue_mgr.get_tasks();
                    int installed_count = count_installed_tasks();
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        if (state.selected_installed > 0) state.selected_installed--;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        if (state.selected_installed < installed_count - 1) state.selected_installed++;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        int tidx = get_selected_installed_task_idx();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
#if defined(_WIN32)
                            state.status_notice = "Запуск игры (" + tasks[tidx].id + ") доступен на PS4";
#else
                            state.status_notice = "Запрос запуска " + tasks[tidx].id + " отправлен в систему";
#endif
                            state.status_notice_until = SDL_GetTicks() + 3000;
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                        int tidx = get_selected_installed_task_idx();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
                            state.confirm = ConfirmType::UNINSTALL_GAME;
                            state.confirm_target_idx = tidx;
                            state.confirm_title = tasks[tidx].title;
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        state.active_tab = 0;
                    }
                }
                // Tab 3: System Settings
                else if (state.active_tab == 3) {
                    const int NUM_SETTINGS = 8;
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        if (state.selected_setting > 0) state.selected_setting--;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        if (state.selected_setting < NUM_SETTINGS - 1) state.selected_setting++;
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        if (state.selected_setting == 0) {
                            if (state.setting_parallel_count > 1) state.setting_parallel_count--;
                            else state.setting_parallel_count = 4;
                            queue_mgr.apply_max_parallel(state.setting_parallel_count);
                        } else if (state.selected_setting == 1) {
                            state.setting_seeding_idx = (state.setting_seeding_idx - 1 + 5) % 5;
                            queue_mgr.apply_seeding_limit(state.setting_seeding_idx);
                        } else if (state.selected_setting == 2) {
                            state.setting_storage_idx = (state.setting_storage_idx - 1 + 3) % 3;
                            queue_mgr.apply_storage_location(state.setting_storage_idx);
                        } else if (state.selected_setting == 3) {
                            state.setting_autostart = !state.setting_autostart;
                            queue_mgr.apply_autostart(state.setting_autostart);
                        } else if (state.selected_setting == 4) {
                            state.setting_resume_session = !state.setting_resume_session;
                            queue_mgr.apply_resume_session(state.setting_resume_session);
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        if (state.selected_setting == 0) {
                            if (state.setting_parallel_count < 4) state.setting_parallel_count++;
                            else state.setting_parallel_count = 1;
                            queue_mgr.apply_max_parallel(state.setting_parallel_count);
                        } else if (state.selected_setting == 1) {
                            state.setting_seeding_idx = (state.setting_seeding_idx + 1) % 5;
                            queue_mgr.apply_seeding_limit(state.setting_seeding_idx);
                        } else if (state.selected_setting == 2) {
                            state.setting_storage_idx = (state.setting_storage_idx + 1) % 3;
                            queue_mgr.apply_storage_location(state.setting_storage_idx);
                        } else if (state.selected_setting == 3) {
                            state.setting_autostart = !state.setting_autostart;
                            queue_mgr.apply_autostart(state.setting_autostart);
                        } else if (state.selected_setting == 4) {
                            state.setting_resume_session = !state.setting_resume_session;
                            queue_mgr.apply_resume_session(state.setting_resume_session);
                        } else if (state.selected_setting == 6) {
                            state.setting_ntp_status = "Успешно: pool.ntp.org [OK]";
                        }
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        state.active_tab = 0;
                    }
                }
            }
            // Keyboard
            else if (ev.type == SDL_KEYDOWN) {
                // Confirm dialog modal
                if (state.confirm != ConfirmType::NONE) {
                    if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_x) {
                        execute_confirm();
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE) {
                        state.confirm = ConfirmType::NONE;
                        state.confirm_target_idx = -1;
                        state.confirm_title.clear();
                    }
                }
                // Search Input mode (hardware keyboard + arrow navigation on OSK)
                else if (state.focus == FocusArea::SEARCH_INPUT) {
                    if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        utf8_pop_back(state.search_query);
                        update_search(state.search_query);
                    } else if (ev.key.keysym.sym == SDLK_UP) {
                        osk_move(-1, 0);
                    } else if (ev.key.keysym.sym == SDLK_DOWN) {
                        osk_move(1, 0);
                    } else if (ev.key.keysym.sym == SDLK_LEFT) {
                        osk_move(0, -1);
                    } else if (ev.key.keysym.sym == SDLK_RIGHT) {
                        osk_move(0, 1);
                    } else if (ev.key.keysym.sym == SDLK_TAB) {
                        state.osk_ru = !state.osk_ru;
                    } else if (ev.key.keysym.sym == SDLK_SPACE) {
                        if (!state.search_query.empty() && state.search_query.back() != ' ') {
                            state.search_query += ' ';
                            update_search(state.search_query);
                        }
                    } else if (ev.key.keysym.sym == SDLK_RETURN) {
                        if (ev.key.keysym.mod & KMOD_SHIFT) {
                            osk_activate();
                        } else {
                            state.focus = FocusArea::GRID;
                            SDL_StopTextInput();
                        }
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        state.focus = FocusArea::GRID;
                        SDL_StopTextInput();
                    }
                }
                // Details popup
                else if (state.show_details) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE) {
                        state.show_details = false;
                    } else if (ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_w) {
                        state.desc_scroll_lines = std::max(0, state.desc_scroll_lines - 2);
                    } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s) {
                        state.desc_scroll_lines += 2;
                    } else if (ev.key.keysym.sym == SDLK_PAGEUP) {
                        state.desc_scroll_lines = std::max(0, state.desc_scroll_lines - 10);
                    } else if (ev.key.keysym.sym == SDLK_PAGEDOWN) {
                        state.desc_scroll_lines += 10;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_x) {
                        if (state.selected_item < (int)active_items.size()) {
                            size_t real_idx = active_items[state.selected_item];
                            const GameItem* sel = catalog.get_game(real_idx);
                            if (sel) {
                                queue_mgr.add_task(real_idx, sel->id, sel->cid, sel->title, sel->torrent_hash, sel->size_mb);
                            }
                        }
                    }
                }
                // Global Tab Navigation (1, 2, 3, 4)
                else if (ev.key.keysym.sym >= SDLK_1 && ev.key.keysym.sym <= SDLK_4) {
                    state.active_tab = ev.key.keysym.sym - SDLK_1;
                    state.focus = FocusArea::GRID;
                }
                // Catalog Navigation
                else if (state.active_tab == 0) {
                    // L2 / R2 equivalents on Keyboard: [ / ] or PageUp / PageDown or , / .
                    if (ev.key.keysym.sym == SDLK_PAGEDOWN || ev.key.keysym.sym == SDLK_RIGHTBRACKET || ev.key.keysym.sym == SDLK_PERIOD) {
                        select_letter(state.alphabet_idx + 1); // R2 jump
                    } else if (ev.key.keysym.sym == SDLK_PAGEUP || ev.key.keysym.sym == SDLK_LEFTBRACKET || ev.key.keysym.sym == SDLK_COMMA) {
                        select_letter(state.alphabet_idx - 1); // L2 jump
                    }
                    // L1 / R1 equivalents on Keyboard: Q / E (Tab cycling)
                    else if (ev.key.keysym.sym == SDLK_e) {
                        state.active_tab = (state.active_tab + 1) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_q) {
                        state.active_tab = (state.active_tab - 1 + (int)state.tabs.size()) % state.tabs.size();
                    }
                    // Triangle / F2 to toggle Alphabet Strip
                    else if (ev.key.keysym.sym == SDLK_TAB || ev.key.keysym.sym == SDLK_F2) {
                        state.focus = (state.focus == FocusArea::GRID) ? FocusArea::ALPHABET_STRIP : FocusArea::GRID;
                    }
                    // Square / F3 or / to Search
                    else if (ev.key.keysym.sym == SDLK_SLASH || ev.key.keysym.sym == SDLK_F3) {
                        state.focus = FocusArea::SEARCH_INPUT;
                        state.osk_row = 1;
                        state.osk_col = 0;
                        SDL_StartTextInput();
                    }
                    // Alphabet Strip focus
                    else if (state.focus == FocusArea::ALPHABET_STRIP) {
                        if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_d) {
                            select_letter(state.alphabet_idx + 1);
                        } else if (ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_a) {
                            select_letter(state.alphabet_idx - 1);
                        } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_RETURN) {
                            state.focus = FocusArea::GRID;
                        } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                            state.focus = FocusArea::GRID;
                        }
                    }
                    // Grid focus
                    else if (state.focus == FocusArea::GRID) {
                        int count = (int)active_items.size();
                        if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_d) {
                            if (state.selected_item < count - 1) state.selected_item++;
                        } else if (ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_a) {
                            if (state.selected_item > 0) state.selected_item--;
                        } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s) {
                            if (state.selected_item + GRID_COLS < count) state.selected_item += GRID_COLS;
                        } else if (ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_w) {
                            if (state.selected_item - GRID_COLS >= 0) {
                                state.selected_item -= GRID_COLS;
                            } else {
                                state.focus = FocusArea::ALPHABET_STRIP;
                            }
                        } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE) {
                            if (count > 0) {
                                state.show_details = true;
                                state.desc_scroll_lines = 0;
                            }
                        } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                            if (!state.search_query.empty()) {
                                update_search("");
                            } else {
                                state.confirm = ConfirmType::EXIT_APP;
                                state.confirm_target_idx = -1;
                                state.confirm_title = "Выйти из PS4 Storre?";
                            }
                        }
                    }
                }
                // Tab 1: Downloads Navigation
                else if (state.active_tab == 1) {
                    int count = count_download_tasks();
                    if (ev.key.keysym.sym == SDLK_e) {
                        state.active_tab = (state.active_tab + 1) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_q) {
                        state.active_tab = (state.active_tab - 1 + (int)state.tabs.size()) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_w) {
                        if (state.selected_download > 0) state.selected_download--;
                    } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s) {
                        if (state.selected_download < count - 1) state.selected_download++;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_x) {
                        int tidx = get_selected_download_task_idx();
                        if (tidx >= 0) queue_mgr.toggle_pause((size_t)tidx);
                    } else if (ev.key.keysym.sym == SDLK_DELETE || ev.key.keysym.sym == SDLK_BACKSPACE || ev.key.keysym.sym == SDLK_d) {
                        int tidx = get_selected_download_task_idx();
                        auto tasks = queue_mgr.get_tasks();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
                            state.confirm = ConfirmType::DELETE_TORRENT;
                            state.confirm_target_idx = tidx;
                            state.confirm_title = tasks[tidx].title;
                        }
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        state.active_tab = 0;
                    }
                }
                // Tab 2: Installed Navigation
                else if (state.active_tab == 2) {
                    auto tasks = queue_mgr.get_tasks();
                    int installed_count = count_installed_tasks();
                    if (ev.key.keysym.sym == SDLK_e) {
                        state.active_tab = (state.active_tab + 1) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_q) {
                        state.active_tab = (state.active_tab - 1 + (int)state.tabs.size()) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_w) {
                        if (state.selected_installed > 0) state.selected_installed--;
                    } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s) {
                        if (state.selected_installed < installed_count - 1) state.selected_installed++;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_x) {
                        int tidx = get_selected_installed_task_idx();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
#if defined(_WIN32)
                            state.status_notice = "Запуск игры (" + tasks[tidx].id + ") доступен на PS4";
#else
                            state.status_notice = "Запрос запуска " + tasks[tidx].id + " отправлен в систему";
#endif
                            state.status_notice_until = SDL_GetTicks() + 3000;
                        }
                    } else if (ev.key.keysym.sym == SDLK_DELETE || ev.key.keysym.sym == SDLK_BACKSPACE) {
                        int tidx = get_selected_installed_task_idx();
                        if (tidx >= 0 && tidx < (int)tasks.size()) {
                            state.confirm = ConfirmType::UNINSTALL_GAME;
                            state.confirm_target_idx = tidx;
                            state.confirm_title = tasks[tidx].title;
                        }
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        state.active_tab = 0;
                    }
                }
                // Tab 3: Settings Navigation
                else if (state.active_tab == 3) {
                    const int NUM_SETTINGS = 8;
                    if (ev.key.keysym.sym == SDLK_e) {
                        state.active_tab = (state.active_tab + 1) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_q) {
                        state.active_tab = (state.active_tab - 1 + (int)state.tabs.size()) % state.tabs.size();
                    } else if (ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_w) {
                        if (state.selected_setting > 0) state.selected_setting--;
                    } else if (ev.key.keysym.sym == SDLK_DOWN || ev.key.keysym.sym == SDLK_s) {
                        if (state.selected_setting < NUM_SETTINGS - 1) state.selected_setting++;
                    } else if (ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_a) {
                        if (state.selected_setting == 0) {
                            if (state.setting_parallel_count > 1) state.setting_parallel_count--;
                            else state.setting_parallel_count = 4;
                            queue_mgr.apply_max_parallel(state.setting_parallel_count);
                        } else if (state.selected_setting == 1) {
                            state.setting_seeding_idx = (state.setting_seeding_idx - 1 + 5) % 5;
                            queue_mgr.apply_seeding_limit(state.setting_seeding_idx);
                        } else if (state.selected_setting == 2) {
                            state.setting_storage_idx = (state.setting_storage_idx - 1 + 3) % 3;
                            queue_mgr.apply_storage_location(state.setting_storage_idx);
                        } else if (state.selected_setting == 3) {
                            state.setting_autostart = !state.setting_autostart;
                            queue_mgr.apply_autostart(state.setting_autostart);
                        } else if (state.selected_setting == 4) {
                            state.setting_resume_session = !state.setting_resume_session;
                            queue_mgr.apply_resume_session(state.setting_resume_session);
                        }
                    } else if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_d || ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_x) {
                        if (state.selected_setting == 0) {
                            if (state.setting_parallel_count < 4) state.setting_parallel_count++;
                            else state.setting_parallel_count = 1;
                            queue_mgr.apply_max_parallel(state.setting_parallel_count);
                        } else if (state.selected_setting == 1) {
                            state.setting_seeding_idx = (state.setting_seeding_idx + 1) % 5;
                            queue_mgr.apply_seeding_limit(state.setting_seeding_idx);
                        } else if (state.selected_setting == 2) {
                            state.setting_storage_idx = (state.setting_storage_idx + 1) % 3;
                            queue_mgr.apply_storage_location(state.setting_storage_idx);
                        } else if (state.selected_setting == 3) {
                            state.setting_autostart = !state.setting_autostart;
                            queue_mgr.apply_autostart(state.setting_autostart);
                        } else if (state.selected_setting == 4) {
                            state.setting_resume_session = !state.setting_resume_session;
                            queue_mgr.apply_resume_session(state.setting_resume_session);
                        } else if (state.selected_setting == 6) {
                            state.setting_ntp_status = "Успешно: pool.ntp.org [OK]";
                        }
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        state.active_tab = 0;
                    }
                }
            }
            else if (ev.type == SDL_MOUSEWHEEL && state.show_details) {
                state.desc_scroll_lines = std::max(0, state.desc_scroll_lines - ev.wheel.y * 2);
            }
            else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (!controller) {
                    controller = SDL_GameControllerOpen(ev.cdevice.which);
                    if (controller) state.gamepad_name = SDL_GameControllerName(controller);
                }
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (controller) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                    state.gamepad_name = "Клавиатура";
                }
            }
        }

        // Right stick smooth scroll in details modal
        if (state.show_details && (state.right_stick_y > 16000 || state.right_stick_y < -16000)) {
            uint32_t now_ms = SDL_GetTicks();
            if (now_ms - state.last_stick_scroll_ms >= 90) {
                state.last_stick_scroll_ms = now_ms;
                state.desc_scroll_lines = std::max(0, state.desc_scroll_lines + (state.right_stick_y > 0 ? 1 : -1));
            }
        }

        // Adjust scroll window
        int cur_row = state.selected_item / GRID_COLS;
        if (cur_row < state.scroll_row) {
            state.scroll_row = cur_row;
        } else if (cur_row >= state.scroll_row + GRID_ROWS) {
            state.scroll_row = cur_row - GRID_ROWS + 1;
        }

        // ----------------- RENDER -----------------
        SDL_SetRenderDrawColor(renderer, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 255);
        SDL_RenderClear(renderer);

        // Header
        SDL_Rect header_rect = { 0, 0, CANVAS_WIDTH, 100 };
        SDL_SetRenderDrawColor(renderer, COLOR_HEADER.r, COLOR_HEADER.g, COLOR_HEADER.b, 255);
        SDL_RenderFillRect(renderer, &header_rect);

        if (icons.app_icon) {
            render_icon(renderer, icons.app_icon, 56, 18, 108, 64);
        } else {
            render_text(renderer, font_title, "PS4 STORRE", 60, 28, COLOR_WHITE);
        }

        // Tabs with L1 / R1 bumper icons
        render_icon(renderer, icons.l1, 205, 41, 48, 18);
        int tab_x = 275;
        for (size_t i = 0; i < state.tabs.size(); ++i) {
            bool is_active = ((int)i == state.active_tab);
            int tw = 0, th = 0;
            TTF_SizeUTF8(font_ui, state.tabs[i].c_str(), &tw, &th);
            render_text(renderer, font_ui, state.tabs[i], tab_x, 36, is_active ? COLOR_WHITE : COLOR_MUTED);
            if (is_active) {
                SDL_Rect bar = { tab_x - 4, 78, tw + 8, 4 };
                SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                SDL_RenderFillRect(renderer, &bar);
            }
            tab_x += tw + 75;
        }
        render_icon(renderer, icons.r1, tab_x - 40, 41, 48, 18);

        // HDD Free Space Indicator in Header (right-aligned)
        uint64_t free_b = get_hdd_free_bytes();
        double free_gb = (double)free_b / (1024.0 * 1024.0 * 1024.0);
        char hdd_str[64];
        snprintf(hdd_str, sizeof(hdd_str), "HDD: %.1f ГБ свободно", free_gb);
        SDL_Color hdd_col = (free_gb > 50.0) ? COLOR_GREEN : ((free_gb > 15.0) ? COLOR_YELLOW : COLOR_RED);
        int hdd_w = 0, hdd_h = 0;
        TTF_SizeUTF8(font_ui, hdd_str, &hdd_w, &hdd_h);
        render_text(renderer, font_ui, hdd_str, CANVAS_WIDTH - 50 - hdd_w, 36, hdd_col);

        // ================= TAB 0: CATALOG =================
        if (state.active_tab == 0) {
            // Alphabet Strip Bar + Integrated Search Bar
            SDL_Rect strip_rect = { 60, 110, CANVAS_WIDTH - 120, 48 };
            SDL_SetRenderDrawColor(renderer, COLOR_STRIP_BG.r, COLOR_STRIP_BG.g, COLOR_STRIP_BG.b, 255);
            SDL_RenderFillRect(renderer, &strip_rect);

            render_icon(renderer, icons.l2, 72, 125, 48, 18);

            int btn_x = 132;
            for (size_t i = 0; i < ALPHABET_LABELS.size(); ++i) {
                int btn_w = (i == 0) ? 48 : 34;
                bool is_current = ((int)i == state.alphabet_idx && state.search_query.empty());

                SDL_Rect let_bg = { btn_x, 118, btn_w, 32 };
                if (is_current) {
                    SDL_Color bg_c = (state.focus == FocusArea::ALPHABET_STRIP) ? COLOR_CARD_SELECT : COLOR_ACCENT;
                    SDL_SetRenderDrawColor(renderer, bg_c.r, bg_c.g, bg_c.b, 255);
                    SDL_RenderFillRect(renderer, &let_bg);
                }

                int lw = 0, lh = 0;
                TTF_SizeUTF8(font_alpha, ALPHABET_LABELS[i].c_str(), &lw, &lh);
                render_text(renderer, font_alpha, ALPHABET_LABELS[i], btn_x + (btn_w - lw) / 2, 122, is_current ? COLOR_WHITE : COLOR_MUTED);

                btn_x += btn_w + 4;
            }

            render_icon(renderer, icons.r2, btn_x + 4, 125, 48, 18);

            // Search Bar Widget on Right of Strip
            int sb_x = 1280;
            SDL_Rect sb_rect = { sb_x, 115, 560, 38 };
            SDL_SetRenderDrawColor(renderer, (state.focus == FocusArea::SEARCH_INPUT) ? 35 : 18,
                                             (state.focus == FocusArea::SEARCH_INPUT) ? 50 : 25,
                                             (state.focus == FocusArea::SEARCH_INPUT) ? 75 : 38, 255);
            SDL_RenderFillRect(renderer, &sb_rect);

            if (state.focus == FocusArea::SEARCH_INPUT) {
                SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                SDL_RenderDrawRect(renderer, &sb_rect);
            }

            render_icon(renderer, icons.triangle, sb_x + 12, 123, 22, 22);
            std::string stext = state.search_query.empty() ? "Поиск по названию или CUSA..." : (state.search_query + "_");
            render_text(renderer, font_card, truncate_utf8(stext, 42), sb_x + 44, 122,
                        (state.focus == FocusArea::SEARCH_INPUT || !state.search_query.empty()) ? COLOR_WHITE : COLOR_MUTED);

            // Game Cards Grid (6 x 2, square PS4 cover art + 2-line titles + status micro-badges)
            int total_filtered = (int)active_items.size();
            if (total_filtered == 0) {
                SDL_Rect no_res = { 110, 260, CANVAS_WIDTH - 220, 180 };
                SDL_SetRenderDrawColor(renderer, COLOR_CARD_BG.r, COLOR_CARD_BG.g, COLOR_CARD_BG.b, 255);
                SDL_RenderFillRect(renderer, &no_res);
                render_text(renderer, font_ui, "Ничего не найдено по запросу: \"" + state.search_query + "\"", 150, 310, COLOR_WHITE);
                render_text(renderer, font_desc, "Попробуйте изменить запрос или сбросьте фильтр кнопкой [□] на нижней панели.", 150, 360, COLOR_MUTED);
            } else {
                auto q_snapshot = queue_mgr.get_tasks();

                for (int r = 0; r < GRID_ROWS; ++r) {
                    for (int c = 0; c < GRID_COLS; ++c) {
                        int item_pos = (state.scroll_row + r) * GRID_COLS + c;
                        if (item_pos >= total_filtered) break;

                        size_t real_game_idx = active_items[item_pos];
                        const GameItem* item = catalog.get_game(real_game_idx);
                        if (!item) continue;

                        int cx = GRID_START_X + c * (CARD_WIDTH + CARD_GAP_X);
                        int cy = GRID_START_Y + r * (CARD_HEIGHT + CARD_GAP_Y);
                        bool is_selected = (item_pos == state.selected_item && state.focus == FocusArea::GRID);

                        SDL_Rect card_rect = { cx, cy, CARD_WIDTH, CARD_HEIGHT };
                        SDL_SetRenderDrawColor(renderer, is_selected ? 30 : COLOR_CARD_BG.r,
                                                         is_selected ? 40 : COLOR_CARD_BG.g,
                                                         is_selected ? 58 : COLOR_CARD_BG.b, 255);
                        SDL_RenderFillRect(renderer, &card_rect);

                        // 1:1 Square PS4 Cover Art
                        int img_h = 250;
                        SDL_Rect img_rect = { cx, cy, CARD_WIDTH, img_h };
                        SDL_Texture* cover_tex = catalog.get_cover_texture(renderer, real_game_idx);

                        if (cover_tex) {
                            SDL_RenderCopy(renderer, cover_tex, nullptr, &img_rect);
                        } else {
                            SDL_SetRenderDrawColor(renderer, 35, 42, 60, 255);
                            SDL_RenderFillRect(renderer, &img_rect);
                            render_text(renderer, font_card, item->id, cx + 68, cy + 112, COLOR_MUTED);
                        }

                        // Corner mini-badges over cover art (only localization & install/queue status)
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                        if (item->has_rus_voice || item->has_rus_ui) {
                            render_badge(renderer, font_card, "RUS", cx + 8, cy + 8, { 25, 115, 62, 225 }, COLOR_WHITE);
                        }

                        bool g_active = false;
                        bool g_inst = false;
                        for (const auto& qt : q_snapshot) {
                            if (qt.id == item->id) {
                                if (qt.torrent_active) g_active = true;
                                if (qt.is_installed) g_inst = true;
                            }
                        }
                        if (g_active) {
                            render_badge(renderer, font_card, "В ОЧЕРЕДИ", cx + 8, cy + img_h - 38, { 35, 130, 75, 235 }, COLOR_WHITE);
                        } else if (g_inst) {
                            render_badge(renderer, font_card, "УСТАНОВЛЕНО", cx + 8, cy + img_h - 38, { 22, 110, 150, 235 }, COLOR_WHITE);
                        }

                        // 2-line wrapped proportional title + size/year footer
                        std::string tline1, tline2;
                        wrap_two_lines(font_card, item->title, CARD_WIDTH - 20, tline1, tline2);
                        render_text(renderer, font_card, tline1, cx + 10, cy + img_h + 8, COLOR_WHITE);
                        if (!tline2.empty()) {
                            render_text(renderer, font_card, tline2, cx + 10, cy + img_h + 34, COLOR_WHITE);
                        }

                        std::string card_meta;
                        if (item->size_mb > 0 && item->year > 0) {
                            card_meta = format_size_mb(item->size_mb) + " • " + std::to_string(item->year);
                        } else if (item->size_mb > 0) {
                            card_meta = format_size_mb(item->size_mb);
                        } else if (item->year > 0) {
                            card_meta = std::to_string(item->year);
                        }
                        if (!card_meta.empty()) {
                            render_text(renderer, font_card, card_meta, cx + 10, cy + img_h + 80, COLOR_MUTED);
                        }

                        if (is_selected) {
                            SDL_Rect glow = { cx - 5, cy - 5, CARD_WIDTH + 10, CARD_HEIGHT + 10 };
                            SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 90);
                            SDL_RenderDrawRect(renderer, &glow);

                            SDL_Rect outline1 = { cx - 3, cy - 3, CARD_WIDTH + 6, CARD_HEIGHT + 6 };
                            SDL_Rect outline2 = { cx - 2, cy - 2, CARD_WIDTH + 4, CARD_HEIGHT + 4 };
                            SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                            SDL_RenderDrawRect(renderer, &outline1);
                            SDL_RenderDrawRect(renderer, &outline2);
                        }
                    }
                }

                // Vertical Grid Scrollbar on the right
                int total_rows = (total_filtered + GRID_COLS - 1) / GRID_COLS;
                if (total_rows > GRID_ROWS) {
                    int track_x = 1844;
                    int track_y = GRID_START_Y;
                    int track_h = GRID_ROWS * CARD_HEIGHT + (GRID_ROWS - 1) * CARD_GAP_Y;
                    SDL_Rect track_rect = { track_x, track_y, 6, track_h };
                    SDL_SetRenderDrawColor(renderer, 25, 33, 46, 255);
                    SDL_RenderFillRect(renderer, &track_rect);

                    int max_scroll_rows = total_rows - GRID_ROWS;
                    int thumb_h = std::max(36, (track_h * GRID_ROWS) / total_rows);
                    int thumb_y = track_y + ((track_h - thumb_h) * std::min(state.scroll_row, max_scroll_rows)) / max_scroll_rows;
                    SDL_Rect thumb_rect = { track_x - 1, thumb_y, 8, thumb_h };
                    SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                    SDL_RenderFillRect(renderer, &thumb_rect);
                }
            }
        }
        // ================= TAB 1: DOWNLOADS / QUEUE =================
        else if (state.active_tab == 1) {
            auto tasks = queue_mgr.get_tasks();

            render_text(renderer, font_title, "ОЧЕРЕДЬ ЗАГРУЗОК И УСТАНОВКИ", 110, 128, COLOR_WHITE);

            std::string web_url = queue_mgr.get_web_ui_url();
            std::string web_hint = "Веб-интерфейс управления: " + web_url;
            render_text(renderer, font_desc, web_hint, 110, 176, COLOR_CARD_SELECT);

#if defined(_WIN32)
            std::string t_status = queue_mgr.is_torrent_service_alive() ? "storre_torrentd (8788): ONLINE" : "storre_torrentd: ПК-ПРЕВЬЮ (симуляция)";
            std::string i_status = queue_mgr.is_installer_service_alive() ? "storre_installerd (8285): ONLINE" : "storre_installerd: ПК-ПРЕВЬЮ (симуляция)";
#else
            std::string t_status = queue_mgr.is_torrent_service_alive() ? "storre_torrentd (8788): ONLINE" : "storre_torrentd: OFFLINE (нужен BinLoader 9090)";
            std::string i_status = queue_mgr.is_installer_service_alive() ? "storre_installerd (8285): ONLINE" : "storre_installerd: OFFLINE (нужен BinLoader 9090)";
#endif
            int tsw = 0, isw = 0;
            TTF_SizeUTF8(font_card, t_status.c_str(), &tsw, nullptr);
            TTF_SizeUTF8(font_card, i_status.c_str(), &isw, nullptr);
            render_text(renderer, font_card, t_status, CANVAS_WIDTH - 110 - tsw, 134, queue_mgr.is_torrent_service_alive() ? COLOR_GREEN : COLOR_YELLOW);
            render_text(renderer, font_card, i_status, CANVAS_WIDTH - 110 - isw, 166, queue_mgr.is_installer_service_alive() ? COLOR_GREEN : COLOR_YELLOW);

            std::vector<size_t> dl_indices;
            for (size_t i = 0; i < tasks.size(); ++i) {
                if (tasks[i].torrent_active) dl_indices.push_back(i);
            }

            if (dl_indices.empty()) {
                SDL_Rect empty_box = { 110, 225, CANVAS_WIDTH - 220, 200 };
                SDL_SetRenderDrawColor(renderer, COLOR_CARD_BG.r, COLOR_CARD_BG.g, COLOR_CARD_BG.b, 255);
                SDL_RenderFillRect(renderer, &empty_box);
                render_text(renderer, font_ui, "Активных загрузок и раздач нет.", 150, 265, COLOR_WHITE);
                render_text(renderer, font_desc, "Игры можно скачивать из вкладки «Каталог» или добавлять через веб-интерфейс.", 150, 308, COLOR_MUTED);
                render_text(renderer, font_card, "Ссылка для браузера (ПК / смартфон): " + web_url, 150, 345, COLOR_CARD_SELECT);
            } else {
                const int VISIBLE_LIST_ROWS = 5;
                if (state.selected_download < 0) state.selected_download = 0;
                if (state.selected_download >= (int)dl_indices.size()) state.selected_download = (int)dl_indices.size() - 1;
                if (state.selected_download < state.downloads_scroll_offset) {
                    state.downloads_scroll_offset = state.selected_download;
                } else if (state.selected_download >= state.downloads_scroll_offset + VISIBLE_LIST_ROWS) {
                    state.downloads_scroll_offset = state.selected_download - VISIBLE_LIST_ROWS + 1;
                }

                int ty = 225;
                for (int vi = 0; vi < VISIBLE_LIST_ROWS; ++vi) {
                    int list_pos = state.downloads_scroll_offset + vi;
                    if (list_pos >= (int)dl_indices.size()) break;

                    const auto& t = tasks[dl_indices[list_pos]];
                    bool is_selected = (list_pos == state.selected_download);

                    SDL_Rect row_rect = { 110, ty, CANVAS_WIDTH - 240, 115 };
                    SDL_SetRenderDrawColor(renderer, is_selected ? 30 : COLOR_CARD_BG.r,
                                                     is_selected ? 38 : COLOR_CARD_BG.g,
                                                     is_selected ? 56 : COLOR_CARD_BG.b, 255);
                    SDL_RenderFillRect(renderer, &row_rect);

                    SDL_Rect thumb = { 125, ty + 10, 95, 95 };
                    SDL_Texture* c_tex = catalog.get_cover_texture(renderer, t.catalog_index);
                    if (c_tex) SDL_RenderCopy(renderer, c_tex, nullptr, &thumb);

                    render_text(renderer, font_ui, truncate_utf8(t.title, 62), 238, ty + 14, COLOR_WHITE);
                    std::string size_part = (t.size_mb > 0) ? (format_size_mb(t.size_mb) + " • ") : "";
                    render_text(renderer, font_card, t.id + " • " + size_part + t.status_text, 238, ty + 47, COLOR_MUTED);

                    std::string badge_txt = "ОЖИДАНИЕ";
                    SDL_Color badge_bg = { 45, 55, 75, 255 };
                    if (t.state == TaskState::DOWNLOADING) { badge_txt = "СКАЧИВАНИЕ"; badge_bg = COLOR_ACCENT; }
                    else if (t.state == TaskState::PAUSED) { badge_txt = "ПАУЗА"; badge_bg = { 160, 120, 20, 255 }; }
                    else if (t.state == TaskState::INSTALLING) { badge_txt = "УСТАНОВКА BGFT"; badge_bg = { 160, 120, 20, 255 }; }
                    else if (t.state == TaskState::SEEDING) { badge_txt = "РАЗДАЧА P2P"; badge_bg = { 35, 130, 75, 255 }; }
                    else if (t.state == TaskState::COMPLETED) { badge_txt = "УСТАНОВЛЕНО"; badge_bg = { 35, 130, 75, 255 }; }
                    else if (t.state == TaskState::ERROR_STATE) { badge_txt = "ОШИБКА"; badge_bg = { 160, 45, 45, 255 }; }

                    int btw = 0, bth = 0;
                    TTF_SizeUTF8(font_card, badge_txt.c_str(), &btw, &bth);
                    render_badge(renderer, font_card, badge_txt, CANVAS_WIDTH - 150 - (btw + 20), ty + 16, badge_bg, COLOR_WHITE);

                    int bar_w = 1445;
                    SDL_Rect p_back = { 238, ty + 80, bar_w, 14 };
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_RenderFillRect(renderer, &p_back);

                    float fill_pct = (t.state == TaskState::INSTALLING) ? (t.install_progress / 100.0f) : (t.download_progress / 100.0f);
                    if (t.state == TaskState::COMPLETED || t.state == TaskState::SEEDING) fill_pct = 1.0f;
                    fill_pct = std::clamp(fill_pct, 0.0f, 1.0f);

                    SDL_Rect p_fill = { 238, ty + 80, (int)(bar_w * fill_pct), 14 };
                    SDL_Color bar_color = (t.state == TaskState::PAUSED) ? COLOR_YELLOW :
                                         ((t.state == TaskState::INSTALLING) ? COLOR_YELLOW :
                                         ((t.state == TaskState::COMPLETED || t.state == TaskState::SEEDING) ? COLOR_GREEN : COLOR_ACCENT));
                    SDL_SetRenderDrawColor(renderer, bar_color.r, bar_color.g, bar_color.b, 255);
                    SDL_RenderFillRect(renderer, &p_fill);

                    int pct_int = (t.state == TaskState::INSTALLING) ? (int)t.install_progress : (int)t.download_progress;
                    if (t.state == TaskState::COMPLETED || t.state == TaskState::SEEDING) pct_int = 100;
                    render_text(renderer, font_ui, std::to_string(pct_int) + "%", 238 + bar_w + 18, ty + 72, bar_color);

                    if (is_selected) {
                        SDL_Rect outline1 = { 108, ty - 2, CANVAS_WIDTH - 236, 119 };
                        SDL_Rect outline2 = { 109, ty - 1, CANVAS_WIDTH - 238, 117 };
                        SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                        SDL_RenderDrawRect(renderer, &outline1);
                        SDL_RenderDrawRect(renderer, &outline2);
                    }

                    ty += 135;
                }

                if ((int)dl_indices.size() > VISIBLE_LIST_ROWS) {
                    int track_x = CANVAS_WIDTH - 118;
                    int track_y = 225;
                    int track_h = VISIBLE_LIST_ROWS * 135 - 20;
                    SDL_Rect track_rect = { track_x, track_y, 6, track_h };
                    SDL_SetRenderDrawColor(renderer, 25, 33, 46, 255);
                    SDL_RenderFillRect(renderer, &track_rect);

                    int thumb_h = std::max(32, (track_h * VISIBLE_LIST_ROWS) / (int)dl_indices.size());
                    int max_scroll = (int)dl_indices.size() - VISIBLE_LIST_ROWS;
                    int thumb_y = track_y + ((track_h - thumb_h) * state.downloads_scroll_offset) / max_scroll;
                    SDL_Rect thumb_rect = { track_x - 1, thumb_y, 8, thumb_h };
                    SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                    SDL_RenderFillRect(renderer, &thumb_rect);
                }
            }
        }
        // ================= TAB 2: INSTALLED =================
        else if (state.active_tab == 2) {
            render_text(renderer, font_title, "УСТАНОВЛЕННЫЕ ИГРЫ", 110, 134, COLOR_WHITE);

            if (!state.status_notice.empty() && SDL_GetTicks() < state.status_notice_until) {
                int nw = 0, nh = 0;
                TTF_SizeUTF8(font_desc, state.status_notice.c_str(), &nw, &nh);
                render_text(renderer, font_desc, state.status_notice, CANVAS_WIDTH - 110 - nw, 148, COLOR_YELLOW);
            }

            auto tasks = queue_mgr.get_tasks();
            std::vector<size_t> inst_indices;
            for (size_t i = 0; i < tasks.size(); ++i) {
                if (tasks[i].is_installed) inst_indices.push_back(i);
            }

            if (inst_indices.empty()) {
                SDL_Rect empty_box = { 110, 225, CANVAS_WIDTH - 220, 180 };
                SDL_SetRenderDrawColor(renderer, COLOR_CARD_BG.r, COLOR_CARD_BG.g, COLOR_CARD_BG.b, 255);
                SDL_RenderFillRect(renderer, &empty_box);
                render_text(renderer, font_ui, "Установленных через Storre игр пока нет.", 150, 275, COLOR_WHITE);
                render_text(renderer, font_desc, "После завершения загрузки и фоновой установки bgft игры появятся здесь.", 150, 318, COLOR_MUTED);
            } else {
                const int VISIBLE_LIST_ROWS = 5;
                if (state.selected_installed < 0) state.selected_installed = 0;
                if (state.selected_installed >= (int)inst_indices.size()) state.selected_installed = (int)inst_indices.size() - 1;
                if (state.selected_installed < state.installed_scroll_offset) {
                    state.installed_scroll_offset = state.selected_installed;
                } else if (state.selected_installed >= state.installed_scroll_offset + VISIBLE_LIST_ROWS) {
                    state.installed_scroll_offset = state.selected_installed - VISIBLE_LIST_ROWS + 1;
                }

                int ty = 225;
                for (int vi = 0; vi < VISIBLE_LIST_ROWS; ++vi) {
                    int list_pos = state.installed_scroll_offset + vi;
                    if (list_pos >= (int)inst_indices.size()) break;

                    const auto& t = tasks[inst_indices[list_pos]];
                    bool is_selected = (list_pos == state.selected_installed);

                    SDL_Rect row_rect = { 110, ty, CANVAS_WIDTH - 240, 100 };
                    SDL_SetRenderDrawColor(renderer, is_selected ? 30 : COLOR_CARD_BG.r,
                                                     is_selected ? 38 : COLOR_CARD_BG.g,
                                                     is_selected ? 56 : COLOR_CARD_BG.b, 255);
                    SDL_RenderFillRect(renderer, &row_rect);

                    SDL_Rect thumb = { 125, ty + 10, 80, 80 };
                    SDL_Texture* c_tex = catalog.get_cover_texture(renderer, t.catalog_index);
                    if (c_tex) SDL_RenderCopy(renderer, c_tex, nullptr, &thumb);

                    render_text(renderer, font_ui, truncate_utf8(t.title, 64), 222, ty + 18, COLOR_WHITE);
                    render_text(renderer, font_card, t.id + " • Зарегистрировано в PS4 OS • Версия 01.00", 222, ty + 54, COLOR_MUTED);

                    std::string inst_status = (t.torrent_active && t.state == TaskState::SEEDING) ? "Раздаётся (P2P)" : "Готово к запуску";
                    SDL_Color inst_bg = (t.torrent_active && t.state == TaskState::SEEDING) ? SDL_Color{ 35, 95, 130, 255 } : SDL_Color{ 35, 135, 78, 255 };
                    int stw = 0, sth = 0;
                    TTF_SizeUTF8(font_card, inst_status.c_str(), &stw, &sth);
                    render_badge(renderer, font_card, inst_status, CANVAS_WIDTH - 150 - (stw + 20), ty + 34, inst_bg, COLOR_WHITE);

                    if (is_selected) {
                        SDL_Rect outline1 = { 108, ty - 2, CANVAS_WIDTH - 236, 104 };
                        SDL_Rect outline2 = { 109, ty - 1, CANVAS_WIDTH - 238, 102 };
                        SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                        SDL_RenderDrawRect(renderer, &outline1);
                        SDL_RenderDrawRect(renderer, &outline2);
                    }

                    ty += 120;
                }

                if ((int)inst_indices.size() > VISIBLE_LIST_ROWS) {
                    int track_x = CANVAS_WIDTH - 118;
                    int track_y = 225;
                    int track_h = VISIBLE_LIST_ROWS * 120 - 20;
                    SDL_Rect track_rect = { track_x, track_y, 6, track_h };
                    SDL_SetRenderDrawColor(renderer, 25, 33, 46, 255);
                    SDL_RenderFillRect(renderer, &track_rect);

                    int thumb_h = std::max(32, (track_h * VISIBLE_LIST_ROWS) / (int)inst_indices.size());
                    int max_scroll = (int)inst_indices.size() - VISIBLE_LIST_ROWS;
                    int thumb_y = track_y + ((track_h - thumb_h) * state.installed_scroll_offset) / max_scroll;
                    SDL_Rect thumb_rect = { track_x - 1, thumb_y, 8, thumb_h };
                    SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                    SDL_RenderFillRect(renderer, &thumb_rect);
                }
            }
        }
        // ================= TAB 3: SETTINGS =================
        else if (state.active_tab == 3) {
            render_text(renderer, font_title, "НАСТРОЙКИ СИСТЕМЫ И СЛУЖБ", 110, 130, COLOR_WHITE);

            if (queue_mgr.is_torrent_service_alive()) {
                auto cfg = queue_mgr.get_torrent_config();
                state.setting_parallel_count = cfg.max_parallel;
                state.setting_seeding_idx = queue_mgr.get_seeding_speed_index();
                state.setting_storage_idx = (cfg.save_to == "usb") ? 1 : 0;
                state.setting_autostart = cfg.autostart;
                state.setting_resume_session = cfg.resume_session;
            }

            struct SettingRow {
                std::string title;
                std::string desc;
                std::string value;
                SDL_Color val_color;
                bool editable;
            };

            const std::vector<std::string> storage_names = {
                "Внутренний HDD (/data/storre)",
                "Внешний накопитель (/mnt/usb0)",
                "Внешний накопитель (/mnt/usb1)"
            };

            const std::vector<std::string> seeding_names = {
                "32 КБ/с (Рекомендуется)",
                "128 КБ/с",
                "256 КБ/с",
                "1024 КБ/с (1 МБ/с)",
                "Без ограничений"
            };

            std::vector<SettingRow> s_rows = {
                {
                    "Параллельные загрузки (одновременные)",
                    "Максимальное количество одновременных закачек (1 для защиты HDD PS4)",
                    std::to_string(state.setting_parallel_count),
                    COLOR_ACCENT,
                    true
                },
                {
                    "Ограничение отдачи торрентов (Seeding)",
                    "Лимит скорости раздачи пирам после завершения загрузки",
                    seeding_names[state.setting_seeding_idx],
                    COLOR_YELLOW,
                    true
                },
                {
                    "Каталог сохранения загрузок",
                    "Целевой диск для сохранения торрентов и временных пакетов .pkg",
                    storage_names[state.setting_storage_idx],
                    COLOR_ACCENT,
                    true
                },
                {
                    "Автозапуск добавленных задач",
                    "Автоматически запускать скачивание сразу после добавления торрента",
                    state.setting_autostart ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО",
                    state.setting_autostart ? COLOR_GREEN : COLOR_MUTED,
                    true
                },
                {
                    "Возобновлять после перезапуска",
                    "Автоматически продолжать незавершенные закачки после старта консоли",
                    state.setting_resume_session ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО",
                    state.setting_resume_session ? COLOR_GREEN : COLOR_MUTED,
                    true
                },
                {
                    "Политика раздачи и очистки (Smart Seed & Clean)",
                    "Фоновая раздача до Ratio 2.0x или 7 дней, затем автоматическая очистка PKG",
                    "2.0x / 7 дней",
                    COLOR_GREEN,
                    false
                },
                {
                    "Синхронизация времени системы (NTP)",
                    "Коррекция системных часов консоли по серверу pool.ntp.org",
                    state.setting_ntp_status,
                    COLOR_GREEN,
                    true
                },
                {
                    "Веб-интерфейс торрента (storre_torrentd)",
                    "Доступ для управления раздачами через браузер любого смартфона или ПК в сети",
                    queue_mgr.get_web_ui_url(),
                    COLOR_CARD_SELECT,
                    false
                }
            };

            // Scroll window: show 7 rows at a time
            const int VISIBLE_SETTINGS_ROWS = 7;
            if (state.selected_setting < state.settings_scroll_offset) {
                state.settings_scroll_offset = state.selected_setting;
            } else if (state.selected_setting >= state.settings_scroll_offset + VISIBLE_SETTINGS_ROWS) {
                state.settings_scroll_offset = state.selected_setting - VISIBLE_SETTINGS_ROWS + 1;
            }

            int ty = 195;
            for (int vi = 0; vi < VISIBLE_SETTINGS_ROWS; ++vi) {
                int i = state.settings_scroll_offset + vi;
                if (i >= (int)s_rows.size()) break;

                const auto& sr = s_rows[i];
                bool is_selected = (i == state.selected_setting);

                SDL_Rect row_rect = { 110, ty, CANVAS_WIDTH - 240, 82 };
                SDL_SetRenderDrawColor(renderer, is_selected ? 30 : COLOR_CARD_BG.r,
                                                 is_selected ? 38 : COLOR_CARD_BG.g,
                                                 is_selected ? 56 : COLOR_CARD_BG.b, 255);
                SDL_RenderFillRect(renderer, &row_rect);

                render_text(renderer, font_ui, sr.title, 140, ty + 16, COLOR_WHITE);
                render_text(renderer, font_card, sr.desc, 140, ty + 48, COLOR_MUTED);

                // Clean right-aligned value (with directional hints when selected)
                std::string disp_val = (is_selected && sr.editable) ? ("<  " + sr.value + "  >") : sr.value;
                int vw = 0, vh = 0;
                TTF_SizeUTF8(font_ui, disp_val.c_str(), &vw, &vh);
                render_text(renderer, font_ui, disp_val, CANVAS_WIDTH - 160 - vw, ty + 27, sr.val_color);

                if (is_selected) {
                    SDL_Rect outline1 = { 108, ty - 2, CANVAS_WIDTH - 236, 86 };
                    SDL_Rect outline2 = { 109, ty - 1, CANVAS_WIDTH - 238, 84 };
                    SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                    SDL_RenderDrawRect(renderer, &outline1);
                    SDL_RenderDrawRect(renderer, &outline2);
                }

                ty += 95;
            }

            // Scrollbar track & thumb on the right
            if ((int)s_rows.size() > VISIBLE_SETTINGS_ROWS) {
                int track_x = CANVAS_WIDTH - 118;
                int track_y = 195;
                int track_h = VISIBLE_SETTINGS_ROWS * 95 - 13;
                SDL_Rect track_rect = { track_x, track_y, 6, track_h };
                SDL_SetRenderDrawColor(renderer, 25, 33, 46, 255);
                SDL_RenderFillRect(renderer, &track_rect);

                int thumb_h = (track_h * VISIBLE_SETTINGS_ROWS) / (int)s_rows.size();
                int max_scroll = (int)s_rows.size() - VISIBLE_SETTINGS_ROWS;
                int thumb_y = track_y + ((track_h - thumb_h) * state.settings_scroll_offset) / max_scroll;
                SDL_Rect thumb_rect = { track_x - 1, thumb_y, 8, thumb_h };
                SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                SDL_RenderFillRect(renderer, &thumb_rect);
            }
        }

        // Details Modal Popup
        bool modal_torrent_active = false;
        bool modal_installed = false;
        if (state.show_details && state.selected_item < (int)active_items.size()) {
            size_t real_game_idx = active_items[state.selected_item];
            const GameItem* sel = catalog.get_game(real_game_idx);
            if (sel) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
                SDL_Rect screen_rect = { 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT };
                SDL_RenderFillRect(renderer, &screen_rect);

                SDL_Rect modal = { 250, 120, 1420, 845 };
                SDL_SetRenderDrawColor(renderer, COLOR_HEADER.r, COLOR_HEADER.g, COLOR_HEADER.b, 255);
                SDL_RenderFillRect(renderer, &modal);
                SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                SDL_RenderDrawRect(renderer, &modal);

                // Left column: 1:1 square cover art + status pill + wrapped badges + wrapped Content ID
                SDL_Rect big_cover = { 290, 158, 340, 340 };
                SDL_Texture* c_tex = catalog.get_cover_texture(renderer, real_game_idx);
                if (c_tex) {
                    SDL_RenderCopy(renderer, c_tex, nullptr, &big_cover);
                } else {
                    SDL_SetRenderDrawColor(renderer, 35, 42, 60, 255);
                    SDL_RenderFillRect(renderer, &big_cover);
                }

                modal_torrent_active = queue_mgr.is_torrent_active(sel->id);
                modal_installed = queue_mgr.is_game_installed(sel->id);
                if (modal_torrent_active) {
                    render_badge(renderer, font_card, "В очереди / Загружается", 290, 514, { 35, 130, 75, 255 }, COLOR_WHITE);
                } else if (modal_installed) {
                    render_badge(renderer, font_card, "Установлено на PS4", 290, 514, { 35, 110, 145, 255 }, COLOR_WHITE);
                } else {
                    render_badge(renderer, font_card, "Готово к скачиванию", 290, 514, { 26, 52, 86, 255 }, COLOR_CARD_SELECT);
                }

                // Localization & FW calculation
                std::string lower_t = sel->title;
                for (char& ch : lower_t) ch = (char)tolower((unsigned char)ch);
                std::string loc_label = "ENG / MULTI";
                SDL_Color loc_col = { 45, 55, 70, 255 };
                SDL_Color loc_txt_col = COLOR_MUTED;
                if (sel->has_rus_voice) {
                    loc_label = "RUS Озвучка";
                    loc_col = { 25, 95, 55, 255 };
                    loc_txt_col = COLOR_WHITE;
                } else if (sel->has_rus_ui || lower_t.find("rus") != std::string::npos || lower_t.find("рус") != std::string::npos || lower_t.find("русификатор") != std::string::npos) {
                    loc_label = "RUS Текст";
                    loc_col = { 25, 95, 55, 255 };
                    loc_txt_col = COLOR_WHITE;
                }

                std::string fw_badge = sel->is_backport ? "Backport 5.05+" : "FW 5.05+";
                if (!sel->is_backport) {
                    if (lower_t.find("11.00") != std::string::npos) fw_badge = "FW 11.00+";
                    else if (lower_t.find("9.00") != std::string::npos) fw_badge = "FW 9.00+";
                    else if (lower_t.find("6.72") != std::string::npos) fw_badge = "FW 6.72+";
                }

                // Wrap-flow badge cloud under cover (max width 340px: 290..630)
                int bx = 290;
                int by = 564;
                const int badge_max_x = 630;
                auto draw_flow_badge = [&](TTF_Font* bf, const std::string& label, SDL_Color bg, SDL_Color fg) {
                    int tw = 0, th = 0;
                    TTF_SizeUTF8(bf, label.c_str(), &tw, &th);
                    int pw = tw + 20;
                    if (bx > 290 && bx + pw > badge_max_x) {
                        bx = 290;
                        by += 40;
                    }
                    render_badge(renderer, bf, label, bx, by, bg, fg);
                    bx += pw + 10;
                };

                draw_flow_badge(font_card, sel->id, { 30, 60, 100, 255 }, COLOR_WHITE);
                if (sel->year > 0) {
                    draw_flow_badge(font_card, std::to_string(sel->year) + " г.", { 42, 54, 74, 255 }, COLOR_WHITE);
                }
                if (sel->size_mb > 0) {
                    draw_flow_badge(font_card, format_size_mb(sel->size_mb), { 22, 95, 130, 255 }, COLOR_WHITE);
                }
                draw_flow_badge(font_card, loc_label, loc_col, loc_txt_col);
                draw_flow_badge(font_card, fw_badge, { 110, 85, 20, 255 }, COLOR_WHITE);
                if (sel->has_dlc) {
                    draw_flow_badge(font_card, "DLC", { 90, 50, 120, 255 }, COLOR_WHITE);
                }

                // Wrapped Content ID card below the badge cloud
                if (!sel->cid.empty()) {
                    int cid_box_y = by + 50;
                    std::vector<std::string> cid_lines;
                    std::string rem = sel->cid;
                    while (!rem.empty() && cid_lines.size() < 3) {
                        int rw = 0;
                        TTF_SizeUTF8(font_card, rem.c_str(), &rw, nullptr);
                        if (rw <= 312) {
                            cid_lines.push_back(rem);
                            break;
                        }
                        size_t split_pos = rem.size();
                        size_t last_sep = std::string::npos;
                        for (size_t k = 1; k <= rem.size(); ++k) {
                            int cw_px = 0;
                            TTF_SizeUTF8(font_card, rem.substr(0, k).c_str(), &cw_px, nullptr);
                            if (cw_px > 312) {
                                split_pos = (last_sep != std::string::npos && last_sep > 6) ? (last_sep + 1) : (k - 1);
                                break;
                            }
                            if (rem[k - 1] == '-' || rem[k - 1] == '_') last_sep = k - 1;
                        }
                        if (split_pos == 0) split_pos = 1;
                        cid_lines.push_back(rem.substr(0, split_pos));
                        rem = rem.substr(split_pos);
                    }

                    int cid_box_h = 48 + (int)cid_lines.size() * 28;
                    SDL_Rect cid_card = { 290, cid_box_y, 340, cid_box_h };
                    SDL_SetRenderDrawColor(renderer, 19, 25, 38, 255);
                    SDL_RenderFillRect(renderer, &cid_card);
                    SDL_SetRenderDrawColor(renderer, 38, 50, 72, 255);
                    SDL_RenderDrawRect(renderer, &cid_card);

                    render_text(renderer, font_card, "Content ID:", 304, cid_box_y + 10, COLOR_MUTED);
                    for (size_t li = 0; li < cid_lines.size(); ++li) {
                        render_text(renderer, font_card, cid_lines[li], 304, cid_box_y + 40 + (int)li * 28, COLOR_CARD_SELECT);
                    }
                }

                // Right column: title only at top + taller scrollable description directly underneath
                std::string tline1, tline2;
                wrap_two_lines(font_title, sel->title, 945, tline1, tline2);
                render_text(renderer, font_title, tline1, 665, 152, COLOR_WHITE);
                if (!tline2.empty()) {
                    render_text(renderer, font_title, tline2, 665, 196, COLOR_WHITE);
                }

                int desc_hdr_y = tline2.empty() ? 214 : 254;
                int desc_y = desc_hdr_y + 36;
                std::string desc = catalog.get_description(real_game_idx);
                if (!desc.empty()) {
                    if (cached_desc_game_idx != real_game_idx) {
                        if (cached_desc_tex) {
                            SDL_DestroyTexture(cached_desc_tex);
                            cached_desc_tex = nullptr;
                        }
                        cached_desc_w = 0;
                        cached_desc_h = 0;
#if defined(_WIN32)
                        cached_desc_line_h = font_desc ? TTF_FontLineSkip(font_desc) : 26;
#else
                        if (font_desc) TTF_SizeUTF8(font_desc, "Ay", nullptr, &cached_desc_line_h);
#endif
                        if (cached_desc_line_h <= 0) cached_desc_line_h = 26;

                        if (font_desc) {
                            SDL_Surface* dsurf = TTF_RenderUTF8_Blended_Wrapped(font_desc, desc.c_str(), COLOR_MUTED, 940);
                            if (dsurf) {
                                cached_desc_tex = SDL_CreateTextureFromSurface(renderer, dsurf);
                                cached_desc_w = dsurf->w;
                                cached_desc_h = dsurf->h;
                                SDL_FreeSurface(dsurf);
                            }
                        }
                        cached_desc_game_idx = real_game_idx;
                    }

                    render_text(renderer, font_desc, "Описание игры:", 665, desc_hdr_y, COLOR_WHITE);

                    if (cached_desc_tex && cached_desc_h > 0) {
                        const int max_box_h = 935 - desc_y;
                        int visible_lines = std::max(1, max_box_h / cached_desc_line_h);
                        int view_h = visible_lines * cached_desc_line_h;
                        int total_lines = (cached_desc_h + cached_desc_line_h - 1) / cached_desc_line_h;
                        int max_scroll_lines = std::max(0, total_lines - visible_lines);

                        state.desc_scroll_lines = std::clamp(state.desc_scroll_lines, 0, max_scroll_lines);
                        int scroll_y = state.desc_scroll_lines * cached_desc_line_h;
                        int copy_h = std::min(view_h, std::max(0, cached_desc_h - scroll_y));

                        SDL_Rect src_r = { 0, scroll_y, cached_desc_w, copy_h };
                        SDL_Rect dst_r = { 665, desc_y, cached_desc_w, copy_h };
                        SDL_RenderCopy(renderer, cached_desc_tex, &src_r, &dst_r);

                        if (max_scroll_lines > 0) {
                            std::string scroll_hint = "Прокрутка: D-Pad / Стик";
                            int shw = 0, shh = 0;
                            TTF_SizeUTF8(font_card, scroll_hint.c_str(), &shw, &shh);
                            render_text(renderer, font_card, scroll_hint, 1605 - shw, desc_hdr_y + 2, COLOR_CARD_SELECT);

                            int track_x = 1626;
                            int track_y = desc_y;
                            int track_h = view_h;
                            SDL_Rect track_rect = { track_x, track_y, 6, track_h };
                            SDL_SetRenderDrawColor(renderer, 25, 33, 46, 255);
                            SDL_RenderFillRect(renderer, &track_rect);

                            int thumb_h = std::max(28, (track_h * visible_lines) / total_lines);
                            int thumb_y = track_y + ((track_h - thumb_h) * state.desc_scroll_lines) / max_scroll_lines;
                            SDL_Rect thumb_rect = { track_x - 1, thumb_y, 8, thumb_h };
                            SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
                            SDL_RenderFillRect(renderer, &thumb_rect);
                        }
                    }
                }
            }
        }

        // On-Screen Keyboard Overlay (DualShock-friendly EN/RU search input without covering top catalog row)
        if (!state.show_details && state.confirm == ConfirmType::NONE && state.active_tab == 0 && state.focus == FocusArea::SEARCH_INPUT) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_Rect osk_bg = { 160, 560, CANVAS_WIDTH - 320, 428 };
            SDL_SetRenderDrawColor(renderer, 14, 19, 30, 245);
            SDL_RenderFillRect(renderer, &osk_bg);
            SDL_SetRenderDrawColor(renderer, COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, 255);
            SDL_RenderDrawRect(renderer, &osk_bg);

            std::string lang_tag = state.osk_ru ? "RU (ЙЦУКЕН)" : "EN (QWERTY)";
            render_text(renderer, font_ui, "ПОИСК В КАТАЛОГЕ", 196, 578, COLOR_WHITE);
            render_badge(renderer, font_card, lang_tag, 466, 579, { 30, 60, 100, 255 }, COLOR_CARD_SELECT);

            std::string query_disp = state.search_query.empty() ? "Наберите название игры или CUSA..." : (state.search_query + "_");
            SDL_Rect qbox = { 660, 575, 1064, 42 };
            SDL_SetRenderDrawColor(renderer, 22, 30, 46, 255);
            SDL_RenderFillRect(renderer, &qbox);
            SDL_SetRenderDrawColor(renderer, 55, 80, 115, 255);
            SDL_RenderDrawRect(renderer, &qbox);
            render_text(renderer, font_ui, truncate_utf8(query_disp, 58), 676, 581,
                        state.search_query.empty() ? COLOR_MUTED : COLOR_WHITE);

            const auto& cur_rows = state.osk_ru ? osk_ru : osk_en;
            int row_y = 638;
            for (int r = 0; r < (int)cur_rows.size(); ++r) {
                const auto& row_keys = cur_rows[r];
                int nkeys = (int)row_keys.size();
                if (r < 4) {
                    int key_w = 112;
                    int key_gap = 10;
                    int total_w = nkeys * key_w + (nkeys - 1) * key_gap;
                    int start_x = (CANVAS_WIDTH - total_w) / 2;
                    for (int c = 0; c < nkeys; ++c) {
                        bool sel_k = (r == state.osk_row && c == state.osk_col);
                        SDL_Rect krect = { start_x + c * (key_w + key_gap), row_y, key_w, 54 };
                        SDL_SetRenderDrawColor(renderer, sel_k ? COLOR_ACCENT.r : 28,
                                                         sel_k ? COLOR_ACCENT.g : 36,
                                                         sel_k ? COLOR_ACCENT.b : 54, 255);
                        SDL_RenderFillRect(renderer, &krect);
                        if (sel_k) {
                            SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                            SDL_RenderDrawRect(renderer, &krect);
                        }
                        int kw = 0, kh = 0;
                        TTF_SizeUTF8(font_ui, row_keys[c].c_str(), &kw, &kh);
                        render_text(renderer, font_ui, row_keys[c],
                                    krect.x + (krect.w - kw) / 2, krect.y + 12, COLOR_WHITE);
                    }
                } else {
                    const int act_widths[5] = { 190, 480, 210, 210, 210 };
                    const int act_gap = 16;
                    int total_w = 0;
                    for (int c = 0; c < nkeys && c < 5; ++c) total_w += act_widths[c];
                    total_w += (nkeys - 1) * act_gap;
                    int kx = (CANVAS_WIDTH - total_w) / 2;
                    for (int c = 0; c < nkeys && c < 5; ++c) {
                        bool sel_k = (r == state.osk_row && c == state.osk_col);
                        int w = act_widths[c];
                        SDL_Rect krect = { kx, row_y + 4, w, 54 };
                        SDL_SetRenderDrawColor(renderer, sel_k ? COLOR_ACCENT.r : 36,
                                                         sel_k ? COLOR_ACCENT.g : 46,
                                                         sel_k ? COLOR_ACCENT.b : 68, 255);
                        SDL_RenderFillRect(renderer, &krect);
                        if (sel_k) {
                            SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
                            SDL_RenderDrawRect(renderer, &krect);
                        }
                        std::string label = row_keys[c];
                        if (label == "LANG") label = state.osk_ru ? "EN / RU" : "RU / EN";
                        else if (label == "SPACE") label = "ПРОБЕЛ";
                        else if (label == "BACK") label = "СТЕРЕТЬ";
                        else if (label == "CLEAR") label = "ОЧИСТИТЬ";
                        else if (label == "DONE") label = "ГОТОВО";

                        int kw = 0, kh = 0;
                        TTF_SizeUTF8(font_card, label.c_str(), &kw, &kh);
                        render_text(renderer, font_card, label,
                                    krect.x + (krect.w - kw) / 2, krect.y + 15, COLOR_WHITE);
                        kx += w + act_gap;
                    }
                }
                row_y += 66;
            }
        }

        // Confirmation Modal Dialog (Safe Exit / Delete Torrent / Uninstall Game)
        if (state.confirm != ConfirmType::NONE) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_Rect full_dim = { 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT };
            SDL_RenderFillRect(renderer, &full_dim);

            SDL_Rect dlg = { 560, 340, 800, 320 };
            SDL_SetRenderDrawColor(renderer, COLOR_HEADER.r, COLOR_HEADER.g, COLOR_HEADER.b, 255);
            SDL_RenderFillRect(renderer, &dlg);
            SDL_SetRenderDrawColor(renderer, COLOR_CARD_SELECT.r, COLOR_CARD_SELECT.g, COLOR_CARD_SELECT.b, 255);
            SDL_RenderDrawRect(renderer, &dlg);

            std::string dlg_title = "ПОДТВЕРЖДЕНИЕ ДЕЙСТВИЯ";
            std::string dlg_sub;
            SDL_Color accent_col = COLOR_YELLOW;
            if (state.confirm == ConfirmType::EXIT_APP) {
                dlg_title = "ВЫЙТИ ИЗ PS4 STORRE?";
                dlg_sub = "Фоновые торрент-задачи продолжат работу, если служба запущена.";
                accent_col = COLOR_CARD_SELECT;
            } else if (state.confirm == ConfirmType::DELETE_TORRENT) {
                dlg_title = "УДАЛИТЬ ТОРРЕНТ ИЗ ОЧЕРЕДИ?";
                dlg_sub = "Загрузка и раздача будут остановлены, временные файлы удалены.";
                accent_col = COLOR_RED;
            } else if (state.confirm == ConfirmType::UNINSTALL_GAME) {
                dlg_title = "УДАЛИТЬ ИГРУ С PS4?";
                dlg_sub = "Установленное приложение будет удалено с консоли.";
                accent_col = COLOR_RED;
            }

            render_text(renderer, font_title, dlg_title, 608, 378, accent_col);
            render_text(renderer, font_ui, truncate_utf8(state.confirm_title, 46), 608, 442, COLOR_WHITE);
            render_text(renderer, font_desc, dlg_sub, 608, 490, COLOR_MUTED);

            SDL_Rect btn_ok = { 608, 560, 220, 48 };
            SDL_Color ok_bg = (state.confirm == ConfirmType::EXIT_APP) ? COLOR_ACCENT : SDL_Color{ 160, 45, 45, 255 };
            SDL_SetRenderDrawColor(renderer, ok_bg.r, ok_bg.g, ok_bg.b, 255);
            SDL_RenderFillRect(renderer, &btn_ok);
            render_icon(renderer, icons.cross, 624, 570, 28, 28);
            render_text(renderer, font_ui, "Подтвердить", 662, 570, COLOR_WHITE);

            SDL_Rect btn_cancel = { 854, 560, 176, 48 };
            SDL_SetRenderDrawColor(renderer, 38, 48, 68, 255);
            SDL_RenderFillRect(renderer, &btn_cancel);
            render_icon(renderer, icons.circle, 870, 570, 28, 28);
            render_text(renderer, font_ui, "Отмена", 908, 570, COLOR_WHITE);
        }

        // Footer Bar (PS4-style colored controller button icons + concise labels per tab/context)
        SDL_Rect footer_rect = { 0, CANVAS_HEIGHT - 70, CANVAS_WIDTH, 70 };
        SDL_SetRenderDrawColor(renderer, COLOR_HEADER.r, COLOR_HEADER.g, COLOR_HEADER.b, 255);
        SDL_RenderFillRect(renderer, &footer_rect);
        SDL_SetRenderDrawColor(renderer, 42, 54, 76, 255);
        SDL_RenderDrawLine(renderer, 0, CANVAS_HEIGHT - 70, CANVAS_WIDTH, CANVAS_HEIGHT - 70);

        std::string pos_str;
        if (state.confirm != ConfirmType::NONE) {
            pos_str = "Подтверждение действия";
        } else if (state.show_details && state.selected_item < (int)active_items.size()) {
            const GameItem* sel = catalog.get_game(active_items[state.selected_item]);
            pos_str = sel ? (sel->id + " • Карточка игры") : "Карточка игры";
        } else if (state.active_tab == 0) {
            std::string cat_label = state.search_query.empty() ? ALPHABET_LABELS[state.alphabet_idx] : "Поиск";
            pos_str = cat_label + " • " + std::to_string(active_items.empty() ? 0 : state.selected_item + 1) + " / " + std::to_string(active_items.size());
        } else if (state.active_tab == 1) {
            int dl_cnt = count_download_tasks();
            pos_str = (dl_cnt == 0) ? "Загрузки: 0" : ("Задача " + std::to_string(state.selected_download + 1) + " / " + std::to_string(dl_cnt));
        } else if (state.active_tab == 2) {
            int installed_cnt = count_installed_tasks();
            pos_str = (installed_cnt == 0) ? "Установленные: 0" : ("Игра " + std::to_string(state.selected_installed + 1) + " / " + std::to_string(installed_cnt));
        } else {
            pos_str = "Параметр " + std::to_string(state.selected_setting + 1) + " / 8";
        }
        render_text(renderer, font_card, pos_str, 50, CANVAS_HEIGHT - 44, COLOR_MUTED);
        int pos_w = 0, pos_h = 0;
        TTF_SizeUTF8(font_card, pos_str.c_str(), &pos_w, &pos_h);

        int gp_w = 0, gp_h = 0;
        TTF_SizeUTF8(font_card, state.gamepad_name.c_str(), &gp_w, &gp_h);
        render_text(renderer, font_card, state.gamepad_name, CANVAS_WIDTH - 50 - gp_w, CANVAS_HEIGHT - 44, COLOR_GREEN);

        std::vector<FooterHint> f_hints;
        if (state.confirm != ConfirmType::NONE) {
            f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Подтвердить" });
            f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "Отмена" });
        } else if (state.show_details) {
            if (!modal_torrent_active) {
                f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, modal_installed ? "Скачать торрент заново" : "Скачать и установить" });
            }
            f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "Назад" });
        } else if (state.active_tab == 0) {
            if (state.focus == FocusArea::SEARCH_INPUT) {
                f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Ввести" });
                f_hints.push_back({ icons.square, 28, 28, nullptr, 0, 0, "Удалить" });
                f_hints.push_back({ icons.triangle, 28, 28, nullptr, 0, 0, "Пробел" });
                f_hints.push_back({ icons.l2, 52, 19, icons.r2, 52, 19, "Язык" });
                f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "Готово" });
            } else if (state.focus == FocusArea::ALPHABET_STRIP) {
                f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "К играм" });
                f_hints.push_back({ icons.triangle, 28, 28, nullptr, 0, 0, "Поиск" });
                f_hints.push_back({ icons.l1, 52, 19, icons.r1, 52, 19, "Вкладки" });
                f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "К сетке" });
            } else {
                f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Подробнее" });
                f_hints.push_back({ icons.triangle, 28, 28, nullptr, 0, 0, "Поиск" });
                if (!state.search_query.empty() || state.alphabet_idx != 0) {
                    f_hints.push_back({ icons.square, 28, 28, nullptr, 0, 0, "Сброс" });
                }
                f_hints.push_back({ icons.l2, 52, 19, icons.r2, 52, 19, "Алфавит" });
                f_hints.push_back({ icons.l1, 52, 19, icons.r1, 52, 19, "Вкладки" });
                f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "Выход" });
            }
        } else if (state.active_tab == 1) {
            auto tasks = queue_mgr.get_tasks();
            int dl_idx = get_selected_download_task_idx();
            if (dl_idx >= 0 && dl_idx < (int)tasks.size()) {
                const auto& cur_t = tasks[dl_idx];
                if (cur_t.state == TaskState::PAUSED) {
                    f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Возобновить" });
                    f_hints.push_back({ icons.square, 28, 28, nullptr, 0, 0, "Удалить торрент" });
                } else {
                    f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Пауза" });
                    f_hints.push_back({ icons.square, 28, 28, nullptr, 0, 0, "Удалить торрент" });
                }
            }
            f_hints.push_back({ icons.l1, 52, 19, icons.r1, 52, 19, "Вкладки" });
            f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "В каталог" });
        } else if (state.active_tab == 2) {
            if (count_installed_tasks() > 0) {
                f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Запустить" });
                f_hints.push_back({ icons.square, 28, 28, nullptr, 0, 0, "Удалить с PS4" });
            }
            f_hints.push_back({ icons.l1, 52, 19, icons.r1, 52, 19, "Вкладки" });
            f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "В каталог" });
        } else if (state.active_tab == 3) {
            f_hints.push_back({ icons.cross, 28, 28, nullptr, 0, 0, "Изменить" });
            f_hints.push_back({ icons.l1, 52, 19, icons.r1, 52, 19, "Вкладки" });
            f_hints.push_back({ icons.circle, 28, 28, nullptr, 0, 0, "В каталог" });
        }

        render_footer_hints(renderer, font_card, f_hints, 50 + pos_w + 30, CANVAS_WIDTH - 50 - gp_w - 30, CANVAS_HEIGHT - 35);

        SDL_RenderPresent(renderer);

        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }

    queue_mgr.stop();

    if (cached_desc_tex) SDL_DestroyTexture(cached_desc_tex);
    clear_text_cache();
    destroy_button_icons(icons);
    if (controller) SDL_GameControllerClose(controller);
    if (font_title) TTF_CloseFont(font_title);
    if (font_ui) TTF_CloseFont(font_ui);
    if (font_card) TTF_CloseFont(font_card);
    if (font_desc) TTF_CloseFont(font_desc);
    if (font_alpha) TTF_CloseFont(font_alpha);

    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
