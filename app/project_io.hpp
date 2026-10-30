#pragma once
#include "routemine/config.hpp"
#include "routemine/design_review.hpp"
#include <QByteArray>
#include <QString>
#include <QStringList>
struct ViewerUiState {
    bool intermediate_enabled{};
    std::optional<routemine::Parameters> local_design;
};
struct ViewerDesignReview {
    routemine::DesignReview review;
    routemine::Parameters parameters;
    bool road_candidates{false};
    double max_intervention_length_m{100};
    std::vector<routemine::SearchResult> branches;
};
struct ViewerProject {
    QString terrain_path;
    routemine::Settings settings;
    std::vector<routemine::Vec3> route;
    routemine::SearchResult route_info;
    QStringList warnings;
    ViewerUiState ui;
    std::optional<ViewerDesignReview> design_review;
};
ViewerProject read_viewer_project(const QByteArray&,const QString& project_path);
routemine::Parameters effective_review_parameters(const routemine::Configuration&,const routemine::Parameters& saved);
QByteArray write_viewer_project(const routemine::Configuration&,const routemine::SearchResult*,const ViewerUiState& ui={},const ViewerDesignReview* review=nullptr);
void save_viewer_file(const QString& path,const QByteArray&,const QStringList& protected_paths);
