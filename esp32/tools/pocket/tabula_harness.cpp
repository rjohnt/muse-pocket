// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// Draws one Tabula panel on the host. Usage:
//   tabula_harness PACK OUT.pgm DATE WEEKDAY HOUR VALID NOW [NAME STATE NOTE CHECKED]...
// The Python test compares the result with the reference composer's panel.
#include "../../main/office/tabula.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 8 || (argc - 8) % 4) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> pack((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!pocket_tabula::open(pack.data(), pack.size())) { std::printf("pack refused\n"); return 1; }
    // A damaged pack must be refused, not drawn from.
    std::vector<uint8_t> cut(pack.begin(), pack.begin() + pack.size() / 2);
    if (pocket_tabula::open(cut.data(), cut.size())) { std::printf("truncated pack accepted\n"); return 1; }
    pocket_tabula::open(pack.data(), pack.size());
    pocket_tabula::Status status;
    for (int i = 8; i + 3 < argc && status.count < pocket_tabula::MAX_SOURCES; i += 4) {
        pocket_tabula::Source& s = status.sources[status.count++];
        std::snprintf(s.name, sizeof(s.name), "%s", argv[i]);
        std::snprintf(s.state, sizeof(s.state), "%s", argv[i + 1]);
        std::snprintf(s.note, sizeof(s.note), "%s", argv[i + 2]);
        s.checked = std::atoll(argv[i + 3]);
    }
    std::vector<uint8_t> canvas(480 * 800);
    pocket_tabula::render(canvas.data(), static_cast<uint32_t>(std::atol(argv[3])), std::atoi(argv[4]), std::atoi(argv[5]),
                          std::atoi(argv[6]) != 0, &status, std::atoll(argv[7]));
    std::ofstream out(argv[2], std::ios::binary);
    out << "P5\n480 800\n255\n";
    out.write(reinterpret_cast<const char*>(canvas.data()), canvas.size());
    return 0;
}
