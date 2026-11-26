#include <proto-include.h>
#include <SDL2/SDL.h>

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>

static FT_Library g_ft_lib = nullptr;
static char g_last_error[256] = {0};

struct _TTF_Font {
    FT_Face face;
    int ptsize;
    int height;
    int ascent;
    int descent;
    int lineskip;
};

typedef struct _TTF_Font TTF_Font;

extern "C" {

int TTF_Init(void) {
    if (g_ft_lib) return 0;
    FT_Error err = FT_Init_FreeType(&g_ft_lib);
    if (err) {
        snprintf(g_last_error, sizeof(g_last_error), "FT_Init_FreeType error %d", err);
        return -1;
    }
    return 0;
}

void TTF_Quit(void) {
    if (g_ft_lib) {
        FT_Done_FreeType(g_ft_lib);
        g_ft_lib = nullptr;
    }
}

const char* TTF_GetError(void) {
    return g_last_error;
}

TTF_Font* TTF_OpenFont(const char* file, int ptsize) {
    if (!g_ft_lib) {
        if (TTF_Init() != 0) return nullptr;
    }
    if (!file || ptsize <= 0) return nullptr;

    FT_Face face = nullptr;
    FT_Error err = FT_New_Face(g_ft_lib, file, 0, &face);
    if (err) {
        snprintf(g_last_error, sizeof(g_last_error), "FT_New_Face error %d for %s", err, file);
        return nullptr;
    }

    err = FT_Set_Pixel_Sizes(face, 0, ptsize);
    if (err) {
        FT_Done_Face(face);
        snprintf(g_last_error, sizeof(g_last_error), "FT_Set_Pixel_Sizes error %d", err);
        return nullptr;
    }

    TTF_Font* font = new TTF_Font();
    font->face = face;
    font->ptsize = ptsize;
    font->height = (face->size->metrics.height >> 6);
    if (font->height <= 0) font->height = ptsize;
    font->ascent = (face->size->metrics.ascender >> 6);
    if (font->ascent <= 0) font->ascent = (ptsize * 4) / 5;
    font->descent = (face->size->metrics.descender >> 6);
    font->lineskip = font->height + 2;

    return font;
}

void TTF_CloseFont(TTF_Font* font) {
    if (font) {
        if (font->face) FT_Done_Face(font->face);
        delete font;
    }
}

// Decode single UTF-8 codepoint
static uint32_t utf8_next(const char*& p) {
    if (!*p) return 0;
    uint8_t c = (uint8_t)*p++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        uint8_t c2 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        return ((c & 0x1F) << 6) | (c2 & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        uint8_t c2 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        uint8_t c3 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        uint8_t c2 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        uint8_t c3 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        uint8_t c4 = (uint8_t)*p ? (uint8_t)*p++ : 0;
        return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }
    return c;
}

int TTF_SizeUTF8(TTF_Font* font, const char* text, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (!font || !font->face || !text) return -1;

    int cur_w = 0, max_w = 0;
    int lines = 1;
    const char* p = text;

    while (*p) {
        if (*p == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            lines++;
            p++;
            continue;
        }
        uint32_t cp = utf8_next(p);
        if (cp == 0) break;
        FT_UInt idx = FT_Get_Char_Index(font->face, cp);
        if (FT_Load_Glyph(font->face, idx, FT_LOAD_DEFAULT) == 0) {
            cur_w += (font->face->glyph->advance.x >> 6);
        }
    }
    if (cur_w > max_w) max_w = cur_w;

    if (w) *w = max_w;
    if (h) *h = lines * font->lineskip;
    return 0;
}

SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font* font, const char* text, SDL_Color fg) {
    if (!font || !font->face || !text || !*text) {
        // Return 1x1 transparent surface for empty text
        return SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_RGBA32);
    }

    int total_w = 0, total_h = 0;
    TTF_SizeUTF8(font, text, &total_w, &total_h);
    if (total_w <= 0) total_w = 1;
    if (total_h <= 0) total_h = font->lineskip;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, total_w, total_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    SDL_FillRect(surf, nullptr, 0x00000000); // Clear transparent

    Uint32* pixels = (Uint32*)surf->pixels;
    int pitch32 = surf->pitch / 4;

    int pen_x = 0;
    int pen_y = 0;
    int baseline = font->ascent;

    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            pen_x = 0;
            pen_y += font->lineskip;
            p++;
            continue;
        }
        uint32_t cp = utf8_next(p);
        if (cp == 0) break;

        FT_UInt idx = FT_Get_Char_Index(font->face, cp);
        if (FT_Load_Glyph(font->face, idx, FT_LOAD_DEFAULT) != 0) continue;
        if (FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;

        FT_GlyphSlot slot = font->face->glyph;
        int gx = pen_x + slot->bitmap_left;
        int gy = pen_y + baseline - slot->bitmap_top;

        for (unsigned int r = 0; r < slot->bitmap.rows; r++) {
            int dst_y = gy + (int)r;
            if (dst_y < 0 || dst_y >= surf->h) continue;

            for (unsigned int c = 0; c < slot->bitmap.width; c++) {
                int dst_x = gx + (int)c;
                if (dst_x < 0 || dst_x >= surf->w) continue;

                uint8_t a = slot->bitmap.buffer[r * slot->bitmap.pitch + c];
                if (a == 0) continue;

                uint32_t final_a = (a * fg.a) / 255;
                uint32_t pixel = (final_a << 24) | (fg.b << 16) | (fg.g << 8) | fg.r;
                pixels[dst_y * pitch32 + dst_x] = pixel;
            }
        }
        pen_x += (slot->advance.x >> 6);
    }

    return surf;
}

SDL_Surface* TTF_RenderUTF8_Blended_Wrapped(TTF_Font* font, const char* text, SDL_Color fg, Uint32 wrap_length) {
    if (!font || !font->face || !text || !*text) {
        return SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_RGBA32);
    }
    if (wrap_length == 0) wrap_length = 800;

    // Simple word-wrap into lines
    std::vector<std::string> lines;
    std::string cur_line;
    int cur_w = 0;

    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            lines.push_back(cur_line);
            cur_line.clear();
            cur_w = 0;
            p++;
            continue;
        }

        // Read next word or space sequence
        const char* word_start = p;
        while (*p && *p != ' ' && *p != '\n') {
            utf8_next(p);
        }
        std::string word(word_start, p - word_start);

        // Measure word
        int ww = 0, wh = 0;
        TTF_SizeUTF8(font, word.c_str(), &ww, &wh);

        if (cur_w + ww > (int)wrap_length && !cur_line.empty()) {
            lines.push_back(cur_line);
            cur_line = word;
            cur_w = ww;
        } else {
            if (!cur_line.empty()) {
                cur_line += " ";
                cur_w += (font->ptsize / 3);
            }
            cur_line += word;
            cur_w += ww;
        }

        while (*p == ' ') p++;
    }
    if (!cur_line.empty()) {
        lines.push_back(cur_line);
    }
    if (lines.empty()) lines.push_back("");

    int total_h = (int)lines.size() * font->lineskip;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, (int)wrap_length, total_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    SDL_FillRect(surf, nullptr, 0x00000000);

    Uint32* pixels = (Uint32*)surf->pixels;
    int pitch32 = surf->pitch / 4;
    int baseline = font->ascent;

    for (size_t li = 0; li < lines.size(); li++) {
        int pen_x = 0;
        int pen_y = (int)li * font->lineskip;
        const char* lp = lines[li].c_str();

        while (*lp) {
            uint32_t cp = utf8_next(lp);
            if (cp == 0) break;

            FT_UInt idx = FT_Get_Char_Index(font->face, cp);
            if (FT_Load_Glyph(font->face, idx, FT_LOAD_DEFAULT) != 0) continue;
            if (FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;

            FT_GlyphSlot slot = font->face->glyph;
            int gx = pen_x + slot->bitmap_left;
            int gy = pen_y + baseline - slot->bitmap_top;

            for (unsigned int r = 0; r < slot->bitmap.rows; r++) {
                int dst_y = gy + (int)r;
                if (dst_y < 0 || dst_y >= surf->h) continue;

                for (unsigned int c = 0; c < slot->bitmap.width; c++) {
                    int dst_x = gx + (int)c;
                    if (dst_x < 0 || dst_x >= surf->w) continue;

                    uint8_t a = slot->bitmap.buffer[r * slot->bitmap.pitch + c];
                    if (a == 0) continue;

                    uint32_t final_a = (a * fg.a) / 255;
                    uint32_t pixel = (final_a << 24) | (fg.b << 16) | (fg.g << 8) | fg.r;
                    pixels[dst_y * pitch32 + dst_x] = pixel;
                }
            }
            pen_x += (slot->advance.x >> 6);
        }
    }

    return surf;
}

} // extern "C"
