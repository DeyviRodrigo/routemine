#include "terrain_view.hpp"
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkColorTransferFunction.h>
#include <vtkCommand.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkLegendBoxActor.h>
#include <vtkSphereSource.h>
#include <vtkTextProperty.h>
#include <vtkUnsignedCharArray.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

TerrainView::TerrainView(QWidget* parent):QWidget(parent) {
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    widget_=new QVTKOpenGLNativeWidget(this);widget_->setMinimumSize(400,350);layout->addWidget(widget_,1);
    window_=vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderer_=vtkSmartPointer<vtkRenderer>::New();window_->AddRenderer(renderer_);
    window_->SetNumberOfLayers(2);overlay_=vtkSmartPointer<vtkRenderer>::New();overlay_->SetLayer(1);
    overlay_->SetActiveCamera(renderer_->GetActiveCamera());overlay_->SetInteractive(false);overlay_->PreserveDepthBufferOff();window_->AddRenderer(overlay_);
    renderer_->SetBackground(0.045,0.065,0.085);renderer_->SetBackground2(0.11,0.16,0.19);renderer_->GradientBackgroundOn();
    widget_->setRenderWindow(window_);
    vtkNew<vtkInteractorStyleTrackballCamera> style;window_->GetInteractor()->SetInteractorStyle(style);
    terrain_actor_=vtkSmartPointer<vtkActor>::New();terrain_actor_->PickableOn();
    route_actor_=vtkSmartPointer<vtkActor>::New();route_actor_->PickableOn();
    reference_actor_=vtkSmartPointer<vtkActor>::New();reference_actor_->PickableOff();reference_actor_->VisibilityOff();
    reference_actor_->GetProperty()->SetColor(0.1,0.45,1);reference_actor_->GetProperty()->SetOpacity(0.30);reference_actor_->GetProperty()->SetLineWidth(3);overlay_->AddActor(reference_actor_);
    search_partial_=vtkSmartPointer<vtkActor>::New();search_complete_=vtkSmartPointer<vtkActor>::New();
    search_partial_->PickableOff();search_complete_->PickableOff();
    search_partial_->GetProperty()->SetColor(0.7,0.73,0.77);search_partial_->GetProperty()->SetOpacity(0.40);search_partial_->GetProperty()->SetLineWidth(1);
    search_complete_->GetProperty()->SetColor(0.1,0.45,1);search_complete_->GetProperty()->SetOpacity(0.30);search_complete_->GetProperty()->SetLineWidth(2);
    overlay_->AddActor(search_partial_);overlay_->AddActor(search_complete_);
    candidate_actor_=vtkSmartPointer<vtkActor>::New();candidate_actor_->PickableOff();candidate_actor_->VisibilityOff();
    candidate_actor_->GetProperty()->SetLineWidth(3);candidate_actor_->GetProperty()->SetOpacity(0.92);overlay_->AddActor(candidate_actor_);
    design_best_=vtkSmartPointer<vtkActor>::New();design_other_=vtkSmartPointer<vtkActor>::New();design_gap_markers_=vtkSmartPointer<vtkActor>::New();
    for(auto* actor:{design_best_.GetPointer(),design_other_.GetPointer(),design_gap_markers_.GetPointer()}){actor->PickableOff();overlay_->AddActor(actor);}
    design_best_->GetProperty()->SetColor(0.32,0.76,1);design_best_->GetProperty()->SetLineWidth(4);
    design_other_->GetProperty()->SetColor(0.07,0.30,0.85);design_other_->GetProperty()->SetLineWidth(3);
    design_gap_markers_->GetProperty()->SetColor(1,0.65,0.12);design_gap_markers_->GetProperty()->SetPointSize(9);
    route_legend_=vtkSmartPointer<vtkLegendBoxActor>::New();route_legend_->SetNumberOfEntries(6);
    vtkNew<vtkPoints> legend_points;legend_points->InsertNextPoint(-1,-0.15,0);legend_points->InsertNextPoint(1,-0.15,0);legend_points->InsertNextPoint(1,0.15,0);legend_points->InsertNextPoint(-1,0.15,0);
    vtkNew<vtkCellArray> legend_line;legend_line->InsertNextCell(4);for(int i=0;i<4;++i)legend_line->InsertCellPoint(i);
    vtkNew<vtkPolyData> symbol;symbol->SetPoints(legend_points);symbol->SetPolys(legend_line);legend_symbol_=symbol;
    const char* captions[]{"Preferido conforme","Alternativa conforme","Corredor / exploración","Extremos por conectar","Corte verificado","Relleno verificado"};
    double colors[6][3]{{0.32,0.76,1},{0.07,0.30,0.85},{0.65,0.68,0.72},{1,0.65,0.12},{0.96,0.2,0.2},{0.18,0.85,0.38}};
    for(int i=0;i<6;++i)route_legend_->SetEntry(i,symbol,captions[i],colors[i]);
    route_legend_->SetPosition(0.02,0.79);route_legend_->SetWidth(0.27);route_legend_->SetHeight(0.18);
    route_legend_->UseBackgroundOn();route_legend_->SetBackgroundColor(0.04,0.065,0.085);route_legend_->SetBackgroundOpacity(0.85);route_legend_->BorderOff();
    route_legend_->GetEntryTextProperty()->SetColor(0.92,0.96,1);route_legend_->GetEntryTextProperty()->SetFontSize(13);route_legend_->GetEntryTextProperty()->ItalicOff();
    overlay_->AddViewProp(route_legend_);
    route_actor_->GetProperty()->SetColor(0.1,0.45,1);route_actor_->GetProperty()->SetLineWidth(4);
    renderer_->AddActor(terrain_actor_);overlay_->AddActor(route_actor_);
    for(int i=0;i<4;++i) {
        markers_[i]=vtkSmartPointer<vtkActor>::New();markers_[i]->PickableOff();markers_[i]->VisibilityOff();
        markers_[i]->GetProperty()->SetColor(i>=2?0.65:(i?0.95:0.25),i>=2?0.4:(i?0.34:0.85),i>=2?1.0:(i?0.34:0.5));overlay_->AddActor(markers_[i]);
        labels_[i]=vtkSmartPointer<vtkBillboardTextActor3D>::New();labels_[i]->SetInput(i==0?"A":i==1?"B":i==2?"P1":"P2");
        labels_[i]->GetTextProperty()->SetFontSize(22);labels_[i]->GetTextProperty()->SetBold(true);
        labels_[i]->GetTextProperty()->SetColor(1,1,1);labels_[i]->PickableOff();labels_[i]->VisibilityOff();overlay_->AddActor(labels_[i]);
    }
    legend_=vtkSmartPointer<vtkScalarBarActor>::New();legend_->SetTitle("Cota (m)");legend_->SetNumberOfLabels(5);
    legend_->SetWidth(0.08);legend_->SetHeight(0.45);legend_->SetPosition(0.88,0.08);legend_->VisibilityOff();
    legend_->SetLabelFormat("%.1f");legend_->UnconstrainedFontSizeOn();legend_->GetTitleTextProperty()->SetFontSize(14);legend_->GetLabelTextProperty()->SetFontSize(13);
    legend_->GetTitleTextProperty()->ItalicOff();legend_->GetLabelTextProperty()->ItalicOff();renderer_->AddViewProp(legend_);
    pick_command_=vtkSmartPointer<vtkCallbackCommand>::New();pick_command_->SetClientData(this);
    pick_command_->SetCallback([](vtkObject*,unsigned long,void* client,void*) {
        auto* self=static_cast<TerrainView*>(client);
        if(self->selection_>=0) {self->pick();self->pick_command_->AbortFlagOn();}
    });
    window_->GetInteractor()->AddObserver(vtkCommand::LeftButtonPressEvent,pick_command_,1.0);
}
void TerrainView::render(){window_->Render();}
void TerrainView::set_terrain(std::shared_ptr<const routemine::Terrain> terrain) {
    terrain_=std::move(terrain);auto bounds=terrain_->bounds();
    origin_={std::midpoint(bounds[0].x,bounds[1].x),std::midpoint(bounds[0].y,bounds[1].y),bounds[0].z};
    scale_=std::max({bounds[1].x-bounds[0].x,bounds[1].y-bounds[0].y,bounds[1].z-bounds[0].z,1.0});
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();
    vtkNew<vtkDoubleArray> heights;heights->SetName("Cota");
    for(auto p:terrain_->vertices()) {auto q=routemine::to_local(p,origin_);points->InsertNextPoint(q.x,q.y,q.z);heights->InsertNextValue(p.z);}
    vtkNew<vtkCellArray> cells;
    for(auto face:terrain_->faces()) {vtkIdType ids[]{static_cast<vtkIdType>(face[0]),static_cast<vtkIdType>(face[1]),static_cast<vtkIdType>(face[2])};cells->InsertNextCell(3,ids);}
    vtkNew<vtkPolyData> mesh;mesh->SetPoints(points);mesh->SetPolys(cells);mesh->GetPointData()->SetScalars(heights);
    vtkNew<vtkColorTransferFunction> colors;double low=bounds[0].z,high=std::max(bounds[1].z,low+0.001),range=high-low;
    colors->AddRGBPoint(low,0.12,0.36,0.61);colors->AddRGBPoint(low+range*0.3,0.25,0.61,0.48);
    colors->AddRGBPoint(low+range*0.6,0.72,0.72,0.39);colors->AddRGBPoint(high,0.72,0.47,0.28);
    vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(mesh);mapper->SetLookupTable(colors);mapper->SetScalarRange(low,high);
    mapper->SetResolveCoincidentTopologyToPolygonOffset();terrain_actor_->SetMapper(mapper);
    terrain_actor_->GetProperty()->SetAmbient(0.55);terrain_actor_->GetProperty()->SetDiffuse(0.45);
    set_design_review({});clear_search();legend_->SetLookupTable(colors);legend_->VisibilityOn();set_points({},{});set_route({});camera_view("ISO");
}
void TerrainView::set_points(std::optional<routemine::Vec3> a,std::optional<routemine::Vec3> b) {
    std::optional<routemine::Vec3> points[]{a,b};
    for(int i=0;i<2;++i) {
        markers_[i]->SetVisibility(points[i].has_value());labels_[i]->SetVisibility(points[i].has_value());
        if(!points[i])continue;
        auto p=routemine::to_local(*points[i],origin_);vtkNew<vtkSphereSource> source;
        source->SetCenter(p.x,p.y,p.z);source->SetRadius(scale_*0.004);source->SetThetaResolution(16);source->SetPhiResolution(16);source->Update();
        vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(source->GetOutput());markers_[i]->SetMapper(mapper);
        labels_[i]->SetPosition(p.x,p.y,p.z+scale_*0.009);
    }
    render();
}
void TerrainView::set_route(std::span<const routemine::Vec3> route,std::span<const routemine::SearchResult::Intervention> interventions,std::span<const routemine::Vec3> reference,bool projected_ramp,double tolerance,bool preliminary) {
    preliminary_=preliminary;
    reference_actor_->GetProperty()->SetColor(preliminary?0.65:0.1,preliminary?0.68:0.45,preliminary?0.72:1);
    reference_requested_visible_=!reference.empty();reference_actor_->SetVisibility(!candidate_mode_&&reference_requested_visible_);
    if(!reference.empty()) {
        vtkNew<vtkPoints> pts;pts->SetDataTypeToDouble();vtkNew<vtkCellArray> cells;cells->InsertNextCell(static_cast<int>(reference.size()));
        for(auto p:reference){auto q=routemine::to_local(p,origin_);cells->InsertCellPoint(pts->InsertNextPoint(q.x,q.y,q.z));}
        vtkNew<vtkPolyData> poly;poly->SetPoints(pts);poly->SetLines(cells);vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(poly);reference_actor_->SetMapper(mapper);
    }
    displayed_route_.assign(route.begin(),route.end());route_cells_.clear();
    route_actor_->SetVisibility(!candidate_mode_&&!route.empty());if(route.empty()){set_route_points({},{});render();return;}
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> lines;
    if(route.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))throw std::length_error("Too many route vertices for display");
    vtkNew<vtkUnsignedCharArray> colors;colors->SetNumberOfComponents(3);colors->SetName("CorteRelleno");
    auto add=[&](routemine::Vec3 a,routemine::Vec3 b,routemine::EarthworkKind kind,std::size_t edge){
        auto first=routemine::to_local(a,origin_),last=routemine::to_local(b,origin_);
        vtkIdType ids[]{points->InsertNextPoint(first.x,first.y,first.z),points->InsertNextPoint(last.x,last.y,last.z)};lines->InsertNextCell(2,ids);route_cells_.push_back(edge);
        unsigned char normal[]{26,115,255},cut[]{245,55,55},fill[]{45,215,95};
        unsigned char gray[]{166,173,184};
        colors->InsertNextTypedTuple(preliminary?gray:kind==routemine::EarthworkKind::cut?cut:kind==routemine::EarthworkKind::fill?fill:normal);
    };
    if(!preliminary&&(projected_ramp||!interventions.empty())&&terrain_){for(const auto& segment:routemine::earthwork_segments(*terrain_,route,tolerance))add(segment.first,segment.last,segment.kind,segment.edge);}
    else for(std::size_t i=1;i<route.size();++i){
        bool intervention=false;for(const auto& span:interventions)if(i-1>=span.first&&i-1<span.last){intervention=true;break;}
        add(route[i-1],route[i],intervention?routemine::EarthworkKind::cut:routemine::EarthworkKind::surface,i-1);
    }
    vtkNew<vtkPolyData> line;line->SetPoints(points);line->SetLines(lines);line->GetCellData()->SetScalars(colors);
    vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(line);mapper->SetScalarModeToUseCellData();mapper->SetColorModeToDirectScalars();route_actor_->SetMapper(mapper);append_search({});render();
}
void TerrainView::set_route_points(std::optional<std::size_t> a,std::optional<std::size_t> b){
    std::optional<std::size_t> selected[]{a,b};
    for(int j=0;j<2;++j){auto i=j+2;bool valid=selected[j]&&*selected[j]<displayed_route_.size();markers_[i]->SetVisibility(valid);labels_[i]->SetVisibility(valid);if(!valid)continue;
        auto p=routemine::to_local(displayed_route_[*selected[j]],origin_);vtkNew<vtkSphereSource> source;source->SetCenter(p.x,p.y,p.z);source->SetRadius(scale_*0.003);source->Update();
        vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(source->GetOutput());markers_[i]->SetMapper(mapper);labels_[i]->SetPosition(p.x,p.y,p.z+scale_*0.009);
    }render();
}
void TerrainView::select_point(int point){selection_=point;widget_->setCursor(point>=0?Qt::CrossCursor:Qt::ArrowCursor);}
void TerrainView::pick() {
    if(!terrain_)return;
    auto* pos=window_->GetInteractor()->GetEventPosition();vtkNew<vtkCellPicker> picker;
    if(selection_>=2){
        picker->PickFromListOn();picker->AddPickList(route_actor_);picker->SetTolerance(0.01);
        if(!picker->Pick(pos[0],pos[1],0,overlay_))return;auto cell=picker->GetCellId();if(cell<0||static_cast<std::size_t>(cell)>=route_cells_.size())return;
        auto edge=route_cells_[static_cast<std::size_t>(cell)];auto* q=picker->GetPickPosition();auto world=routemine::to_world({q[0],q[1],q[2]},origin_);
        auto squared=[&](routemine::Vec3 p){return (p.x-world.x)*(p.x-world.x)+(p.y-world.y)*(p.y-world.y)+(p.z-world.z)*(p.z-world.z);};
        auto index=squared(displayed_route_[edge])<=squared(displayed_route_[edge+1])?edge:edge+1;auto selected=selection_;select_point(-1);if(route_point_picked)route_point_picked(selected-2,index);return;
    }

    picker->PickFromListOn();picker->AddPickList(terrain_actor_);picker->SetTolerance(0.0005);
    if(!picker->Pick(pos[0],pos[1],0,renderer_))return;
    auto* p=picker->GetPickPosition();auto world=routemine::to_world({p[0],p[1],p[2]},origin_);
    auto z=terrain_->elevation(world.x,world.y);if(!z)return;world.z=*z;
    auto selected=selection_;select_point(-1);if(point_picked)point_picked(selected,world);
}
void TerrainView::camera_view(const QString& view) {
    if(!terrain_)return;auto bounds=terrain_->bounds();
    auto center=routemine::to_local({std::midpoint(bounds[0].x,bounds[1].x),std::midpoint(bounds[0].y,bounds[1].y),std::midpoint(bounds[0].z,bounds[1].z)},origin_);
    routemine::Vec3 direction{1,-1,0.85},up{0,0,1};
    if(view=="TOP"){direction={0,0,1};up={0,1,0};}
    if(view=="BOT"){direction={0,0,-1};up={0,-1,0};}
    if(view=="FRONT")direction={0,-1,0};if(view=="BACK")direction={0,1,0};
    if(view=="LEFT")direction={-1,0,0};if(view=="RIGHT")direction={1,0,0};
    auto* camera=renderer_->GetActiveCamera();camera->SetFocalPoint(center.x,center.y,center.z);
    camera->SetPosition(center.x+direction.x*scale_*2,center.y+direction.y*scale_*2,center.z+direction.z*scale_*2);
    camera->SetViewUp(up.x,up.y,up.z);renderer_->ResetCamera();renderer_->ResetCameraClippingRange();render();
}
void TerrainView::zoom(double factor){renderer_->GetActiveCamera()->Dolly(factor);renderer_->ResetCameraClippingRange();render();}

void TerrainView::clear_search(){
    partial_paths_.clear();complete_paths_.clear();footprint_.clear();verified_footprint_.clear();explored_=connected_=0;
    append_search({});
}
void TerrainView::show_search(bool show){search_requested_visible_=show;search_partial_->SetVisibility(show);search_complete_->SetVisibility(show);render();}
void TerrainView::set_exploration_grade_limit(double percent){
    if(!std::isfinite(percent)||percent<0)return;
    exploration_grade_limit_=percent;append_search({});
}
bool TerrainView::has_visible_route() const {
    if(candidate_mode_){auto* mapper=vtkPolyDataMapper::SafeDownCast(candidate_actor_->GetMapper());return candidate_actor_->GetVisibility()!=0&&mapper&&mapper->GetInput()&&mapper->GetInput()->GetNumberOfLines()>0;}
    auto* mapper=vtkPolyDataMapper::SafeDownCast(route_actor_->GetMapper());
    return route_actor_->GetVisibility()!=0&&mapper&&mapper->GetInput()&&mapper->GetInput()->GetNumberOfLines()>0;
}
void TerrainView::append_search(SearchTrace::Batch batch){
    explored_+=batch.explored;connected_+=batch.connected;
    if(batch.footprint_changed)footprint_=std::move(batch.footprint);
    if(batch.verified_footprint_changed)verified_footprint_=std::move(batch.verified_footprint);
    auto append=[](auto& dst,auto& src,std::size_t limit){for(auto& path:src){
        bool duplicate=false;
        if(limit==16)for(const auto& old:dst)if(old.size()==path.size()&&std::equal(old.begin(),old.end(),path.begin(),[](auto a,auto b){return a.x==b.x&&a.y==b.y&&a.z==b.z;})){duplicate=true;break;}
        if(!duplicate)dst.push_back(std::move(path));
    }while(dst.size()>limit)dst.pop_front();};
    append(partial_paths_,batch.partial,3000);append(complete_paths_,batch.complete,16);
    exploration_counts_={};
    auto update=[&](vtkActor* actor,const auto& paths){
        vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> lines;
        const bool classify=candidate_mode_&&actor==search_partial_.GetPointer();
        vtkNew<vtkUnsignedCharArray> colors;colors->SetNumberOfComponents(3);colors->SetName("PendienteExploracion");
        for(const auto& path:paths){
            if(path.size()<2)continue;
            if(!candidate_mode_&&actor==search_complete_.GetPointer()&&displayed_route_.size()>=2){
                bool selected=true;for(std::size_t i=0;i<path.size();++i){auto a=path[i],b=displayed_route_[i*(displayed_route_.size()-1)/(path.size()-1)];if(a.x!=b.x||a.y!=b.y||a.z!=b.z){selected=false;break;}}
                if(selected)continue;
            }
            if(classify){
                for(std::size_t i=1;i<path.size();++i){
                    const auto first=path[i-1],last=path[i];const double horizontal=std::hypot(last.x-first.x,last.y-first.y);
                    if(horizontal<=1e-12)continue;
                    const double grade=100*(last.z-first.z)/horizontal;
                    const std::size_t kind=std::abs(grade)>exploration_grade_limit_+1e-9?(grade>0?1:2):0;
                    const unsigned char gray[]{179,186,196},up[]{245,55,55},down[]{45,215,95};
                    colors->InsertNextTypedTuple(kind==1?up:kind==2?down:gray);++exploration_counts_[kind];
                    const auto a=routemine::to_local(first,origin_),b=routemine::to_local(last,origin_);
                    vtkIdType ids[]{points->InsertNextPoint(a.x,a.y,a.z),points->InsertNextPoint(b.x,b.y,b.z)};lines->InsertNextCell(2,ids);
                }
            }else{lines->InsertNextCell(static_cast<int>(path.size()));for(auto p:path){auto q=routemine::to_local(p,origin_);lines->InsertCellPoint(points->InsertNextPoint(q.x,q.y,q.z));}}
        }
        vtkNew<vtkPolyData> poly;poly->SetPoints(points);poly->SetLines(lines);vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(poly);
        if(classify){poly->GetCellData()->SetScalars(colors);mapper->SetScalarModeToUseCellData();mapper->SetColorModeToDirectScalars();}else mapper->ScalarVisibilityOff();
        actor->SetMapper(mapper);
    };
    // The complete spatial footprint replaces the sliding recent-branch window.
    // A batch without a changed footprint retains all previous regions.
    if(!footprint_.empty())update(search_partial_,footprint_);else update(search_partial_,partial_paths_);
    if(!verified_footprint_.empty())update(search_complete_,verified_footprint_);else update(search_complete_,complete_paths_);
    search_partial_->SetVisibility(search_requested_visible_);search_complete_->SetVisibility(search_requested_visible_);
    render();
}

void TerrainView::set_design_review(const routemine::DesignReview& review){
    candidate_mode_=false;candidate_counts_={};exploration_counts_={};candidate_actor_->VisibilityOff();
    if(search_partial_->GetMapper())search_partial_->GetMapper()->ScalarVisibilityOff();
    search_partial_->GetProperty()->SetOpacity(0.40);search_partial_->GetProperty()->SetLineWidth(1);
    search_complete_->GetProperty()->SetColor(0.1,0.45,1);search_complete_->GetProperty()->SetOpacity(0.30);search_complete_->GetProperty()->SetLineWidth(2);
    route_actor_->SetVisibility(displayed_route_.size()>=2);reference_actor_->SetVisibility(reference_requested_visible_);
    search_partial_->SetVisibility(search_requested_visible_);search_complete_->SetVisibility(search_requested_visible_);
    design_best_->VisibilityOn();design_other_->VisibilityOn();set_candidate_legend(false);
    verified_sections_=review.sections.size()+(review.connected?1:0);design_gaps_=review.gaps.size();
    vtkNew<vtkPoints> best_points;best_points->SetDataTypeToDouble();vtkNew<vtkCellArray> best_lines;
    vtkNew<vtkPoints> other_points;other_points->SetDataTypeToDouble();vtkNew<vtkCellArray> other_lines;
    auto add=[&](const auto& route,bool preferred){auto* points=preferred?best_points.GetPointer():other_points.GetPointer();auto* lines=preferred?best_lines.GetPointer():other_lines.GetPointer();
        if(route.size()<2)return;lines->InsertNextCell(static_cast<int>(route.size()));for(auto p:route){auto q=routemine::to_local(p,origin_);lines->InsertCellPoint(points->InsertNextPoint(q.x,q.y,q.z));}};
    auto same=[](routemine::Vec3 a,routemine::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y)<1e-6&&std::abs(a.z-b.z)<1e-6;};
    for(const auto& section:review.sections){bool preferred=true;
        for(const auto& other:review.sections)if(std::abs(other.first_station_m-section.first_station_m)<1e-6&&std::abs(other.last_station_m-section.last_station_m)<1e-6&&same(other.candidate.route.front(),section.candidate.route.front())&&same(other.candidate.route.back(),section.candidate.route.back())&&
            routemine::measure(other.candidate.route).horizontal_m<routemine::measure(section.candidate.route).horizontal_m-1e-6){preferred=false;break;}
        add(section.candidate.route,preferred&&!review.connected);
    }
    if(review.connected)add(review.connected->route,true);
    auto map=[&](vtkActor* actor,vtkPoints* points,vtkCellArray* cells,bool vertices){vtkNew<vtkPolyData> poly;poly->SetPoints(points);if(vertices)poly->SetVerts(cells);else poly->SetLines(cells);vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(poly);actor->SetMapper(mapper);};
    map(design_best_,best_points,best_lines,false);map(design_other_,other_points,other_lines,false);
    vtkNew<vtkPoints> ends;ends->SetDataTypeToDouble();vtkNew<vtkCellArray> markers;
    for(const auto& gap:review.gaps)for(auto p:{gap.start,gap.end}){auto q=routemine::to_local(p,origin_);auto id=ends->InsertNextPoint(q.x,q.y,q.z);markers->InsertNextCell(1,&id);}
    map(design_gap_markers_,ends,markers,true);
    for(auto* actor:{design_other_.GetPointer(),design_best_.GetPointer(),design_gap_markers_.GetPointer()}){overlay_->RemoveActor(actor);overlay_->AddActor(actor);}
    render();
}

std::array<std::size_t,3> TerrainView::design_cell_counts() const {
    auto count=[](vtkActor* actor){auto* mapper=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());return mapper&&mapper->GetInput()?static_cast<std::size_t>(mapper->GetInput()->GetNumberOfCells()):std::size_t{};};
    return {count(design_best_),count(design_other_),count(design_gap_markers_)};
}


void TerrainView::set_candidate_legend(bool candidate){
    if(candidate){
        const char* captions[]{"Exploración","Exceso ascendente","Exceso descendente","Cumple criterios"};
        double colors[4][3]{{0.7,0.73,0.77},{245./255,55./255,55./255},{45./255,215./255,95./255},{82./255,194./255,1}};
        route_legend_->SetNumberOfEntries(4);for(int i=0;i<4;++i)route_legend_->SetEntry(i,legend_symbol_,captions[i],colors[i]);
        route_legend_->SetPosition(0.02,0.83);route_legend_->SetHeight(0.14);
    }else{
        const char* captions[]{"Preferido conforme","Alternativa conforme","Corredor / exploración","Extremos por conectar","Corte verificado","Relleno verificado"};
        double colors[6][3]{{0.32,0.76,1},{0.07,0.30,0.85},{0.65,0.68,0.72},{1,0.65,0.12},{0.96,0.2,0.2},{0.18,0.85,0.38}};
        route_legend_->SetNumberOfEntries(6);for(int i=0;i<6;++i)route_legend_->SetEntry(i,legend_symbol_,captions[i],colors[i]);
        route_legend_->SetPosition(0.02,0.79);route_legend_->SetHeight(0.18);
    }
}

void TerrainView::set_candidate_review(const routemine::DesignReview& review,double tolerance,std::span<const routemine::SearchResult> branches){
    const bool entering=!candidate_mode_;
    candidate_mode_=true;candidate_counts_={};verified_sections_=review.sections.size()+(review.connected?1:0)+branches.size();design_gaps_=review.gaps.size();
    search_partial_->GetProperty()->SetOpacity(0.75);search_partial_->GetProperty()->SetLineWidth(2);
    search_complete_->GetProperty()->SetColor(82./255,194./255,1);search_complete_->GetProperty()->SetOpacity(0.75);search_complete_->GetProperty()->SetLineWidth(2);
    route_actor_->VisibilityOff();reference_actor_->VisibilityOff();search_partial_->SetVisibility(search_requested_visible_);search_complete_->SetVisibility(search_requested_visible_);design_best_->VisibilityOff();design_other_->VisibilityOff();
    set_candidate_legend(true);
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> lines;
    vtkNew<vtkUnsignedCharArray> colors;colors->SetNumberOfComponents(3);colors->SetName("CandidatosVerificados");
    std::vector<const std::vector<routemine::Vec3>*> drawn;
    auto add=[&](const routemine::SearchResult& candidate){
        const auto& route=candidate.route;if(!terrain_||route.size()<2)return;
        for(auto* old:drawn)if(old->size()==route.size()&&std::equal(old->begin(),old->end(),route.begin(),[](auto a,auto b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}))return;
        std::vector<routemine::EarthworkSegment> segments;
        try{segments=routemine::earthwork_segments(*terrain_,route,tolerance);}catch(const std::exception&){return;}
        drawn.push_back(&route);
        for(const auto& segment:segments){
            auto a=routemine::to_local(segment.first,origin_),b=routemine::to_local(segment.last,origin_);
            vtkIdType ids[]{points->InsertNextPoint(a.x,a.y,a.z),points->InsertNextPoint(b.x,b.y,b.z)};lines->InsertNextCell(2,ids);
            const unsigned char cyan[]{82,194,255};colors->InsertNextTypedTuple(cyan);++candidate_counts_[0];
        }
    };
    for(const auto& section:review.sections)add(section.candidate);if(review.connected)add(*review.connected);for(const auto& branch:branches)add(branch);
    vtkNew<vtkPolyData> poly;poly->SetPoints(points);poly->SetLines(lines);poly->GetCellData()->SetScalars(colors);
    vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(poly);mapper->SetScalarModeToUseCellData();mapper->SetColorModeToDirectScalars();candidate_actor_->SetMapper(mapper);candidate_actor_->SetVisibility(lines->GetNumberOfCells()>0);
    vtkNew<vtkPoints> ends;ends->SetDataTypeToDouble();vtkNew<vtkCellArray> markers;
    for(const auto& gap:review.gaps)for(auto p:{gap.start,gap.end}){auto q=routemine::to_local(p,origin_);auto id=ends->InsertNextPoint(q.x,q.y,q.z);markers->InsertNextCell(1,&id);}
    vtkNew<vtkPolyData> gaps;gaps->SetPoints(ends);gaps->SetVerts(markers);vtkNew<vtkPolyDataMapper> gap_mapper;gap_mapper->SetInputData(gaps);design_gap_markers_->SetMapper(gap_mapper);design_gap_markers_->SetVisibility(markers->GetNumberOfCells()>0);
    // Keep grade-classified exploration behind verified alternatives and their ends.
    for(auto* actor:{search_partial_.GetPointer(),search_complete_.GetPointer(),candidate_actor_.GetPointer(),design_gap_markers_.GetPointer()}){overlay_->RemoveActor(actor);overlay_->AddActor(actor);}
    if(entering)append_search({});else render();
}

bool TerrainView::has_visible_raw_route() const {return route_actor_->GetVisibility()!=0;}
bool TerrainView::has_visible_gray_exploration() const {
    auto* mapper=vtkPolyDataMapper::SafeDownCast(search_partial_->GetMapper());
    return search_partial_->GetVisibility()!=0&&mapper&&mapper->GetInput()&&mapper->GetInput()->GetNumberOfLines()>0;
}
bool TerrainView::has_visible_verified_exploration() const {
    auto* mapper=vtkPolyDataMapper::SafeDownCast(search_complete_->GetMapper());
    return search_complete_->GetVisibility()!=0&&mapper&&mapper->GetInput()&&mapper->GetInput()->GetNumberOfLines()>0;
}
