#include "renderengine.h"

#include <pxr/imaging/glf/drawTarget.h>
#include "pxr/imaging/hdx/types.h"
#include "pxr/imaging/hdSt/textureUtils.h"

const GfVec4f CLEAR_COLOR = GfVec4f(0.5f);
const GfVec4f SCENE_AMBIENT = GfVec4f(0.01f, 0.01f, 0.01f, 1.0f);
const GfVec4f SPECULAR_DEFAULT = GfVec4f(0.1f, 0.1f, 0.1f, 1.0f);
const GfVec4f AMBIENT_DEFAULT = GfVec4f(0.2f, 0.2f, 0.2f, 1.0f);
const float SHININESS_DEFAULT = 32.0;

RenderEngine::RenderEngine(OpenGLContext* context)
    : m_context(context)
    , m_imagingEngine(HdDriver(), TfToken(), true)
    , m_renderParams()
    , m_material()
    , isDirty(true)
{
    initDefaults();
}

RenderEngine::~RenderEngine() {}

bool RenderEngine::getIsDirty()
{
    return isDirty;
}

void RenderEngine::makeDirty()
{
    qDebug() << "Making render engine dirty...";
    this->isDirty = true;
}

bool RenderEngine::changeMode(RenderEngineMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        this->makeDirty();

        qDebug() << "Render engine mode set to:" << mode;
        return true;
    }
    return false;
}

void RenderEngine::initDefaults()
{
    m_imagingEngine.SetEnablePresentation(true);
    m_imagingEngine.SetRendererAov(HdAovTokens->color);
    m_renderParams.clearColor = CLEAR_COLOR;
    m_renderParams.showProxy = false;
    m_renderParams.showRender = false;
    m_renderParams.showGuides = false;
    m_renderParams.bboxLineDashSize = 0.f;

    this->setComplexity(1.0);
    this->setColorCorrectionMode(TfToken::Find("sRGB"));
    this->setDomeLightVisibility(true);
    this->setCameraLightEnabled(false);

    m_material.SetAmbient(AMBIENT_DEFAULT);
    m_material.SetSpecular(SPECULAR_DEFAULT);
    m_material.SetShininess(SHININESS_DEFAULT);

    m_mode = RenderEngineMode::FIXED_CAMERA;

    this->resize();

    qDebug() << "Render engine configured successfully.";

    return;
}

void RenderEngine::setComplexity(float complexity)
{
    m_renderParams.complexity = complexity;
}

void RenderEngine::setColorCorrectionMode(TfToken mode)
{
    m_renderParams.colorCorrectionMode = mode;
}

void RenderEngine::setDomeLightVisibility(bool visibility)
{
    m_domeLightVisibility = visibility;
    m_imagingEngine.SetRendererSetting(HdRenderSettingsTokens->domeLightCameraVisibility,
                                       VtValue(m_domeLightVisibility));
    return;
}

void RenderEngine::setCameraLightEnabled(bool enabled)
{
    m_cameraLightEnabled = enabled;
}

void RenderEngine::render(StageManager* manager, bool shouldRecord)
{
    this->clearRender();
    this->resize();

    GfCamera gfCamera;

    if (m_mode == RenderEngineMode::FIXED_CAMERA) {
        gfCamera = manager->getGfCameraAtFrame(manager->getCurrentFrame());
    } else if (m_mode == RenderEngineMode::FREE_CAMERA) {
        gfCamera = manager->m_freeCam->createGfCamera();
    }

    const GfFrustum frustum = gfCamera.GetFrustum();

    m_imagingEngine.SetCameraState(frustum.ComputeViewMatrix(), frustum.ComputeProjectionMatrix());

    GlfSimpleLightVector lights;
    if (m_cameraLightEnabled) {
        const GfVec3d& cameraPos = frustum.GetPosition();
        GlfSimpleLight cameraLight(GfVec4f(cameraPos[0], cameraPos[1], cameraPos[2], 1.0f));
        cameraLight.SetTransform(frustum.ComputeViewInverse());
        cameraLight.SetAmbient(SCENE_AMBIENT);
        lights.push_back(cameraLight);
    }

    m_imagingEngine.SetLightingState(lights, m_material, SCENE_AMBIENT);

    UsdPrim* root = manager->getPsuedoRoot();

    if (!root) {
        qDebug() << "Usd pseudo root of stage was not valid when render engine queried for it.";
        return;
    }

    unsigned int sleepTime = 10;  // Initial wait time of 10 ms
    while (true) {
        m_imagingEngine.Render(*root, m_renderParams);
        if (m_imagingEngine.IsConverged()) {
            if (shouldRecord) {
                this->record(manager);
            } else if (isDirty) {
                qInfo() << "ALL CLEAN!";
                isDirty = false;
            }
            break;
        } else {
            // Allow render thread to progress before invoking Render again.
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
            // Increase the sleep time up to a max of 100 ms
            sleepTime = std::min(100u, sleepTime + 5);
        }
    };

    return;
}

void RenderEngine::record(StageManager* manager)
{
    if (!isDirty) {
        qDebug() << "Not dirty";
        return;
    }

    HgiTextureHandle textureHandle = m_imagingEngine.GetAovTexture(HdAovTokens->color);

    HioImage::StorageSpec storage;
    storage.flipped = true;

    size_t size = 0;
    HdStTextureUtils::AlignedBuffer<uint8_t> mappedColorTextureBuffer;

    storage.width = textureHandle->GetDescriptor().dimensions[0];
    storage.height = textureHandle->GetDescriptor().dimensions[1];
    storage.format = HdxGetHioFormat(textureHandle->GetDescriptor().format);

    mappedColorTextureBuffer = HdStTextureUtils::HgiTextureReadback(m_imagingEngine.GetHgi(),
                                                                    textureHandle,
                                                                    &size);
    storage.data = mappedColorTextureBuffer.get();

    int frame = manager->getCurrentFrame();
    QString filename = manager->getOutputImagePath(frame);

    const HioImageSharedPtr image = HioImage::OpenForWriting(filename.toStdString());
    const bool writeSuccess = image && image->Write(storage);

    if (!writeSuccess) {
        qWarning() << "Write was not success";
    } else {
        qDebug() << "Success! Written to" << filename;
        isDirty = false;
    }
}

void RenderEngine::clearRender()
{
    if (isDirty) {
        qDebug() << "Clearing render engine...";
        m_imagingEngine.SetRendererAov(TfToken());  // render momentarily to null aov

        GlfDrawTargetRefPtr drawTarget = GlfDrawTarget::New(
            GfVec2i(m_context->width() * m_context->devicePixelRatio(),
                    m_context->height() * m_context->devicePixelRatio()));
        drawTarget->Bind();
        drawTarget->AddAttachment(HdAovTokens->color, GL_RGBA, GL_FLOAT, GL_RGBA);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawTarget->Unbind();

        m_imagingEngine.SetRendererAov(HdAovTokens->color);
    }
}

void RenderEngine::resize()
{
    if (isDirty) {
        GfRect2i dataWindow;
        int deviceWidth = m_context->width() * m_context->devicePixelRatio();
        int deviceHeight = m_context->height() * m_context->devicePixelRatio();

        if (m_mode == RenderEngineMode::FREE_CAMERA) {
            dataWindow = GfRect2i(GfVec2i(0), GfVec2i(deviceWidth - 1, deviceHeight - 1));
        } else if (m_mode == RenderEngineMode::FIXED_CAMERA) {
            if (deviceWidth > deviceHeight) {
                int frameStartX = (deviceWidth - deviceHeight) / 2;
                int frameEndX = deviceWidth - frameStartX
                                - 1;  // additional pixel should not be rendered
                dataWindow = GfRect2i(GfVec2i(frameStartX, 0), GfVec2i(frameEndX, deviceHeight - 1));
            } else {
                int frameStartY = (deviceHeight - deviceWidth) / 2;
                int frameEndY = deviceHeight - frameStartY - 1;

                dataWindow = GfRect2i(GfVec2i(0, frameStartY), GfVec2i(deviceWidth - 1, frameEndY));
            }
        }

        qInfo().Ns() << "New render engine aspect ratio: " << dataWindow.GetWidth() << ":"
                     << dataWindow.GetHeight();

        m_imagingEngine.SetFraming(CameraUtilFraming(dataWindow));
        m_imagingEngine.SetRenderBufferSize(GfVec2i(deviceWidth, deviceHeight));
    }
}
