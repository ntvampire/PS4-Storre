#define SDL_MAIN_HANDLED
#include <iostream>
#include <cassert>
#include "catalog/catalog.h"

int main() {
    CatalogManager catalog;
    bool ok = catalog.init("assets/misc/catalog.json", "assets/misc/catalog.pak");
    if (!ok) {
        std::cerr << "FAILED: catalog.init returned false!\n";
        return 1;
    }

    std::cout << "Catalog loaded. Total: " << catalog.get_count() << "\n";
    assert(catalog.get_count() == 3699);

    const auto& all_items = catalog.get_items_by_letter('*');
    std::cout << "All items count: " << all_items.size() << "\n";
    assert(all_items.size() == 3699);

    for (size_t idx : all_items) {
        const auto* g = catalog.get_game(idx);
        assert(g != nullptr);
        assert(!g->torrent_hash.empty());
    }

    const auto& num_items = catalog.get_items_by_letter('#');
    std::cout << "# items count: " << num_items.size() << "\n";
    assert(!num_items.empty());

    const auto& a_items = catalog.get_items_by_letter('A');
    std::cout << "A items count: " << a_items.size() << "\n";
    assert(!a_items.empty());

    const auto& b_items = catalog.get_items_by_letter('B');
    std::cout << "B items count: " << b_items.size() << "\n";
    assert(!b_items.empty());
    const auto* first_b = catalog.get_game(b_items[0]);
    std::cout << "First B game: " << (first_b ? first_b->title : "null") << "\n";

    const auto& p_items = catalog.get_items_by_letter('P');
    std::cout << "P items count: " << p_items.size() << "\n";
    assert(!p_items.empty());

    const auto& z_items = catalog.get_items_by_letter('Z');
    std::cout << "Z items count: " << z_items.size() << "\n";
    assert(!z_items.empty());

    size_t pos_b = catalog.find_first_by_letter('B', all_items);
    std::cout << "find_first_by_letter('B') position in all_items: " << pos_b << "\n";
    assert(pos_b == b_items[0]);

    std::cout << "ALL ALPHABET TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
