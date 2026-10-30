#include "project_io.hpp"
#include "design_export.hpp"
#include "routemine/continuous_search.hpp"
#include "routemine/road_candidates.hpp"
#include "routemine/road_standards.hpp"
#include "terrain_view.hpp"
#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenuBar>
#include <QMenu>
#include <QTabWidget>
#include <QKeySequence>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#include <QtConcurrentRun>
#include <vtkCommand.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkRenderWindowInteractor.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <functional>
#include <iostream>
#include <memory>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <set>
#include <stop_token>
#include <tuple>
using namespace routemine;
namespace {
// Keep the editor compact when its representable maximum has hundreds of digits.
class UnboundedDistanceInput final:public QDoubleSpinBox {
public:
    QSize sizeHint() const override {auto size=QDoubleSpinBox::sizeHint();size.setWidth(180);return size;}
    QSize minimumSizeHint() const override {auto size=QDoubleSpinBox::minimumSizeHint();size.setWidth(80);return size;}
};
QString format(double v){return QLocale::c().toString(v,'f',2);}
QString concise(double v){return QLocale::c().toString(v,'g',12);}
QString remaining_time(qint64 elapsed_ms,int percent) {
    if(percent>=100)return "Completado";
    if(percent>=99)return "Finalizando…";
    if(elapsed_ms<2000||percent<=0)return "Estimando…";
    // Work-based extrapolation, not a promised completion time. A stalled stage
    // increases the estimate instead of counting down to an artificial zero.
    const auto seconds=static_cast<qint64>(std::ceil(double(elapsed_ms)/1000*(100-percent)/percent));
    if(seconds>=3600)return QString("≈ %1 h %2 min").arg(seconds/3600).arg((seconds%3600)/60);
    if(seconds>=60)return QString("≈ %1 min %2 s").arg(seconds/60).arg(seconds%60);
    return QString("≈ %1 s").arg(std::max<qint64>(1,seconds));
}
void bounded_row(QFormLayout* form,const QString& label,QWidget* input,double min,double max,double initial) {
    input->setProperty("initialValue",initial);
    auto* row=new QWidget;auto* layout=new QVBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
    auto text=QString("Permitido: %1–%2\nInicial: %3").arg(concise(min),concise(max),concise(initial));
    auto* hint=new QLabel(text);hint->setWordWrap(true);hint->setStyleSheet("color:#9fb5c4;font-size:10px;");
    hint->setObjectName(input->objectName()+"Limits");input->setProperty("rangeHelp",text);input->setToolTip(text);
    layout->addWidget(input);layout->addWidget(hint);form->addRow(label,row);
}
std::string number(double v){std::ostringstream out;out<<std::setprecision(17)<<v;return out.str();}
std::filesystem::path native_path(const QString& p){
#ifdef Q_OS_WIN
    return std::filesystem::path(p.toStdWString());
#else
    return std::filesystem::u8path(p.toUtf8().toStdString());
#endif
}
struct LoadResult {std::shared_ptr<const Terrain> terrain;DxfImportReport report;std::optional<SearchResult> restored;QString error,route_error,review_error;std::optional<ViewerDesignReview> design_review;};
struct PlanResult {RoadAlternatives alternatives;QString error;std::optional<Configuration> effective;std::optional<SearchResult> base_surface;std::optional<Configuration> base_config;std::optional<ViewerDesignReview> design_review;};
class ProcessDialog final:public QDialog {
public:
    explicit ProcessDialog(QWidget* parent):QDialog(parent,Qt::Window|Qt::WindowTitleHint|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint) {}
    std::function<void()> minimize;
    void reject() override {if(minimize)minimize();}
protected:
    void closeEvent(QCloseEvent* event) override {event->ignore();if(minimize)minimize();}
    void changeEvent(QEvent* event) override {
        QDialog::changeEvent(event);
        if(event->type()==QEvent::WindowStateChange&&isMinimized())
            QMetaObject::invokeMethod(this,[this]{if(isMinimized()&&minimize)minimize();},Qt::QueuedConnection);
    }
};
const std::vector<std::pair<const char*,const char*>> cost_fields{
    {"road_per_m2","Vía / m²"},{"soil_cut_per_m3","Corte suelo / m³"},{"rock_cut_per_m3","Corte roca / m³"},
    {"fill_placement_per_m3","Colocación relleno / m³"},{"imported_material_per_m3","Material importado / m³"},
    {"disposal_per_m3","Disposición / m³"},{"haul_per_m3_km","Acarreo / m³·km"},{"rock_fraction","Fracción roca (0–1)"},
    {"internal_haul_km","Acarreo interno (km)"},{"import_haul_km","Acarreo importación (km)"},{"disposal_haul_km","Acarreo disposición (km)"}};
}
class RouteWindow final:public QMainWindow {
public:
    RouteWindow();
    void smoke_test(const QString& terrain,const QString& output,bool capture_only,bool road_options=false,Settings test_settings={});
    void smoke_proposals(const QString& terrain,const QString& output);
    void smoke_features(const QString& terrain,const QString& output);
    void smoke_search(const QString&,const Settings&,const QString&);
    void smoke_validation(const QString&);
    void smoke_history(const QString&);
    void smoke_design_colors(const QString&);
    void smoke_design_standards(const QString&);
    void smoke_verified_history(const QString&);
    void smoke_continuous(const QString&,const Settings&,const QString&);
    void smoke_candidates(const QString&,const Settings&,const QString&);
    void smoke_gray_candidates(const QString&,const Settings&,const QString&);
    void smoke_guide_fallback(const QString&);
    void smoke_candidate_colors(const QString&);
    void smoke_corridor(const QString&,const Settings&,const QString&);
    void capture_project(const QString& project,const QString& output);
    void capture_guide(const QString& project,const QString& output);
    void open_project_file(const QString&);
protected:
    void closeEvent(QCloseEvent* event) override {stop_.request_stop();++generation_;event->accept();}
private:
    void load(QString,std::optional<ViewerProject> project={});
    void open_project();void register_point(int);void set_point(int,Vec3);
    void clear_route();void clear_points();Settings settings() const;Configuration configuration() const;
    void apply_settings(const Settings&);void reset_advanced();QString configuration_issue() const;
    RoadStandardInput design_context() const;void refresh_design_criteria(bool apply=true);
    void calculate(bool adjust=false,bool retry=true);void show_diagnostics();void configure_local();void optimize_segment();void set_local_point(int,std::size_t);void choose_route(int);void show_result();void export_route(bool);void save_project(bool save_as=false);
    void show_settings(QDialog*);
    void restore_result(bool keep_search=false);
    ViewerUiState ui_state() const {return {local_tools_enabled_,local_parameters_};}
    void update_design_limits();void update_controls();void busy(bool,const QString& message={});
    void show_process();void minimize_process();void update_process();void cancel_process();
    void error(const QString& m);
    QDoubleSpinBox* field(QFormLayout*,const QString&,const std::string&,double,double,double,int=3);
    QWidget* sidebar_{};TerrainView* view_{};QLabel *file_name_{},*metrics_{},*result_status_{},*message_{};
    std::array<std::array<QLineEdit*,3>,2> coordinates_{};std::array<QPushButton*,2> select_{},register_{};
    QPushButton *local_pick_[2]{},*local_optimize_{},*local_clear_{};QGroupBox* local_panel_{};bool local_tools_enabled_{};
    std::array<std::optional<ViewerDesignReview>,2> design_reviews_;
    std::array<std::optional<std::size_t>,2> local_points_;std::optional<Parameters> local_parameters_;
    QPushButton *calculate_{},*clear_{},*cancel_{},*reset_{};QLabel* precision_hint_{};
    QAction *save_{},*save_as_{},*csv_{},*dxf_{},*open_action_{},*design_action_{},*advanced_action_{},*local_action_{},*details_action_{};
    QDialog *design_dialog_{},*advanced_dialog_{};QTabWidget* advanced_tabs_{};
    QDoubleSpinBox* intervention_length_{};
    QComboBox *standards_mode_{},*design_terrain_{};QCheckBox* design_ice_risk_{};QLabel* standards_hint_{};
    std::map<std::string,double> requested_design_;
    QProgressBar* progress_{};QComboBox *cost_profile_{},*route_selection_{};QCheckBox *adaptive_{},*continuous_{},*extend_limits_{};QGroupBox *advanced_{},*z_domain_{};
    ProcessDialog* process_dialog_{};QWidget* process_panel_{};QLabel *process_activity_{},*process_time_{},*process_summary_{};
    QPushButton *process_cancel_{},*process_restore_{};QString process_message_;
    QProgressBar* floating_progress_{};QLabel *process_percent_{},*process_remaining_{};
    std::shared_ptr<std::atomic<int>> job_progress_;
    std::shared_ptr<SearchTrace> search_trace_;
    bool continuous_search_{},exercise_continuous_{},exercise_candidates_{};std::size_t search_round_{};
    std::map<std::string,QDoubleSpinBox*> fields_;std::map<std::string,QSpinBox*> budgets_;
    std::shared_ptr<const Terrain> terrain_;QString terrain_path_,project_path_;std::array<std::optional<Vec3>,2> points_;
    Settings base_=routetec_interface_defaults();std::optional<Configuration> result_config_;std::optional<SearchResult> result_;
    std::optional<RoadAlternatives> alternatives_;
    std::array<QString,2> option_feedback_,option_diagnostics_;
    std::array<std::optional<Configuration>,2> option_configs_;
    std::array<std::vector<std::string>,2> option_messages_;
    std::set<std::string> changed_;std::stop_source stop_;std::size_t generation_{};bool busy_{},applying_{},testing_{};QElapsedTimer elapsed_;QTimer timer_;
};
QDoubleSpinBox* RouteWindow::field(QFormLayout* form,const QString& label,const std::string& key,double value,double min,double max,int decimals) {
    QDoubleSpinBox* input=max==std::numeric_limits<double>::max()?new UnboundedDistanceInput:new QDoubleSpinBox;input->setLocale(QLocale::c());input->setDecimals(decimals);input->setRange(min,max);input->setValue(value);
    input->setKeyboardTracking(false);input->setObjectName(QString::fromStdString(key));bounded_row(form,label,input,min,max,value);fields_[key]=input;
    const bool design_parameter=key=="max_grade_percent"||key=="min_radius_m"||key=="min_straight_m"||key=="road_width_m";
    if(design_parameter)requested_design_[key]=value;
    connect(input,&QDoubleSpinBox::valueChanged,this,[this,key,design_parameter]{if(!applying_){if(design_parameter)requested_design_[key]=fields_.at(key)->value();changed_.insert(key);if(design_parameter&&standards_hint_)refresh_design_criteria();clear_route();}});return input;
}
RouteWindow::RouteWindow() {
    setWindowTitle("RouteTec");resize(1400,900);setMinimumSize(1000,650);
    auto* central=new QWidget;auto* layout=new QHBoxLayout(central);layout->setContentsMargins(0,0,0,0);setCentralWidget(central);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFixedWidth(365);layout->addWidget(scroll);
    sidebar_=new QWidget;scroll->setWidget(sidebar_);auto* side=new QVBoxLayout(sidebar_);side->setSpacing(10);
    auto* box=new QGroupBox("Terreno");auto* terrain_layout=new QVBoxLayout(box);
    auto* load_button=new QPushButton("CARGAR TOPOGRAFÍA DXF / OBJ");load_button->setObjectName("loadTerrain");terrain_layout->addWidget(load_button);
    auto* file_menu=menuBar()->addMenu("&Archivo");file_menu->setObjectName("fileMenu");
    open_action_=file_menu->addAction("&Abrir proyecto…");open_action_->setObjectName("openProject");open_action_->setShortcut(QKeySequence::Open);
    file_menu->addSeparator();
    save_=file_menu->addAction("&Guardar proyecto");save_->setObjectName("saveProject");save_->setShortcut(QKeySequence::Save);
    save_as_=file_menu->addAction("Guardar proyecto &como…");save_as_->setShortcut(QKeySequence::SaveAs);
    file_menu->addSeparator();
    csv_=file_menu->addAction("Exportar ruta &CSV…");csv_->setObjectName("exportRouteCsv");
    dxf_=file_menu->addAction("Exportar eje &DXF…");dxf_->setObjectName("exportRouteDxf");
    connect(csv_,&QAction::triggered,this,[this]{export_route(false);});connect(dxf_,&QAction::triggered,this,[this]{export_route(true);});
    file_menu->addSeparator();auto* quit=file_menu->addAction("&Salir");connect(quit,&QAction::triggered,this,&QWidget::close);
    auto* project_menu=menuBar()->addMenu("&Configuración");project_menu->setObjectName("settingsMenu");
    design_action_=project_menu->addAction("&Diseño…");design_action_->setObjectName("projectDesign");
    advanced_action_=project_menu->addAction("Configuración &avanzada…");advanced_action_->setObjectName("advancedSettings");
    local_action_=project_menu->addAction("Puntos &intermedios…");local_action_->setObjectName("intermediateSettingsAction");connect(local_action_,&QAction::triggered,this,&RouteWindow::configure_local);
    auto make_dialog=[this](const QString& title,const QString& name){
        auto* dialog=new QDialog(this,Qt::Window);dialog->setWindowTitle(title);dialog->setObjectName(name);
        dialog->setModal(false);dialog->resize(650,730);dialog->setMinimumSize(480,400);
        auto* body=new QVBoxLayout(dialog);
        auto* note=new QLabel("Los cambios se aplican al proyecto y requieren recalcular la ruta. Use Archivo > Guardar proyecto para conservarlos.");note->setWordWrap(true);body->addWidget(note);
        return dialog;
    };
    design_dialog_=make_dialog("Diseño del proyecto","designDialog");
    advanced_dialog_=make_dialog("Configuración avanzada del proyecto","advancedDialog");
    auto finish_dialog=[](QDialog* dialog){auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);buttons->button(QDialogButtonBox::Close)->setText("Cerrar");dialog->layout()->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::hide);};
    connect(design_action_,&QAction::triggered,this,[this]{show_settings(design_dialog_);});
    connect(advanced_action_,&QAction::triggered,this,[this]{show_settings(advanced_dialog_);});
    connect(save_,&QAction::triggered,this,[this]{save_project();});
    connect(save_as_,&QAction::triggered,this,[this]{save_project(true);});
    file_name_=new QLabel("No se ha cargado una topografía.");file_name_->setWordWrap(true);terrain_layout->addWidget(file_name_);side->addWidget(box);
    connect(load_button,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"Cargar topografía",terrain_path_,"Topografía (*.dxf *.obj)");if(!p.isEmpty())load(p);});
    connect(open_action_,&QAction::triggered,this,&RouteWindow::open_project);
    for(int i=0;i<2;++i) {
        auto* point_box=new QGroupBox(i?"B · DESTINO":"A · ORIGEN");auto* point_layout=new QVBoxLayout(point_box);auto* row=new QFormLayout;
        for(int j=0;j<3;++j) {
            auto* input=new QLineEdit;coordinates_[i][j]=input;
            input->setPlaceholderText(QString("%1 %2 *").arg(QChar("XYZ"[j])).arg(QChar(i?'B':'A')));
            input->setAccessibleName(QString("Coordenada %1 del punto %2").arg(QChar("XYZ"[j])).arg(QChar(i?'B':'A')));
            input->setObjectName(QString("point%1%2").arg(QChar(i?'B':'A')).arg(QChar("XYZ"[j])));row->addRow(QString(QChar("XYZ"[j])),input);
            connect(input,&QLineEdit::textEdited,this,[this,i]{points_[i].reset();view_->set_points(points_[0],points_[1]);clear_route();});
        }
        point_layout->addLayout(row);register_[i]=new QPushButton("REGISTRAR");register_[i]->setObjectName(i?"registerPointB":"registerPointA");
        select_[i]=new QPushButton(i?"MARCAR B EN 3D":"MARCAR A EN 3D");select_[i]->setObjectName(i?"pickPointB":"pickPointA");auto* point_buttons=new QHBoxLayout;point_buttons->addWidget(register_[i]);point_buttons->addWidget(select_[i]);point_layout->addLayout(point_buttons);side->addWidget(point_box);
        connect(register_[i],&QPushButton::clicked,this,[this,i]{register_point(i);});
        connect(select_[i],&QPushButton::clicked,this,[this,i]{view_->select_point(i);statusBar()->showMessage(i?"Seleccione B sobre la topografía":"Seleccione A sobre la topografía");});
    }
    auto* design=new QGroupBox("Diseño");auto* design_form=new QFormLayout(design);
    design_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    standards_mode_=new QComboBox;standards_mode_->setObjectName("standards_mode");
    standards_mode_->addItem("Exploración preliminar","preliminary");standards_mode_->addItem("DG2018 tercera clase","dg2018");standards_mode_->addItem("Personalizado","custom");design_form->addRow("Criterios de diseño",standards_mode_);
    field(design_form,"Velocidad de diseño (km/h)","design_speed_kmh",20,20,130,0)->setSingleStep(10);
    design_terrain_=new QComboBox;design_terrain_->setObjectName("design_terrain");
    design_terrain_->addItem("Plano","flat");design_terrain_->addItem("Ondulado","rolling");design_terrain_->addItem("Accidentado","rugged");design_terrain_->addItem("Escarpado","steep");design_terrain_->setCurrentIndex(3);design_form->addRow("Orografía",design_terrain_);
    field(design_form,"Peralte de referencia (%)","design_superelevation_percent",6,0,12,1);
    design_ice_risk_=new QCheckBox("Considerar riesgo de hielo");design_ice_risk_->setObjectName("design_ice_risk");design_ice_risk_->setChecked(true);design_form->addRow(design_ice_risk_);
    auto* altitude=field(design_form,"Altitud de referencia (m; 0 = máxima TIN)","design_altitude_m",0,0,10000,1);altitude->setSpecialValueText("Automática · máxima de la TIN");
    extend_limits_=new QCheckBox("Extender los límites");extend_limits_->setObjectName("extendDesignLimits");design_form->addRow(extend_limits_);
    field(design_form,"Pendiente máxima (%)","max_grade_percent",10,0,12,1);
    continuous_=new QCheckBox("Curvas continuas y vueltas completas",design_dialog_);continuous_->setObjectName("continuous_curves");continuous_->setChecked(true);continuous_->setEnabled(false);continuous_->hide();
    field(design_form,"Recta mínima de avance (m)","min_straight_m",35,35,std::numeric_limits<double>::max(),2);
    field(design_form,"Radio mínimo de curva (m)","min_radius_m",30,30,std::numeric_limits<double>::max(),2);
    field(design_form,"Giro máximo: modo anterior (°)","max_turn_degrees",120,1,180,2);
    design_form->setRowVisible(fields_.at("max_turn_degrees")->parentWidget(),false);
    update_design_limits();
    connect(extend_limits_,&QCheckBox::toggled,this,[this]{update_design_limits();if(!applying_){for(const auto* key:{"max_grade_percent","min_radius_m","min_straight_m"}){changed_.insert(key);requested_design_[key]=fields_.at(key)->value();}clear_route();refresh_design_criteria();}});
    auto* geometry_hint=new QLabel("Se buscan alternativas con rectas y curvas tangentes, incluyendo rodeos largos y vueltas alrededor del terreno. Gris: exploración; rojo/verde: exceso de pendiente ascendente/descendente en el sentido del trazo; celeste: cumple los criterios comprobados del eje y el radio configurado.");
    geometry_hint->setObjectName("alignmentReference");geometry_hint->setWordWrap(true);design_form->addRow(geometry_hint);
    auto* design_scroll=new QScrollArea;design_scroll->setWidgetResizable(true);design_scroll->setWidget(design);design_dialog_->layout()->addWidget(design_scroll);finish_dialog(design_dialog_);design_dialog_->resize(650,510);
    route_selection_=new QComboBox;route_selection_->setObjectName("routeSelection");
    route_selection_->addItem("1. Propuesta respetando el terreno.");route_selection_->addItem("2. Propuesto con corte y/o relleno.");route_selection_->setParent(this);route_selection_->hide();
    route_selection_->setToolTip("Seleccione una propuesta y pulse CALCULAR RUTA para generarla. Las propuestas ya calculadas se muestran al seleccionarlas.");
    connect(route_selection_,&QComboBox::currentIndexChanged,this,[this](int index){if(!applying_){view_->clear_search();search_trace_.reset();choose_route(index);}});
    calculate_=new QPushButton("CALCULAR RUTA");calculate_->setObjectName("calculateRoute");calculate_->setProperty("primary",true);side->addWidget(calculate_);
    clear_=new QPushButton("LIMPIAR RUTA Y PUNTOS");side->addWidget(clear_);
    connect(calculate_,&QPushButton::clicked,this,[this]{calculate();});connect(clear_,&QPushButton::clicked,this,&RouteWindow::clear_points);
    advanced_tabs_=new QTabWidget;advanced_tabs_->setObjectName("advancedGroups");advanced_dialog_->layout()->addWidget(advanced_tabs_);
    auto add_tab=[this](QWidget* content,const QString& title){auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(content);advanced_tabs_->addTab(scroll,title);};
    advanced_=new QGroupBox("Diseño de rasante e intervención");auto* advanced_layout=new QVBoxLayout(advanced_);auto* form=new QFormLayout;
    reset_=new QPushButton("Restablecer valores");reset_->setObjectName("resetAdvancedSettings");qobject_cast<QVBoxLayout*>(advanced_dialog_->layout())->insertWidget(1,reset_);
    auto* reset_hint=new QLabel("Restaura exploración preliminar a 20 km/h, radio solicitado 30 m, recta 35 m, corte y relleno 2 m, precisión, recursos y costos. Conserva terreno, A/B y pendiente máxima solicitada.");reset_hint->setWordWrap(true);qobject_cast<QVBoxLayout*>(advanced_dialog_->layout())->insertWidget(2,reset_hint);
    connect(reset_,&QPushButton::clicked,this,&RouteWindow::reset_advanced);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    field(form,"Corte máximo (m)","max_cut_m",2,0,10000);field(form,"Relleno máximo (m)","max_fill_m",2,0,10000);
    intervention_length_=new QDoubleSpinBox;intervention_length_->setObjectName("maxInterventionLength");intervention_length_->setRange(1,10000);intervention_length_->setDecimals(1);intervention_length_->setValue(100);intervention_length_->setKeyboardTracking(false);
    form->addRow("Longitud continua máxima de intervención (m)",intervention_length_);
    connect(intervention_length_,&QDoubleSpinBox::valueChanged,this,[this]{if(!applying_)clear_route();});
    field(form,"Ancho de cálculo (m)","road_width_m",8,0.01,1000);advanced_layout->addLayout(form);
    standards_hint_=new QLabel;standards_hint_->setObjectName("designStandardsHint");standards_hint_->setWordWrap(true);design_form->addRow(standards_hint_);
    auto* apply_criteria=new QPushButton("Aplicar criterios de diseño");apply_criteria->setObjectName("applyDesignStandards");design_form->addRow(apply_criteria);
    auto context_changed=[this]{if(!applying_){refresh_design_criteria();clear_route();}};
    connect(standards_mode_,&QComboBox::currentIndexChanged,this,[this]{if(!applying_){
        // These saved fields were derived by the previous profile. A deliberate
        // mode change starts from the selected profile's defaults; loading an
        // explicit custom configuration does not enter this branch.
        base_.erase("min_straight_same_direction_m");base_.erase("apply_dg_grade_guidance");refresh_design_criteria();clear_route();
    }});connect(design_terrain_,&QComboBox::currentIndexChanged,this,context_changed);connect(design_ice_risk_,&QCheckBox::toggled,this,context_changed);
    for(const auto* key:{"design_speed_kmh","design_superelevation_percent","design_altitude_m"})connect(fields_.at(key),&QDoubleSpinBox::valueChanged,this,context_changed);
    connect(apply_criteria,&QPushButton::clicked,this,[this]{refresh_design_criteria();clear_route();statusBar()->showMessage("Criterios aplicados a los parámetros del proyecto. Revise el resumen de Diseño.");});
    auto* pending=new QLabel("Una sola búsqueda incluye recorridos sobre la superficie y variantes con corte/relleno. El eje respeta la pendiente de diseño. La pendiente longitudinal del terreno no puede superar el doble; las excepciones y las intervenciones deben caber dentro de la longitud continua configurada. El ancho separa las vueltas de cada carretera; plataforma y estabilidad requieren diseño posterior.");pending->setWordWrap(true);advanced_layout->addWidget(pending);
    adaptive_=new QCheckBox("Adaptar resolución y recursos");adaptive_->setChecked(true);
    connect(adaptive_,&QCheckBox::toggled,this,[this]{if(!applying_)clear_route();});
    auto* precision=new QGroupBox("Precisión del cálculo");auto* pf=new QFormLayout(precision);
    pf->setRowWrapPolicy(QFormLayout::WrapLongRows);
    field(pf,"Paso XY objetivo (m)","xy_spacing_m",10,0.001,100000,3);
    field(pf,"Muestreo (m)","sample_spacing_m",2,0.001,1000,3);field(pf,"Tolerancia superficie (m)","surface_tolerance_m",0.01,0,100,6);
    field(pf,"Paso de direcciones (°)","heading_step_degrees",7.5,1,45,2);
    auto* resources=new QGroupBox("Recursos CPU");auto* rf=new QFormLayout(resources);rf->setRowWrapPolicy(QFormLayout::WrapLongRows);rf->addRow(adaptive_);
    for(const auto& [key,label,value]:std::vector<std::tuple<const char*,const char*,int>>{{"max_states","Nodos XY iniciales",200000},{"max_expansions","Expansiones iniciales",100000},{"max_edges","Aristas iniciales",1000000},{"max_samples","Muestras",1000000},{"max_ramp_candidates","Candidatos por variante",64}}) {
        auto* input=new QSpinBox;input->setRange(std::string(key)=="max_states"?16:(std::string(key)=="max_samples"?2:1),10000000);input->setValue(value);
        input->setKeyboardTracking(false);input->setObjectName(key);budgets_[key]=input;
        bounded_row(rf,QString::fromUtf8(label),input,input->minimum(),input->maximum(),value);
        connect(input,&QSpinBox::valueChanged,this,[this,key]{if(!applying_){changed_.insert(key);clear_route();}});
    }
    precision_hint_=new QLabel;precision_hint_->setObjectName("samplingBudgetHint");precision_hint_->setWordWrap(true);pf->addRow(precision_hint_);
    add_tab(advanced_,"Diseño");add_tab(precision,"Precisión");add_tab(resources,"Recursos");z_domain_=new QGroupBox("Fijar dominio Z manualmente");z_domain_->setCheckable(true);z_domain_->setChecked(false);
    auto* df=new QFormLayout(z_domain_);field(df,"Z mínima (m)","z_min",0,-1e9,1e9,6);field(df,"Z máxima (m)","z_max",0,-1e9,1e9,6);z_domain_->setParent(advanced_);z_domain_->hide();
    auto* cost_box=new QGroupBox("Estimación de costos");auto* cf=new QFormLayout(cost_box);cf->setRowWrapPolicy(QFormLayout::WrapLongRows);cost_profile_=new QComboBox;
    cost_profile_->addItem("Longitud (sin precios)","length_only");cost_profile_->addItem("Costos configurados (CU)","custom");cost_profile_->addItem("Ejemplo sintético (CU ficticias)","synthetic_example");cf->addRow(cost_profile_);
    for(const auto& [key,label]:cost_fields)field(cf,QString::fromUtf8(label),key,std::string(key)=="road_per_m2"?1:0,0,std::string(key)=="rock_fraction"?1:1e9,6);
    add_tab(cost_box,"Costos");finish_dialog(advanced_dialog_);
    connect(z_domain_,&QGroupBox::toggled,this,[this]{if(!applying_)clear_route();});
    connect(cost_profile_,&QComboBox::currentIndexChanged,this,[this]{if(!applying_)clear_route();update_controls();});
    local_panel_=new QGroupBox("Puntos intermedios");local_panel_->setObjectName("intermediateSettings");auto* il=new QGridLayout(local_panel_);
    for(int i=0;i<2;++i){local_pick_[i]=new QPushButton(i?"MARCAR P2":"MARCAR P1");local_pick_[i]->setObjectName(i?"pickLocalEnd":"pickLocalStart");il->addWidget(local_pick_[i],0,i);connect(local_pick_[i],&QPushButton::clicked,this,[this,i]{view_->select_point(i+2);statusBar()->showMessage(i?"Seleccione P2 sobre la carretera.":"Seleccione P1 sobre la carretera.");});}
    local_optimize_=new QPushButton("OPTIMIZAR");local_optimize_->setObjectName("optimizeSelectedSection");il->addWidget(local_optimize_,1,0);connect(local_optimize_,&QPushButton::clicked,this,&RouteWindow::optimize_segment);
    local_clear_=new QPushButton("LIMPIAR");local_clear_->setObjectName("clearIntermediatePoints");il->addWidget(local_clear_,1,1);connect(local_clear_,&QPushButton::clicked,this,[this]{local_points_={};view_->set_route_points({},{});update_controls();});side->addWidget(local_panel_);local_panel_->hide();
    auto* rb=new QGroupBox("Resultados");auto* rl=new QVBoxLayout(rb);result_status_=new QLabel("Sin ruta calculada");result_status_->setWordWrap(true);metrics_=new QLabel("—");metrics_->setWordWrap(true);
    rl->addWidget(result_status_);rl->addWidget(metrics_);side->addWidget(rb);side->addStretch();
    auto* scene=new QWidget;auto* sl=new QVBoxLayout(scene);sl->setContentsMargins(0,8,8,8);layout->addWidget(scene,1);
    auto* views_menu=menuBar()->addMenu("&Vistas");views_menu->setObjectName("viewsMenu");
    for(const auto& [label,name]:std::vector<std::pair<const char*,const char*>>{{"ISO","ISO"},{"Top","TOP"},{"Bot","BOT"},{"Front","FRONT"},{"Back","BACK"},{"Left","LEFT"},{"Right","RIGHT"}}) {
        auto* action=views_menu->addAction(QString::fromUtf8(label));action->setObjectName(QString("view%1").arg(name));
        connect(action,&QAction::triggered,this,[this,name]{view_->camera_view(name);});
    }
    views_menu->addSeparator();
    for(const auto& [label,factor]:std::vector<std::pair<QString,double>>{{"Acercar",1.25},{"Alejar",0.8}}) {
        auto* action=views_menu->addAction(label);action->setObjectName(factor>1?"zoomIn":"zoomOut");
        connect(action,&QAction::triggered,this,[this,factor]{view_->zoom(factor);});
    }
    views_menu->addSeparator();auto* show_search=views_menu->addAction("Mostrar exploración");show_search->setObjectName("showSearchPaths");show_search->setCheckable(true);show_search->setChecked(true);
    connect(show_search,&QAction::toggled,this,[this](bool show){view_->show_search(show);});
    auto* help_menu=menuBar()->addMenu("A&yuda");help_menu->setObjectName("helpMenu");
    details_action_=help_menu->addAction("Detalles del resultado…");details_action_->setObjectName("resultDetails");connect(details_action_,&QAction::triggered,this,&RouteWindow::show_diagnostics);
    auto* about=help_menu->addAction("Acerca de RouteTec…");about->setObjectName("aboutRouteTec");connect(about,&QAction::triggered,this,[this]{QMessageBox::about(this,"RouteTec","RouteTec 1.0\nPlanificación preliminar de carreteras sobre topografía 3D.\n\nComprueba pendiente, radio, cobertura y límites de corte y relleno del eje. La plataforma, estabilidad, peralte y transiciones requieren diseño de ingeniería.");});
    view_=new TerrainView;sl->addWidget(view_,1);message_=new QLabel(this);message_->hide();
    view_->point_picked=[this](int which,Vec3 p){if(!busy_)set_point(which,p);};
    process_panel_=new QWidget;process_panel_->setObjectName("processPanel");
    auto* process_row=new QHBoxLayout(process_panel_);process_row->setContentsMargins(0,0,0,0);
    process_summary_=new QLabel;process_summary_->setObjectName("processSummary");process_row->addWidget(process_summary_,1);
    progress_=new QProgressBar;progress_->setRange(0,100);progress_->setFormat("%p%");progress_->setAlignment(Qt::AlignCenter);progress_->setFixedWidth(125);process_row->addWidget(progress_);
    process_restore_=new QPushButton("VER PROGRESO");process_restore_->setObjectName("restoreProcess");process_row->addWidget(process_restore_);
    cancel_=new QPushButton("CANCELAR");cancel_->setObjectName("cancelProcessPanel");process_row->addWidget(cancel_);
    process_panel_->hide();statusBar()->addPermanentWidget(process_panel_,1);
    process_dialog_=new ProcessDialog(this);process_dialog_->setObjectName("processDialog");process_dialog_->setWindowTitle("Progreso de la operación");
    process_dialog_->setModal(false);process_dialog_->resize(540,300);process_dialog_->setMinimumWidth(440);
    auto* process_body=new QVBoxLayout(process_dialog_);process_body->setSpacing(14);
    process_remaining_=new QLabel;process_remaining_->setObjectName("processRemaining");process_remaining_->setWordWrap(true);
    process_remaining_->setStyleSheet("QLabel#processRemaining{font-size:20px;font-weight:bold;color:#8ce0ce;}");process_body->addWidget(process_remaining_);
    process_activity_=new QLabel;process_activity_->setObjectName("processActivity");process_activity_->setWordWrap(true);process_body->addWidget(process_activity_);
    process_percent_=new QLabel;process_percent_->setObjectName("processPercent");process_body->addWidget(process_percent_);
    floating_progress_=new QProgressBar;floating_progress_->setObjectName("processProgress");floating_progress_->setRange(0,100);floating_progress_->setFormat("%p%");floating_progress_->setAlignment(Qt::AlignCenter);process_body->addWidget(floating_progress_);
    process_time_=new QLabel;process_time_->setObjectName("processTime");process_body->addWidget(process_time_);
    auto* progress_note=new QLabel("El tiempo restante es aproximado y se actualiza con el avance. Puede variar según la etapa de carga o búsqueda.");progress_note->setWordWrap(true);process_body->addWidget(progress_note);
    auto* process_buttons=new QHBoxLayout;process_buttons->addStretch();
    auto* minimize=new QPushButton("MINIMIZAR");minimize->setObjectName("minimizeProcess");process_buttons->addWidget(minimize);
    process_cancel_=new QPushButton("CANCELAR");process_cancel_->setObjectName("cancelProcessDialog");process_buttons->addWidget(process_cancel_);process_body->addLayout(process_buttons);
    process_dialog_->minimize=[this]{minimize_process();};
    connect(minimize,&QPushButton::clicked,this,&RouteWindow::minimize_process);
    connect(process_restore_,&QPushButton::clicked,this,&RouteWindow::show_process);
    connect(cancel_,&QPushButton::clicked,this,&RouteWindow::cancel_process);
    connect(process_cancel_,&QPushButton::clicked,this,&RouteWindow::cancel_process);
    connect(&timer_,&QTimer::timeout,this,&RouteWindow::update_process);
    setStyleSheet("QMenuBar{background:#172731;} QMenuBar::item:selected,QMenu::item:selected{background:#2e4d61;} QMenu{background:#172731;border:1px solid #3b5667;} QMenu::item{padding:8px 28px;} QMenu::item:disabled{color:#63747e;} QTabBar::tab{padding:10px 18px;background:#203442;} QTabBar::tab:selected{background:#1b6857;} QMainWindow,QWidget{background:#101a22;color:#dce5eb;font-size:12px;} QGroupBox{border:1px solid #304451;border-radius:7px;margin-top:10px;padding:12px 7px 6px;} QGroupBox::title{subcontrol-origin:margin;left:10px;color:#9fb5c4;} QPushButton,QToolButton{background:#203442;border:1px solid #3b5667;border-radius:5px;padding:8px;} QPushButton:hover{background:#2e4d61;} QPushButton:disabled{color:#63747e;background:#17242d;} QPushButton[primary=true]{background:#1b6857;font-weight:bold;} QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#0b141b;border:1px solid #3a5262;border-radius:4px;padding:5px;min-width:45px;} QScrollArea{border:0;} QStatusBar{background:#0b141b;}");
    view_->route_point_picked=[this](int point,std::size_t index){set_local_point(point,index);};
    refresh_design_criteria();update_controls();statusBar()->showMessage("Listo · CPU");
}
void RouteWindow::busy(bool value,const QString& message) {
    continuous_search_=false;if(value)search_round_=0;progress_->setRange(0,100);floating_progress_->setRange(0,100);cancel_->setText("CANCELAR");process_cancel_->setText("Cancelar cálculo");
    busy_=value;sidebar_->setEnabled(!value);design_dialog_->setEnabled(!value);advanced_dialog_->setEnabled(!value);
    view_->select_point(-1);
    if(value){job_progress_=std::make_shared<std::atomic<int>>(0);process_message_=message;elapsed_.start();cancel_->setEnabled(true);process_cancel_->setEnabled(true);update_process();show_process();timer_.start(100);}
    else {timer_.stop();process_dialog_->hide();process_panel_->hide();}
    statusBar()->showMessage(value?QString{}:message);update_controls();
}
void RouteWindow::show_process() {
    if(!busy_)return;
    process_panel_->hide();statusBar()->clearMessage();
    process_dialog_->setWindowState(Qt::WindowNoState);
    process_dialog_->show();process_dialog_->raise();process_dialog_->activateWindow();
}
void RouteWindow::minimize_process() {
    if(!busy_)return;
    process_dialog_->hide();process_panel_->show();statusBar()->clearMessage();update_process();
}
void RouteWindow::update_process() {
    if(!busy_)return;
    if(search_trace_){auto batch=search_trace_->take();if(batch.footprint_changed||batch.verified_footprint_changed||!batch.partial.empty()||!batch.complete.empty())view_->append_search(std::move(batch));}
    const auto activity=stop_.stop_requested()?QString("Deteniendo; se conservan el corredor y los tramos verificados…"):process_message_;
    if(continuous_search_){
        progress_->setRange(0,0);floating_progress_->setRange(0,0);
        process_remaining_->setText("Búsqueda continua hasta pulsar Detener búsqueda");
        process_percent_->setText(QString("Rondas adicionales: %1").arg(search_round_));
        process_activity_->setText(activity);process_time_->setText(QString("Tiempo transcurrido: %1 s · CPU").arg(elapsed_.elapsed()/1000));
        process_summary_->setText(QString("Búsqueda continua · ronda %1 · %2 s · %3").arg(search_round_).arg(elapsed_.elapsed()/1000).arg(activity));
        return;
    }
    const auto time=QString("Tiempo transcurrido: %1 s · CPU").arg(elapsed_.elapsed()/1000);
    const int percent=std::clamp(job_progress_->load(),0,100);
    const auto remaining=stop_.stop_requested()?QString("Cancelando…"):remaining_time(elapsed_.elapsed(),percent);
    process_remaining_->setText("Tiempo restante estimado: "+remaining);
    progress_->setValue(percent);floating_progress_->setValue(percent);
    process_percent_->setText(QString("Avance estimado: %1 %").arg(percent));
    process_activity_->setText(activity);process_time_->setText(time);
    process_summary_->setText(QString("Restante: %1 · %2 % · %3").arg(remaining).arg(percent).arg(activity));
    process_summary_->setToolTip(time);
}
void RouteWindow::cancel_process() {
    if(!busy_)return;
    stop_.request_stop();cancel_->setEnabled(false);process_cancel_->setEnabled(false);update_process();
}
void RouteWindow::update_design_limits() {
    const bool extended=extend_limits_->isChecked();
    for(const auto* key:{"max_grade_percent","min_radius_m","min_straight_m"}) {
        auto* input=fields_.at(key);QSignalBlocker blocker(input);
        const bool grade=std::string(key)=="max_grade_percent";
        const double minimum=grade||extended?0:(std::string(key)=="min_radius_m"?30:35);
        input->setRange(minimum,grade?(extended?100:12):std::numeric_limits<double>::max());
        const auto range=grade?QString("%1–%2").arg(concise(minimum),concise(input->maximum())):QString("Desde %1 m · sin máximo de uso").arg(concise(minimum));
        const auto help=QString("Permitido: %1\nInicial: %2").arg(range,concise(input->property("initialValue").toDouble()));
        input->setProperty("rangeHelp",help);input->setToolTip(help);
        if(auto* hint=findChild<QLabel*>(input->objectName()+"Limits"))hint->setText(help);
    }
}
RoadStandardInput RouteWindow::design_context() const {
    RoadStandardInput input;input.design_speed_kmh=fields_.at("design_speed_kmh")->value();
    input.terrain=static_cast<DesignTerrain>(design_terrain_->currentIndex());
    input.altitude_m=fields_.at("design_altitude_m")->value();if(input.altitude_m==0&&terrain_)input.altitude_m=terrain_->bounds()[1].z;
    input.superelevation_percent=fields_.at("design_superelevation_percent")->value();input.ice_risk=design_ice_risk_->isChecked();return input;
}
void RouteWindow::refresh_design_criteria(bool apply) {
    if(!standards_hint_||!fields_.contains("road_width_m"))return;
    try{
        const auto input=design_context();const auto advice=recommend_road_standards(input);const auto mode=standards_mode_->currentData().toString();
        QString text=QString("Referencia a %1 km/h y %2 m de altitud: radio calculado %3 m; recta S (curvas de sentido opuesto) %4 m; recta O (otros casos de la tabla) %5 m. Las curvas contiguas del mismo sentido no necesitan una recta intermedia.")
            .arg(concise(input.design_speed_kmh),concise(input.altitude_m),concise(advice.min_radius_m),concise(advice.min_straight_s_m),concise(advice.min_straight_o_m));
        if(!advice.tabulated_speed)text+=QString("\nEsta combinación de velocidad y orografía no tiene fila de tercera clase en DG2018. Se conservan %1 km/h para explorar; la velocidad no cambia automáticamente a 30 km/h.").arg(concise(input.design_speed_kmh));
        if(mode=="custom")text+="\nPersonalizado: se conservan los parámetros solicitados; estas referencias no se aplican automáticamente.";
        else if(mode=="preliminary")text+="\nExploración preliminar: aplica radio calculado y rectas de referencia. Pendiente y ancho siguen siendo los solicitados.";
        else text+="\nDG2018 tercera clase: aplica también pendiente y ancho de la fila disponible. Sin fila tabulada, seleccione otra combinación o Exploración preliminar.";
        text+="\nEl perfil no certifica una carretera: faltan comprobaciones de vehículo, peralte real, visibilidad, plataforma y estabilidad.";
        for(const auto& diagnostic:advice.diagnostics)text+="\n"+QString::fromStdString(diagnostic);
        if(!advice.source_references.empty()){text+="\nFuentes: ";for(std::size_t i=0;i<advice.source_references.size();++i){if(i)text+="; ";text+=QString::fromStdString(advice.source_references[i]);}}
        standards_hint_->setText(text);standards_hint_->setStyleSheet(!advice.tabulated_speed?"color:#ffca70;":"color:#9fb5c4;");
        if(!apply||(mode=="dg2018"&&!advice.tabulated_speed))return;
        auto effective=requested_design_;
        if(mode!="custom"){
            effective["min_radius_m"]=std::max(effective.at("min_radius_m"),advice.min_radius_m);
            effective["min_straight_m"]=std::max(effective.at("min_straight_m"),advice.min_straight_s_m);
            if(mode=="dg2018"){
                if(advice.max_grade_percent)effective["max_grade_percent"]=std::min(effective.at("max_grade_percent"),*advice.max_grade_percent);
                if(advice.base_road_width_m)effective["road_width_m"]=std::max(effective.at("road_width_m"),*advice.base_road_width_m);
            }
        }
        if(effective.at("max_grade_percent")>12||effective.at("min_radius_m")<30||effective.at("min_straight_m")<35){QSignalBlocker blocker(extend_limits_);extend_limits_->setChecked(true);update_design_limits();}
        for(const auto& [key,value]:effective){auto* widget=fields_.at(key);QSignalBlocker blocker(widget);widget->setValue(value);widget->setProperty("requestedDesignValue",requested_design_.at(key));}
    }catch(const std::exception& e){standards_hint_->setText("Revise el contexto de diseño: "+QString::fromUtf8(e.what()));standards_hint_->setStyleSheet("color:#ffca70;");}
}
void RouteWindow::update_controls() {
    if(!cost_profile_||!calculate_||!csv_)return;
    bool loaded=bool(terrain_),ready=loaded&&points_[0]&&points_[1]&&!busy_;
    const auto issue=configuration_issue();
    precision_hint_->setText(issue.isEmpty()?"Un muestreo menor usa más muestras. El presupuesto incluye cruces de caras TIN y todos los tramos del recorrido; un rodeo puede necesitar más recursos. Inicial: 2 m y 1000000 muestras.":issue);
    precision_hint_->setStyleSheet(issue.isEmpty()?"color:#9fb5c4;":"color:#ffca70;");
    for(const auto& [key,input]:fields_) {
        auto help=input->property("rangeHelp").toString();
        if(requested_design_.contains(key))help+=QString("\nSolicitado: %1\nValor efectivo: %2").arg(concise(requested_design_.at(key)),concise(input->value()));
        else if(base_.contains(key)&&!changed_.contains(key))help+="\nValor efectivo: "+QString::fromStdString(base_.at(key));
        input->setToolTip(help);
    }
    for(int i=0;i<2;++i){select_[i]->setEnabled(loaded&&!busy_);register_[i]->setEnabled(loaded&&!busy_);for(auto* input:coordinates_[i])input->setEnabled(loaded&&!busy_);}
    calculate_->setEnabled(ready&&issue.isEmpty());calculate_->setToolTip(issue);save_->setEnabled(ready||(continuous_search_&&result_.has_value()));save_as_->setEnabled(ready||(continuous_search_&&result_.has_value()));
    for(auto* action:{open_action_,design_action_,advanced_action_})action->setEnabled(!busy_);clear_->setEnabled(loaded);csv_->setEnabled(result_.has_value()&&!busy_);dxf_->setEnabled(result_.has_value()&&!busy_);
    route_selection_->setEnabled(!busy_);
    for(const auto& [key,label]:cost_fields)fields_.at(key)->setEnabled(cost_profile_->currentData().toString()=="custom");
    fields_.at("min_straight_m")->setEnabled(!busy_);fields_.at("max_turn_degrees")->setEnabled(!continuous_->isChecked());
    const bool local_ready=local_tools_enabled_&&result_&&result_->estimate&&result_->status==SearchStatus::found&&result_config_&&result_config_->parameters.continuous_curves&&!busy_;
    if(local_panel_){
        local_action_->setEnabled(!busy_);local_panel_->setVisible(local_tools_enabled_);
        local_pick_[0]->setEnabled(local_ready);local_pick_[1]->setEnabled(local_ready);
        local_optimize_->setEnabled(local_ready&&local_points_[0]&&local_points_[1]&&*local_points_[0]<*local_points_[1]);local_clear_->setEnabled(local_ready&&(local_points_[0]||local_points_[1]));
        QString summary;
        for(int i=0;i<2;++i)if(result_&&local_points_[i]&&*local_points_[i]<result_->route.size()){
            auto index=*local_points_[i];auto point=result_->route[index];double station=0;for(std::size_t j=1;j<=index;++j)station+=std::hypot(result_->route[j].x-result_->route[j-1].x,result_->route[j].y-result_->route[j-1].y);
            summary+=QString("P%1 · punto %2 · progresiva %3 m\nX %4 · Y %5 · Z %6\n").arg(i+1).arg(index+1).arg(format(station),format(point.x),format(point.y),format(point.z));
        }
        if(local_parameters_)summary+=QString("Local: pendiente %1 % · radio %2 m · recta %3 m").arg(concise(local_parameters_->max_grade_percent),concise(local_parameters_->min_radius_m),concise(local_parameters_->min_straight_m));
        local_panel_->setToolTip(summary.isEmpty()?"Marque P1 y P2 sobre una carretera calculada. Parámetros en Configuración > Puntos intermedios.":summary);
    }
    details_action_->setEnabled((!busy_||continuous_search_)&&!option_diagnostics_[route_selection_->currentIndex()].isEmpty());
    calculate_->setText("BUSCAR RUTAS POSIBLES");
}
QString RouteWindow::configuration_issue() const {
    const auto values=settings();
    try{if(standards_mode_->currentData().toString()=="dg2018"&&!recommend_road_standards(design_context()).tabulated_speed)return "DG2018 tercera clase no tiene una fila para esta velocidad y orografía. Seleccione otra combinación o Exploración preliminar; los 20 km/h no se sustituyen automáticamente.";}catch(const std::exception& e){return "Revise el contexto de diseño: "+QString::fromUtf8(e.what());}
    if(std::stod(values.at("min_radius_m"))<=0&&(values.at("continuous_curves")=="1"||std::stod(values.at("min_straight_m"))>0||std::stod(values.at("max_turn_degrees"))<180))
        return "Ingrese un radio mínimo mayor que cero para buscar curvas contiguas y espirales.";
    const double budget=std::stod(values.at("max_samples"));
    if(budget<4)return "Muestras insuficientes: se necesitan al menos 4 incluso para un tramo sin cruces interiores. Aumente Muestras o restablezca valores.";
    if(!points_[0]||!points_[1])return {};
    const double distance=std::hypot(points_[1]->x-points_[0]->x,points_[1]->y-points_[0]->y);
    const double spacing=std::stod(values.at("sample_spacing_m"));
    const double minimum_samples=std::max(1.0,std::ceil(distance/spacing))+3;
    if(minimum_samples<=budget)return {};
    const double minimum_spacing=std::max(0.001,std::ceil(distance/(budget-3)*1000)/1000);
    return QString("Muestreo incompatible con el presupuesto: A/B requiere al menos %1 muestras y el límite es %2. Para la distancia directa, use Muestreo ≥ %3 m; el recorrido real y la TIN pueden requerir más. Aumente Muestreo o Muestras, o pulse Restablecer valores.")
        .arg(concise(minimum_samples),concise(budget),concise(minimum_spacing));
}
void RouteWindow::reset_advanced() {
    {QSignalBlocker block(intervention_length_);intervention_length_->setValue(100);}
    if(busy_)return;
    const auto grade=settings().at("max_grade_percent");
    applying_=true;base_=routetec_interface_defaults();base_["max_grade_percent"]=grade;changed_.clear();
    for(const auto& [key,input]:fields_)if(key!="max_grade_percent")input->setValue(input->property("initialValue").toDouble());
    fields_.at("max_grade_percent")->setValue(std::stod(grade));for(auto& [key,value]:requested_design_)value=fields_.at(key)->value();
    standards_mode_->setCurrentIndex(0);design_terrain_->setCurrentIndex(3);design_ice_risk_->setChecked(true);
    for(const auto& [key,input]:budgets_)input->setValue(input->property("initialValue").toInt());
    cost_profile_->setCurrentIndex(0);z_domain_->setChecked(false);
    adaptive_->setChecked(true);applying_=false;
    applying_=true;continuous_->setChecked(true);applying_=false;
    refresh_design_criteria();clear_route();statusBar()->showMessage("Valores avanzados y contexto de exploración restablecidos. Pulse BUSCAR RUTAS POSIBLES.");
}
void RouteWindow::error(const QString& m) {
    QString message=m;
    message.replace("Sampling budget exceeded; choose an explicit coarser spacing",
        "Se agotó el presupuesto de muestras. Aumente Muestreo (m) o Muestras en Configuración avanzada, o pulse Restablecer valores (2 m y 1000000 muestras). Un paso menor consume más recursos.");
    message.replace("Face-boundary sampling budget exceeded",
        "Se agotó el presupuesto al comprobar cruces de caras TIN. Aumente Muestras en Configuración avanzada. Restablecer valores recupera 1000000 muestras; una TIN muy densa puede necesitar más.");
    if(testing_){std::cerr<<message.toStdString()<<'\n';QCoreApplication::exit(2);return;}
    statusBar()->showMessage("Operación detenida");
    if(message.startsWith("La ruta candidata no superó la validación.")){
        QDialog dialog(this);dialog.setWindowTitle("RouteTec · Restricciones incumplidas");dialog.resize(850,500);
        auto* layout=new QVBoxLayout(&dialog);
        auto* details=new QPlainTextEdit(&dialog);details->setObjectName("validationDiagnostics");
        details->setReadOnly(true);details->setPlainText(message);layout->addWidget(details);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
        buttons->button(QDialogButtonBox::Close)->setText("Cerrar");
        auto* copy=buttons->addButton("Copiar diagnóstico",QDialogButtonBox::ActionRole);
        connect(copy,&QPushButton::clicked,details,[details]{details->selectAll();details->copy();});
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);dialog.exec();return;
    }
    QMessageBox::warning(this,"RouteTec",message);
}
void RouteWindow::clear_route() {
    design_reviews_={};if(view_)view_->set_design_review({});
    search_trace_.reset();if(view_)view_->clear_search();
    local_points_={};result_.reset();result_config_.reset();alternatives_.reset();option_feedback_={};option_diagnostics_={};option_configs_={};option_messages_={};if(view_)view_->set_route({});
    if(result_status_)result_status_->setText("Sin ruta calculada");if(metrics_)metrics_->setText("—");update_controls();
}
void RouteWindow::clear_points() {
    points_={};for(auto& inputs:coordinates_)for(auto* input:inputs)input->clear();view_->select_point(-1);view_->set_points({},{});clear_route();
}
void RouteWindow::set_point(int which,Vec3 p) {
    points_[which]=p;double values[]{p.x,p.y,p.z};for(int j=0;j<3;++j){coordinates_[which][j]->setText(QString::fromStdString(number(values[j])));coordinates_[which][j]->setCursorPosition(0);}
    view_->select_point(-1);view_->set_points(points_[0],points_[1]);clear_route();statusBar()->showMessage(which?"Destino registrado":"Origen registrado");
}
void RouteWindow::register_point(int which) {
    double values[3];for(int j=0;j<3;++j) {
        auto text=coordinates_[which][j]->text().trimmed();std::istringstream input(text.toStdString());std::string extra;bool ok=bool(input>>values[j])&&!(input>>extra);
        if(text.isEmpty()||!ok||!std::isfinite(values[j])){coordinates_[which][j]->setFocus();error("Ingrese X, Y y Z como números finitos; use punto decimal y sin separadores de miles.");return;}
    }
    set_point(which,{values[0],values[1],values[2]});
}
Settings RouteWindow::settings() const {
    auto values=base_;values["terrain_path"]=terrain_path_.toUtf8().toStdString();values["earthwork_mode"]=route_selection_->currentIndex()==1?"1":"0";values["cost_profile"]=cost_profile_->currentData().toString().toStdString();
    values["adaptive_resolution"]=adaptive_->isChecked()?"1":"0";values["propose_earthwork"]="0";values["additional_grade_degrees"]="0";
    values["continuous_curves"]="1";
    for(const auto& [key,input]:fields_) {if((key=="z_min"||key=="z_max")&&!z_domain_->isChecked()){values.erase(key);continue;}if(!values.contains(key)||changed_.contains(key))values[key]=number(input->value());}
    values["standards_mode"]=standards_mode_->currentData().toString().toStdString();values["design_terrain"]=design_terrain_->currentData().toString().toStdString();values["design_ice_risk"]=design_ice_risk_->isChecked()?"1":"0";
    for(const auto* key:{"design_speed_kmh","design_superelevation_percent","design_altitude_m"})values[key]=number(fields_.at(key)->value());
    // Keep the requested values distinct from their displayed effective limits,
    // so reducing the design speed can reduce an automatically applied limit.
    for(const auto& [key,value]:requested_design_)values[key]=number(value);
    if(values["cost_profile"]=="length_only") {for(const auto& [key,label]:cost_fields)values[key]=std::string(key)=="road_per_m2"?"1":"0";}
    values["reusable_cut_fraction"]="0";for(const auto& [key,input]:budgets_)if(!values.contains(key)||changed_.contains(key))values[key]=std::to_string(input->value());
    for(int i=0;i<2;++i)if(points_[i]) {auto p=*points_[i];std::string prefix=i?"finish_":"start_";values[prefix+"x"]=number(p.x);values[prefix+"y"]=number(p.y);values[prefix+"z"]=number(p.z);}
    return values;
}
Configuration RouteWindow::configuration() const {
    if(!terrain_||!points_[0]||!points_[1])throw std::invalid_argument("Load terrain and register A/B first");
    return resolve_configuration(*terrain_,terrain_path_.toUtf8().toStdString(),settings());
}
void RouteWindow::apply_settings(const Settings& values) {
    auto current=values;current["continuous_curves"]="1";
    if(!current.contains("min_radius_m")||std::stod(current.at("min_radius_m"))==0)current["min_radius_m"]=routetec_interface_defaults().at("min_radius_m");
    auto config=resolve_configuration(*terrain_,terrain_path_.toUtf8().toStdString(),current);std::ostringstream output;write_configuration(output,config);std::istringstream input(output.str());base_=read_settings(input);applying_=true;
    extend_limits_->setChecked(config.parameters.max_grade_percent>12||config.parameters.min_radius_m<30||config.parameters.min_straight_m<35);update_design_limits();
    for(const auto& [key,widget]:fields_)widget->setValue(std::stod(base_.at(key)));for(const auto& [key,widget]:budgets_)widget->setValue(std::stoi(base_.at(key)));
    for(auto& [key,value]:requested_design_)value=current.contains(key)?std::stod(current.at(key)):fields_.at(key)->value();
    standards_mode_->setCurrentIndex(standards_mode_->findData(QString::fromStdString(base_.at("standards_mode"))));design_terrain_->setCurrentIndex(design_terrain_->findData(QString::fromStdString(base_.at("design_terrain"))));design_ice_risk_->setChecked(base_.at("design_ice_risk")=="1");
    route_selection_->setCurrentIndex(config.parameters.mode==DesignMode::earthwork?1:0);cost_profile_->setCurrentIndex(cost_profile_->findData(QString::fromStdString(config.cost_profile)));z_domain_->setChecked(values.contains("z_min")||values.contains("z_max"));
    adaptive_->setChecked(config.road_search.adaptive_resolution);
    continuous_->setChecked(config.parameters.continuous_curves);
    changed_.clear();applying_=false;refresh_design_criteria();set_point(0,config.start);set_point(1,config.finish);update_controls();
}
void RouteWindow::load(QString path,std::optional<ViewerProject> project) {
    bool migrated=project&&!(project->design_review&&project->design_review->road_candidates)&&project->route_info.strategy!="surface_reference"&&(!project->settings.contains("continuous_curves")||project->settings.at("continuous_curves")!="1");
    if(migrated) {
        project->settings["continuous_curves"]="1";
        if(!project->settings.contains("min_radius_m")||std::stod(project->settings.at("min_radius_m"))==0)project->settings["min_radius_m"]=routetec_interface_defaults().at("min_radius_m");
        project->route.clear();project->route_info=SearchResult{};
    }
    if(busy_)return;clear_route();stop_=std::stop_source{};auto token=stop_.get_token();auto job=++generation_;busy(true,"Leyendo topografía…");auto* watcher=new QFutureWatcher<LoadResult>(this);
    connect(watcher,&QFutureWatcher<LoadResult>::finished,this,[this,watcher,path,project,job,token,migrated] {
        auto loaded=watcher->result();watcher->deleteLater();if(job!=generation_)return;update_process();busy(false,"Listo · CPU · 100 %");
        if(token.stop_requested()){statusBar()->showMessage("Carga cancelada; no se aplicó el terreno nuevo");return;}if(!loaded.error.isEmpty()){error(loaded.error);return;}
        terrain_=loaded.terrain;terrain_path_=QFileInfo(path).absoluteFilePath();clear_points();view_->set_terrain(terrain_);if(!project)project_path_.clear();
        file_name_->setText(QString("%1\n%2 triángulos · %3 caras degeneradas omitidas").arg(QFileInfo(path).fileName()).arg(terrain_->faces().size()).arg(loaded.report.degenerate_faces_skipped));
        message_->setText("TIN original · cálculo en doble precisión · CPU. Seleccione o registre A/B.");if(loaded.report.degenerate_faces_skipped)message_->setText(message_->text()+" Se omitieron caras degeneradas en XY; no se rellenaron huecos.");
        if(project)try {
            auto control_settings=project->settings;
            if(project->design_review){const auto& p=project->design_review->parameters;
                control_settings["max_grade_percent"]=number(p.max_grade_percent);control_settings["min_radius_m"]=number(p.min_radius_m);control_settings["min_straight_m"]=number(p.min_straight_m);
                control_settings["road_width_m"]=number(p.road_width_m);control_settings["max_turn_degrees"]=number(p.max_turn_degrees);control_settings["max_cut_m"]=number(p.max_cut_m);control_settings["max_fill_m"]=number(p.max_fill_m);
            }
            apply_settings(control_settings);
            if(loaded.design_review){const auto& saved=*loaded.design_review;design_reviews_[saved.road_candidates?0:saved.parameters.mode==DesignMode::earthwork?1:0]=saved;
                if(saved.road_candidates){QSignalBlocker block(intervention_length_);intervention_length_->setValue(saved.max_intervention_length_m);}
            }
            local_tools_enabled_=project->ui.intermediate_enabled;local_parameters_.reset();
            if(project->ui.local_design){local_parameters_=configuration().parameters;local_parameters_->max_grade_percent=project->ui.local_design->max_grade_percent;local_parameters_->min_radius_m=project->ui.local_design->min_radius_m;local_parameters_->min_straight_m=project->ui.local_design->min_straight_m;}
            update_controls();
            if(migrated)message_->setText("Proyecto anterior: espirales y curvas contiguas habilitadas. Recalcule la ruta con esta configuración.");
            if(loaded.restored) {
                RoadAlternatives recovered;
                if(loaded.restored->strategy=="earthwork_shortcuts") {
                    auto config=configuration();recovered.surface.route=loaded.restored->reference_route;recovered.surface.strategy="surface_adaptive";recovered.surface.status=SearchStatus::found;
                    recovered.surface.effective_xy_spacing_m=loaded.restored->effective_xy_spacing_m;recovered.surface.effective_heading_step_degrees=loaded.restored->effective_heading_step_degrees;
                    recovered.surface.estimate=estimate_cost(recovered.surface.route,*terrain_,road_parameters(config.parameters,config.road_search,false),config.costs);
                    recovered.earthwork=std::move(*loaded.restored);alternatives_=std::move(recovered);choose_route(1);
                } else {
                    const int index=loaded.design_review&&loaded.design_review->road_candidates?0:configuration().parameters.mode==DesignMode::earthwork?1:0;
                    if(index==1)recovered.earthwork=std::move(*loaded.restored);else recovered.surface=std::move(*loaded.restored);
                    alternatives_=std::move(recovered);option_configs_[index]=resolve_configuration(*terrain_,terrain_path_.toUtf8().toStdString(),project->settings);
                    const auto& restored=index==1?*alternatives_->earthwork:alternatives_->surface;option_diagnostics_[index]=QString::fromStdString(restored.adjustment_report);choose_route(index);
                }
            }
            if(loaded.design_review){const auto& saved=*loaded.design_review;if(saved.road_candidates){view_->set_exploration_grade_limit(saved.parameters.max_grade_percent);view_->set_candidate_review(saved.review,saved.parameters.surface_tolerance_m,saved.branches);}else view_->set_design_review(saved.review);}
            if(!loaded.review_error.isEmpty())error("Revisión de diseño descartada; se conserva el corredor gris.\n"+loaded.review_error);
            if(!loaded.route_error.isEmpty())error("Ruta guardada descartada; recalcule con los parámetros actuales.\n"+loaded.route_error);
            if(!project->warnings.isEmpty())QMessageBox::information(this,"Proyecto importado",project->warnings.join('\n'));
        }catch(const std::exception& e){clear_route();error(QString("Terreno cargado; revise el proyecto o recalcule la ruta.\n%1").arg(QString::fromUtf8(e.what())));}
        refresh_design_criteria();update_controls();
    });
    watcher->setFuture(QtConcurrent::run([path,project,token,progress=job_progress_] {
        LoadResult r;try{r.terrain=std::make_shared<Terrain>(load_terrain(native_path(path),&r.report,[progress](int value){progress->store(value*90/100);}));}catch(const std::exception& e){r.error=QString::fromUtf8(e.what());return r;}
        if(project&&!project->route.empty()&&!token.stop_requested())try {
            auto c=resolve_configuration(*r.terrain,path.toUtf8().toStdString(),project->settings);const auto& route=project->route;
            auto same=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
            if(!same(route.front(),c.start)||!same(route.back(),c.finish))throw std::invalid_argument("Stored route does not match project endpoints");
            SearchResult restored=project->route_info;restored.route=route;
            auto parameters=(restored.strategy.empty()||restored.strategy=="surface_reference")?c.parameters:road_parameters(c.parameters,c.road_search,restored.strategy=="earthwork_shortcuts");
            validate_interventions(restored,*r.terrain,c.parameters,c.road_search);
            restored.estimate=estimate_sectioned_route(restored,*r.terrain,parameters,c.costs);restored.status=SearchStatus::found;r.restored=std::move(restored);
        }catch(const std::exception& e){r.route_error=QString::fromUtf8(e.what());}
        if(project&&project->design_review&&!token.stop_requested())try{
            auto assessment=*project->design_review;
            auto c=resolve_configuration(*r.terrain,path.toUtf8().toStdString(),project->settings);
            if(assessment.road_candidates)assessment.parameters=effective_review_parameters(c,assessment.parameters);
            if(!validate_design_review(*r.terrain,assessment.review,assessment.parameters))throw std::invalid_argument("Los tramos guardados no cumplen los criterios efectivos del proyecto en la TIN cargada.");
            if(!project->route.empty()){
                const auto& route=project->route;std::vector<double> station(route.size());for(std::size_t i=1;i<route.size();++i)station[i]=station[i-1]+std::hypot(route[i].x-route[i-1].x,route[i].y-route[i-1].y);
                auto anchor=[&](double s){if(s>station.back()+1e-5)throw std::invalid_argument("Intervalo de revisión fuera del corredor guardado.");auto found=std::lower_bound(station.begin(),station.end(),s);if(found==station.begin())return route.front();if(found==station.end())return route.back();auto i=static_cast<std::size_t>(found-station.begin());double t=(s-station[i-1])/(station[i]-station[i-1]);return Vec3{std::lerp(route[i-1].x,route[i].x,t),std::lerp(route[i-1].y,route[i].y,t),std::lerp(route[i-1].z,route[i].z,t)};};
                auto same=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y)<1e-5&&std::abs(a.z-b.z)<1e-5;};
                for(const auto& section:assessment.review.sections)if(!same(anchor(section.first_station_m),section.candidate.route.front())||!same(anchor(section.last_station_m),section.candidate.route.back()))throw std::invalid_argument("Los extremos de un tramo no corresponden al corredor guardado.");
                for(const auto& gap:assessment.review.gaps)if(!same(anchor(gap.first_station_m),gap.start)||!same(anchor(gap.last_station_m),gap.end))throw std::invalid_argument("Los extremos pendientes no corresponden al corredor guardado.");
            }
            if(assessment.road_candidates){RoadCandidateLimits limits;limits.max_intervention_length_m=assessment.max_intervention_length_m;
                for(const auto& section:assessment.review.sections)if(!validate_road_candidate(*r.terrain,section.candidate,assessment.parameters,limits))throw std::invalid_argument("Un tramo guardado supera los límites de pendiente o de intervención continua.");
                for(auto& branch:assessment.branches){if(!validate_road_candidate(*r.terrain,branch,assessment.parameters,limits))throw std::invalid_argument("Una alternativa guardada no cumple los límites de carretera.");branch.estimate=estimate_cost(branch.route,*r.terrain,assessment.parameters,c.costs,branch.curves);}
                if(assessment.review.connected&&!validate_road_candidate(*r.terrain,*assessment.review.connected,assessment.parameters,limits))throw std::invalid_argument("La conexión guardada supera los límites de intervención continua.");
            }
            for(auto& section:assessment.review.sections)section.candidate.estimate=estimate_cost(section.candidate.route,*r.terrain,assessment.parameters,c.costs,section.candidate.curves);
            if(assessment.review.connected){auto& connected=*assessment.review.connected;
                auto same=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y)<1e-6&&std::abs(a.z-b.z)<1e-6;};
                if(!same(connected.route.front(),c.start)||!same(connected.route.back(),c.finish))throw std::invalid_argument("La conexión guardada no une los puntos A/B del proyecto.");
                connected.estimate=estimate_cost(connected.route,*r.terrain,assessment.parameters,c.costs,connected.curves);
            }
            r.design_review=std::move(assessment);
            if(project->route.empty()&&r.design_review->road_candidates){
                SearchResult guide;guide.strategy="candidate_guide";guide.status=SearchStatus::no_path;guide.route={c.start,c.finish};
                guide.adjustment_report="Búsqueda independiente sin corredor A/B verificado. La guía de anclaje no es una ruta y no se dibuja.";
                r.restored=std::move(guide);
            }
        }catch(const std::exception& e){r.review_error=QString::fromUtf8(e.what());}
        progress->store(99);return r;
    }));
}
void RouteWindow::open_project() {
    auto path=QFileDialog::getOpenFileName(this,"Abrir proyecto",project_path_,"Proyecto JSON (*.json)");if(path.isEmpty())return;
    open_project_file(path);
}
void RouteWindow::open_project_file(const QString& path) {
    try {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open project");if(file.size()>128*1024*1024)throw std::invalid_argument("Project exceeds 128 MiB limit");
        auto project=read_viewer_project(file.readAll(),path);
        if(!QFileInfo::exists(project.terrain_path)) {auto replacement=QFileDialog::getOpenFileName(this,"Localizar terreno del proyecto",QFileInfo(path).absolutePath(),"Topografía (*.dxf *.obj)");if(replacement.isEmpty())return;project.terrain_path=replacement;project.warnings<<"Se utilizó el terreno seleccionado; la ruta guardada se verificó de nuevo.";}
        project_path_=path;load(project.terrain_path,project);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}
void RouteWindow::calculate(bool adjust,bool retry) {
    if(busy_)return;
    try {
        const auto issue=configuration_issue();if(!issue.isEmpty()){error(issue);return;}
        auto config=configuration();config.road_search.first_feasible=true;const bool candidate_mode=!testing_||exercise_candidates_;
        const int index=candidate_mode?0:route_selection_->currentIndex();const bool keep_searching=!testing_||exercise_continuous_||exercise_candidates_;
        if(candidate_mode){QSignalBlocker blocker(route_selection_);route_selection_->setCurrentIndex(0);config.parameters.mode=DesignMode::earthwork;}
        const double max_intervention=intervention_length_->value();
        std::optional<SearchResult> cached_surface;
        std::optional<Configuration> cached_config;
        if(index==1&&alternatives_&&alternatives_->surface.status==SearchStatus::found&&option_configs_[0]){cached_surface=alternatives_->surface;cached_config=option_configs_[0];}
        view_->clear_search();view_->set_design_review({});design_reviews_[index].reset();view_->set_route({});if(cached_surface)view_->set_route(cached_surface->route,{}, {},false,config.parameters.surface_tolerance_m,true);search_trace_=std::make_shared<SearchTrace>();
        if(candidate_mode){
            view_->set_exploration_grade_limit(config.parameters.max_grade_percent);
            view_->set_candidate_review({},config.parameters.surface_tolerance_m);
            SearchResult guide;guide.route={config.start,config.finish};guide.status=SearchStatus::no_path;guide.strategy="candidate_guide";
            result_=guide;result_config_=config;alternatives_.emplace();alternatives_->surface=guide;
            option_configs_[0]=config;design_reviews_[0]=ViewerDesignReview{{},config.parameters,true,max_intervention,{}};
            result_status_->setText("Explorando recorridos grises · buscando alternativas");metrics_->setText("La búsqueda comienza desde A, B y otros puntos de la TIN.");
        }
        auto trace=search_trace_;config.road_search.trace=[trace,candidate_mode](std::span<const Vec3> path,bool){if(!candidate_mode)trace->record(path,false);};
        message_->setText(candidate_mode?"Gris: exploración sobre la TIN. Excesos hasta 2× la pendiente máxima: rojo si sube, verde si baja, en el sentido del trazo. Celeste: alternativas con pendiente, curvas, radio y separación comprobados. La búsqueda continúa hasta detenerla.":"Gris: corredor preliminar y exploración. Azul claro: tramo conforme preferido; azul fuerte: alternativa conforme. Naranja: extremos pendientes de conexión. Se mantienen los parámetros de diseño solicitados para los tramos azules.");
        stop_=std::stop_source{};auto token=stop_.get_token();auto terrain=terrain_;auto job=++generation_;
        busy(true,candidate_mode?"Generando carreteras alternativas con curvas, superficie y corte/relleno…":adjust?"Buscando con ajustes permitidos: recursos → recta → radio → pendiente…":index==0?"Generando opción 1 sobre la topografía original…":"Generando opción 2 con límites de corte y/o relleno…");auto* watcher=new QFutureWatcher<PlanResult>(this);
        if(candidate_mode&&keep_searching){continuous_search_=true;cancel_->setText("DETENER BÚSQUEDA");process_cancel_->setText("Detener búsqueda");update_process();}
        connect(watcher,&QFutureWatcher<PlanResult>::finished,this,[this,watcher,config,index,job,token,adjust] {
            auto planned=watcher->result();watcher->deleteLater();if(job!=generation_)return;update_process();busy(false,"Listo · CPU · 100 %");
            auto& surface=planned.alternatives.surface;
            if(planned.design_review)design_reviews_[index]=std::move(planned.design_review);
            const bool guide_output=surface.strategy=="candidate_guide"&&design_reviews_[index]&&design_reviews_[index]->road_candidates;
            if(planned.base_surface&&planned.base_config){
                if(!alternatives_)alternatives_.emplace();alternatives_->surface=*planned.base_surface;option_configs_[0]=*planned.base_config;
                option_configs_[0]->road_search.trace={};option_feedback_[0].clear();option_diagnostics_[0]=QString::fromStdString(planned.base_surface->adjustment_report);
            }
            if((token.stop_requested()||surface.status==SearchStatus::cancelled)&&surface.status!=SearchStatus::found&&!guide_output){
                if(index==1&&planned.base_surface&&planned.base_config){auto kept=*planned.base_config;kept.parameters.mode=DesignMode::earthwork;
                    alternatives_->earthwork=*planned.base_surface;option_configs_[1]=kept;choose_route(1);
                }else restore_result(true);statusBar()->showMessage("Cálculo cancelado; se conserva la ruta base disponible.");return;}
            if(!planned.error.isEmpty()){restore_result(true);error(planned.error);return;}
            if(surface.status!=SearchStatus::found&&!guide_output) {
                option_feedback_[index]=surface.status==SearchStatus::no_path?"No se encontró una ruta con estos parámetros.":"Se agotaron los recursos de búsqueda y reintento. No se ha demostrado que la ruta sea imposible.";
                option_diagnostics_[index]=QString::fromStdString(surface.adjustment_report);
                if(option_diagnostics_[index].isEmpty()){
                    option_diagnostics_[index]=option_feedback_[index]+QString("\n\nValores solicitados: recta %1 m; radio %2 m; pendiente %3 %.\nCorte máximo %4 m; relleno máximo %5 m; ancho %6 m.\nOrden de ajustes: ampliar búsqueda, reducir recta hasta 35 m, reducir radio hasta 30 m, elevar pendiente hasta 12 %.\nLos máximos de corte/relleno no se elevan automáticamente.")
                        .arg(concise(config.parameters.min_straight_m),concise(config.parameters.min_radius_m),concise(config.parameters.max_grade_percent),concise(config.parameters.max_cut_m),concise(config.parameters.max_fill_m),concise(config.parameters.road_width_m));
                    option_diagnostics_[index]+=QString("\nRechazos observados: pendiente %1; cobertura %2; corte/relleno %3; separación/cruces %4; geometría %5. Son causas de descarte de candidatos, no pruebas de imposibilidad.")
                        .arg(surface.diagnostics.grade).arg(surface.diagnostics.coverage).arg(surface.diagnostics.earthwork).arg(surface.diagnostics.spacing).arg(surface.diagnostics.geometry);
                    const double dz=std::abs(config.start.z-config.finish.z);
                    if(config.parameters.max_grade_percent==0&&dz>1e-8)option_diagnostics_[index]+="\nIncompatibilidad comprobada: pendiente 0 % y cotas A/B diferentes.";
                    if(dz>0)option_diagnostics_[index]+=QString("\nLongitud horizontal mínima por desnivel al 12 %: %1 m. Debe caber en la superficie respetando radio y ancho.").arg(concise(dz/0.12));
                }
                choose_route(index);statusBar()->showMessage("Sin resultado · consulte Ayuda > Detalles del resultado");return;
            }
            if(!guide_output&&(!surface.estimate||!surface.estimate->validation.checked_constraints_pass())){restore_result();error(surface.estimate?QString::fromUtf8(validation_failure_message(surface.estimate->validation).c_str()):QString("La ruta no tiene una validación disponible; se descartó."));return;}
            if(!alternatives_)alternatives_.emplace();
            option_configs_[index]=planned.effective.value_or(config);option_configs_[index]->road_search.trace={};
            option_diagnostics_[index]=QString::fromStdString(surface.adjustment_report);
            if(option_diagnostics_[index].isEmpty())option_diagnostics_[index]="Ruta encontrada con los parámetros solicitados. No se aplicaron ajustes geométricos.";
            if(index==1)alternatives_->earthwork=std::move(surface);else alternatives_->surface=std::move(surface);
            option_feedback_[index].clear();option_messages_[index]=std::move(planned.alternatives.messages);choose_route(index);if(token.stop_requested())statusBar()->showMessage("Búsqueda cancelada; se conservan el corredor gris y los tramos verificados.");
        });
        if(!testing_)minimize_process();
        QPointer<RouteWindow> self(this);
        watcher->setFuture(QtConcurrent::run([terrain,config,index,cached_surface,cached_config,token,adjust,retry,keep_searching,candidate_mode,max_intervention,trace,self,job,progress=job_progress_] {
            PlanResult r;try{
                auto report=[progress](int value){progress->store(std::min(99,value));};
                auto working=config;
                auto stage=[self,job](const std::string& text){if(self)QMetaObject::invokeMethod(self,[self,job,text]{if(self&&self->generation_==job&&self->busy_){self->process_message_=QString::fromStdString(text);self->update_process();}},Qt::QueuedConnection);};
                if(candidate_mode){
                    auto& guide=r.alternatives.surface;guide.route={config.start,config.finish};guide.status=SearchStatus::no_path;
                    guide.strategy="candidate_guide";
                    guide.adjustment_report="Exploración desde A, B y otros puntos de la TIN, con pendiente del terreno ≤ 2× la máxima solicitada. Gris: exploración; rojo/verde: exceso ascendente/descendente en el sentido del trazo; celeste: criterios del eje comprobados. La guía A/B no se dibuja ni se considera una carretera.";
                }else if(index==1&&cached_surface&&cached_config){
                    r.base_surface=*cached_surface;r.base_config=*cached_config;working=*cached_config;
                    working.road_search.trace=config.road_search.trace;
                }else{
                    if(index==1)working.parameters.mode=DesignMode::surface;
                    if(adjust||retry){
                        auto planner=adjust?plan_road_adjusted:plan_surface_reference;
                        auto changed=planner(*terrain,working.start,working.finish,working.parameters,working.costs,working.search,working.road_search,token,
                            [report](int value){report(value*15/100);},stage);
                        working.parameters=changed.parameters;working.search=changed.search;working.road_search=changed.road;
                        if(changed.route.strategy=="surface_reference"){
                            std::erase_if(working.warnings,[](const std::string& text){return text.starts_with("Continuous ramp:")||text.starts_with("Sampled tangent/arc alignment:");});
                            working.warnings.push_back("Corredor preliminar: curvas, radio, tangencias y plataforma pendientes de diseño.");
                        }
                        r.alternatives.surface=std::move(changed.route);
                    }else r.alternatives=plan_road(*terrain,working.start,working.finish,working.parameters,working.costs,working.search,working.road_search,token,report);
                    if(index==1&&r.alternatives.surface.status==SearchStatus::found){r.base_surface=r.alternatives.surface;r.base_config=working;}
                }
                if(index==1&&r.base_surface&&r.base_config&&self){
                    QMetaObject::invokeMethod(self,[self,job,base=*r.base_surface,base_config=*r.base_config]{if(self&&self->generation_==job&&self->busy_){
                        if(!self->alternatives_)self->alternatives_.emplace();self->alternatives_->surface=base;self->option_configs_[0]=base_config;self->option_configs_[0]->road_search.trace={};
                        self->view_->set_route(base.route,{}, {},false,base_config.parameters.surface_tolerance_m,true);
                    }},Qt::QueuedConnection);
                }
                if(index==1&&r.base_surface&&!token.stop_requested()){
                    stage("Opción 2: ruta base disponible; optimizando tramos con corte/relleno.");
                    working.parameters.mode=DesignMode::earthwork;
                    working.parameters.max_cut_m=config.parameters.max_cut_m;working.parameters.max_fill_m=config.parameters.max_fill_m;
                    r.alternatives.surface=optimize_surface_reference(*terrain,*r.base_surface,working.parameters,working.costs,working.search,working.road_search,token,
                        [report](int value){report(15+value*10/100);});
                }
                r.effective=working;
                if(retry&&(r.alternatives.surface.status==SearchStatus::found||(candidate_mode&&r.alternatives.surface.strategy=="candidate_guide"))){
                    stage(candidate_mode?"Dibujando exploración gris sobre la TIN (≤ 2× pendiente máxima) y buscando alternativas con curvas; se admiten rodeos largos.":"Corredor gris disponible; buscando tramos con curvas, radios y separación del diseño solicitado…");
                    if(self)QMetaObject::invokeMethod(self,[self,job,index,keep_searching,candidate_mode,max_intervention,base=r.alternatives.surface,working]{if(self&&self->generation_==job&&self->busy_){
                        self->result_=base;self->result_config_=working;if(!self->alternatives_)self->alternatives_.emplace();
                        if(index==1)self->alternatives_->earthwork=base;else self->alternatives_->surface=base;
                        self->option_configs_[index]=working;self->option_configs_[index]->road_search.trace={};
                        if(candidate_mode)self->design_reviews_[index]=ViewerDesignReview{{},working.parameters,true,max_intervention,{}};
                        self->continuous_search_=keep_searching;self->search_round_=0;
                        if(keep_searching){self->cancel_->setText("DETENER BÚSQUEDA");self->process_cancel_->setText("Detener búsqueda");}
                        self->view_->set_route(base.route,{}, {},false,working.parameters.surface_tolerance_m,true);if(candidate_mode)self->view_->set_candidate_review({},working.parameters.surface_tolerance_m);self->update_controls();self->update_process();
                    }},Qt::QueuedConnection);
                    ViewerDesignReview assessment;assessment.parameters=config.parameters;assessment.road_candidates=candidate_mode;assessment.max_intervention_length_m=max_intervention;
                    auto publish=[self,job,index,candidate_mode,max_intervention,requested=config.parameters](const DesignReview& review,std::size_t round,const std::vector<SearchResult>& branches){if(self)QMetaObject::invokeMethod(self,[self,job,index,candidate_mode,max_intervention,requested,review,round,branches]{if(self&&self->generation_==job&&self->busy_){
                        self->design_reviews_[index]=ViewerDesignReview{review,requested,candidate_mode,max_intervention,branches};
                        if(candidate_mode)self->view_->set_candidate_review(review,requested.surface_tolerance_m,branches);else self->view_->set_design_review(review);
                        self->search_round_=round;self->option_diagnostics_[index]=QString::fromStdString(review.report);
                        if(self->continuous_search_){self->result_status_->setText(candidate_mode?(review.connected?QString("Conexión A–B: %1 m · buscando mejoras").arg(concise(measure(review.connected->route).horizontal_m)):QString("%1 alternativas verificadas · explorando más recorridos").arg(review.sections.size()+branches.size())):review.connected?"Conexión conforme · buscando alternativas":QString("%1 tramos conformes · buscando más").arg(review.sections.size()));
                            auto summary=QString("Ronda %1 · %2 conexiones pendientes.\nPendiente %3 % · radio %4 m · recta %5 m.\nLa búsqueda sigue hasta pulsar DETENER BÚSQUEDA. Puede guardar los resultados actuales.").arg(round).arg(review.gaps.size()).arg(concise(requested.max_grade_percent),concise(requested.min_radius_m),concise(requested.min_straight_m));
                            if(candidate_mode&&review.connected)summary+=QString("\nMejor conexión A–B encontrada: %1 m. Se sigue optimizando el objetivo configurado.").arg(concise(measure(review.connected->route).horizontal_m));self->metrics_->setText(summary);}

                        self->process_message_=candidate_mode?QString("Ronda %1 · exploración gris ≤ %2 % · %3 alternativas verificadas · intervención continua ≤ %4 m.").arg(round).arg(concise(2*requested.max_grade_percent)).arg(review.sections.size()+branches.size()).arg(max_intervention):QString("Ronda %1 · %2 tramos conformes · %3 conexiones pendientes; explorando más direcciones y sectores.").arg(round).arg(review.sections.size()).arg(review.gaps.size());
                        self->update_controls();self->update_process();
                        self->message_->setText(candidate_mode?"Gris: exploración. Exceso ascendente: rojo; descendente: verde, en el sentido del trazo (hasta 2× la pendiente). Celeste: cumple los criterios comprobados del eje. Se conservan los sectores explorados y se sigue buscando; el usuario elegirá la ruta.":QString("Corredor gris. %1 tramos conformes; %2 intervalos por conectar. Azul claro: preferido por longitud entre conexiones equivalentes. Los extremos naranjas requieren decisión de diseño.").arg(review.sections.size()).arg(review.gaps.size()));
                    }},Qt::QueuedConnection);};
                    if(candidate_mode){
                        RoadCandidateLimits candidate_limits;candidate_limits.max_intervention_length_m=max_intervention;
                        std::size_t round_now=0,branch_vertices=0;
                        auto review_update=[&](const DesignReview& value,std::size_t round){round_now=round;assessment.review=value;publish(value,round,assessment.branches);};
                        auto branch_update=[&](const SearchResult& branch){
                            // Archive every checked candidate for live drawing, even
                            // when the bounded list of exportable roads is full.
                            trace->record(branch.route,true);
                            if(assessment.branches.size()>=256||branch_vertices+branch.route.size()>800000)return;
                            for(const auto& old:assessment.branches)if(old.route.size()==branch.route.size()&&std::equal(old.route.begin(),old.route.end(),branch.route.begin(),[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y)<1e-6&&std::abs(a.z-b.z)<1e-6;}))return;
                            assessment.branches.push_back(branch);branch_vertices+=branch.route.size();publish(assessment.review,round_now,assessment.branches);
                        };
                        auto exploratory=[trace](std::span<const Vec3> path){
                            // Preserve each original TIN segment: display thinning must not
                            // introduce chords across bends, steep faces, or coverage gaps.
                            for(std::size_t begin=0;begin+1<path.size();begin+=63)
                                trace->record(path.subspan(begin,std::min<std::size_t>(64,path.size()-begin)),false);
                        };
                        assessment.review=explore_road_candidates(*terrain,r.alternatives.surface,assessment.parameters,config.costs,config.search,config.road_search,candidate_limits,token,review_update,branch_update,exploratory);
                    }else if(keep_searching)assessment.review=explore_design_continuously(*terrain,r.alternatives.surface,assessment.parameters,config.costs,config.search,config.road_search,token,[&](const DesignReview& value,std::size_t round){publish(value,round,{});});
                    else assessment.review=review_surface_corridor(*terrain,r.alternatives.surface,assessment.parameters,config.costs,config.search,config.road_search,token,
                        [report](int value){report(25+value*74/100);},[&](const DesignReview& review){publish(review,0,{});});
                    r.design_review=std::move(assessment);
                }
            }catch(const std::exception& e){
                if(index==1&&r.base_surface&&r.base_config){r.alternatives.surface=*r.base_surface;r.effective=*r.base_config;r.effective->parameters.mode=DesignMode::earthwork;
                    r.alternatives.surface.adjustment_report+="\nOptimización interrumpida; ruta base conservada: "+std::string(e.what());
                }else if(r.alternatives.surface.status==SearchStatus::found||r.alternatives.surface.strategy=="candidate_guide"){r.alternatives.surface.adjustment_report+="\nRevisión de diseño interrumpida: "+std::string(e.what());}
                else r.error=QString::fromUtf8(e.what());
            }return r;
        }));
    }catch(const std::exception& e){restore_result();error(QString::fromUtf8(e.what()));}
}
void RouteWindow::set_local_point(int which,std::size_t index){
    if(!result_||which<0||which>1||index>=result_->route.size())return;local_points_[which]=index;
    if(local_points_[0]&&local_points_[1]&&*local_points_[0]>*local_points_[1])std::swap(local_points_[0],local_points_[1]);
    view_->set_route_points(local_points_[0],local_points_[1]);update_controls();
}
void RouteWindow::restore_result(bool keep_search){
    search_trace_.reset();if(!keep_search)view_->clear_search();
    if(result_&&result_config_){show_result();view_->set_route_points(local_points_[0],local_points_[1]);}
}
void RouteWindow::configure_local(){
    if(busy_)return;Parameters defaults;
    const auto values=settings();defaults.max_grade_percent=std::stod(values.at("max_grade_percent"));defaults.min_radius_m=std::stod(values.at("min_radius_m"));defaults.min_straight_m=std::stod(values.at("min_straight_m"));
    if(result_config_)defaults=result_config_->parameters;if(local_parameters_)defaults=*local_parameters_;
    auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);dialog->setWindowTitle("Configuración de puntos intermedios");dialog->setObjectName("intermediateConfigurationDialog");dialog->resize(560,380);
    auto* layout=new QVBoxLayout(dialog);auto* enabled=new QCheckBox("Habilitar puntos intermedios");enabled->setObjectName("enableIntermediatePoints");enabled->setChecked(local_tools_enabled_);layout->addWidget(enabled);
    auto* hint=new QLabel("Marque P1 y P2 sobre una ruta calculada para optimizar únicamente ese tramo. Sus parámetros aparecen aquí; el resto de la ruta conserva el diseño del proyecto.");hint->setWordWrap(true);layout->addWidget(hint);auto* form=new QFormLayout;layout->addLayout(form);
    const auto count=result_?result_->route.size():std::size_t(1);
    QSpinBox* selected[2];for(int i=0;i<2;++i){selected[i]=new QSpinBox;selected[i]->setRange(1,static_cast<int>(count));selected[i]->setValue(static_cast<int>(local_points_[i].value_or(i?count-1:0))+1);selected[i]->setEnabled(bool(result_));selected[i]->setObjectName(i?"localLastIndex":"localFirstIndex");form->addRow(i?"P2 · punto del recorrido":"P1 · punto del recorrido",selected[i]);}
    auto* grade=new QDoubleSpinBox;grade->setObjectName("localGrade");grade->setRange(0,12);grade->setDecimals(1);grade->setValue(defaults.max_grade_percent);form->addRow("Pendiente máxima local (%)",grade);
    auto* radius=new UnboundedDistanceInput;radius->setObjectName("localRadius");radius->setRange(30,std::numeric_limits<double>::max());radius->setDecimals(2);radius->setValue(defaults.min_radius_m);form->addRow("Radio mínimo local (m)",radius);
    auto* straight=new UnboundedDistanceInput;straight->setObjectName("localStraight");straight->setRange(35,std::numeric_limits<double>::max());straight->setDecimals(2);straight->setValue(defaults.min_straight_m);form->addRow("Recta local mínima de avance (m)",straight);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Apply)->setText("Aplicar");buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this,dialog,enabled,defaults,first=selected[0],last=selected[1],grade,radius,straight]{
        if(enabled->isChecked()&&result_&&first->value()>=last->value()){QMessageBox::warning(dialog,"Puntos intermedios","P1 debe estar antes de P2 en el recorrido.");return;}
        local_tools_enabled_=enabled->isChecked();local_parameters_=defaults;local_parameters_->max_grade_percent=grade->value();local_parameters_->min_radius_m=radius->value();local_parameters_->min_straight_m=straight->value();
        if(result_&&local_tools_enabled_){local_points_[0]=static_cast<std::size_t>(first->value()-1);local_points_[1]=static_cast<std::size_t>(last->value()-1);}else local_points_={};view_->set_route_points(local_points_[0],local_points_[1]);update_controls();dialog->close();
    });dialog->show();
}
void RouteWindow::optimize_segment(){
    if(busy_||!result_||!result_->estimate||!result_config_||!local_points_[0]||!local_points_[1]||*local_points_[0]>=*local_points_[1])return;
    auto original=*result_;auto config=*result_config_;
    view_->clear_search();search_trace_=std::make_shared<SearchTrace>();auto trace=search_trace_;
    config.road_search.trace=[trace](std::span<const Vec3> path,bool){trace->record(path,false);};
    auto local=local_parameters_.value_or(config.parameters);const int index=route_selection_->currentIndex();
    auto first=*local_points_[0],last=*local_points_[1];auto terrain=terrain_;stop_=std::stop_source{};auto token=stop_.get_token();auto job=++generation_;
    busy(true,"Optimizando tramo intermedio; se conservan los extremos y las restricciones de los demás tramos…");auto* watcher=new QFutureWatcher<RouteRefinement>(this);
    connect(watcher,&QFutureWatcher<RouteRefinement>::finished,this,[this,watcher,config,index,job,token]{
        auto refined=watcher->result();watcher->deleteLater();if(job!=generation_)return;update_process();busy(false,"Optimización de tramo terminada");if(token.stop_requested()){statusBar()->showMessage("Optimización cancelada; ruta anterior conservada.");return;}
        option_diagnostics_[index]+="\n"+QString::fromStdString(refined.report);
        if(refined.candidate){
            if(index==1)alternatives_->earthwork=std::move(*refined.candidate);else alternatives_->surface=std::move(*refined.candidate);option_configs_[index]=config;option_configs_[index]->road_search.trace={};choose_route(index);statusBar()->showMessage("Tramo optimizado.");
        }else{update_controls();statusBar()->showMessage("No se encontró una mejora; ruta conservada.");}
    });
    if(!testing_)minimize_process();
    watcher->setFuture(QtConcurrent::run([terrain,original,config,local,first,last,token,progress=job_progress_]{
        try{return optimize_route_section(*terrain,original,first,last,config.parameters,local,config.costs,config.search,config.road_search,token,[progress](int value){progress->store(std::min(99,value));});}
        catch(const std::exception& e){return RouteRefinement{std::nullopt,std::string("Tramo conservado: ")+e.what()};}
    }));
}
void RouteWindow::show_diagnostics() {
    auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("routeDiagnosticDialog");dialog->setWindowTitle("Detalles del resultado");dialog->resize(820,600);
    auto* layout=new QVBoxLayout(dialog);auto* text=new QPlainTextEdit;text->setReadOnly(true);text->setPlainText(option_diagnostics_[route_selection_->currentIndex()]);layout->addWidget(text);
    auto* settings_row=new QHBoxLayout;auto* design=new QPushButton("Revisar diseño…");auto* earthwork=new QPushButton("Revisar corte/relleno…");earthwork->setEnabled(route_selection_->currentIndex()==1);settings_row->addWidget(design);settings_row->addWidget(earthwork);layout->addLayout(settings_row);
    connect(design,&QPushButton::clicked,this,[this,dialog]{dialog->close();show_settings(design_dialog_);});connect(earthwork,&QPushButton::clicked,this,[this,dialog]{dialog->close();advanced_tabs_->setCurrentIndex(0);show_settings(advanced_dialog_);});
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close);close->button(QDialogButtonBox::Close)->setText("Cerrar");connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::close);layout->addWidget(close);dialog->show();
}
void RouteWindow::choose_route(int index) {
    {QSignalBlocker block(route_selection_);route_selection_->setCurrentIndex(index);}
    const SearchResult* cached=nullptr;
    if(alternatives_){if(index==1&&alternatives_->earthwork)cached=&*alternatives_->earthwork;
        else if(index==0&&(alternatives_->surface.status==SearchStatus::found||(alternatives_->surface.strategy=="candidate_guide"&&design_reviews_[index]&&design_reviews_[index]->road_candidates)))cached=&alternatives_->surface;}
    if(!cached){
        result_.reset();result_config_.reset();view_->set_route({});
        result_status_->setText(option_feedback_[index].isEmpty()?"Sin ruta calculada":"No se obtuvo una ruta validada");
        metrics_->setText(option_feedback_[index].isEmpty()?"Seleccione A y B y calcule la ruta.":option_feedback_[index]+"\nRevise los detalles en Ayuda.");
        message_->setText(index==0?"Opción 1: corredor sobre la TIN original. Se comprueban pendiente y cobertura; curvas y radio quedan pendientes.":"Opción 2: parte de la opción 1 y prueba mejoras locales de corte/relleno. Si no mejora, conserva la ruta base.");
        update_controls();return;
    }
    local_points_={};view_->set_route_points({},{});result_=*cached;result_config_=option_configs_[index].value_or(configuration());show_result();
}
void RouteWindow::show_result() {
    const int selected=route_selection_->currentIndex();const auto& independent=design_reviews_[selected];
    if(!result_->estimate&&result_->strategy=="candidate_guide"&&independent&&independent->road_candidates){
        const auto& saved=*independent;view_->set_route({});view_->set_exploration_grade_limit(saved.parameters.max_grade_percent);view_->set_candidate_review(saved.review,saved.parameters.surface_tolerance_m,saved.branches);
        result_status_->setText(saved.review.connected?"Conexión A–B verificada · alternativas conservadas":QString("%1 alternativas independientes · conexión A/B pendiente").arg(saved.review.sections.size()+saved.branches.size()));
        const auto connection=saved.review.connected?QString("Mejor conexión A–B encontrada: %1 m.").arg(concise(measure(saved.review.connected->route).horizontal_m)):QString("No hay un corredor A/B verificado. La guía de anclaje no se dibuja como carretera.");
        const auto text=connection+QString("\nPendiente del eje ≤ %1 % · radio ≥ %2 m.\nIntervención continua ≤ %3 m.\nGris: exploración; rojo/verde: exceso ascendente/descendente; celeste: cumple criterios del eje.")
            .arg(concise(saved.parameters.max_grade_percent),concise(saved.parameters.min_radius_m),concise(saved.max_intervention_length_m));
        metrics_->setText(text);option_diagnostics_[selected]=text+"\n\n"+QString::fromStdString(saved.review.report);
        message_->setText("Se conservan las alternativas verificadas aunque no exista una conexión completa A/B. No se rellenan huecos de la TIN automáticamente.");update_controls();return;
    }
    const auto& e=*result_->estimate;const auto& v=e.validation;const auto& c=*result_config_;const bool earthwork=c.parameters.mode==DesignMode::earthwork;
    const auto& reference=earthwork&&result_->reference_route.empty()&&alternatives_&&alternatives_->surface.status==SearchStatus::found?
        alternatives_->surface.route:result_->reference_route;
    view_->set_route(result_->route,result_->interventions,reference,earthwork,c.parameters.surface_tolerance_m,result_->strategy=="surface_reference");
    const auto& assessment=design_reviews_[earthwork?1:0];
    if(assessment&&assessment->road_candidates){view_->set_exploration_grade_limit(assessment->parameters.max_grade_percent);view_->set_candidate_review(assessment->review,assessment->parameters.surface_tolerance_m,assessment->branches);}
    else view_->set_design_review(assessment?assessment->review:DesignReview{});
    result_status_->setText(result_->strategy=="surface_reference"?(earthwork?"Corredor optimizado · curvas pendientes":"Corredor sobre terreno · curvas pendientes"):(earthwork?"Ruta calculada · corte y relleno":"Ruta calculada · sobre terreno"));
    QString text="Longitud horizontal: "+format(v.metrics.horizontal_m)+" m\nDistancia 3D: "+format(v.metrics.spatial_m)+" m\nDesnivel B − A: "+format(c.finish.z-c.start.z)+" m\nPendiente máxima: "+format(v.metrics.max_grade_percent)+" %\nCorte máximo: "+format(v.max_cut_m)+" m\nRelleno máximo: "+format(v.max_fill_m)+" m\nCorte / relleno del eje: "+format(e.cut_m3)+" / "+format(e.fill_m3)+" m³\n";
    if(result_->strategy=="surface_reference")text+="CURVAS PENDIENTES: radio, tangencias y ancho de plataforma no verificados.\n";
    text+=c.cost_profile=="length_only"?"Objetivo: longitud, sin precios":QString(c.cost_profile=="synthetic_example"?"Costo ilustrativo: %1 CU ficticias":"Costo preliminar: %1 CU").arg(format(e.total));metrics_->setText(text+QString("\n%1 puntos · %2 expansiones").arg(result_->route.size()).arg(result_->expanded));
    text=metrics_->text()+QString("\nInclinación máxima del eje: %1°\nLímite base: %2 % (%3°)\nMargen propuesta: %4°")
        .arg(format(std::atan(v.metrics.max_grade_percent/100)*180/std::numbers::pi),format(c.parameters.max_grade_percent),format(std::atan(c.parameters.max_grade_percent/100)*180/std::numbers::pi),format(c.road_search.additional_grade_degrees));
    if(result_->effective_heading_step_degrees>0)text+=QString("\nPaso XY efectivo: %1 m\nPaso angular base: %2°").arg(format(result_->effective_xy_spacing_m),format(result_->effective_heading_step_degrees));
    else text+="\nRefinamiento automático sobre la superficie";
    for(std::size_t i=0;i<result_->interventions.size();++i) {const auto& s=result_->interventions[i];text+=QString("\n\nTramo rojo %1\nRodeo: %2 m → propuesta: %3 m\nRelación: %4 veces (umbral > %5)\nCerro: %6° · corte/relleno: %7 / %8 m")
        .arg(i+1).arg(format(s.detour_m),format(s.proposal_m),format(s.detour_m/s.proposal_m),format(c.road_search.detour_ratio),format(s.terrain_angle_degrees),format(s.max_cut_m),format(s.max_fill_m));}
    if(earthwork){
        text+=QString("\nLímites configurados · corte: 0–%1 m · relleno: 0–%2 m").arg(format(c.parameters.max_cut_m),format(c.parameters.max_fill_m));
        if(alternatives_&&alternatives_->surface.status==SearchStatus::found){
            if(option_configs_[0]){const auto& reference_params=option_configs_[0]->parameters;
                if(reference_params.max_grade_percent!=c.parameters.max_grade_percent||reference_params.min_radius_m!=c.parameters.min_radius_m||reference_params.min_straight_m!=c.parameters.min_straight_m||!alternatives_->surface.local_sections.empty()||!result_->local_sections.empty())text+="\nLa comparación incluye configuraciones distintas; consulte los valores y los tramos locales de cada opción.";
            }
            double original=alternatives_->surface.estimate->validation.metrics.horizontal_m,saving=original-v.metrics.horizontal_m;
            text+=saving>1e-6?QString("\nReducción frente a opción 1: %1 m (%2 %)").arg(format(saving),format(100*saving/original)):
                "\nEsta búsqueda no logró una ruta más corta que la opción 1 con los límites actuales.";
        }else text+="\nCalcule la opción 1 para comparar la reducción de longitud.";
    }
    if(assessment){const auto& review=assessment->review;
        result_status_->setText(review.connected?"Conexión A–B conforme encontrada":QString("%1 tramos conformes · conexiones pendientes").arg(review.sections.size()));
        text+=QString("\n\nDISEÑO SOLICITADO: pendiente %1 % · radio %2 m · recta %3 m · separación %4 m.\nGris: corredor pendiente. Azul claro: preferido conforme; azul fuerte: alternativa conforme.\n%5 tramos conformes · %6 intervalos pendientes; no se dibujan puentes entre tramos.")
            .arg(concise(assessment->parameters.max_grade_percent),concise(assessment->parameters.min_radius_m),concise(assessment->parameters.min_straight_m),concise(assessment->parameters.road_width_m)).arg(review.sections.size()).arg(review.gaps.size());
        for(const auto& gap:review.gaps)text+=QString("\nPendiente de conectar: %1–%2 m desde A.").arg(format(gap.first_station_m),format(gap.last_station_m));
        option_diagnostics_[earthwork?1:0]=QString::fromStdString(review.report);
    }
    for(const auto& item:option_messages_[earthwork?1:0])text+="\n"+QString::fromStdString(item);metrics_->setText(text);
    if(alignment_enabled(c.parameters)&&!c.parameters.continuous_curves) {
        text+=QString("\nCurvas: %1\nRecta mínima exigida: %2 m\nRadio mínimo exigido: %3 m\nGiro máximo permitido: %4°")
            .arg(result_->curves.size()).arg(format(c.parameters.min_straight_m),format(c.parameters.min_radius_m),format(c.parameters.max_turn_degrees));
        double maximum=0;for(const auto& curve:result_->curves)maximum=std::max(maximum,std::abs(curve.sweep_degrees));
        text+=QString("\nGiro máximo obtenido: %1°\nTangencias y arcos muestreados comprobados.").arg(format(maximum));metrics_->setText(text);
    }
    if(c.parameters.continuous_curves){text+=QString("\nCurvas continuas y vueltas completas habilitadas.\nRadio mínimo exigido: %1 m\nSeparación entre vueltas: ≥ %2 m\nRecta al invertir el giro: %3 m. Sin límite de giro acumulado.").arg(format(c.parameters.min_radius_m),format(c.parameters.road_width_m),format(c.parameters.min_straight_m));metrics_->setText(text);}
    if(!result_->local_sections.empty()){
        text+="\n\nCONFIGURACIONES DE TRAMOS INTERMEDIOS";
        for(const auto& section:result_->local_sections)text+=QString("\nPuntos %1–%2: pendiente %3 % · radio %4 m · recta %5 m").arg(section.first+1).arg(section.last+1).arg(concise(section.max_grade_percent),concise(section.min_radius_m),concise(section.min_straight_m));
        text+="\nLos demás tramos conservan sus restricciones; empalmes y recorrido completo comprobados.";metrics_->setText(text);
    }
    if(!result_->adjustment_attempts.empty()){
        text+=QString("\n\nRUTA CON AJUSTES PERMITIDOS · %1 intentos\nUtilizado: recta %2 m · radio %3 m · pendiente máxima %4 %.\nPrimera ruta válida encontrada en la secuencia; no se demuestra que sea única.\nLos controles de Diseño conservan los valores solicitados. Ayuda > Detalles del resultado muestra los cambios.")
            .arg(result_->adjustment_attempts.size()).arg(concise(c.parameters.min_straight_m),concise(c.parameters.min_radius_m),concise(c.parameters.max_grade_percent));metrics_->setText(text);
    }
    message_->setText(alignment_enabled(c.parameters)?"Azul: eje con rectas y arcos circulares muestreados; pendiente y geometría del eje comprobadas. Sección vehicular y peralte por revisar.":"Azul: ruta sobre terreno. Azul transparente: rodeo original. Rojo: corte/nivelación propuesto. Modo anterior: radio, recta mínima y giro máximo desactivados.");update_controls();
    if(c.parameters.continuous_curves){
        auto legend=earthwork?QString("Rojo: corte. Verde: relleno. Azul: sin movimiento de tierras. Clasificación del eje respecto de la TIN original; plataforma y estabilidad por revisar."):
            QString("Azul: eje sobre la topografía original, sin proyectar corte ni relleno. La plataforma vial completa requiere revisión de ingeniería.");
        if(earthwork&&!reference.empty())legend.prepend("Azul transparente: opción 1 de referencia. ");message_->setText(legend);
    }
    if(assessment&&assessment->road_candidates){
        const auto count=assessment->review.sections.size()+assessment->branches.size();result_status_->setText(QString("%1 alternativas de carretera dibujadas").arg(count));
        text=QString("Pendiente del eje ≤ %1 % · radio ≥ %2 m.\nCorte ≤ %3 m · relleno ≤ %4 m.\nIntervención continua ≤ %5 m.\nGris: exploración; rojo/verde: exceso ascendente/descendente; celeste: cumple criterios del eje.\nLas alternativas largas se conservan; cada carretera se comprueba por separado.")
            .arg(concise(assessment->parameters.max_grade_percent),concise(assessment->parameters.min_radius_m),concise(assessment->parameters.max_cut_m),concise(assessment->parameters.max_fill_m),concise(assessment->max_intervention_length_m));
        metrics_->setText(text);message_->setText("Alternativas con rectas y curvas enlazadas. No se dibujan rayos ni se unen extremos sin trazado. La elección de la ruta corresponde al usuario.");
        option_diagnostics_[earthwork?1:0]=text+"\n\n"+QString::fromStdString(assessment->review.report);return;
    }
    option_diagnostics_[earthwork?1:0]=text+"\n\n"+QString::fromStdString(result_->adjustment_report)+(assessment?"\n\n"+QString::fromStdString(assessment->review.report):QString{})+"\n\nLa ruta es un corredor preliminar. La plataforma, estabilidad, peralte y transiciones requieren diseño de ingeniería.";
    metrics_->setText(QString("Longitud: %1 m\nPendiente máxima: %2 %\nDesnivel: %3 m\nCorte / relleno: %4 / %5 m")
        .arg(format(v.metrics.horizontal_m),format(v.metrics.max_grade_percent),format(c.finish.z-c.start.z),format(v.max_cut_m),format(v.max_fill_m)));
    metrics_->setToolTip("Las métricas detalladas están en Ayuda > Detalles del resultado.");update_controls();
}
void RouteWindow::export_route(bool dxf) {
    if(!result_)return;const auto& assessment=design_reviews_[route_selection_->currentIndex()];
    const bool verified=assessment&&(!assessment->review.sections.empty()||assessment->review.connected||!assessment->branches.empty());
    if(result_->strategy=="candidate_guide"&&!verified){statusBar()->showMessage("Todavía no hay alternativas verificadas para exportar; la guía de anclaje no se exporta como ruta.");return;}
    QString extension=dxf?"dxf":"csv",name=verified?"RouteTec_Tramos_conformes.":result_->strategy=="surface_reference"?"RouteTec_Corredor_gris.":"RouteTec_Eje.";
    auto path=QFileDialog::getSaveFileName(this,verified?"Exportar tramos conformes separados":"Exportar corredor o eje",name+extension,QString("Archivo %1 (*.%2)").arg(extension.toUpper(),extension));
    if(path.isEmpty())return;if(QFileInfo(path).suffix().isEmpty())path+="."+extension;
    try{std::ostringstream output;
        if(verified){if(!validate_design_review(*terrain_,assessment->review,assessment->parameters))throw std::invalid_argument("Los tramos no superaron la validación previa a exportar.");
            if(assessment->road_candidates){RoadCandidateLimits limits;limits.max_intervention_length_m=assessment->max_intervention_length_m;for(const auto& branch:assessment->branches)if(!validate_road_candidate(*terrain_,branch,assessment->parameters,limits))throw std::invalid_argument("Alternativa no válida para exportar.");}
            if(dxf)write_design_review_dxf(output,assessment->review,assessment->branches);else write_design_review_csv(output,assessment->review,assessment->branches);
        }else{bool ramp=result_config_->parameters.mode==DesignMode::earthwork;
            if(ramp){if(dxf)write_classified_route_dxf(output,*terrain_,result_->route,result_config_->parameters.surface_tolerance_m);else write_classified_route_csv(output,*terrain_,result_->route,result_config_->parameters.surface_tolerance_m);}
            else if(dxf)write_route_dxf(output,result_->route,result_->interventions,false);else write_route_csv(output,result_->route,result_->interventions,false);
        }
        save_viewer_file(path,QByteArray::fromStdString(output.str()),{terrain_path_,project_path_});statusBar()->showMessage((verified?"Tramos independientes exportados, sin unir huecos: ":"Corredor/eje exportado: ")+path);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::show_settings(QDialog* dialog) {
    dialog->show();dialog->raise();dialog->activateWindow();
}
void RouteWindow::save_project(bool save_as) {
    try {auto config=result_&&result_config_?*result_config_:configuration();auto path=(!save_as&&!project_path_.isEmpty())?project_path_:QFileDialog::getSaveFileName(this,"Guardar proyecto",project_path_.isEmpty()?"RouteTec_Proyecto.json":project_path_,"Proyecto JSON (*.json)");if(path.isEmpty())return;if(QFileInfo(path).suffix().isEmpty())path+=".json";save_viewer_file(path,write_viewer_project(config,result_?&*result_:nullptr,ui_state(),design_reviews_[route_selection_->currentIndex()]?&*design_reviews_[route_selection_->currentIndex()]:nullptr),{terrain_path_});project_path_=path;statusBar()->showMessage("Proyecto guardado: "+path);}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}
void RouteWindow::smoke_test(const QString& terrain,const QString& output,bool capture_only,bool road_options,Settings test_settings) {
    testing_=true;if(!QDir().mkpath(output)){error("Cannot create smoke-test output folder");return;}
    if(remaining_time(1000,10)!="Estimando…"||remaining_time(5000,0)!="Estimando…"||
       remaining_time(10000,25)!="≈ 30 s"||remaining_time(20000,25)!="≈ 1 min 0 s"||
       remaining_time(120000,2)!="≈ 1 h 38 min"||remaining_time(10000,99)!="Finalizando…"||remaining_time(10000,100)!="Completado") {
        error("Remaining time estimate assertion failed");return;
    }
    auto check_process=[this,output](const QString& name) {
        auto require=[](bool value){if(!value)throw std::runtime_error("Process window assertion failed");};
        require(busy_&&process_dialog_->isVisible()&&process_panel_->isHidden());
        update_process();require(progress_->minimum()==0&&progress_->maximum()==100&&progress_->value()==floating_progress_->value());
        require(process_percent_->text().contains(QString::number(progress_->value())+" %"));
        require(!process_activity_->text().isEmpty()&&process_time_->text().contains("CPU"));
        require(process_remaining_->text().startsWith("Tiempo restante estimado:"));
        require(process_dialog_->grab().save(QDir(output).filePath(name+"-progress.png")));
        process_dialog_->findChild<QPushButton*>("minimizeProcess")->click();
        require(process_dialog_->isHidden()&&process_panel_->isVisible()&&!stop_.stop_requested());
        require(grab().save(QDir(output).filePath(name+"-minimized.png")));
        process_restore_->click();require(process_dialog_->isVisible()&&process_panel_->isHidden());
        require(progress_->value()==floating_progress_->value()&&process_summary_->text().contains(" %"));
        require(process_summary_->text().startsWith("Restante:"));
        process_dialog_->showMinimized();QCoreApplication::sendPostedEvents(process_dialog_,QEvent::MetaCall);
        require(process_dialog_->isHidden()&&process_panel_->isVisible());
        process_restore_->click();require(process_dialog_->isVisible()&&!process_dialog_->isMinimized());
        process_dialog_->close();require(process_dialog_->isHidden()&&process_panel_->isVisible()&&!stop_.stop_requested());
        process_restore_->click();process_dialog_->reject();require(process_panel_->isVisible()&&!stop_.stop_requested());
        process_restore_->click();require(process_dialog_->isVisible()&&process_panel_->isHidden());
    };
    try{load(terrain);check_process("load");}catch(const std::exception& e){error(QString::fromUtf8(e.what()));return;}
    auto* poll=new QTimer(this);auto phase=std::make_shared<int>(0);auto cancelled_export=std::make_shared<std::string>();
    connect(poll,&QTimer::timeout,this,[this,output,capture_only,road_options,test_settings,phase,check_process,cancelled_export] {
        if(busy_)return;
        try {
            auto require=[](bool value){if(!value)throw std::runtime_error("GUI smoke assertion failed");};
            require(process_dialog_->isHidden()&&process_panel_->isHidden());
            require(bool(terrain_));
            if(capture_only) {
                require(grab().save(QDir(output).filePath("terrain.png")));
                std::cout<<"capture=passed triangles="<<terrain_->faces().size()<<'\n';QCoreApplication::exit(0);return;
            }
            if(*phase==0) {
                for(const auto* name:{"viewISO","viewTOP","viewBOT","viewFRONT","viewBACK","viewLEFT","viewRIGHT","zoomIn","zoomOut"}) {
                    auto* action=findChild<QAction*>(name);require(action&&action->isEnabled());action->trigger();
                }
                findChild<QAction*>("viewISO")->trigger();
                require(continuous_->isChecked()&&!continuous_->isEnabled());continuous_->click();require(continuous_->isChecked());
                require(!extend_limits_->isChecked());
                auto* grade=fields_.at("max_grade_percent");auto* radius=fields_.at("min_radius_m");auto* straight=fields_.at("min_straight_m");
                require(grade->minimum()==0&&grade->maximum()==12&&radius->minimum()==30&&straight->minimum()==35);
                if(test_settings.empty())require(grade->value()==10&&radius->value()==30&&straight->value()==35);
                require(radius->property("initialValue").toDouble()==30&&straight->property("initialValue").toDouble()==35);
                grade->setValue(0);require(settings().at("max_grade_percent")=="0");
                grade->setValue(13);require(grade->value()==12);
                radius->setValue(50000);straight->setValue(50000);require(radius->value()==50000&&straight->value()==50000);
                extend_limits_->setChecked(true);grade->setValue(20);radius->setValue(20);straight->setValue(20);
                require(grade->maximum()==100&&std::stod(settings().at("max_grade_percent"))==20);
                extend_limits_->setChecked(false);
                require(grade->value()==12&&radius->value()==30&&straight->value()==35);
                require(std::stod(settings().at("max_grade_percent"))==12&&std::stod(settings().at("min_radius_m"))==30);
                grade->setValue(10);radius->setValue(30);straight->setValue(35);

                if(!road_options&&test_settings.empty()){select_[0]->click();auto* canvas=findChild<QVTKOpenGLNativeWidget*>();require(canvas!=nullptr);
                auto* render_window=canvas->renderWindow();auto* size=render_window->GetSize();auto* interactor=render_window->GetInteractor();
                interactor->SetEventPosition(size[0]/2,size[1]/2);interactor->InvokeEvent(vtkCommand::LeftButtonPressEvent);
                require(points_[0].has_value()&&!coordinates_[0][2]->text().isEmpty());}
                require(route_selection_->isEnabled()&&route_selection_->count()==2);
                require(route_selection_->itemText(0)=="1. Propuesta respetando el terreno."&&route_selection_->itemText(1)=="2. Propuesto con corte y/o relleno.");
                if(road_options){fields_.at("max_grade_percent")->setValue(2);fields_.at("max_cut_m")->setValue(6);fields_.at("max_fill_m")->setValue(6);fields_.at("xy_spacing_m")->setValue(5);route_selection_->setCurrentIndex(1);}
                if(!test_settings.empty())apply_settings(test_settings);
                else for(int i=0;i<2;++i){coordinates_[i][0]->setText(i?(road_options?"40":"100"):"0");coordinates_[i][1]->setText("0");coordinates_[i][2]->setText("0");register_[i]->click();}
                design_action_->trigger();require(design_dialog_->isVisible());
                require(design_dialog_->grab().save(QDir(output).filePath("design-dialog.png")));design_dialog_->close();
                require(!design_dialog_->isVisible());
                require(grab().save(QDir(output).filePath("design.png")));
                require(calculate_->isEnabled());calculate_->click();check_process("route");*phase=1;
            } else if(*phase==1) {
                require(result_.has_value()&&csv_->isEnabled()&&dxf_->isEnabled());
                if(road_options)require(result_->strategy=="continuous_ramp"&&configuration().parameters.continuous_curves);
                auto config=configuration();double expected=test_settings.empty()?(road_options?2:10):std::stod(test_settings.at("max_grade_percent"));require(config.parameters.max_grade_percent==expected);
                require(result_->estimate->validation.checked_constraints_pass());
                auto json=write_viewer_project(config,&*result_);save_viewer_file(QDir(output).filePath("project.json"),json,{terrain_path_});
                std::ostringstream csv,dxf;write_route_csv(csv,result_->route,result_->interventions,config.parameters.mode==DesignMode::earthwork);write_route_dxf(dxf,result_->route,result_->interventions,config.parameters.mode==DesignMode::earthwork);
                save_viewer_file(QDir(output).filePath("route.csv"),QByteArray::fromStdString(csv.str()),{terrain_path_});
                save_viewer_file(QDir(output).filePath("route.dxf"),QByteArray::fromStdString(dxf.str()),{terrain_path_});
                if(road_options||!test_settings.empty()){view_->camera_view("TOP");if(auto* panel=findChild<QScrollArea*>())panel->ensureWidgetVisible(result_status_);}
                require(grab().save(QDir(output).filePath("route.png")));
                if(config.parameters.continuous_curves){require(grab().save(QDir(output).filePath("route-top.png")));view_->camera_view("ISO");require(grab().save(QDir(output).filePath("route-iso.png")));}
                fields_.at("max_grade_percent")->setValue(test_settings.empty()?(road_options?1.5:7.5):std::stod(test_settings.at("max_grade_percent"))*0.9);require(!result_&&!csv_->isEnabled());
                auto project=read_viewer_project(json,QDir(output).filePath("project.json"));load(project.terrain_path,project);*phase=2;
            } else if(*phase==2) {
                require(result_.has_value()&&configuration().parameters.max_grade_percent==(test_settings.empty()?(road_options?2:10):std::stod(test_settings.at("max_grade_percent"))));
                if(road_options)require(result_->strategy=="continuous_ramp"&&configuration().parameters.continuous_curves);
                require(view_->has_visible_route());std::ostringstream expected_export;write_route_csv(expected_export,result_->route,result_->interventions,result_config_->parameters.mode==DesignMode::earthwork);*cancelled_export=expected_export.str();
                require(grab().save(QDir(output).filePath("reopened.png")));calculate_->click();require(!view_->has_visible_raw_route());
                process_cancel_->click();require(stop_.stop_requested()&&!cancel_->isEnabled()&&!process_cancel_->isEnabled());
                minimize_process();require(process_summary_->text().contains("Cancelando"));
                process_restore_->click();require(process_activity_->text().contains("Cancelando"));*phase=3;
                require(process_remaining_->text().contains("Cancelando"));
            } else if(*phase==3) {
                require(result_.has_value()&&csv_->isEnabled()&&dxf_->isEnabled()&&view_->has_visible_route());
                std::ostringstream actual_export;write_route_csv(actual_export,result_->route,result_->interventions,result_config_->parameters.mode==DesignMode::earthwork);require(actual_export.str()==*cancelled_export);
                require(grab().save(QDir(output).filePath("cancelled-restored.png")));
                // Reproduce a saved project with incompatible precision/resources, including hidden base_ values.
                auto altered=settings();altered["sample_spacing_m"]="0.001";altered["max_samples"]="16";
                altered["max_cut_m"]="0";altered["max_fill_m"]="0";altered["adaptive_resolution"]="0";
                altered["propose_earthwork"]="0";altered["cost_profile"]="custom";altered["road_per_m2"]="99";
                altered["continuous_curves"]="0";altered["min_radius_m"]="0";
                apply_settings(altered);require(configuration().parameters.continuous_curves&&configuration().parameters.min_radius_m==30);
                require(!calculate_->isEnabled());require(precision_hint_->text().contains("Muestreo incompatible"));
                require(fields_.at("sample_spacing_m")->toolTip().contains("Permitido:"));
                advanced_action_->trigger();require(advanced_dialog_->isVisible()&&advanced_tabs_->count()==4);advanced_tabs_->setCurrentIndex(1);*phase=4;
            } else if(*phase==4) {
                if(auto* panel=qobject_cast<QScrollArea*>(advanced_tabs_->currentWidget()))panel->ensureWidgetVisible(precision_hint_);
                require(advanced_dialog_->grab().save(QDir(output).filePath("sampling-blocked.png")));
                const auto before=configuration();const auto terrain_before=terrain_;
                reset_->click();const auto after=configuration();
                require(terrain_==terrain_before&&after.start.x==before.start.x&&after.start.y==before.start.y&&after.start.z==before.start.z);
                require(after.finish.x==before.finish.x&&after.finish.y==before.finish.y&&after.finish.z==before.finish.z);
                require(after.parameters.max_grade_percent==before.parameters.max_grade_percent&&calculate_->isEnabled());
                for(const auto& [key,widget]:fields_)if(key!="max_grade_percent")require(widget->value()==widget->property("initialValue").toDouble());
                for(const auto& [key,widget]:budgets_)require(widget->value()==widget->property("initialValue").toInt());
                require(after.parameters.sample_spacing_m==2&&after.parameters.max_samples==1000000&&after.parameters.max_cut_m==2&&after.parameters.max_fill_m==2&&after.parameters.continuous_curves);
                require(after.cost_profile=="length_only"&&after.costs.road_per_m2==1&&after.road_search.adaptive_resolution&&!after.road_search.propose_earthwork&&!z_domain_->isChecked());
                require(!fields_.at("sample_spacing_m")->toolTip().contains("Valor efectivo: 0.001"));
                *phase=5;
            } else if(*phase==5) {
                advanced_tabs_->setCurrentIndex(2);
                require(advanced_dialog_->grab().save(QDir(output).filePath("advanced-reset.png")));
                advanced_dialog_->close();advanced_action_->trigger();require(advanced_dialog_->isVisible());advanced_dialog_->close();
                if(road_options){fields_.at("max_cut_m")->setValue(6);fields_.at("max_fill_m")->setValue(6);}
                calculate_->click();*phase=6;
            } else if(*phase==6) {
                require(result_.has_value()&&csv_->isEnabled()&&result_->estimate->validation.checked_constraints_pass());
                auto legacy=read_viewer_project(write_viewer_project(configuration(),&*result_),QDir(output).filePath("legacy.json"));
                legacy.settings["continuous_curves"]="0";legacy.settings["min_radius_m"]="0";load(legacy.terrain_path,legacy);*phase=7;
            } else if(*phase==7) {
                require(!result_&&!csv_->isEnabled()&&calculate_->isEnabled());
                require(configuration().parameters.continuous_curves&&configuration().parameters.min_radius_m==30);
                calculate_->click();*phase=8;
            } else {
                require(result_.has_value()&&csv_->isEnabled()&&result_->estimate->validation.checked_constraints_pass());
                std::cout<<"smoke=passed floating progress, minimize button/native/close/escape, restore, synchronized cancellation, completion cleanup, menus, views, settings windows, planning, exports, reload, sampling guard, reset, permanent curves, legacy migration, recalculation\n";QCoreApplication::exit(0);
            }
        }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
    });
    poll->start(200);QTimer::singleShot(test_settings.empty()?45000:120000,this,[this]{error("GUI smoke timed out");});
}
void RouteWindow::smoke_proposals(const QString& terrain,const QString& output) {
    testing_=true;if(!QDir().mkpath(output)){error("Cannot create proposal smoke output");return;}
    load(terrain);auto* poll=new QTimer(this);auto phase=std::make_shared<int>(0);
    auto original=std::make_shared<SearchResult>();auto alternative=std::make_shared<SearchResult>();
    auto write=[this,output](const QString& name){
        const auto c=configuration();const bool projected=c.parameters.mode==DesignMode::earthwork;
        save_viewer_file(QDir(output).filePath(name+".json"),write_viewer_project(c,&*result_),{terrain_path_});
        std::ostringstream csv,dxf;write_route_csv(csv,result_->route,{},projected);write_route_dxf(dxf,result_->route,{},projected);
        save_viewer_file(QDir(output).filePath(name+".csv"),QByteArray::fromStdString(csv.str()),{terrain_path_});
        save_viewer_file(QDir(output).filePath(name+".dxf"),QByteArray::fromStdString(dxf.str()),{terrain_path_});
    };
    connect(poll,&QTimer::timeout,this,[this,output,phase,original,alternative,write]{
        if(busy_)return;
        try{
            auto require=[](bool ok){if(!ok)throw std::runtime_error("Proposal options GUI assertion failed");};
            require(bool(terrain_));
            if(*phase==0){
                require(route_selection_->isEnabled()&&route_selection_->count()==2);
                require(route_selection_->itemText(0)=="1. Propuesta respetando el terreno."&&route_selection_->itemText(1)=="2. Propuesto con corte y/o relleno.");
                fields_.at("max_grade_percent")->setValue(2);adaptive_->setChecked(false);
                set_point(0,{-100,0,0});set_point(1,{100,0,0});route_selection_->setCurrentIndex(0);
                require(configuration().parameters.mode==DesignMode::surface);calculate_->click();*phase=1;
            }else if(*phase==1){
                require(result_&&result_->estimate->validation.checked_constraints_pass());*original=*result_;
                require(result_->estimate->validation.max_cut_m<1e-8&&result_->estimate->validation.max_fill_m<1e-8);
                write("option-1");view_->camera_view("TOP");
                if(auto* panel=findChild<QScrollArea*>())panel->ensureWidgetVisible(route_selection_);
                require(grab().save(QDir(output).filePath("option-1.png")));
                route_selection_->setCurrentIndex(1);require(!result_&&!csv_->isEnabled()&&calculate_->isEnabled());
                require(alternatives_&&alternatives_->surface.status==SearchStatus::found);
                route_selection_->setCurrentIndex(0);require(result_&&result_->route.size()==original->route.size());
                route_selection_->setCurrentIndex(1);calculate_->click();*phase=2;
            }else if(*phase==2){
                require(result_&&result_->estimate->validation.checked_constraints_pass());*alternative=*result_;
                const auto c=configuration();require(c.parameters.mode==DesignMode::earthwork&&c.parameters.max_grade_percent==2);
                require(result_->estimate->validation.max_cut_m<=2&&result_->estimate->validation.max_fill_m<=2);
                require(result_->estimate->validation.metrics.horizontal_m<original->estimate->validation.metrics.horizontal_m);
                require(option_diagnostics_[1].contains("Reducción frente a opción 1:"));write("option-2");
                require(grab().save(QDir(output).filePath("option-2.png")));
                if(auto* panel=findChild<QScrollArea*>())panel->ensureWidgetVisible(result_status_);
                require(grab().save(QDir(output).filePath("comparison.png")));
                route_selection_->setCurrentIndex(0);require(result_&&result_->route.size()==original->route.size());
                require(configuration().parameters.mode==DesignMode::surface);
                route_selection_->setCurrentIndex(1);require(result_&&result_->route.size()==alternative->route.size());
                calculate_->click();process_cancel_->click();require(stop_.stop_requested());*phase=3;
            }else if(*phase==3){
                require(result_&&alternatives_->surface.status==SearchStatus::found&&alternatives_->earthwork);
                advanced_action_->trigger();advanced_tabs_->setCurrentIndex(0);*phase=4;
            }else if(*phase==4){
                require(advanced_dialog_->grab().save(QDir(output).filePath("limits.png")));advanced_dialog_->close();
                QFile file(QDir(output).filePath("option-2.json"));require(file.open(QIODevice::ReadOnly));
                auto project=read_viewer_project(file.readAll(),file.fileName());load(project.terrain_path,project);*phase=5;
            }else if(*phase==5){
                require(result_&&route_selection_->currentIndex()==1&&configuration().parameters.mode==DesignMode::earthwork);
                require(result_->route.size()==alternative->route.size());
                route_selection_->setCurrentIndex(0);require(!result_&&!csv_->isEnabled());
                calculate_->click();*phase=6;
            }else if(*phase==6){
                require(result_&&result_->route.size()==original->route.size());route_selection_->setCurrentIndex(1);
                require(result_&&option_diagnostics_[1].contains("Reducción frente a opción 1:"));
                fields_.at("max_cut_m")->setValue(0);require(!result_&&!alternatives_&&!csv_->isEnabled());
                calculate_->click();*phase=7;
            }else{
                require(result_&&result_->estimate->validation.checked_constraints_pass());
                require(result_->estimate->validation.max_cut_m<1e-8&&result_->estimate->validation.max_fill_m<=2);
                write("zero-cut");
                std::cout<<"proposals=passed selectable before planning, surface without earthwork, independent cut/fill, same grade/radius, shorter alternative, cached switching, cancellation preserves both, selected-mode exports/reload, parameter invalidation, zero cut ban\n";
                QCoreApplication::exit(0);
            }
        }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
    });
    poll->start(200);QTimer::singleShot(60000,this,[this]{error("Proposal GUI smoke timed out");});
}
void RouteWindow::capture_project(const QString& project,const QString& output){
    testing_=true;QDir().mkpath(output);open_project_file(project);auto* poll=new QTimer(this);auto phase=std::make_shared<int>(0);
    connect(poll,&QTimer::timeout,this,[this,output,phase]{if(busy_)return;try{
        if(!result_)throw std::runtime_error("Project capture has no validated route");
        if(*phase==0){view_->camera_view("TOP");*phase=1;}
        else if(*phase==1){if(!grab().save(QDir(output).filePath("route-top.png")))throw std::runtime_error("Capture failed");view_->camera_view("ISO");*phase=2;}
        else {if(!grab().save(QDir(output).filePath("route-iso.png")))throw std::runtime_error("Capture failed");save_viewer_file(QDir(output).filePath("validated-project.json"),write_viewer_project(*result_config_,&*result_),{terrain_path_,project_path_});std::cout<<"project_capture=passed independent validation, classification, TOP and ISO\n";QCoreApplication::exit(0);}
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}});poll->start(300);QTimer::singleShot(90000,this,[this]{error("Project capture timed out");});
}
void RouteWindow::capture_guide(const QString& project,const QString& output){
    testing_=true;resize(1400,900);QDir().mkpath(output);
    auto phase=std::make_shared<int>(-1);auto poll=new QTimer(this);auto captured_load=std::make_shared<bool>(false);
    auto save=[this,output](QWidget* widget,const QString& name){if(!widget->grab().save(QDir(output).filePath(name+".png")))throw std::runtime_error("Guide capture failed");};
    connect(poll,&QTimer::timeout,this,[this,project,output,phase,captured_load,save]{try{
        if(*phase==-1){save(this,"01_inicio");open_project_file(project);save(process_dialog_,"02_progreso_carga");minimize_process();save(this,"03_progreso_minimizado");*captured_load=true;*phase=0;return;}
        if(busy_){if(!*captured_load){save(process_dialog_,"02_progreso_carga");minimize_process();save(this,"03_progreso_minimizado");*captured_load=true;}return;}
        if(!result_||!result_config_)throw std::runtime_error("Guide requires a validated project");
        auto capture_menu=[&](const char* object,const QString& name){auto* menu=findChild<QMenu*>(object);if(!menu)throw std::runtime_error("Missing guide menu");menu->popup(menuBar()->mapToGlobal(QPoint(0,menuBar()->height())));QApplication::processEvents();save(menu,name);menu->hide();};
        switch((*phase)++){
        case 0:view_->camera_view("ISO");break;
        case 1:save(this,"04_ruta_iso");save(sidebar_,"05_panel_controles");view_->camera_view("TOP");break;
        case 2:save(this,"06_ruta_superior");save(view_,"07_leyenda_y_ruta");capture_menu("fileMenu","08_menu_archivo");break;
        case 3:capture_menu("settingsMenu","09_menu_configuracion");show_settings(design_dialog_);break;
        case 4:save(design_dialog_,"10_diseno");design_dialog_->hide();advanced_tabs_->setCurrentIndex(0);show_settings(advanced_dialog_);break;
        case 5:save(advanced_dialog_,"11_avanzado_diseno");advanced_tabs_->setCurrentIndex(1);break;
        case 6:save(advanced_dialog_,"12_avanzado_precision");advanced_tabs_->setCurrentIndex(2);break;
        case 7:save(advanced_dialog_,"13_avanzado_recursos");advanced_tabs_->setCurrentIndex(3);applying_=true;cost_profile_->setCurrentIndex(cost_profile_->findData("custom"));applying_=false;update_controls();break;
        case 8:{save(advanced_dialog_,"14_avanzado_costos");if(auto* scroll=qobject_cast<QScrollArea*>(advanced_tabs_->currentWidget()))scroll->ensureWidgetVisible(fields_.at("disposal_haul_km"));QApplication::processEvents();save(advanced_dialog_,"14b_costos_acarreos");if(auto* scroll=qobject_cast<QScrollArea*>(advanced_tabs_->currentWidget()))scroll->ensureWidgetVisible(cost_profile_);QApplication::processEvents();cost_profile_->showPopup();QApplication::processEvents();save(cost_profile_->view()->window(),"14c_perfiles_costos");cost_profile_->hidePopup();applying_=true;cost_profile_->setCurrentIndex(cost_profile_->findData(QString::fromStdString(result_config_->cost_profile)));applying_=false;update_controls();advanced_dialog_->hide();configure_local();break;}
        case 9:{auto* d=findChild<QDialog*>("intermediateConfigurationDialog");save(d,"15_configuracion_intermedios");d->findChild<QCheckBox*>("enableIntermediatePoints")->setChecked(true);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();break;}
        case 10:set_local_point(0,result_->route.size()/4);set_local_point(1,3*result_->route.size()/4);break;
        case 11:save(this,"16_puntos_p1_p2");save(local_panel_,"17_cuatro_botones_intermedios");configure_local();break;
        case 12:{auto* d=findChild<QDialog*>("intermediateConfigurationDialog");save(d,"18_parametros_tramo");d->close();capture_menu("viewsMenu","19_menu_vistas");break;}
        case 13:capture_menu("helpMenu","20_menu_ayuda");show_diagnostics();break;
        case 14:{auto* d=findChild<QDialog*>("routeDiagnosticDialog");save(d,"21_detalles_resultado");d->close();view_->camera_view("FRONT");break;}
        case 15:save(this,"22_vista_frontal");view_->camera_view("ISO");break;
        case 16:{
            save_viewer_file(QDir(output).filePath("Ejemplo_RouteTec.json"),write_viewer_project(*result_config_,&*result_,ui_state()),{terrain_path_,project_path_});
            for(bool dxf:{false,true}){std::ostringstream stream;if(result_config_->parameters.mode==DesignMode::earthwork){if(dxf)write_classified_route_dxf(stream,*terrain_,result_->route,result_config_->parameters.surface_tolerance_m);else write_classified_route_csv(stream,*terrain_,result_->route,result_config_->parameters.surface_tolerance_m);}else if(dxf)write_route_dxf(stream,result_->route,result_->interventions,false);else write_route_csv(stream,result_->route,result_->interventions,false);save_viewer_file(QDir(output).filePath(dxf?"Ejemplo_Eje.dxf":"Ejemplo_Ruta.csv"),QByteArray::fromStdString(stream.str()),{terrain_path_,project_path_});}
            auto* dialog=new QFileDialog(this,"Guardar proyecto");dialog->setObjectName("guideSaveDialog");dialog->setOption(QFileDialog::DontUseNativeDialog);dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setDirectory(output);dialog->setNameFilter("Proyecto RouteTec (*.json)");dialog->selectFile("Mi_proyecto.json");dialog->show();break;}
        case 17:{auto* d=findChild<QFileDialog*>("guideSaveDialog");save(d,"23_guardar_proyecto");d->close();auto* dialog=new QFileDialog(this,"Exportar ruta CSV");dialog->setObjectName("guideCsvDialog");dialog->setOption(QFileDialog::DontUseNativeDialog);dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setDirectory(output);dialog->setNameFilter("Ruta CSV (*.csv)");dialog->selectFile("Mi_ruta.csv");dialog->show();break;}
        case 18:{auto* d=findChild<QFileDialog*>("guideCsvDialog");save(d,"24_exportar_csv");d->close();auto* dialog=new QFileDialog(this,"Exportar eje DXF");dialog->setObjectName("guideDxfDialog");dialog->setOption(QFileDialog::DontUseNativeDialog);dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setDirectory(output);dialog->setNameFilter("Eje DXF (*.dxf)");dialog->selectFile("Mi_eje.dxf");dialog->show();break;}
        case 19:{auto* d=findChild<QFileDialog*>("guideDxfDialog");save(d,"25_exportar_dxf");d->close();auto* dialog=new QFileDialog(this,"Cargar topografía");dialog->setObjectName("guideLoadDialog");dialog->setOption(QFileDialog::DontUseNativeDialog);dialog->setDirectory(QFileInfo(terrain_path_).absolutePath());dialog->setNameFilter("Topografía (*.dxf *.obj)");dialog->show();break;}
        case 20:{auto* d=findChild<QFileDialog*>("guideLoadDialog");save(d,"26_cargar_topografia");d->close();std::cout<<"guide_capture=passed 26 application views, settings, local points and verified exports\n";QCoreApplication::exit(0);break;}
        }
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}});poll->start(350);QTimer::singleShot(120000,this,[this]{error("Guide capture timed out");});
}
void RouteWindow::smoke_features(const QString& terrain,const QString& output){
    testing_=true;QDir().mkpath(output);load(terrain);auto phase=std::make_shared<int>(0);auto seed=std::make_shared<SearchResult>();auto poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,output,phase,seed]{if(busy_)return;try{
        auto require=[&](bool value){if(!value)throw std::runtime_error("Feature GUI assertion failed at phase "+std::to_string(*phase));};
        auto capture=[&](const QString& name){require(grab().save(QDir(output).filePath(name+".png")));};
        if(*phase==0){
            require(bool(terrain_));fields_.at("min_radius_m")->setValue(50);set_point(0,{-20,0,0});set_point(1,{300,-80,0});auto config=configuration();
            SearchResult r;r.strategy="continuous_ramp";r.status=SearchStatus::found;r.effective_xy_spacing_m=10;r.effective_heading_step_degrees=7.5;r.route={{-20,0,0},{0,0,0}};
            double heading=0;for(double radius:{50.,70.,90.,110.}){auto a=r.route.back();Vec3 center{a.x-radius*std::sin(heading),a.y+radius*std::cos(heading),0};auto first=r.route.size()-1;
                for(int i=1;i<=90;++i){double h=heading+i*std::numbers::pi/180;r.route.push_back({center.x+radius*std::sin(h),center.y-radius*std::cos(h),0});}r.curves.push_back({first,r.route.size()-1,center,90});heading+=std::numbers::pi/2;
            }r.route.push_back({200,-80,0});r.route.push_back({300,-80,0});r.estimate=estimate_sectioned_route(r,*terrain_,config.parameters,config.costs);*seed=r;
            alternatives_.emplace();alternatives_->surface=r;option_configs_[0]=config;choose_route(0);view_->camera_view("TOP");
            local_tools_enabled_=true;update_controls();require(local_action_->isEnabled());
            auto* canvas=findChild<QVTKOpenGLNativeWidget*>();auto* renderer=canvas->renderWindow()->GetRenderers()->GetFirstRenderer();auto bounds=terrain_->bounds();Vec3 origin{std::midpoint(bounds[0].x,bounds[1].x),std::midpoint(bounds[0].y,bounds[1].y),bounds[0].z};
            for(int i=0;i<2;++i){auto target=to_local(r.route[i?r.route.size()-2:1],origin);renderer->SetWorldPoint(target.x,target.y,target.z,1);renderer->WorldToDisplay();auto* display=renderer->GetDisplayPoint();
                local_pick_[i]->click();auto* interactor=canvas->renderWindow()->GetInteractor();interactor->SetEventPosition(static_cast<int>(std::lround(display[0])),static_cast<int>(std::lround(display[1])));interactor->InvokeEvent(vtkCommand::LeftButtonPressEvent);require(local_points_[i].has_value());
            }
            local_action_->trigger();auto* dialog=findChild<QDialog*>("intermediateConfigurationDialog");require(dialog&&dialog->isVisible());
            dialog->findChild<QSpinBox*>("localFirstIndex")->setValue(2);dialog->findChild<QSpinBox*>("localLastIndex")->setValue(static_cast<int>(r.route.size()-1));
            dialog->findChild<QDoubleSpinBox*>("localGrade")->setValue(12);dialog->findChild<QDoubleSpinBox*>("localRadius")->setValue(30);dialog->findChild<QDoubleSpinBox*>("localStraight")->setValue(35);
            require(dialog->grab().save(QDir(output).filePath("intermediate-configuration.png")));dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
            capture("selected-section");require(local_optimize_->isEnabled());local_optimize_->click();*phase=1;
        }else if(*phase==1){
            require(result_&&result_->local_sections.size()==1&&result_->estimate->total<seed->estimate->total);
            require(configuration().parameters.min_radius_m==50&&result_config_->parameters.min_radius_m==50);
            auto bytes=write_viewer_project(*result_config_,&*result_);save_viewer_file(QDir(output).filePath("local-project.json"),bytes,{terrain_path_});capture("optimized-section");
            load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("local-project.json")));*phase=2;
        }else if(*phase==2){
            require(result_&&result_->local_sections.size()==1);capture("reopened-local");auto config=configuration();config.parameters.mode=DesignMode::earthwork;auto colored=*seed;double station=0,total=seed->estimate->validation.metrics.horizontal_m;
            for(std::size_t i=0;i<colored.route.size();++i){if(i)station+=std::hypot(colored.route[i].x-colored.route[i-1].x,colored.route[i].y-colored.route[i-1].y);colored.route[i].z=1.5*std::sin(2*std::numbers::pi*station/total);}colored.route.front().z=colored.route.back().z=0;
            colored.estimate=estimate_sectioned_route(colored,*terrain_,config.parameters,config.costs);alternatives_->earthwork=colored;option_configs_[1]=config;choose_route(1);view_->camera_view("TOP");
            auto segments=earthwork_segments(*terrain_,colored.route);bool cut=false,fill=false,normal=false;for(auto s:segments){cut|=s.kind==EarthworkKind::cut;fill|=s.kind==EarthworkKind::fill;normal|=s.kind==EarthworkKind::surface;}require(cut&&fill&&normal);
            *phase=3;
        }else if(*phase==3){
            capture("cut-fill-colors");std::ostringstream csv,dxf;write_classified_route_csv(csv,*terrain_,result_->route);write_classified_route_dxf(dxf,*terrain_,result_->route);save_viewer_file(QDir(output).filePath("classified.csv"),QByteArray::fromStdString(csv.str()),{terrain_path_});save_viewer_file(QDir(output).filePath("classified.dxf"),QByteArray::fromStdString(dxf.str()),{terrain_path_});
            auto strip=QFileInfo(terrain_path_).dir().filePath("adjustment-strip.obj");load(strip);*phase=4;
        }else if(*phase==4){
            set_point(0,{0,0,0});set_point(1,{100,0,11});budgets_.at("max_states")->setValue(200);budgets_.at("max_expansions")->setValue(100);budgets_.at("max_edges")->setValue(800);adaptive_->setChecked(false);route_selection_->setCurrentIndex(0);calculate(false,false);*phase=5;
        }else if(*phase==5){
            require(!result_&&details_action_->isEnabled());details_action_->trigger();auto* dialog=findChild<QDialog*>("routeDiagnosticDialog");require(dialog&&dialog->isVisible());require(dialog->grab().save(QDir(output).filePath("failure-diagnostic.png")));dialog->close();calculate_->click();*phase=6;
        }else if(*phase==6){
            require(result_&&result_->strategy=="surface_reference"&&result_config_->parameters.max_grade_percent>=10&&configuration().parameters.max_grade_percent==10);require(result_->estimate->validation.max_cut_m<1e-8&&result_->estimate->validation.max_fill_m<1e-8);
            capture("adjusted-surface");route_selection_->setCurrentIndex(1);calculate(true);*phase=7;
        }else if(*phase==7){
            require(result_&&result_->strategy=="surface_reference"&&result_config_->parameters.mode==DesignMode::earthwork);capture("adjusted-earthwork");
            auto bytes=write_viewer_project(*result_config_,&*result_);save_viewer_file(QDir(output).filePath("adjusted-project.json"),bytes,{terrain_path_});load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("adjusted-project.json")));*phase=8;
        }else{
            require(result_&&result_config_->parameters.max_grade_percent>=10&&result_->strategy=="surface_reference");
            std::cout<<"features=passed route picking, intermediate configuration, local optimization, scoped validation/reload, cut/fill colors/exports, failure diagnostic, adjustments in both modes, effective parameters/reload\n";QCoreApplication::exit(0);
        }
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}});poll->start(200);QTimer::singleShot(90000,this,[this]{error("Feature GUI timed out");});
}
void RouteWindow::smoke_search(const QString& terrain,const Settings& values,const QString& output){
    testing_=true;QDir().mkpath(output);load(terrain);
    auto* poll=new QTimer(this);auto phase=std::make_shared<int>(0);auto captured=std::make_shared<bool>(false);
    connect(poll,&QTimer::timeout,this,[this,values,output,phase,captured,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Live search smoke assertion failed");};
        if(*phase==0){if(busy_)return;require(bool(terrain_));apply_settings(values);view_->camera_view("TOP");calculate();minimize_process();*phase=1;return;}
        if(busy_){
            update_process();
            if(!*captured){require(grab().save(QDir(output).filePath("search-live.png")));*captured=true;}
            return;
        }
        require(result_&&result_->estimate->validation.checked_constraints_pass());
        require(grab().save(QDir(output).filePath("search-complete.png")));
        auto* toggle=findChild<QAction*>("showSearchPaths");require(toggle&&toggle->isChecked());toggle->setChecked(false);
        require(grab().save(QDir(output).filePath("search-hidden.png")));toggle->setChecked(true);
        require(grab().save(QDir(output).filePath("search-restored.png")));
        std::cout<<"search_smoke=passed live worker updates, complete route, visibility toggle, final classification\n";
        poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}});
    poll->start(100);QTimer::singleShot(120000,this,[this]{error("Live search smoke timed out");});
}

void RouteWindow::smoke_corridor(const QString& terrain,const Settings& values,const QString& output){
    testing_=true;QDir().mkpath(output);load(terrain);auto phase=std::make_shared<int>(0);auto saved=std::make_shared<std::vector<Vec3>>();auto* poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,values,output,phase,saved,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Surface corridor workflow smoke failed");};
        if(busy_)return;
        if(*phase==0){require(bool(terrain_));apply_settings(values);route_selection_->setCurrentIndex(0);view_->camera_view("TOP");calculate_->click();*phase=1;return;}
        if(*phase==1){require(result_&&result_->strategy=="surface_reference"&&result_config_->parameters.mode==DesignMode::surface);
            require(view_->corridor_is_preliminary()&&design_reviews_[0]&&validate_design_review(*terrain_,design_reviews_[0]->review,design_reviews_[0]->parameters));
            require(!result_config_->parameters.continuous_curves&&result_->estimate->validation.max_cut_m<1e-6&&result_->estimate->validation.max_fill_m<1e-6);
            *saved=result_->route;require(grab().save(QDir(output).filePath("corridor-base.png")));
            save_viewer_file(QDir(output).filePath("corridor-base.json"),write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]),{terrain_path_});
            route_selection_->setCurrentIndex(1);calculate_->click();*phase=2;return;}
        if(*phase==2){require(result_&&result_->strategy=="surface_reference"&&result_config_->parameters.mode==DesignMode::earthwork&&alternatives_->surface.route.size()==saved->size());
            for(std::size_t i=0;i<saved->size();++i){auto a=(*saved)[i],b=alternatives_->surface.route[i];require(a.x==b.x&&a.y==b.y&&a.z==b.z);}
            require(result_->estimate->total<=alternatives_->surface.estimate->total+1e-6);
            require(grab().save(QDir(output).filePath("corridor-optimized.png")));
            require(design_reviews_[1]&&validate_design_review(*terrain_,design_reviews_[1]->review,design_reviews_[1]->parameters));
            auto bytes=write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[1]);save_viewer_file(QDir(output).filePath("corridor-optimized.json"),bytes,{terrain_path_});
            load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("corridor-optimized.json")));*phase=3;return;}
        require(result_&&result_->strategy=="surface_reference"&&!result_config_->parameters.continuous_curves&&design_reviews_[1]&&view_->corridor_is_preliminary());require(grab().save(QDir(output).filePath("corridor-reloaded.png")));
        std::cout<<"corridor_workflow=passed surface base, pending curves, exact reuse, improvement or fallback, save/reload\n";poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}});
    poll->start(150);QTimer::singleShot(150000,this,[this]{error("Surface corridor workflow timed out");});
}

void RouteWindow::smoke_guide_fallback(const QString& output){
    testing_=true;exercise_candidates_=true;
    if(!QDir().mkpath(output)){error("Cannot create guide fallback output");return;}
    const auto terrain_path=QDir(output).filePath("disconnected.obj");
    save_viewer_file(terrain_path,"v 0 0 0\nv 200 0 0\nv 0 200 0\nv 200 200 0\nv 300 0 0\nv 500 0 0\nv 300 200 0\nv 500 200 0\nf 1 2 3\nf 2 4 3\nf 5 6 7\nf 6 8 7\n",{});
    load(terrain_path);auto phase=std::make_shared<int>(0);auto* poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,output,phase,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Guide fallback workflow failed");};
        if(*phase==0){if(busy_)return;require(bool(terrain_));
            auto values=routetec_interface_defaults();for(const auto& [key,value]:Settings{{"start_x","20"},{"start_y","100"},{"start_z","0"},{"finish_x","480"},{"finish_y","100"},{"finish_z","0"},{"max_states","80"},{"max_expansions","20"},{"max_edges","200"}})values[key]=value;
            apply_settings(values);view_->camera_view("TOP");calculate_->click();minimize_process();*phase=1;return;}
        if(*phase==1){require(busy_);if(!continuous_search_||search_round_<4||!design_reviews_[0]||design_reviews_[0]->branches.empty())return;
            require(result_&&result_->strategy=="candidate_guide"&&result_->status==SearchStatus::no_path&&!result_->estimate);
            require(!design_reviews_[0]->review.connected&&view_->candidate_view_active()&&!view_->has_visible_raw_route());
            require(grab().save(QDir(output).filePath("guide-independent-live.png")));cancel_->click();*phase=2;return;}
        if(*phase==2){if(busy_)return;require(result_&&result_->strategy=="candidate_guide"&&!result_->estimate&&design_reviews_[0]&&!design_reviews_[0]->branches.empty());
            const auto bytes=write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]);
            require(QJsonDocument::fromJson(bytes).object()["route"].isNull());
            save_viewer_file(QDir(output).filePath("guide-independent.json"),bytes,{terrain_path_});
            load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("guide-independent.json")));*phase=3;return;}
        if(busy_)return;require(result_&&result_->strategy=="candidate_guide"&&result_->status==SearchStatus::no_path&&!result_->estimate&&design_reviews_[0]&&!design_reviews_[0]->branches.empty());
        require(!design_reviews_[0]->review.connected&&view_->candidate_view_active()&&!view_->has_visible_raw_route());
        require(grab().save(QDir(output).filePath("guide-independent-reloaded.png")));
        std::cout<<"guide_fallback=passed no valid A/B reference, independent exploration, unrendered/unexported guide, stop, route:null save and reload\n";poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){stop_.request_stop();error(QString::fromUtf8(e.what()));}});
    poll->start(150);QTimer::singleShot(120000,this,[this]{stop_.request_stop();error("Guide fallback workflow timed out");});
}

void RouteWindow::smoke_gray_candidates(const QString& terrain,const Settings& values,const QString& output){
    testing_=true;exercise_candidates_=true;QDir().mkpath(output);load(terrain);
    auto phase=std::make_shared<int>(0);auto first_round=std::make_shared<std::size_t>(0);
    auto first_count=std::make_shared<std::size_t>(0);auto latency=std::make_shared<qint64>(0);auto* poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,values,output,phase,first_round,first_count,latency,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Gray candidate workflow failed");};
        if(*phase==0){if(busy_)return;require(bool(terrain_));apply_settings(values);view_->camera_view("TOP");calculate_->click();minimize_process();*phase=1;return;}
        if(*phase==1){require(busy_);if(!continuous_search_||!view_->has_visible_gray_exploration())return;
            require(result_&&result_->strategy=="candidate_guide"&&!result_->estimate&&design_reviews_[0]&&design_reviews_[0]->road_candidates);
            require(progress_->maximum()==0&&save_->isEnabled()&&!view_->has_visible_raw_route());
            *latency=elapsed_.elapsed();require(*latency<15000);*first_round=search_round_;*first_count=view_->search_history_segments();
            const auto bytes=write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]);
            require(QJsonDocument::fromJson(bytes).object()["route"].isNull()&&read_viewer_project(bytes,QDir(output).filePath("gray-live.json")).design_review.has_value());
            save_viewer_file(QDir(output).filePath("gray-live.json"),bytes,{terrain_path_});
            require(grab().save(QDir(output).filePath("gray-first.png")));*phase=2;return;}
        if(*phase==2){require(busy_&&continuous_search_);if(search_round_<*first_round+3||elapsed_.elapsed()<*latency+3000)return;
            require(view_->has_visible_gray_exploration()&&view_->search_history_segments()>=*first_count);
            auto* toggle=findChild<QAction*>("showSearchPaths");require(toggle&&toggle->isChecked());toggle->setChecked(false);require(!view_->has_visible_gray_exploration());
            toggle->setChecked(true);require(view_->has_visible_gray_exploration()&&!view_->has_visible_raw_route());
            require(grab().save(QDir(output).filePath("gray-more.png")));cancel_->click();*phase=3;return;}
        if(busy_)return;require(view_->has_visible_gray_exploration()&&view_->search_history_segments()>=*first_count&&design_reviews_[0]);
        require(grab().save(QDir(output).filePath("gray-stopped.png")));
        std::cout<<"gray_candidates=passed immediate guide, progressive gray footprint, continued rounds, visibility toggle, stop retains exploration; first_gray_ms="<<*latency<<" rounds="<<search_round_<<" segments="<<view_->search_history_segments()<<'\n';poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){stop_.request_stop();error(QString::fromUtf8(e.what()));}});
    poll->start(100);QTimer::singleShot(120000,this,[this]{stop_.request_stop();error("Gray candidate workflow timed out");});
}

void RouteWindow::smoke_candidates(const QString& terrain,const Settings& values,const QString& output){
    testing_=true;exercise_candidates_=true;QDir().mkpath(output);load(terrain);auto phase=std::make_shared<int>(0);auto count=std::make_shared<std::size_t>(0);auto* poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,values,output,phase,count,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Unified road candidate workflow failed");};
        if(*phase==0){if(busy_)return;require(bool(terrain_)&&route_selection_->isHidden());apply_settings(values);view_->camera_view("TOP");calculate_->click();minimize_process();*phase=1;return;}
        if(*phase==1){require(busy_);if(!continuous_search_||search_round_<8||!design_reviews_[0]||design_reviews_[0]->branches.size()<3)return;
            const auto& saved=*design_reviews_[0];require(saved.road_candidates&&saved.parameters.mode==DesignMode::earthwork&&saved.max_intervention_length_m==100);
            require(view_->candidate_view_active()&&!view_->has_visible_raw_route()&&save_->isEnabled());RoadCandidateLimits limits;limits.max_intervention_length_m=100;
            bool curves=false;for(const auto& branch:saved.branches){require(validate_road_candidate(*terrain_,branch,saved.parameters,limits));curves|=!branch.curves.empty();}require(curves);
            const auto colors=view_->candidate_cell_counts();require(colors[0]+colors[1]+colors[2]>0);*count=saved.branches.size();
            require(grab().save(QDir(output).filePath("road-alternatives-live.png")));
            save_viewer_file(QDir(output).filePath("road-alternatives-live.json"),write_viewer_project(*result_config_,&*result_,{},&saved),{terrain_path_});cancel_->click();*phase=2;return;}
        if(*phase==2){if(busy_)return;require(result_&&design_reviews_[0]&&design_reviews_[0]->branches.size()>=*count&&view_->candidate_view_active());
            auto bytes=write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]);save_viewer_file(QDir(output).filePath("road-alternatives.json"),bytes,{terrain_path_});
            require(grab().save(QDir(output).filePath("road-alternatives-stopped.png")));load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("road-alternatives.json")));*phase=3;return;}
        if(busy_)return;require(design_reviews_[0]&&design_reviews_[0]->road_candidates&&design_reviews_[0]->branches.size()>=*count&&view_->candidate_view_active());
        require(intervention_length_->value()==100&&route_selection_->isHidden());require(grab().save(QDir(output).filePath("road-alternatives-reloaded.png")));
        std::cout<<"road_candidates=passed curved alternatives, unified hiddenselector, no rays, strict validation, live save, stop and reload; branches="<<design_reviews_[0]->branches.size()<<'\n';poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){stop_.request_stop();error(QString::fromUtf8(e.what()));}});
    poll->start(150);QTimer::singleShot(180000,this,[this]{stop_.request_stop();error("Road candidate workflow timed out");});
}
void RouteWindow::smoke_candidate_colors(const QString& output){
    testing_=true;try{auto require=[](bool ok){if(!ok)throw std::runtime_error("Candidate colors workflow failed");};require(QDir().mkpath(output));
        auto terrain=std::make_shared<Terrain>(std::vector<Vec3>{{0,0,0},{50,0,3},{100,0,0},{0,100,0},{50,100,3},{100,100,0},{150,0,7.5},{150,100,7.5}},std::vector<std::array<std::size_t,3>>{{0,1,3},{1,4,3},{1,2,4},{2,5,4},{2,6,5},{6,7,5}});
        view_->set_terrain(terrain);view_->camera_view("TOP");Parameters p;p.mode=DesignMode::earthwork;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;p.max_cut_m=p.max_fill_m=4;
        SearchResult surface,cut,fill;surface.status=cut.status=fill.status=SearchStatus::found;
        surface.route={{0,50,0},{50,50,3},{100,50,0}};cut.route={{0,10,0},{50,10,0},{100,10,0}};fill.route={{0,90,1},{50,90,4},{100,90,1}};
        std::vector<SearchResult> branches{surface,cut,fill};RoadCandidateLimits limits;for(const auto& route:branches)require(validate_road_candidate(*terrain,route,p,limits));
        view_->set_exploration_grade_limit(p.max_grade_percent);view_->set_candidate_review({},0.01,branches);const auto counts=view_->candidate_cell_counts();require(counts[0]>0&&counts[1]==0&&counts[2]==0);
        SearchTrace trace;std::vector<Vec3> ascent{{0,20,0},{50,20,3},{100,20,0},{150,20,7.5}},descent{{150,80,7.5},{100,80,0},{50,80,3},{0,80,0}};
        require(validate_grade_trace(*terrain,ascent,2*p.max_grade_percent)&&validate_grade_trace(*terrain,descent,2*p.max_grade_percent));
        trace.record(ascent,false);trace.record(descent,false);view_->append_search(trace.take());const auto exploration=view_->exploration_cell_counts();require(exploration[0]>0&&exploration[1]>0&&exploration[2]>0);
        view_->set_route(surface.route);view_->show_search(true);require(view_->candidate_view_active()&&!view_->has_visible_raw_route());
        view_->show_search(false);require(!view_->has_visible_gray_exploration()&&view_->has_visible_route());view_->show_search(true);require(view_->has_visible_gray_exploration());
        require(grab().save(QDir(output).filePath("exploration-grade-colors.png")));std::cout<<"candidate_colors=passed gray exploration, red uphill excess, green downhill excess, verified cyan including cut/fill, independent cells and suppressed reference/rays\n";QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::smoke_continuous(const QString& terrain,const Settings& values,const QString& output){
    testing_=true;exercise_continuous_=true;QDir().mkpath(output);load(terrain);auto phase=std::make_shared<int>(0);auto first_round=std::make_shared<std::size_t>(0);auto* poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this,values,output,phase,first_round,poll]{try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Continuous search workflow smoke failed");};
        if(*phase==0){if(busy_)return;require(bool(terrain_));apply_settings(values);route_selection_->setCurrentIndex(0);view_->camera_view("TOP");calculate_->click();minimize_process();*phase=1;return;}
        if(*phase==1){require(busy_);if(!continuous_search_||search_round_<1)return;require(result_&&result_config_&&design_reviews_[0]);
            require(progress_->minimum()==0&&progress_->maximum()==0&&save_->isEnabled()&&details_action_->isEnabled());
            require(view_->corridor_is_preliminary());require(grab().save(QDir(output).filePath("continuous-first-round.png")));*first_round=search_round_;*phase=2;return;}
        if(*phase==2){require(busy_&&continuous_search_);if(search_round_<*first_round+3)return;
            require(validate_design_review(*terrain_,design_reviews_[0]->review,design_reviews_[0]->parameters));
            save_viewer_file(QDir(output).filePath("continuous-live.json"),write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]),{terrain_path_});
            require(grab().save(QDir(output).filePath("continuous-later-round.png")));cancel_->click();*phase=3;return;}
        if(*phase==3){if(busy_)return;require(result_&&design_reviews_[0]&&!continuous_search_&&view_->search_history_segments()>0);
            require(validate_design_review(*terrain_,design_reviews_[0]->review,design_reviews_[0]->parameters));require(grab().save(QDir(output).filePath("continuous-stopped.png")));
            auto bytes=write_viewer_project(*result_config_,&*result_,{},&*design_reviews_[0]);save_viewer_file(QDir(output).filePath("continuous-stopped.json"),bytes,{terrain_path_});
            load(terrain_path_,read_viewer_project(bytes,QDir(output).filePath("continuous-stopped.json")));*phase=4;return;}
        if(busy_)return;require(result_&&design_reviews_[0]&&view_->corridor_is_preliminary());
        require(validate_design_review(*terrain_,design_reviews_[0]->review,design_reviews_[0]->parameters));
        std::cout<<"continuous_workflow=passed multiple ongoing rounds, indeterminate progress, live save, cancel retains geometry/history, reload\n";poll->stop();QCoreApplication::exit(0);
    }catch(const std::exception& e){stop_.request_stop();error(QString::fromUtf8(e.what()));}});
    poll->start(150);QTimer::singleShot(180000,this,[this]{stop_.request_stop();error("Continuous search workflow timed out");});
}

void RouteWindow::smoke_verified_history(const QString& output){
    testing_=true;try{auto require=[](bool ok){if(!ok)throw std::runtime_error("Verified history workflow failed");};require(QDir().mkpath(output));
        auto terrain=std::make_shared<Terrain>(std::vector<Vec3>{{0,0,0},{500,0,0},{0,2000,0},{500,2000,0}},std::vector<std::array<std::size_t,3>>{{0,1,2},{1,3,2}});
        view_->set_terrain(terrain);view_->camera_view("TOP");view_->set_candidate_review({});SearchTrace trace;Parameters p;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;RoadCandidateLimits limits;
        for(int i=0;i<300;++i){SearchResult candidate;candidate.status=SearchStatus::found;candidate.route={{20,10+5.*i,0},{120,10+5.*i,0}};require(validate_road_candidate(*terrain,candidate,p,limits));trace.record(candidate.route,true);}
        auto first=trace.take();require(first.complete.size()==16&&first.verified_footprint.size()==300);view_->append_search(std::move(first));
        require(view_->verified_history_segments()==300&&view_->has_visible_verified_exploration()&&!view_->has_visible_raw_route());
        std::vector<Vec3> later{{20,1950,0},{120,1950,0}};require(validate_grade_trace(*terrain,later,10));trace.record(later,true);view_->append_search(trace.take());
        require(view_->verified_history_segments()==301&&view_->has_visible_verified_exploration());
        view_->show_search(false);require(!view_->has_visible_verified_exploration());view_->show_search(true);require(view_->has_visible_verified_exploration()&&view_->verified_history_segments()==301);
        require(grab().save(QDir(output).filePath("verified-history-301.png")));view_->clear_search();require(view_->verified_history_segments()==0&&!view_->has_visible_verified_exploration());
        std::cout<<"verified_history=passed 301 checked paths, older and newer regions retained beyond256 and recent16, visibility and clear\n";QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::smoke_design_standards(const QString& output){
    testing_=true;
    try{
        auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(std::string("Design standards UI smoke failed: ")+reason);};require(QDir().mkpath(output),"output");
        require(standards_mode_->currentData().toString()=="preliminary"&&fields_.at("design_speed_kmh")->value()==20,"desktop defaults");
        require(design_ice_risk_->isChecked()&&design_terrain_->currentData().toString()=="steep"&&fields_.at("design_altitude_m")->value()==0,"context defaults");
        terrain_=std::make_shared<Terrain>(std::vector<Vec3>{{0,0,3500},{500,0,3500},{0,500,3500},{500,500,3500}},std::vector<std::array<std::size_t,3>>{{0,1,2},{1,3,2}});terrain_path_="standards-smoke.obj";view_->set_terrain(terrain_);set_point(0,{10,10,3500});set_point(1,{400,400,3500});refresh_design_criteria();
        require(design_context().altitude_m==3500&&configuration().parameters.min_radius_m==30,"automatic altitude and baseline");
        fields_.at("design_speed_kmh")->setValue(40);const auto raised=configuration();
        require(raised.parameters.min_radius_m>30&&fields_.at("min_radius_m")->value()>30,"speed applies radius");
        require(std::stod(settings().at("min_radius_m"))==30,"requested radius retained");
        fields_.at("design_speed_kmh")->setValue(20);require(fields_.at("min_radius_m")->value()==30&&fields_.at("min_straight_m")->value()==35&&configuration().parameters.min_radius_m==30,"lower speed restores requested limits");
        standards_mode_->setCurrentIndex(standards_mode_->findData("dg2018"));require(!calculate_->isEnabled()&&!configuration_issue().isEmpty()&&fields_.at("design_speed_kmh")->value()==20,"20 not silently promoted into DG table");
        standards_mode_->setCurrentIndex(standards_mode_->findData("preliminary"));fields_.at("min_radius_m")->setValue(80);fields_.at("design_speed_kmh")->setValue(40);fields_.at("design_speed_kmh")->setValue(20);require(configuration().parameters.min_radius_m==80,"explicit radius preserved");
        reset_advanced();require(fields_.at("min_radius_m")->value()==30&&fields_.at("design_speed_kmh")->value()==20&&standards_mode_->currentData().toString()=="preliminary","reset context");
        fields_.at("design_speed_kmh")->setValue(40);std::ostringstream high_stream;write_configuration(high_stream,configuration());std::istringstream high_input(high_stream.str());apply_settings(read_settings(high_input));
        require(std::abs(configuration().parameters.min_straight_same_direction_m-111)<1e-8&&configuration().parameters.apply_dg_grade_guidance,"saved high-speed derived criteria");
        fields_.at("design_speed_kmh")->setValue(20);fields_.at("min_straight_m")->setValue(35);fields_.at("min_radius_m")->setValue(30);
        standards_mode_->setCurrentIndex(standards_mode_->findData("custom"));const auto custom=configuration();
        require(custom.parameters.min_straight_m==35&&custom.parameters.min_straight_same_direction_m==0&&!custom.parameters.apply_dg_grade_guidance,"custom clears hidden derived criteria after reload");
        standards_mode_->setCurrentIndex(standards_mode_->findData("preliminary"));require(std::abs(configuration().parameters.min_straight_same_direction_m-55.6)<1e-8&&configuration().parameters.apply_dg_grade_guidance,"preliminary recomputes O at lower speed");
        auto explicit_custom=settings();explicit_custom["standards_mode"]="custom";explicit_custom["min_straight_same_direction_m"]="73";explicit_custom["apply_dg_grade_guidance"]="0";apply_settings(explicit_custom);require(configuration().parameters.min_straight_same_direction_m==73&&!configuration().parameters.apply_dg_grade_guidance,"loading explicit custom O is preserved");reset_advanced();
        design_ice_risk_->setChecked(false);fields_.at("design_altitude_m")->setValue(4200);fields_.at("design_superelevation_percent")->setValue(4);design_terrain_->setCurrentIndex(1);
        std::ostringstream stream;write_configuration(stream,configuration());std::istringstream input(stream.str());const auto saved=read_settings(input);apply_settings(saved);
        require(settings().at("design_ice_risk")=="0"&&fields_.at("design_altitude_m")->value()==4200&&fields_.at("design_superelevation_percent")->value()==4&&design_terrain_->currentData().toString()=="rolling","context round trip");
        auto legacy=saved;for(const auto* key:{"standards_mode","design_speed_kmh","design_terrain","design_superelevation_percent","design_ice_risk","design_altitude_m"})legacy.erase(key);apply_settings(legacy);require(standards_mode_->currentData().toString()=="custom","old settings stay custom");
        reset_advanced();show_settings(design_dialog_);require(design_dialog_->grab().save(QDir(output).filePath("design-standards-20.png")),"capture");
        std::cout<<"design_standards_ui=passed default20, context fields, reversible effective limits, requested radius, untabulated DG guard, automatic altitude, persistence, mode change clears hidden derived criteria, explicit custom load, legacy custom, reset\n";QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::smoke_design_colors(const QString& output){
    testing_=true;
    try{auto require=[](bool ok){if(!ok)throw std::runtime_error("Design colors/gaps smoke failed");};require(QDir().mkpath(output));
        auto terrain=std::make_shared<Terrain>(std::vector<Vec3>{{-100,-150,-0.015},{800,-150,-0.015},{-100,150,0.015},{800,150,0.015}},std::vector<std::array<std::size_t,3>>{{0,1,2},{1,3,2}});
        view_->set_terrain(terrain);view_->camera_view("TOP");view_->set_points(Vec3{0,0,0},Vec3{600,0,0});
        std::vector<Vec3> gray{{0,0,0},{50,20,0},{100,-20,0},{200,0,0},{300,30,0},{400,0,0},{500,-20,0},{600,0,0}};
        view_->set_route(gray,{}, {},false,0.01,true);
        Parameters p;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;p.mode=DesignMode::surface;
        SearchResult first,alternate,second;first.status=alternate.status=second.status=SearchStatus::found;first.route={{0,0,0},{200,0,0}};second.route={{400,0,0},{600,0,0}};
        Vec3 center{100,-std::sqrt(30000.0),0};for(int i=0;i<=60;++i){double a=(120-i)*std::numbers::pi/180;alternate.route.push_back({center.x+200*std::cos(a),center.y+200*std::sin(a),0});}
        alternate.route.front()=first.route.front();alternate.route.back()=first.route.back();alternate.curves={{0,60,center,-60}};
        DesignReview review;review.sections={{first,0,200},{alternate,0,200},{second,400,600}};review.gaps={{200,400,first.route.back(),second.route.front(),"Conexión pendiente"}};
        require(validate_design_review(*terrain,review,p));view_->set_design_review(review);
        require(view_->corridor_is_preliminary());require(view_->design_cell_counts()==std::array<std::size_t,3>{2,1,2});
        require(grab().save(QDir(output).filePath("gray-blue-disconnected.png")));
        std::ostringstream csv,dxf;write_design_review_csv(csv,review);write_design_review_dxf(dxf,review);save_viewer_file(QDir(output).filePath("verified-sections.csv"),QByteArray::fromStdString(csv.str()),{});save_viewer_file(QDir(output).filePath("verified-sections.dxf"),QByteArray::fromStdString(dxf.str()),{});
        std::cout<<"design_colors=passed gray corridor, shortest lightblue, alternative darkblue, independent cells, orange gap endpoints, separate exports\n";QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::smoke_history(const QString& output){
    testing_=true;
    try{
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Spatial search history smoke failed");};
        require(QDir().mkpath(output));
        auto terrain=std::make_shared<Terrain>(std::vector<Vec3>{{0,0,0},{400,0,0},{0,400,0},{400,400,0}},std::vector<std::array<std::size_t,3>>{{0,1,2},{1,3,2}});
        view_->set_terrain(terrain);view_->camera_view("TOP");SearchTrace trace;
        auto region=[&](double offset){for(int y=0;y<80;++y)for(int x=0;x<80;++x){std::array<Vec3,2> edge{{{20+x*2.0,offset+y*2,0},{21.5+x*2,offset+y*2,0}}};trace.record(edge,false);}};
        region(20);view_->append_search(trace.take());require(view_->search_history_segments()==6400);
        require(grab().save(QDir(output).filePath("history-first-region.png")));
        region(220);view_->append_search(trace.take());require(view_->search_history_segments()==12800);
        require(grab().save(QDir(output).filePath("history-both-regions.png")));
        view_->append_search({});view_->show_search(false);view_->show_search(true);require(view_->search_history_segments()==12800);
        view_->clear_search();require(view_->search_history_segments()==0);
        std::cout<<"search_history=passed retained both regions beyond 3000 branches, idle batch, visibility toggle, reset\n";QCoreApplication::exit(0);
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}

void RouteWindow::smoke_validation(const QString& output){
    if(!QDir().mkpath(output)){QCoreApplication::exit(2);return;}
    Terrain terrain({{793000,8440000,0},{794000,8440000,0},{793000,8441000,0}},{{0,1,2}});
    std::vector<Vec3> route{{793010,8440010,0},{793020,8440010,2},{793030,8440010,-2}};
    Parameters p;p.max_cut_m=1;p.max_fill_m=1;
    auto message=QString::fromUtf8(validation_failure_message(validate(route,terrain,p)).c_str());
    auto passed=std::make_shared<bool>(false);
    QTimer::singleShot(250,this,[output,message,passed]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* details=dialog?dialog->findChild<QPlainTextEdit*>("validationDiagnostics"):nullptr;
        *passed=details&&details->isReadOnly()&&details->toPlainText()==message&&
            dialog->grab().save(QDir(output).filePath("diagnostico-restricciones.png"));
        if(dialog)dialog->accept();else QCoreApplication::exit(2);
    });
    error(message);
    std::cout<<"validation_dialog="<<(*passed?"passed":"failed")<<'\n';QCoreApplication::exit(*passed?0:2);
}

int main(int argc,char** argv) {
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QApplication app(argc,argv);app.setApplicationName("RouteTec");app.setApplicationVersion("1.0.0");app.setOrganizationName("RouteTec");RouteWindow window;window.show();
    if(argc==3&&QString(argv[1])=="--smoke-design-standards"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_design_standards(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-verified-history"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_verified_history(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-guide-fallback"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_guide_fallback(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-candidate-colors"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_candidate_colors(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-design-colors"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_design_colors(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-history"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_history(output);});}
    if(argc==3&&QString(argv[1])=="--smoke-validation"){auto output=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,output]{window.smoke_validation(output);});}
    if(argc==3&&QString(argv[1])=="--project"){auto path=QString::fromLocal8Bit(argv[2]);QTimer::singleShot(0,&window,[&window,path]{window.open_project_file(path);});}
    if(argc==4&&QString(argv[1])=="--capture-project"){
        QString project=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);QTimer::singleShot(0,&window,[&window,project,output]{window.capture_project(project,output);});
    }
    if(argc==4&&QString(argv[1])=="--capture-guide"){
        QString project=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);QTimer::singleShot(0,&window,[&window,project,output]{window.capture_guide(project,output);});
    }
    if(argc==4&&QString(argv[1])=="--smoke-features"){
        QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);QTimer::singleShot(0,&window,[&window,terrain,output]{window.smoke_features(terrain,output);});
    }
    if(argc==4&&QString(argv[1])=="--smoke-proposals"){
        QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
        QTimer::singleShot(0,&window,[&window,terrain,output]{window.smoke_proposals(terrain,output);});
    }
    if(argc==4&&(QString(argv[1])=="--smoke-test"||QString(argv[1])=="--smoke-road-options"||QString(argv[1])=="--capture-terrain")) {
        QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);bool capture=QString(argv[1])=="--capture-terrain",roads=QString(argv[1])=="--smoke-road-options";
        QTimer::singleShot(0,&window,[&window,terrain,output,capture,roads]{window.smoke_test(terrain,output,capture,roads);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-gray-candidates"){
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_gray_candidates(terrain,values,output);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-candidates"){
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_candidates(terrain,values,output);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-continuous"){
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_continuous(terrain,values,output);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-corridor"){
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_corridor(terrain,values,output);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-search"){
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_search(terrain,values,output);});
    }
    if(argc==5&&QString(argv[1])=="--smoke-request") {
        QFile request(QString::fromLocal8Bit(argv[3]));if(!request.open(QIODevice::ReadOnly))return 2;std::istringstream input(request.readAll().toStdString());
        auto values=read_settings(input);QString terrain=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(0,&window,[&window,terrain,output,values]{window.smoke_test(terrain,output,false,false,values);});
    }
    return app.exec();
}
