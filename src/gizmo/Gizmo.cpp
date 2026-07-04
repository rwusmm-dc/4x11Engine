#include "Gizmo.h"
#include <imgui.h>
#include <cmath>
#include <cstring>

#ifndef FLT_EPSILON
#define FLT_EPSILON 1.192092896e-07F
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const unsigned int COL_AXIS[3] = {
    IM_COL32(255,64,64,255),
    IM_COL32(64,255,64,255),
    IM_COL32(64,64,255,255),
};
static const unsigned int COL_HOV[3] = {
    IM_COL32(255,192,64,255),
    IM_COL32(192,255,64,255),
    IM_COL32(64,192,255,255),
};

Gizmo::Gizmo()
    : m_RX(0), m_RY(0), m_RW(800), m_RH(600)
    , m_GizmoLen(0.75f)
    , m_Using(false), m_Over(false)
    , m_HovAxis(-1), m_SelAxis(-1)
    , m_Operation(TRANSLATE), m_Mode(WORLD)
    , m_ActiveEntity(0)
{
    std::memset(m_View, 0, sizeof(m_View));
    std::memset(m_Proj, 0, sizeof(m_Proj));
    std::memset(m_ViewProj, 0, sizeof(m_ViewProj));
    std::memset(m_Matrix, 0, sizeof(m_Matrix));
    std::memset(m_DragStartMat, 0, sizeof(m_DragStartMat));
    std::memset(m_Center, 0, sizeof(m_Center));
    std::memset(m_DragStartCenter, 0, sizeof(m_DragStartCenter));
    std::memset(m_DragAxis, 0, sizeof(m_DragAxis));
    std::memset(m_DragStartPoint, 0, sizeof(m_DragStartPoint));
    m_Matrix[0] = m_Matrix[5] = m_Matrix[10] = m_Matrix[15] = 1.0f;
}

void Gizmo::SetRect(float x, float y, float w, float h) {
    m_RX = x; m_RY = y; m_RW = w; m_RH = h;
}

void Gizmo::ComputeBB(const float* verts, int stride, int count,
                      float* center, float& radius) const {
    if (!verts || count == 0) {
        center[0] = center[1] = center[2] = 0.0f;
        radius = 1.0f;
        return;
    }
    float mnX = verts[0], mxX = verts[0];
    float mnY = verts[1], mxY = verts[1];
    float mnZ = verts[2], mxZ = verts[2];
    for (int i = 0; i < count; i++) {
        float x = verts[i*stride], y = verts[i*stride+1], z = verts[i*stride+2];
        if (x < mnX) mnX = x;
        if (x > mxX) mxX = x;
        if (y < mnY) mnY = y;
        if (y > mxY) mxY = y;
        if (z < mnZ) mnZ = z;
        if (z > mxZ) mxZ = z;
    }
    center[0] = (mnX + mxX) * 0.5f;
    center[1] = (mnY + mxY) * 0.5f;
    center[2] = (mnZ + mxZ) * 0.5f;
    float hx = (mxX - mnX) * 0.5f;
    float hy = (mxY - mnY) * 0.5f;
    float hz = (mxZ - mnZ) * 0.5f;
    radius = std::sqrt(hx*hx + hy*hy + hz*hz);
}

void Gizmo::MatMul(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[r*4+k] * b[k*4+c];
            out[r*4+c] = s;
        }
}

void Gizmo::MatTransform(const float m[16], const float v[4], float out[4]) {
    for (int c = 0; c < 4; c++)
        out[c] = v[0]*m[c] + v[1]*m[4+c] + v[2]*m[8+c] + v[3]*m[12+c];
}

void Gizmo::MatInverse(const float m[16], float out[16]) {
    float a0 = m[0]*m[5] - m[1]*m[4];
    float a1 = m[0]*m[6] - m[2]*m[4];
    float a2 = m[0]*m[7] - m[3]*m[4];
    float a3 = m[1]*m[6] - m[2]*m[5];
    float a4 = m[1]*m[7] - m[3]*m[5];
    float a5 = m[2]*m[7] - m[3]*m[6];
    float b0 = m[8]*m[13] - m[9]*m[12];
    float b1 = m[8]*m[14] - m[10]*m[12];
    float b2 = m[8]*m[15] - m[11]*m[12];
    float b3 = m[9]*m[14] - m[10]*m[13];
    float b4 = m[9]*m[15] - m[11]*m[13];
    float b5 = m[10]*m[15] - m[11]*m[14];
    float det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
    if (std::fabs(det) < 1e-30f) { std::memcpy(out, m, 16*sizeof(float)); return; }
    float inv = 1.0f / det;
    out[0]  = ( m[5]*b5 - m[6]*b4 + m[7]*b3) * inv;
    out[1]  = (-m[1]*b5 + m[2]*b4 - m[3]*b3) * inv;
    out[2]  = ( m[13]*a5 - m[14]*a4 + m[15]*a3) * inv;
    out[3]  = (-m[9]*a5 + m[10]*a4 - m[11]*a3) * inv;
    out[4]  = (-m[4]*b5 + m[6]*b2 - m[7]*b1) * inv;
    out[5]  = ( m[0]*b5 - m[2]*b2 + m[3]*b1) * inv;
    out[6]  = (-m[12]*a5 + m[14]*a2 - m[15]*a1) * inv;
    out[7]  = ( m[8]*a5 - m[10]*a2 + m[11]*a1) * inv;
    out[8]  = ( m[4]*b4 - m[5]*b2 + m[7]*b0) * inv;
    out[9]  = (-m[0]*b4 + m[1]*b2 - m[3]*b0) * inv;
    out[10] = ( m[12]*a4 - m[13]*a2 + m[15]*a0) * inv;
    out[11] = (-m[8]*a4 + m[9]*a2 - m[11]*a0) * inv;
    out[12] = (-m[4]*b3 + m[5]*b1 - m[6]*b0) * inv;
    out[13] = ( m[0]*b3 - m[1]*b1 + m[2]*b0) * inv;
    out[14] = (-m[12]*a3 + m[13]*a1 - m[14]*a0) * inv;
    out[15] = ( m[8]*a3 - m[9]*a1 + m[10]*a0) * inv;
}

void Gizmo::Cross(float ax, float ay, float az,
                  float bx, float by, float bz,
                  float& cx, float& cy, float& cz) {
    cx = ay*bz - az*by;
    cy = az*bx - ax*bz;
    cz = ax*by - ay*bx;
}

float Gizmo::Dot(float ax, float ay, float az, float bx, float by, float bz) {
    return ax*bx + ay*by + az*bz;
}

void Gizmo::Normalize(float* x, float* y, float* z) {
    float len = std::sqrt((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (len > 1e-10f) { *x /= len; *y /= len; *z /= len; }
}

void Gizmo::Perpendiculars(float ax, float ay, float az,
                           float& p1x, float& p1y, float& p1z,
                           float& p2x, float& p2y, float& p2z) {
    float rx, ry, rz;
    if (std::fabs(ax) < 0.9f) {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    } else {
        rx = 0.0f; ry = 1.0f; rz = 0.0f;
    }
    Cross(ax, ay, az, rx, ry, rz, p1x, p1y, p1z);
    Normalize(&p1x, &p1y, &p1z);
    Cross(p1x, p1y, p1z, ax, ay, az, p2x, p2y, p2z);
    Normalize(&p2x, &p2y, &p2z);
}

bool Gizmo::WorldToScreen(const float mvp[16], float wx, float wy, float wz,
                          float& sx, float& sy) const {
    float p[4] = { wx, wy, wz, 1.0f };
    float c[4];
    MatTransform(mvp, p, c);
    if (std::fabs(c[3]) < FLT_EPSILON) return false;
    float iw = 1.0f / c[3];
    sx = (c[0]*iw*0.5f + 0.5f) * m_RW + m_RX;
    sy = (1.0f - (c[1]*iw*0.5f + 0.5f)) * m_RH + m_RY;
    return true;
}

bool Gizmo::ScreenToWorldRay(const float invVP[16],
                             float sx, float sy,
                             float& ox, float& oy, float& oz,
                             float& dx, float& dy, float& dz) const {
    float nx = (sx - m_RX) / m_RW * 2.0f - 1.0f;
    float ny = 1.0f - (sy - m_RY) / m_RH * 2.0f;

    float nearP[4] = { nx, ny, 0.0f, 1.0f };
    float farP[4]  = { nx, ny, 1.0f, 1.0f };
    float nearW[4], farW[4];
    MatTransform(invVP, nearP, nearW);
    MatTransform(invVP, farP, farW);
    if (std::fabs(nearW[3]) < FLT_EPSILON || std::fabs(farW[3]) < FLT_EPSILON)
        return false;
    float inw = 1.0f / nearW[3], ifw = 1.0f / farW[3];
    ox = nearW[0]*inw; oy = nearW[1]*inw; oz = nearW[2]*inw;
    float fx = farW[0]*ifw, fy = farW[1]*ifw, fz = farW[2]*ifw;
    dx = fx - ox; dy = fy - oy; dz = fz - oz;
    Normalize(&dx, &dy, &dz);
    return true;
}

bool Gizmo::RayPlaneIntersect(float rox, float roy, float roz,
                              float rdx, float rdy, float rdz,
                              float pnX, float pnY, float pnZ, float pD,
                              float& t) const {
    float denom = Dot(rdx, rdy, rdz, pnX, pnY, pnZ);
    if (std::fabs(denom) < 1e-10f) return false;
    t = -(Dot(rox, roy, roz, pnX, pnY, pnZ) + pD) / denom;
    return true;
}

void Gizmo::GetAxisDir(int axis, int op, float* dir) const {
    if (m_Mode == LOCAL) {
        dir[0] = m_Matrix[axis*4];
        dir[1] = m_Matrix[axis*4+1];
        dir[2] = m_Matrix[axis*4+2];
    } else {
        dir[0] = (axis==0) ? 1.0f : 0.0f;
        dir[1] = (axis==1) ? 1.0f : 0.0f;
        dir[2] = (axis==2) ? 1.0f : 0.0f;
    }
    Normalize(&dir[0], &dir[1], &dir[2]);
}

bool Gizmo::Manipulate(const float* view, const float* projection,
                       float* inOutMatrix,
                       const float* entityVerts, int vertStride,
                       int vertCount,
                       Operation op, Mode mode) {
    m_Operation = op;
    m_Mode = mode;
    std::memcpy(m_View, view, 16*sizeof(float));
    std::memcpy(m_Proj, projection, 16*sizeof(float));
    std::memcpy(m_Matrix, inOutMatrix, 16*sizeof(float));
    MatMul(m_View, m_Proj, m_ViewProj);

    // ---- compute bounding box center in world space ----
    float localCenter[3], localRadius;
    ComputeBB(entityVerts, vertStride, vertCount, localCenter, localRadius);

    float lc[4] = { localCenter[0], localCenter[1], localCenter[2], 1.0f };
    float wc[4];
    MatTransform(m_Matrix, lc, wc);
    m_Center[0] = wc[0]; m_Center[1] = wc[1]; m_Center[2] = wc[2];

    // ---- gizmo world-space length from bounding box ----
    {
        float corn[4] = { localCenter[0]+localRadius,
                          localCenter[1]+localRadius,
                          localCenter[2]+localRadius, 1.0f };
        float cw[4];
        MatTransform(m_Matrix, corn, cw);
        float dx = cw[0]-m_Center[0], dy = cw[1]-m_Center[1], dz = cw[2]-m_Center[2];
        float wr = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (wr < 0.01f) wr = 0.01f;
        m_GizmoLen = wr * 1.0f;
    }

    // ---- screen-space center ----
    float cx, cy;
    if (!WorldToScreen(m_ViewProj, m_Center[0], m_Center[1], m_Center[2], cx, cy))
        return false;

    float mx = ImGui::GetMousePos().x;
    float my = ImGui::GetMousePos().y;
    bool inside = (mx >= m_RX && mx <= m_RX+m_RW && my >= m_RY && my <= m_RY+m_RH);

    // ---- axis endpoints for translate/scale lines ----
    float axes[3][3];
    float endScrX[3], endScrY[3];
    for (int i = 0; i < 3; i++) {
        GetAxisDir(i, m_Operation, axes[i]);
        float ew[3] = { m_Center[0] + axes[i][0]*m_GizmoLen,
                        m_Center[1] + axes[i][1]*m_GizmoLen,
                        m_Center[2] + axes[i][2]*m_GizmoLen };
        if (!WorldToScreen(m_ViewProj, ew[0], ew[1], ew[2], endScrX[i], endScrY[i])) {
            endScrX[i] = cx; endScrY[i] = cy;
        }
    }

    // ---- hit test ----
    float hitR = 15.0f;
    if (!m_Using) {
        m_HovAxis = -1;
        if (inside) {
            if (m_Operation == ROTATE) {
                // Hit test each axis ring
                float invVP[16];
                MatInverse(m_ViewProj, invVP);
                for (int i = 0; i < 3; i++) {
                    float axis[3];
                    GetAxisDir(i, ROTATE, axis);
                    float p1[3], p2[3];
                    Perpendiculars(axis[0], axis[1], axis[2],
                                   p1[0], p1[1], p1[2],
                                   p2[0], p2[1], p2[2]);
                    // Check mouse distance to ring at several sample points
                    int ns = 24;
                    float bestDist = 1e10f;
                    for (int j = 0; j < ns; j++) {
                        float theta = (float)j / (float)ns * 2.0f * (float)M_PI;
                        float ct = std::cos(theta), st = std::sin(theta);
                        float wx = m_Center[0] + m_GizmoLen * (ct*p1[0] + st*p2[0]);
                        float wy = m_Center[1] + m_GizmoLen * (ct*p1[1] + st*p2[1]);
                        float wz = m_Center[2] + m_GizmoLen * (ct*p1[2] + st*p2[2]);
                        float sx, sy;
                        if (WorldToScreen(m_ViewProj, wx, wy, wz, sx, sy)) {
                            float d = std::sqrt((mx-sx)*(mx-sx)+(my-sy)*(my-sy));
                            if (d < bestDist) bestDist = d;
                        }
                    }
                    if (bestDist < hitR * 2.0f) {
                        float aw[3] = { m_Center[0] + axis[0]*m_GizmoLen,
                                        m_Center[1] + axis[1]*m_GizmoLen,
                                        m_Center[2] + axis[2]*m_GizmoLen };
                        float asx, asy;
                        if (WorldToScreen(m_ViewProj, aw[0], aw[1], aw[2], asx, asy)) {
                            float axisScrX = asx - cx;
                            float axisScrY = asy - cy;
                            float axLen = std::sqrt(axisScrX*axisScrX + axisScrY*axisScrY);
                            if (axLen > 1.0f) { m_HovAxis = i; break; }
                        }
                    }
                }
            } else {
                // Line hit test (translate/scale)
                for (int i = 0; i < 3; i++) {
                    float dx = endScrX[i]-cx, dy = endScrY[i]-cy;
                    float len2 = dx*dx+dy*dy;
                    if (len2 < 1e-10f) continue;
                    float t = ((mx-cx)*dx + (my-cy)*dy) / len2;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    float px = cx + t*dx, py = cy + t*dy;
                    float dist = std::sqrt((mx-px)*(mx-px)+(my-py)*(my-py));
                    if (dist < hitR) { m_HovAxis = i; break; }
                }
            }
        }
        m_Over = (m_HovAxis >= 0);
    }

    // ---- start drag ----
    if (!m_Using && m_HovAxis >= 0 && inside &&
        ImGui::IsMouseClicked(0) && !ImGui::GetIO().WantCaptureMouse) {
        m_Using = true;
        m_SelAxis = m_HovAxis;
        std::memcpy(m_DragStartMat, m_Matrix, 16*sizeof(float));
        std::memcpy(m_DragStartCenter, m_Center, 3*sizeof(float));
        m_DragMouseStart[0] = mx;
        m_DragMouseStart[1] = my;

        GetAxisDir(m_SelAxis, m_Operation, m_DragAxis);

        if (m_Operation == ROTATE) {
            // Project mouse onto plane perpendicular to rotation axis
            float invVP[16];
            MatInverse(m_ViewProj, invVP);
            float rox, roy, roz, rdx, rdy, rdz;
            if (ScreenToWorldRay(invVP, mx, my, rox, roy, roz, rdx, rdy, rdz)) {
                float pD = -(m_DragAxis[0]*m_Center[0] +
                             m_DragAxis[1]*m_Center[1] +
                             m_DragAxis[2]*m_Center[2]);
                float t;
                if (RayPlaneIntersect(rox, roy, roz, rdx, rdy, rdz,
                                      m_DragAxis[0], m_DragAxis[1], m_DragAxis[2], pD, t)) {
                    m_DragStartPoint[0] = rox + t*rdx;
                    m_DragStartPoint[1] = roy + t*rdy;
                    m_DragStartPoint[2] = roz + t*rdz;
                } else {
                    // If ray is parallel to plane, fall back to screen-space direction
                    m_DragStartPoint[0] = m_Center[0];
                    m_DragStartPoint[1] = m_Center[1];
                    m_DragStartPoint[2] = m_Center[2];
                }
            }
        }
    }

    // ---- drag update ----
    if (m_Using) {
        if (!ImGui::IsMouseDown(0)) {
            m_Using = false;
            m_SelAxis = -1;
        } else {
            if (m_Operation == TRANSLATE) {
                float axisWorld[3];
                GetAxisDir(m_SelAxis, m_Operation, axisWorld);

                float csx, csy, esx, esy;
                if (WorldToScreen(m_ViewProj,
                        m_DragStartCenter[0], m_DragStartCenter[1], m_DragStartCenter[2],
                        csx, csy)) {
                    float ews[3] = {
                        m_DragStartCenter[0] + axisWorld[0] * m_GizmoLen,
                        m_DragStartCenter[1] + axisWorld[1] * m_GizmoLen,
                        m_DragStartCenter[2] + axisWorld[2] * m_GizmoLen
                    };
                    if (WorldToScreen(m_ViewProj, ews[0], ews[1], ews[2], esx, esy)) {
                        float asx = esx - csx, asy = esy - csy;
                        float asl2 = asx*asx + asy*asy;
                        if (asl2 > 1.0f) {
                            float dmx = mx - m_DragMouseStart[0];
                            float dmy = my - m_DragMouseStart[1];
                            float t = (dmx * asx + dmy * asy) / asl2;
                            float worldDelta = t * m_GizmoLen;
                            m_Matrix[12] = m_DragStartMat[12] + axisWorld[0] * worldDelta;
                            m_Matrix[13] = m_DragStartMat[13] + axisWorld[1] * worldDelta;
                            m_Matrix[14] = m_DragStartMat[14] + axisWorld[2] * worldDelta;
                            m_Center[0] = m_DragStartCenter[0] + axisWorld[0] * worldDelta;
                            m_Center[1] = m_DragStartCenter[1] + axisWorld[1] * worldDelta;
                            m_Center[2] = m_DragStartCenter[2] + axisWorld[2] * worldDelta;
                        }
                    }
                }
            } else if (m_Operation == ROTATE) {
                float invVP[16];
                MatInverse(m_ViewProj, invVP);
                float rox, roy, roz, rdx, rdy, rdz;
                if (ScreenToWorldRay(invVP, mx, my, rox, roy, roz, rdx, rdy, rdz)) {
                    float pD = -(m_DragAxis[0]*m_DragStartCenter[0] +
                                 m_DragAxis[1]*m_DragStartCenter[1] +
                                 m_DragAxis[2]*m_DragStartCenter[2]);
                    float t;
                    if (RayPlaneIntersect(rox, roy, roz, rdx, rdy, rdz,
                                          m_DragAxis[0], m_DragAxis[1], m_DragAxis[2], pD, t)) {
                        float curX = rox + t*rdx;
                        float curY = roy + t*rdy;
                        float curZ = roz + t*rdz;

                        // Vectors from center to start and current points
                        float v1x = m_DragStartPoint[0] - m_DragStartCenter[0];
                        float v1y = m_DragStartPoint[1] - m_DragStartCenter[1];
                        float v1z = m_DragStartPoint[2] - m_DragStartCenter[2];
                        float v2x = curX - m_DragStartCenter[0];
                        float v2y = curY - m_DragStartCenter[1];
                        float v2z = curZ - m_DragStartCenter[2];

                        Normalize(&v1x, &v1y, &v1z);
                        Normalize(&v2x, &v2y, &v2z);

                        // Signed angle between v1 and v2 around rotation axis
                        float crossVec[3];
                        Cross(v1x, v1y, v1z, v2x, v2y, v2z,
                              crossVec[0], crossVec[1], crossVec[2]);
                        float sinAngle = Dot(crossVec[0], crossVec[1], crossVec[2],
                                             m_DragAxis[0], m_DragAxis[1], m_DragAxis[2]);
                        float cosAngle = Dot(v1x, v1y, v1z, v2x, v2y, v2z);
                        if (cosAngle > 1.0f) cosAngle = 1.0f;
                        if (cosAngle < -1.0f) cosAngle = -1.0f;
                        float angle = -std::atan2(sinAngle, cosAngle);

                        int a = m_SelAxis;
                        int a1 = (a+1)%3, a2 = (a+2)%3;
                        float c = std::cos(angle);
                        float s = std::sin(angle);
                        float rot[16];
                        std::memset(rot, 0, sizeof(rot));
                        rot[15] = 1.0f;
                        rot[a*4+a] = 1.0f;
                        rot[a1*4+a1] = c;  rot[a1*4+a2] = -s;
                        rot[a2*4+a1] = s;  rot[a2*4+a2] =  c;

                        float temp[16];
                        MatMul(rot, m_DragStartMat, temp);
                        std::memcpy(m_Matrix, temp, sizeof(temp));
                    }
                }
            } else if (m_Operation == SCALE) {
                float axisWorld[3];
                GetAxisDir(m_SelAxis, m_Operation, axisWorld);

                float csx, csy, esx, esy;
                if (WorldToScreen(m_ViewProj,
                        m_DragStartCenter[0], m_DragStartCenter[1], m_DragStartCenter[2],
                        csx, csy)) {
                    float ews[3] = {
                        m_DragStartCenter[0] + axisWorld[0] * m_GizmoLen,
                        m_DragStartCenter[1] + axisWorld[1] * m_GizmoLen,
                        m_DragStartCenter[2] + axisWorld[2] * m_GizmoLen
                    };
                    if (WorldToScreen(m_ViewProj, ews[0], ews[1], ews[2], esx, esy)) {
                        float asx = esx - csx, asy = esy - csy;
                        float asl2 = asx*asx + asy*asy;
                        if (asl2 > 1.0f) {
                            float dmx = mx - m_DragMouseStart[0];
                            float dmy = my - m_DragMouseStart[1];
                            float t = (dmx * asx + dmy * asy) / asl2;

                            int dim = m_SelAxis;
                            float baseLen = std::sqrt(
                                m_DragStartMat[dim*4]*m_DragStartMat[dim*4] +
                                m_DragStartMat[dim*4+1]*m_DragStartMat[dim*4+1] +
                                m_DragStartMat[dim*4+2]*m_DragStartMat[dim*4+2]);
                            if (baseLen < 1e-10f) baseLen = 1.0f;
                            float newLen = baseLen + t * m_GizmoLen * 0.5f;
                            if (newLen < 0.01f) newLen = 0.01f;
                            float ratio = newLen / baseLen;
                            for (int r = 0; r < 3; r++)
                                m_Matrix[dim*4+r] = m_DragStartMat[dim*4+r] * ratio;
                        }
                    }
                }
            }
        }
    }

    // ---- draw ----
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float baseThick = 2.5f;

    if (m_Operation == TRANSLATE) {
        for (int i = 0; i < 3; i++) {
            float thick = (m_Using && i==m_SelAxis) ? 4.0f :
                          (i==m_HovAxis && !m_Using) ? 3.5f : baseThick;
            unsigned int col = (m_Using && i==m_SelAxis) ? IM_COL32(255,255,0,255) :
                               (i==m_HovAxis && !m_Using) ? COL_HOV[i] : COL_AXIS[i];
            dl->AddLine(ImVec2(cx,cy), ImVec2(endScrX[i],endScrY[i]), col, thick);
            float adx = endScrX[i]-cx, ady = endScrY[i]-cy;
            float alen = std::sqrt(adx*adx+ady*ady);
            if (alen > 1.0f) {
                float nx = adx/alen, ny = ady/alen;
                float asz = 12.0f;
                float bx = endScrX[i]-nx*asz, by = endScrY[i]-ny*asz;
                dl->AddTriangleFilled(ImVec2(endScrX[i],endScrY[i]),
                    ImVec2(bx-ny*asz*0.4f, by+nx*asz*0.4f),
                    ImVec2(bx+ny*asz*0.4f, by-nx*asz*0.4f), col);
            }
        }
    } else if (m_Operation == ROTATE) {
        // Draw 3D rings for each axis
        for (int i = 0; i < 3; i++) {
            unsigned int col = (m_Using && i==m_SelAxis) ? IM_COL32(255,255,0,255) :
                               (i==m_HovAxis && !m_Using) ? COL_HOV[i] : COL_AXIS[i];
            float thick = (m_Using && i==m_SelAxis) ? 4.0f :
                          (i==m_HovAxis && !m_Using) ? 3.5f : baseThick;

            float axis[3];
            GetAxisDir(i, ROTATE, axis);

            float p1[3], p2[3];
            Perpendiculars(axis[0], axis[1], axis[2],
                           p1[0], p1[1], p1[2],
                           p2[0], p2[1], p2[2]);

            // Generate ring points in 3D and project to screen
            int segs = 48;
            ImVec2 pts[48];
            int npts = 0;
            for (int j = 0; j < segs; j++) {
                float theta = (float)j / (float)segs * 2.0f * (float)M_PI;
                float ct = std::cos(theta), st = std::sin(theta);
                float wx = m_Center[0] + m_GizmoLen * (ct*p1[0] + st*p2[0]);
                float wy = m_Center[1] + m_GizmoLen * (ct*p1[1] + st*p2[1]);
                float wz = m_Center[2] + m_GizmoLen * (ct*p1[2] + st*p2[2]);
                float sx, sy;
                if (WorldToScreen(m_ViewProj, wx, wy, wz, sx, sy))
                    pts[npts++] = ImVec2(sx, sy);
            }

            if (npts > 2) {
                // Fade ring on back-facing side (optional depth cue)
                dl->AddPolyline(pts, npts, col, 0, thick);
            }
        }

        // Draw angle arc when dragging
        if (m_Using) {
            float invVP[16];
            MatInverse(m_ViewProj, invVP);
            float rox, roy, roz, rdx, rdy, rdz;
            if (ScreenToWorldRay(invVP, mx, my, rox, roy, roz, rdx, rdy, rdz)) {
                float pD = -(m_DragAxis[0]*m_DragStartCenter[0] +
                             m_DragAxis[1]*m_DragStartCenter[1] +
                             m_DragAxis[2]*m_DragStartCenter[2]);
                float t;
                if (RayPlaneIntersect(rox, roy, roz, rdx, rdy, rdz,
                                      m_DragAxis[0], m_DragAxis[1], m_DragAxis[2], pD, t)) {
                    float curX = rox + t*rdx;
                    float curY = roy + t*rdy;
                    float curZ = roz + t*rdz;

                    float v1x = m_DragStartPoint[0] - m_DragStartCenter[0];
                    float v1y = m_DragStartPoint[1] - m_DragStartCenter[1];
                    float v1z = m_DragStartPoint[2] - m_DragStartCenter[2];
                    float v2x = curX - m_DragStartCenter[0];
                    float v2y = curY - m_DragStartCenter[1];
                    float v2z = curZ - m_DragStartCenter[2];
                    Normalize(&v1x, &v1y, &v1z);
                    Normalize(&v2x, &v2y, &v2z);
                    float crossVec[3];
                    Cross(v1x, v1y, v1z, v2x, v2y, v2z,
                          crossVec[0], crossVec[1], crossVec[2]);
                    float sinAngle = Dot(crossVec[0], crossVec[1], crossVec[2],
                                         m_DragAxis[0], m_DragAxis[1], m_DragAxis[2]);
                    float cosAngle = Dot(v1x, v1y, v1z, v2x, v2y, v2z);
                    if (cosAngle > 1.0f) cosAngle = 1.0f;
                    if (cosAngle < -1.0f) cosAngle = -1.0f;
                    float angle = std::atan2(sinAngle, cosAngle);

                    // Draw arc from start to current position
                    if (std::fabs(angle) > 0.01f) {
                        int arcSegs = 36;
                        int steps = (int)(std::fabs(angle) / (2.0f*(float)M_PI) * arcSegs);
                        if (steps < 2) steps = 2;
                        if (steps > arcSegs) steps = arcSegs;
                        ImVec2 arcPts[36];
                        int nArc = 0;
                        float p1[3], p2[3];
                        Perpendiculars(m_DragAxis[0], m_DragAxis[1], m_DragAxis[2],
                                       p1[0], p1[1], p1[2],
                                       p2[0], p2[1], p2[2]);

                        // Project v1 onto the perpendicular basis to get start angle
                        float startAngle = std::atan2(
                            Dot(v1x, v1y, v1z, p2[0], p2[1], p2[2]),
                            Dot(v1x, v1y, v1z, p1[0], p1[1], p1[2]));

                        for (int j = 0; j <= steps; j++) {
                            float a = startAngle + angle * (float)j / (float)steps;
                            float ca = std::cos(a), sa = std::sin(a);
                            float wx = m_DragStartCenter[0] + m_GizmoLen * (ca*p1[0] + sa*p2[0]);
                            float wy = m_DragStartCenter[1] + m_GizmoLen * (ca*p1[1] + sa*p2[1]);
                            float wz = m_DragStartCenter[2] + m_GizmoLen * (ca*p1[2] + sa*p2[2]);
                            float sx, sy;
                            if (WorldToScreen(m_ViewProj, wx, wy, wz, sx, sy))
                                arcPts[nArc++] = ImVec2(sx, sy);
                        }
                        if (nArc > 2) {
                            unsigned int arcCol = (m_SelAxis==0) ? IM_COL32(255,128,128,200) :
                                                   (m_SelAxis==1) ? IM_COL32(128,255,128,200) :
                                                                     IM_COL32(128,128,255,200);
                            dl->AddPolyline(arcPts, nArc, arcCol, 0, 3.0f);
                        }
                    }
                }
            }
        }
    } else if (m_Operation == SCALE) {
        float box = 8.0f;
        for (int i = 0; i < 3; i++) {
            float thick = (m_Using && i==m_SelAxis) ? 4.0f :
                          (i==m_HovAxis && !m_Using) ? 3.5f : baseThick;
            unsigned int col = (m_Using && i==m_SelAxis) ? IM_COL32(255,255,0,255) :
                               (i==m_HovAxis && !m_Using) ? COL_HOV[i] : COL_AXIS[i];
            dl->AddLine(ImVec2(cx,cy), ImVec2(endScrX[i],endScrY[i]), col, thick);
            dl->AddRectFilled(ImVec2(endScrX[i]-box, endScrY[i]-box),
                              ImVec2(endScrX[i]+box, endScrY[i]+box), col);
        }
    }

    std::memcpy(inOutMatrix, m_Matrix, 16*sizeof(float));
    return m_Using;
}
