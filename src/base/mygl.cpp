#include "mygl.h"

#include <QKeyEvent>
#include <QApplication>

MyGL::MyGL(QWidget* parent)
    : OpenGLContext(parent)
    , m_timer()
    , m_mousePosPrev()
    , m_manager(mkU<StageManager>())
    , m_engine(mkU<RenderEngine>(this))
    , isRecording(false)
{
    connect(&m_timer, &QTimer::timeout, this, &MyGL::tick);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

MyGL::~MyGL()
{
    makeCurrent();
}

void MyGL::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.25, 0.25, 0.25, 1);

    printGLErrorLog();

    m_manager->initFreeCam(this->width(), this->height());

    m_timer.start(16);
}

void MyGL::resizeGL(int w, int h)
{
    m_manager->initFreeCam(w, h);

    qInfo() << "Window stats: Width" << w
             << "| Height" << h
             << "| Device Pixel Ratio" << devicePixelRatio();
}

void MyGL::paintGL()
{
    if (!m_engine || !m_manager) {
        return;
    }
    m_engine->render(m_manager.get(), isRecording);
}

void MyGL::resetEngine()
{
    m_engine = nullptr;

    m_engine = mkU<RenderEngine>(this);

    this->update();
}

void MyGL::tick()
{
    this->update();
}

void MyGL::keyPressEvent(QKeyEvent* e)
{
    this->slot_changeRenderEngineMode("free");

    float amount = 1.0f;
    if (e->modifiers() & Qt::ShiftModifier) {
        amount = 5.0f;
    }

    switch (e->key()) {
        case (Qt::Key_Escape): QApplication::quit(); break;
        case (Qt::Key_Right): m_manager->m_freeCam->rotateAboutUp(-amount); break;
        case (Qt::Key_Left): m_manager->m_freeCam->rotateAboutUp(amount); break;
        case (Qt::Key_Up): m_manager->m_freeCam->rotateAboutRight(amount); break;
        case (Qt::Key_Down): m_manager->m_freeCam->rotateAboutRight(-amount); break;

        case (Qt::Key_W): m_manager->m_freeCam->zoom(amount); break;
        case (Qt::Key_S): m_manager->m_freeCam->zoom(-amount); break;
        case (Qt::Key_D): m_manager->m_freeCam->translateAlongRight(amount); break;
        case (Qt::Key_A): m_manager->m_freeCam->translateAlongRight(-amount); break;
        case (Qt::Key_Q): m_manager->m_freeCam->translateAlongUp(-amount); break;
        case (Qt::Key_E): m_manager->m_freeCam->translateAlongUp(amount); break;
    }
    update();
}

void MyGL::mousePressEvent(QMouseEvent* e)
{
    this->slot_changeRenderEngineMode("free");

    if (e->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton)) {
        m_mousePosPrev = GfVec2d(e->pos().x(), e->pos().y());
    }
    update();
}

void MyGL::mouseMoveEvent(QMouseEvent* e)
{
    GfVec2d pos(e->pos().x(), e->pos().y());
    if (e->buttons() & Qt::LeftButton) {
        // Rotation
        GfVec2d diff = 0.04f * (pos - m_mousePosPrev);
        m_mousePosPrev = pos;
        m_manager->m_freeCam->orbitAboutOrigin(-diff[1], -diff[0]);
        // m_manager->m_freeCam->rotatePhi(-diff[0]);
        // m_manager->m_freeCam->rotateTheta(-diff[1]);
    } else if (e->buttons() & Qt::RightButton) {
        GfVec2d diff = 0.02f * (pos - m_mousePosPrev);
        m_mousePosPrev = pos;
        m_manager->m_freeCam->zoom(diff[1]);
    } else if (e->buttons() & Qt::MiddleButton) {
        // Panning
        GfVec2d diff = 0.02f * (pos - m_mousePosPrev);
        m_mousePosPrev = pos;
        m_manager->m_freeCam->translateAlongRight(-diff[0]);
        m_manager->m_freeCam->translateAlongUp(diff[1]);
    }
    update();
}

void MyGL::wheelEvent(QWheelEvent* e)
{
    this->slot_changeRenderEngineMode("free");

    m_manager->m_freeCam->zoom(e->angleDelta().y() * 0.02f);
    update();
}

void MyGL::slot_setStageManagerCurrentFrame(int frame)
{
    m_manager->setCurrentFrame(frame);
}

void MyGL::slot_changeRenderEngineMode(QString mode)
{
    bool changed = false;
    if (mode == "fixed") {
        changed = m_engine->changeMode(RenderEngineMode::FIXED_CAMERA);
    } else if (mode == "free") {
        changed = m_engine->changeMode(RenderEngineMode::FREE_CAMERA);

        if (changed) {
            m_manager->m_freeCam->setFromGfCamera(
                m_manager->getGfCameraAtFrame(m_manager->getCurrentFrame()));
        }
    }

    if (changed) {
        Q_EMIT engineModeChanged(mode);
    }
}

void MyGL::loadStageManager(const QString& stagePath, const QString& domeLightPath)
{
    bool success = m_manager->loadUsdStage(stagePath, domeLightPath);

    if (success) {
        qDebug() << "Usd stage loaded successfully by stage manager.";
        m_manager->generateCameraFrames(106);
        m_manager->initFreeCam(this->width(), this->height());

        this->resetEngine();
    } else {
        qFatal() << "Stage manager was unable to load Usd stage";
    }
}

void MyGL::handleEngineRecordProcess()
{
    this->slot_changeRenderEngineMode("fixed");

    int numFrames = m_manager->getNumFrames();

    int currFrameToRecord = 0;
    this->isRecording = true;

    while (currFrameToRecord <= numFrames) {
        if (!m_engine->getIsDirty()) {
            if (currFrameToRecord == numFrames) {
                this->isRecording = false;
                break;
            }
            m_manager->setCurrentFrame(currFrameToRecord);
            m_engine->makeDirty();

            currFrameToRecord += 1;
        }
    }

    qInfo() << "RECORDING COMPLETE!";
}
