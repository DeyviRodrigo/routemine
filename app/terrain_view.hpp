#pragma once
#include "routemine/core.hpp"
#include "search_trace.hpp"
#include "routemine/design_review.hpp"
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <vtkSmartPointer.h>

class QVTKOpenGLNativeWidget;
class QLabel;
class vtkActor;
class vtkBillboardTextActor3D;
class vtkCallbackCommand;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkScalarBarActor;
class vtkLegendBoxActor;
class vtkPolyData;

class TerrainView final:public QWidget {
public:
    explicit TerrainView(QWidget* parent=nullptr);
    void set_terrain(std::shared_ptr<const routemine::Terrain>);
    void set_points(std::optional<routemine::Vec3>,std::optional<routemine::Vec3>);
    void set_route(std::span<const routemine::Vec3>,std::span<const routemine::SearchResult::Intervention> interventions={},std::span<const routemine::Vec3> reference={},bool projected_ramp=false,double tolerance=0.01,bool preliminary=false);
    void select_point(int point);
    void set_route_points(std::optional<std::size_t>,std::optional<std::size_t>);
    void camera_view(const QString& view);
    void zoom(double factor);
    void clear_search();
    void set_design_review(const routemine::DesignReview&);
    // Unified candidate view: verified roads in cyan, with grade-classified
    // exploration underneath. Separate cells never bridge disconnected paths.
    void set_candidate_review(const routemine::DesignReview&,double tolerance=0.01,std::span<const routemine::SearchResult> branches={});
    std::array<std::size_t,3> candidate_cell_counts() const {return candidate_counts_;}
    std::array<std::size_t,3> exploration_cell_counts() const {return exploration_counts_;}
    bool candidate_view_active() const {return candidate_mode_;}
    std::size_t verified_section_count() const {return verified_sections_;}
    std::size_t design_gap_count() const {return design_gaps_;}
    bool corridor_is_preliminary() const {return preliminary_;}
    std::array<std::size_t,3> design_cell_counts() const;
    void append_search(SearchTrace::Batch);
    void set_exploration_grade_limit(double percent);
    void show_search(bool);
    bool has_visible_route() const;
    bool has_visible_raw_route() const;
    bool has_visible_gray_exploration() const;
    bool has_visible_verified_exploration() const;
    std::size_t search_history_segments() const {return footprint_.size();}
    std::size_t verified_history_segments() const {return verified_footprint_.size();}
    std::function<void(int,routemine::Vec3)> point_picked;
    std::function<void(int,std::size_t)> route_point_picked;
private:
    void pick();
    void render();
    void set_candidate_legend(bool);
    QVTKOpenGLNativeWidget* widget_{};
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> window_;
    vtkSmartPointer<vtkRenderer> renderer_,overlay_;
    vtkSmartPointer<vtkActor> terrain_actor_,route_actor_,reference_actor_,markers_[4];
    vtkSmartPointer<vtkActor> search_partial_,search_complete_,design_best_,design_other_,design_gap_markers_,candidate_actor_;
    bool candidate_mode_{},search_requested_visible_{true},reference_requested_visible_{};
    std::array<std::size_t,3> candidate_counts_{},exploration_counts_{};
    double exploration_grade_limit_{10};
    std::size_t verified_sections_{},design_gaps_{};bool preliminary_{};
    SearchTrace::Paths partial_paths_,complete_paths_;
    SearchTrace::Footprint footprint_,verified_footprint_;
    vtkSmartPointer<vtkLegendBoxActor> route_legend_;
    vtkSmartPointer<vtkPolyData> legend_symbol_;
    std::size_t explored_{},connected_{};
    vtkSmartPointer<vtkBillboardTextActor3D> labels_[4];
    vtkSmartPointer<vtkScalarBarActor> legend_;
    vtkSmartPointer<vtkCallbackCommand> pick_command_;
    std::shared_ptr<const routemine::Terrain> terrain_;
    routemine::Vec3 origin_{};
    double scale_{1};
    std::vector<routemine::Vec3> displayed_route_;std::vector<std::size_t> route_cells_;
    int selection_{-1};
};
