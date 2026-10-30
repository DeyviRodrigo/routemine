#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <istream>
#include <limits>
#include <ostream>
#include <optional>
#include <span>
#include <stop_token>
#include <functional>
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
    // Gradient only in a face interior; null at edges where departure slopes may differ.
    std::optional<std::array<double,2>> surface_gradient(double x,double y,bool interior_only=true) const;
    // Exact face-boundary breakpoints for a straight XY segment; null for gaps/ambiguity.
    std::optional<std::vector<Vec3>> segment_profile(Vec3 a, Vec3 b,double max_surface_grade_percent=std::numeric_limits<double>::infinity()) const;
    std::array<Vec3,2> bounds() const;
    std::span<const Vec3> vertices() const { return vertices_; }
    std::span<const std::array<std::size_t,3>> faces() const { return faces_; }
private:
    struct Node { Vec3 lo,hi; std::size_t begin{},end{},left{},right{}; };
    std::size_t build_index(std::size_t begin,std::size_t end);
    std::vector<std::size_t> candidates(Vec3 lo,Vec3 hi) const;
    std::vector<Vec3> vertices_;
    std::vector<std::array<std::size_t, 3>> faces_;
    std::vector<std::size_t> face_order_;
    std::vector<Node> index_;
};
// Estimated work progress (0..100), called on the worker thread; no elapsed-time simulation.
using ProgressCallback=std::function<void(int)>;
Terrain load_obj(std::istream& input,ProgressCallback progress={});
// ASCII DXF model-space 3DFACE surfaces. Unsupported surface entities fail explicitly.
struct DxfImportReport { std::size_t faces_read{},triangles_loaded{},degenerate_faces_skipped{}; };
// With a report, degenerate XY faces are omitted and counted; without one, they fail.
Terrain load_dxf(std::istream& input,DxfImportReport* report=nullptr,ProgressCallback progress={});
Terrain load_terrain(const std::filesystem::path&,DxfImportReport* report=nullptr,ProgressCallback progress={});
enum class DesignMode { surface, earthwork };
struct Parameters {
    double max_grade_percent{10.0};
    double max_cut_m{20.0}, max_fill_m{20.0};
    double min_radius_m{}, road_width_m{8.0}, min_straight_m{};
    double max_turn_degrees{180.0};
    bool continuous_curves{};
    DesignMode mode{DesignMode::earthwork};
    double sample_spacing_m{1.0}, surface_tolerance_m{0.01};
    std::size_t max_samples{1000000};
    double min_straight_same_direction_m{};
    bool apply_dg_grade_guidance{};
};
struct Validation {
    Metrics metrics;
    std::vector<std::string> violations;
    // First localized occurrence per failed constraint, in Spanish.
    std::vector<std::string> diagnostics;
    std::vector<std::string> not_evaluated;
    double max_cut_m{}, max_fill_m{};
    std::size_t samples{};
    bool checked_constraints_pass() const { return violations.empty(); }
    bool all_constraints_pass() const { return violations.empty() && not_evaluated.empty(); }
};
// Inclusive sampled arc span; XY center and signed sweep define the circular primitive.
struct AlignmentCurve {std::size_t first{},last{};Vec3 center;double sweep_degrees{};};
bool alignment_enabled(const Parameters&);
void validate_alignment(std::span<const Vec3>,std::span<const AlignmentCurve>,const Parameters&,Validation&);
Validation validate(std::span<const Vec3> route, const Terrain& terrain, const Parameters& params,
                    std::span<const AlignmentCurve> curves={});
// Record one localized diagnostic per constraint; vertex indices are zero-based internally.
void validation_issue(Validation&, std::span<const Vec3>, const char* code,
                      std::size_t first, std::size_t last, const std::string& description);
std::string validation_failure_message(const Validation&);
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
                           const Parameters& params, const UnitCosts& costs,std::span<const AlignmentCurve> curves={});
struct SearchOptions {
    double xy_spacing_m{10}, z_min{}, z_max{}, z_step_m{1};
    std::size_t max_states{200000}, max_expansions{100000}, max_edges{1000000};
};
enum class SearchStatus { found, no_path, budget_exceeded, cancelled };
struct AdjustmentAttempt {
    std::string step;
    double min_straight_m{},min_radius_m{},max_grade_percent{},xy_spacing_m{},heading_step_degrees{};
    SearchStatus status{SearchStatus::no_path};
    std::size_t expanded{},edges_checked{};
};
struct SearchDiagnostics {
    std::size_t grade{},coverage{},earthwork{},spacing{},geometry{};
    SearchDiagnostics& operator+=(const SearchDiagnostics& b){grade+=b.grade;coverage+=b.coverage;earthwork+=b.earthwork;spacing+=b.spacing;geometry+=b.geometry;return *this;}
};
struct LocalSection {std::size_t first{},last{};double max_grade_percent{},min_radius_m{},min_straight_m{};};
struct SearchResult {
    SearchStatus status{SearchStatus::no_path};
    std::vector<Vec3> route;
    std::vector<AlignmentCurve> curves;
    std::optional<CostEstimate> estimate;
    std::size_t expanded{}, edges_checked{};
    // Inclusive vertex ranges in the proposal and the verified surface reference.
    struct Intervention {std::size_t first{},last{},reference_first{},reference_last{};double detour_m{},proposal_m{},max_cut_m{},max_fill_m{},terrain_angle_degrees{};};
    std::vector<Intervention> interventions;
    std::vector<Vec3> reference_route;
    std::string strategy;
    double effective_xy_spacing_m{};
    double effective_heading_step_degrees{};
    std::vector<AdjustmentAttempt> adjustment_attempts;
    std::string adjustment_report;
    SearchDiagnostics diagnostics;
    std::vector<LocalSection> local_sections;
};
void write_route_csv(std::ostream&,std::span<const Vec3>,std::span<const SearchResult::Intervention> interventions={},bool projected_ramp=false);
void write_route_dxf(std::ostream&,std::span<const Vec3>,std::span<const SearchResult::Intervention> interventions={},bool projected_ramp=false);
void write_classified_route_csv(std::ostream&,const Terrain&,std::span<const Vec3>,double tolerance=0.01);
void write_classified_route_dxf(std::ostream&,const Terrain&,std::span<const Vec3>,double tolerance=0.01);
// Dijkstra on a bounded XY/Z lattice. Reuse is excluded from its additive objective.
SearchResult plan_cpu(const Terrain&, Vec3 start, Vec3 finish, const Parameters&,
                      const UnitCosts&, const SearchOptions&,std::stop_token stop={});
struct RoadOptions {
    // Worker-thread observer. Spans are valid only during the call. Partial
    // polylines are explored branches; complete ones have passed validation.
    // Transient visualization only: never persisted as a design parameter.
    std::function<void(std::span<const Vec3>,bool complete)> trace;
    bool adaptive_resolution{true},propose_earthwork{true};
    std::optional<double> start_heading,finish_heading; // Optional fixed poses for local refinement.
    bool first_feasible{}; // Stop an adjustment trial after its first verified route.
    double detour_ratio{5},heading_step_degrees{7.5},additional_grade_degrees{};
    std::size_t max_shortcut_checks{10000},max_ramp_candidates{64};
};
struct RoadAlternatives {
    SearchResult surface;
    std::optional<SearchResult> earthwork;
    std::vector<std::string> messages;
};
SearchResult plan_alignment(const Terrain&,Vec3,Vec3,const Parameters&,const UnitCosts&,
                            const SearchOptions&,const RoadOptions&,std::stop_token={});
SearchResult plan_continuous_ramp(const Terrain&,Vec3,Vec3,const Parameters&,const UnitCosts&,
                                  const SearchOptions&,const RoadOptions&,std::stop_token={},ProgressCallback progress={});
struct RouteRefinement {std::optional<SearchResult> candidate;std::string report;};
// Local constraints override only the selected index range; all joins are revalidated.
CostEstimate estimate_sectioned_route(const SearchResult&,const Terrain&,const Parameters&,const UnitCosts&);
RouteRefinement optimize_route_section(const Terrain&,const SearchResult&,std::size_t first,std::size_t last,
    const Parameters& base,const Parameters& local,const UnitCosts&,const SearchOptions&,const RoadOptions&,
    std::stop_token stop={},ProgressCallback progress={});
enum class EarthworkKind { surface,cut,fill };
struct EarthworkSegment {Vec3 first,last;EarthworkKind kind;std::size_t edge{};};
std::vector<EarthworkSegment> earthwork_segments(const Terrain&,std::span<const Vec3>,double tolerance=0.01);
// A* in XY on the original TIN, followed by independently verified optional interventions.
RoadAlternatives plan_road(const Terrain&,Vec3 start,Vec3 finish,const Parameters&,const UnitCosts&,
                          const SearchOptions&,const RoadOptions& = {},std::stop_token stop={},ProgressCallback progress={});
struct AdjustedRoadResult {
    SearchResult route;
    Parameters parameters;
    SearchOptions search;
    RoadOptions road;
};
// Explicit opt-in: resources, then reversal straight >=35 m, radius >=30 m,
// grade <=12%. Width, cut/fill, endpoints and surface/earthwork mode stay fixed.
AdjustedRoadResult plan_road_adjusted(const Terrain&,Vec3,Vec3,const Parameters&,const UnitCosts&,
    const SearchOptions&,const RoadOptions&,std::stop_token stop={},ProgressCallback progress={},
    std::function<void(const std::string&)> stage={});
// Try requested values first, then the bounded adjustment sequence on failure.
AdjustedRoadResult plan_road_with_retries(const Terrain&,Vec3,Vec3,const Parameters&,const UnitCosts&,
    const SearchOptions&,const RoadOptions&,std::stop_token stop={},ProgressCallback progress={},
    std::function<void(const std::string&)> stage={});
// Fast terrain-following corridor; horizontal curve design remains pending.
AdjustedRoadResult plan_surface_reference(const Terrain&,Vec3,Vec3,const Parameters&,const UnitCosts&,
    const SearchOptions&,const RoadOptions&,std::stop_token stop={},ProgressCallback progress={},
    std::function<void(const std::string&)> stage={});
// Keep a validated surface baseline and try bounded local earthwork improvements.
SearchResult optimize_surface_reference(const Terrain&,const SearchResult&,const Parameters&,const UnitCosts&,
    const SearchOptions&,const RoadOptions&,std::stop_token stop={},ProgressCallback progress={});
Parameters road_parameters(const Parameters&,const RoadOptions&,bool intervention);
// Recompute intervention lengths, grade/depth/coverage and the strict detour ratio from XYZ.
void validate_interventions(SearchResult&,const Terrain&,const Parameters&,const RoadOptions&);
// Batch backend seam: CPU reference available on every supported platform.
std::vector<double> grades_cpu(std::span<const Vec3> route);
#ifdef ROUTEMINE_HAS_CUDA
// Caller chooses explicitly; CUDA errors throw, never silently claim GPU use.
std::vector<double> grades_cuda(std::span<const Vec3> route);
#endif
}
