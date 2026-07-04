#include "FPSCamera.h"
using namespace DirectX;

FPSCamera::FPSCamera()
    : m_position(0.0f, 2.0f, -5.0f)
    , m_lookDir(0.0f, 0.0f, 1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_yaw(0.0f)
    , m_pitch(0.0f)
    , m_moveSpeed(3.0f)
    , m_lookSensitivity(0.002f)
    , m_isMouseLocked(false)
{
    UpdateVectors();
}

void FPSCamera::SetPosition(float x, float y, float z)
{
    m_position = XMFLOAT3(x, y, z);
}

XMFLOAT3 FPSCamera::GetPosition() const { return m_position; }
XMFLOAT3 FPSCamera::GetForward() const { return m_lookDir; }
XMFLOAT3 FPSCamera::GetRight() const { return m_right; }
XMFLOAT3 FPSCamera::GetUp() const { return m_up; }

void FPSCamera::SetMoveSpeed(float speed) { m_moveSpeed = speed; }
void FPSCamera::SetLookSensitivity(float sensitivity) { m_lookSensitivity = sensitivity; }

void FPSCamera::LockMouse(bool locked) { m_isMouseLocked = locked; }
bool FPSCamera::IsMouseLocked() const { return m_isMouseLocked; }

void FPSCamera::ProcessMouseMove(int dx, int dy)
{
    if (!m_isMouseLocked) return;

    m_yaw   += static_cast<float>(dx) * m_lookSensitivity;
    m_pitch += static_cast<float>(dy) * m_lookSensitivity;
    UpdateVectors();
}

void FPSCamera::Update(float deltaTime, const bool keys[256])
{
    XMVECTOR pos     = XMLoadFloat3(&m_position);
    XMVECTOR forward = XMLoadFloat3(&m_lookDir);
    XMVECTOR right   = XMLoadFloat3(&m_right);
    XMVECTOR up      = XMLoadFloat3(&m_up);

    float speed = m_moveSpeed * deltaTime;
    if (keys[VK_SHIFT]) speed *= 0.2f;

    if (keys['W'] || keys['w']) pos += forward * speed;
    if (keys['S'] || keys['s']) pos -= forward * speed;
    if (keys['A'] || keys['a']) pos -= right * speed;
    if (keys['D'] || keys['d']) pos += right * speed;
    if (keys[VK_SPACE])         pos += up * speed;
    if (keys['X'] || keys['x']) pos -= up * speed;

    XMStoreFloat3(&m_position, pos);
}

XMMATRIX FPSCamera::GetViewMatrix() const
{
    XMVECTOR pos    = XMLoadFloat3(&m_position);
    XMVECTOR fwd    = XMLoadFloat3(&m_lookDir);
    XMVECTOR up     = XMLoadFloat3(&m_up);
    return XMMatrixLookAtLH(pos, pos + fwd, up);
}

void FPSCamera::UpdateVectors()
{
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
    XMVECTOR fwd = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot);
    fwd = XMVector3Normalize(fwd);

    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right   = XMVector3Normalize(XMVector3Cross(worldUp, fwd));
    XMVECTOR up      = XMVector3Normalize(XMVector3Cross(fwd, right));

    XMStoreFloat3(&m_lookDir, fwd);
    XMStoreFloat3(&m_right,   right);
    XMStoreFloat3(&m_up,      up);
}
