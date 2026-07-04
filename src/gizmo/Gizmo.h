#pragma once
#include <cstdint>

struct Entity;

class Gizmo {
public:
    enum Operation { TRANSLATE = 0, ROTATE, SCALE };
    enum Mode { LOCAL, WORLD };

    Gizmo();

    void SetRect(float x, float y, float width, float height);

    bool Manipulate(const float* view, const float* projection,
                    float* inOutMatrix,
                    const float* entityVerts, int vertStride,
                    int vertCount,
                    Operation op, Mode mode);

    bool IsUsing() const { return m_Using; }
    bool IsOver() const { return m_Over || m_Using; }

    void SetOperation(Operation op) { m_Operation = op; }
    void SetMode(Mode mode) { m_Mode = mode; }
    Operation GetOperation() const { return m_Operation; }
    Mode GetMode() const { return m_Mode; }

    void SetActiveEntity(uint64_t id) {
        if (id != m_ActiveEntity) {
            m_Using = false;
            m_Over = false;
            m_HovAxis = -1;
            m_SelAxis = -1;
            m_ActiveEntity = id;
        }
    }

private:
    void ComputeBB(const float* verts, int stride, int count,
                   float* center, float& radius) const;

    static void MatMul(const float a[16], const float b[16], float out[16]);
    static void MatTransform(const float m[16], const float v[4], float out[4]);
    static void MatInverse(const float m[16], float out[16]);
    static void Cross(float ax, float ay, float az,
                      float bx, float by, float bz,
                      float& cx, float& cy, float& cz);
    static float Dot(float ax, float ay, float az, float bx, float by, float bz);
    static void Normalize(float* x, float* y, float* z);

    bool WorldToScreen(const float mvp[16], float wx, float wy, float wz,
                       float& sx, float& sy) const;
    bool ScreenToWorldRay(const float invVP[16],
                          float sx, float sy,
                          float& ox, float& oy, float& oz,
                          float& dx, float& dy, float& dz) const;
    bool RayPlaneIntersect(float rox, float roy, float roz,
                           float rdx, float rdy, float rdz,
                           float pnX, float pnY, float pnZ, float pD,
                           float& t) const;

    void GetAxisDir(int axis, int op, float* dir) const;
    static void Perpendiculars(float ax, float ay, float az,
                               float& p1x, float& p1y, float& p1z,
                               float& p2x, float& p2y, float& p2z);

    float m_RX, m_RY, m_RW, m_RH;

    float m_View[16];
    float m_Proj[16];
    float m_ViewProj[16];
    float m_Matrix[16];

    float m_GizmoLen;
    float m_Center[3];

    bool m_Using;
    bool m_Over;
    int m_HovAxis;
    int m_SelAxis;

    Operation m_Operation;
    Mode m_Mode;

    uint64_t m_ActiveEntity;

    // Drag state
    float m_DragStartMat[16];
    float m_DragStartCenter[3];
    float m_DragAxis[3];
    float m_DragMouseStart[2];
    float m_DragStartPoint[3];
};
