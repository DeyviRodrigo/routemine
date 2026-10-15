#include <QApplication>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkConeSource.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
int main(int argc,char** argv) {
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc,argv);
    QMainWindow window; window.setWindowTitle("RouteMine - integration demo (synthetic geometry)");
    auto* widget=new QVTKOpenGLNativeWidget(&window); window.setCentralWidget(widget);
    vtkNew<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkNew<vtkRenderer> renderer; renderWindow->AddRenderer(renderer);
    widget->setRenderWindow(renderWindow);
    vtkNew<vtkConeSource> source;
    vtkNew<vtkPolyDataMapper> mapper; mapper->SetInputConnection(source->GetOutputPort());
    vtkNew<vtkActor> actor; actor->SetMapper(mapper); renderer->AddActor(actor); renderer->ResetCamera();
    window.resize(1100,720);window.show();return app.exec();
}
