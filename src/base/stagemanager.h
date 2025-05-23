#pragma once

#include "framemetadata.h"
#include "freecamera.h"

#include <mycpp/mydefines.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdLux/domeLight.h>

#include <QString>

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class StageManager : public QObject
{
    Q_OBJECT

Q_SIGNALS:
    void frameChanged(int frame);

public:
    uPtr<FreeCamera> m_freeCam = nullptr;

    StageManager();
    ~StageManager();

    bool loadUsdStage(const QString& stagePath, const QString& domeLightPath);
    bool generateCameraFrames(int numFrames);
    void exportDataJson() const;
    bool initFreeCam(int width, int height);

    UsdPrim* getPsuedoRoot();

    void setCurrentFrame(int frame);
    int getCurrentFrame() const;
    int getNumFrames() const;

    void setDomeLightIntensity(float intensity);
    float getDomeLightIntensity() const;

    void setModelScale(float scale);
    float getModelScale() const;

    const UsdStageRefPtr& getUsdStage() const;
    const UsdGeomCamera& getGeomCamera() const;
    GfCamera getGfCameraAtFrame(int frame) const;

    QString getOutputImagePath(int frame);
    QMap<QString, QString> getOutputPathMap() const;

    double getProgress() const;

private:
    void reset();
    bool configureUsdCamera();
    bool configureLuxDomeLight();
    void setUsdCameraTransformAtFrame(const GfMatrix4d& transform, int frame);

    static GfMatrix4d s_usdToNerfMatrix(const GfMatrix4d& cameraToWorld);

    UsdStageRefPtr m_usdStage;
    UsdGeomCamera m_geomCamera;
    UsdLuxDomeLight m_luxDomeLight;

    uPtr<UsdPrim> m_pseudoRoot = nullptr;

    QString m_inputStagePath;
    QString m_inputDomeLightPath;

    QString m_outputStagePath;
    QString m_outputDataJsonPath;
    QString m_outputImageDir;
    QString m_outputImagePrefix;

    int m_numFrames = 0;
    int m_currentFrame = 0;
    float m_modelScale = 1.f;

    std::vector<uPtr<FrameMetadata>> m_allFrameMeta;
};
