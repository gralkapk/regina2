#include <cstdint>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include <glm/glm.hpp>

// https://stackoverflow.com/questions/49748864/morton-reverse-encoding-for-a-3d-grid

/* Morton encoding in binary (components 21-bit: 0..2097151)
                                0zyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyxzyx */
#define BITMASK_0000000001000001000001000001000001000001000001000001000001000001 UINT64_C(18300341342965825)
#define BITMASK_0000001000001000001000001000001000001000001000001000001000001000 UINT64_C(146402730743726600)
#define BITMASK_0001000000000000000000000000000000000000000000000000000000000000 UINT64_C(1152921504606846976)
/*              0000000ccc0000cc0000cc0000cc0000cc0000cc0000cc0000cc0000cc0000cc */
#define BITMASK_0000000000000011000000000011000000000011000000000011000000000011 UINT64_C(844631138906115)
#define BITMASK_0000000111000000000011000000000011000000000011000000000011000000 UINT64_C(126113986927919296)
/*              00000000000ccccc00000000cccc00000000cccc00000000cccc00000000cccc */
#define BITMASK_0000000000000000000000000000000000001111000000000000000000001111 UINT64_C(251658255)
#define BITMASK_0000000000000000000000001111000000000000000000001111000000000000 UINT64_C(1030792212480)
#define BITMASK_0000000000011111000000000000000000000000000000000000000000000000 UINT64_C(8725724278030336)
/*              000000000000000000000000000ccccccccccccc0000000000000000cccccccc */
#define BITMASK_0000000000000000000000000000000000000000000000000000000011111111 UINT64_C(255)
#define BITMASK_0000000000000000000000000001111111111111000000000000000000000000 UINT64_C(137422176256)
/*                                                         ccccccccccccccccccccc */
#define BITMASK_21BITS UINT64_C(2097151)

struct MortonConfig {
    static const uint64_t mask = 0b111111111111111111000000000000000000000000000000000000000;
    static const int prefix_offset = 39;
    static const uint64_t factor = (1 << 19) - 1;
    static const int code_offset = 9;
};

static inline uint64_t morton_encode(uint32_t xsrc, uint32_t ysrc, uint32_t zsrc) {
    constexpr uint64_t const mask0 = 0b0000000001000001000001000001000001000001000001000001000001000001,
                             mask1 = 0b0000001000001000001000001000001000001000001000001000001000001000,
                             mask2 = 0b0001000000000000000000000000000000000000000000000000000000000000,
                             mask3 = 0b0000000000000011000000000011000000000011000000000011000000000011,
                             mask4 = 0b0000000111000000000011000000000011000000000011000000000011000000,
                             mask5 = 0b0000000000000000000000000000000000001111000000000000000000001111,
                             mask6 = 0b0000000000000000000000001111000000000000000000001111000000000000,
                             mask7 = 0b0000000000011111000000000000000000000000000000000000000000000000,
                             mask8 = 0b0000000000000000000000000000000000000000000000000000000011111111,
                             mask9 = 0b0000000000000000000000000001111111111111000000000000000000000000;
    uint64_t x = xsrc, y = ysrc, z = zsrc;
    /* 0000000000000000000000000000000000000000000ccccccccccccccccccccc */
    x = (x & mask8) | ((x << 16) & mask9);
    y = (y & mask8) | ((y << 16) & mask9);
    z = (z & mask8) | ((z << 16) & mask9);
    /* 000000000000000000000000000ccccccccccccc0000000000000000cccccccc */
    x = (x & mask5) | ((x << 8) & mask6) | ((x << 16) & mask7);
    y = (y & mask5) | ((y << 8) & mask6) | ((y << 16) & mask7);
    z = (z & mask5) | ((z << 8) & mask6) | ((z << 16) & mask7);
    /* 00000000000ccccc00000000cccc00000000cccc00000000cccc00000000cccc */
    x = (x & mask3) | ((x << 4) & mask4);
    y = (y & mask3) | ((y << 4) & mask4);
    z = (z & mask3) | ((z << 4) & mask4);
    /* 0000000ccc0000cc0000cc0000cc0000cc0000cc0000cc0000cc0000cc0000cc */
    x = (x & mask0) | ((x << 2) & mask1) | ((x << 4) & mask2);
    y = (y & mask0) | ((y << 2) & mask1) | ((y << 4) & mask2);
    z = (z & mask0) | ((z << 2) & mask1) | ((z << 4) & mask2);
    /* 000c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c00c */
    return x | (y << 1) | (z << 2);
}

std::vector<std::pair<uint64_t, glm::vec3>> create_morton_codes(
    std::vector<glm::vec3> const& data) {
    std::vector<glm::uvec3> codes(data.size());

    // constexpr uint64_t const factor = 1 << 21;
    // constexpr uint64_t const factor = 1 << 15;
    static const uint64_t factor = (1 << 19) - 1;
    auto const dfactor = static_cast<double>(factor);

    auto const span = glm::dvec3(1.);
    auto const lower = glm::dvec3(0.);

    // #pragma omp parallel for
    for (int64_t i = 0; i < data.size(); ++i) {
        auto const pos = (glm::dvec3(data[i]) - lower) / span;
        codes[i] = glm::uvec3(pos * dfactor);
    }

    std::vector<std::pair<uint64_t, glm::vec3>> mc(codes.size());
    // #pragma omp parallel for
    for (int64_t i = 0; i < codes.size(); ++i) {
        mc[i] = std::make_pair(morton_encode(codes[i].x, codes[i].y, codes[i].z), data[i]);
    }

    return mc;
}

float rbf(float const dis, float const rad) {
    if (dis <= rad) {
        return std::expf(1.f / (1.f - dis * dis)) * rad;
    }
    return 0.f;
}

void particle_over_grid_sorted(std::vector<float>& grid, std::vector<std::pair<uint64_t, glm::vec3>> const& particles, glm::ivec3 const& num_cells_dir, float const rad) {
    auto const cell_center_base = glm::vec3(1.f / (num_cells_dir.x + 1.f), 1.f / (num_cells_dir.y + 1.f), 1.f / (num_cells_dir.z + 1.f));
    int const filterSizeX = static_cast<int>(std::ceil(rad / cell_center_base.x));
    int const filterSizeY = static_cast<int>(std::ceil(rad / cell_center_base.y));
    int const filterSizeZ = static_cast<int>(std::ceil(rad / cell_center_base.z));

    for (int i = 0; i < particles.size(); ++i) {
        auto const x_idx = glm::clamp(static_cast<int>(particles[i].second.x * num_cells_dir.x), 0, num_cells_dir.x - 1);
        auto const y_idx = glm::clamp(static_cast<int>(particles[i].second.y * num_cells_dir.y), 0, num_cells_dir.y - 1);
        auto const z_idx = glm::clamp(static_cast<int>(particles[i].second.z * num_cells_dir.z), 0, num_cells_dir.z - 1);

        for (int hz = z_idx - filterSizeZ; hz <= z_idx + filterSizeZ; ++hz) {
            for (int hy = y_idx - filterSizeY; hy <= y_idx + filterSizeY; ++hy) {
                for (int hx = x_idx - filterSizeX; hx <= x_idx + filterSizeX; ++hx) {
                    if (hz < 0 || hy < 0 || hx < 0)
                        continue;
                    if (hz >= num_cells_dir.z || hy >= num_cells_dir.y || hx >= num_cells_dir.x)
                        continue;
                    auto const cell_idx = hx + num_cells_dir.x * (hy + hz * num_cells_dir.y);
                    auto const cell_pos = cell_center_base * glm::vec3(hx + 1, hy + 1, hz + 1);
                    auto const dis = glm::length(cell_pos - particles[i].second);
                    grid[cell_idx] += rbf(dis, rad);
                }
            }
        }
    }
}

void particle_over_grid(std::vector<float>& grid, std::vector<glm::vec3> const& particles, glm::ivec3 const& num_cells_dir, float const rad) {
    auto const cell_center_base = glm::vec3(1.f / (num_cells_dir.x + 1.f), 1.f / (num_cells_dir.y + 1.f), 1.f / (num_cells_dir.z + 1.f));
    int const filterSizeX = static_cast<int>(std::ceil(rad / cell_center_base.x));
    int const filterSizeY = static_cast<int>(std::ceil(rad / cell_center_base.y));
    int const filterSizeZ = static_cast<int>(std::ceil(rad / cell_center_base.z));

    for (int i = 0; i < particles.size(); ++i) {
        auto const x_idx = glm::clamp(static_cast<int>(particles[i].x * num_cells_dir.x), 0, num_cells_dir.x - 1);
        auto const y_idx = glm::clamp(static_cast<int>(particles[i].y * num_cells_dir.y), 0, num_cells_dir.y - 1);
        auto const z_idx = glm::clamp(static_cast<int>(particles[i].z * num_cells_dir.z), 0, num_cells_dir.z - 1);

        for (int hz = z_idx - filterSizeZ; hz <= z_idx + filterSizeZ; ++hz) {
            for (int hy = y_idx - filterSizeY; hy <= y_idx + filterSizeY; ++hy) {
                for (int hx = x_idx - filterSizeX; hx <= x_idx + filterSizeX; ++hx) {
                    if (hz < 0 || hy < 0 || hx < 0)
                        continue;
                    if (hz >= num_cells_dir.z || hy >= num_cells_dir.y || hx >= num_cells_dir.x)
                        continue;
                    auto const cell_idx = hx + num_cells_dir.x * (hy + hz * num_cells_dir.y);
                    auto const cell_pos = cell_center_base * glm::vec3(hx + 1, hy + 1, hz + 1);
                    auto const dis = glm::length(cell_pos - particles[i]);
                    grid[cell_idx] += rbf(dis, rad);
                }
            }
        }
    }
}

void grid_over_particle(std::vector<float>& grid, std::vector<glm::vec3> const& particles, glm::ivec3 const& num_cells_dir, float const rad) {
    auto const cell_center_base = glm::vec3(1.f / (num_cells_dir.x + 1.f), 1.f / (num_cells_dir.y + 1.f), 1.f / (num_cells_dir.z + 1.f));
    for (int z = 0; z < num_cells_dir.z; ++z) {
        for (int y = 0; y < num_cells_dir.y; ++y) {
            for (int x = 0; x < num_cells_dir.x; ++x) {
                auto const cell_idx = x + num_cells_dir.x * (y + z * num_cells_dir.y);
                for (int i = 0; i < particles.size(); ++i) {
                    auto const cell_pos = cell_center_base * glm::vec3(x + 1, y + 1, z + 1);
                    auto const dis = glm::length(cell_pos - particles[i]);
                    grid[cell_idx] += rbf(dis, rad);
                }
            }
        }
    }
}

int main() {
    int const num_particles = 1000;
    auto const num_cells_dir = glm::ivec3(10);
    auto const rad = 0.15f;

    std::vector<glm::vec3> particles(num_particles);

    auto distro = std::uniform_real_distribution<float>();
    auto rng = std::mt19937_64(42);

    std::generate(particles.begin(), particles.end(), [&]() { return glm::vec3(distro(rng), distro(rng), distro(rng)); });

    std::vector<float> grid(num_cells_dir.x * num_cells_dir.y * num_cells_dir.z, 0.f);

    auto const gop_start = std::chrono::steady_clock::now();
    grid_over_particle(grid, particles, num_cells_dir, rad);
    auto const gop_end = std::chrono::steady_clock::now();
    auto const gop_val = std::accumulate(grid.begin(), grid.end(), 0.f);
    std::cout << "GoP sum " << gop_val << " in " << std::chrono::duration_cast<std::chrono::microseconds>(gop_end - gop_start).count() << "mus" << std::endl;

    std::fill(grid.begin(), grid.end(), 0.f);
    auto const pog_start = std::chrono::steady_clock::now();
    particle_over_grid(grid, particles, num_cells_dir, rad);
    auto const pog_end = std::chrono::steady_clock::now();
    auto const pog_val = std::accumulate(grid.begin(), grid.end(), 0.f);
    std::cout << "PoG sum " << pog_val << " in " << std::chrono::duration_cast<std::chrono::microseconds>(pog_end - pog_start).count() << "mus" << std::endl;

    std::fill(grid.begin(), grid.end(), 0.f);
    auto ps = create_morton_codes(particles);
    std::sort(ps.begin(), ps.end(), [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });
    std::transform(ps.begin(), ps.end(), particles.begin(), [](auto const& el) { return el.second; });
    auto const pogs_start = std::chrono::steady_clock::now();
    particle_over_grid(grid, particles, num_cells_dir, rad);
    auto const pogs_end = std::chrono::steady_clock::now();
    auto const pogs_val = std::accumulate(grid.begin(), grid.end(), 0.f);
    std::cout << "PoGS sum " << pogs_val << " in " << std::chrono::duration_cast<std::chrono::microseconds>(pogs_end - pogs_start).count() << "mus" << std::endl;

    return 0;
}