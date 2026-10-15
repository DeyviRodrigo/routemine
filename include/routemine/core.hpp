#pragma once
#include <array>
#include <cstddef>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace routemine {
struct Vec3 { double x{}, y{}, z{}; };
bool finite(Vec3 p);
Vec3 to_local(Vec3 world, Vec3 origin);
Vec3 to_world(Vec3 local, Vec3 origin);
struct Metrics { double horizontal_m{}, spatial_m{}, max_grade_percent{}; };
Metrics measure(std::span<const Vec3> route);
// Original faces only. No global retriangulation or automatic hole filling.
class Terrain {
public:
    Terrain(std::vector<Vec3> vertices, std::vector<std::array<std::size_t, 3>> faces);
    // Null for no face or conflicting elevations in overlapping XY triangles.
    std::optional<double> elevation(double x, double y) const;
    // Exact face-boundary breakpoints for a straight XY segment; null for gaps/ambiguity.
    std::optional<std::vector<Vec3>> segment_profile(Vec3 a, Vec3 b) const;
    std::array<Vec3,2> bounds() const;
private:
    std::vector<Vec3> vertices_;
    std::vector<std::array<std::size_t, 3>> faces_;
};
Terrain load_obj(std::istream& input);
enum class DesignMode { surface, earthwork };
struct Parameters {
    double max_grade_percent{10.0};
    double max_cut_m{20.0}, max_fill_m{20.0};
    double min_radius_m{30.0}, road_width_m{8.0}, min_straight_m{500.0};
    DesignMode mode{DesignMode::earthwork};
    double sample_spacing_m{1.0}, surface_tolerance_m{0.01};
    std::size_t max_samples{1000000};
};
struct Validation {
    Metrics metrics;
    std::vector<std::string> violations;
    std::vector<std::string> not_evaluated;
    double max_cut_m{}, max_fill_m{};
    std::size_t samples{};
    bool checked_constraints_pass() const { return violations.empty(); }
    bool all_constraints_pass() const { return violations.empty() && not_evaluated.empty(); }
};
Validation validate(std::span<const Vec3> route, const Terrain& terrain, const Parameters& params);
// User supplied currency units (CU); no market prices are embedded.
struct UnitCosts {
    double road_per_m2{}, soil_cut_per_m3{}, rock_cut_per_m3{};
    double fill_placement_per_m3{}, imported_material_per_m3{}, disposal_per_m3{};
    double haul_per_m3_km{};
    double rock_fraction{}, reusable_cut_fraction{};
    double internal_haul_km{}, import_haul_km{}, disposal_haul_km{};
};
struct CostEstimate {
    Validation validation;
    double cut_m3{}, fill_m3{}, reused_m3{}, imported_m3{}, surplus_m3{};
    double road{}, excavation{}, fill_placement{}, material{}, disposal{}, haul{}, total{};
};
// Preliminary centerline rectangular-section estimate, not a full earthworks model.
CostEstimate estimate_cost(std::span<const Vec3> route, const Terrain& terrain,
                           const Parameters& params, const UnitCosts& costs);
struct SearchOptions {
    double xy_spacing_m{10}, z_min{}, z_max{}, z_step_m{1};
    std::size_t max_states{200000}, max_expansions{100000}, max_edges{1000000};
};
enum class SearchStatus { found, no_path, budget_exceeded };
struct SearchResult {
    SearchStatus status{SearchStatus::no_path};
    std::vector<Vec3> route;
    std::optional<CostEstimate> estimate;
    std::size_t expanded{}, edges_checked{};
};
// Dijkstra on a bounded XY/Z lattice. Reuse is excluded from its additive objective.
SearchResult plan_cpu(const Terrain&, Vec3 start, Vec3 finish, const Parameters&,
                      const UnitCosts&, const SearchOptions&);
// Batch backend seam: CPU reference available on every supported platform.
std::vector<double> grades_cpu(std::span<const Vec3> route);
#ifdef ROUTEMINE_HAS_CUDA
// Caller chooses explicitly; CUDA errors throw, never silently claim GPU use.
std::vector<double> grades_cuda(std::span<const Vec3> route);
#endif
}
