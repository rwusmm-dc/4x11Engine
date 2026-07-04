#pragma once
#include <windows.h>
#include <DirectXMath.h>

class FPSCamera {
public:
    FPSCamera();

    void SetPosition(float x, float y, float z);
    DirectX::XMFLOAT3 GetPosition() const;
    DirectX::XMFLOAT3 GetForward() const;
    DirectX::XMFLOAT3 GetRight() const;
    DirectX::XMFLOAT3 GetUp() const;

    void SetMoveSpeed(float speed);
    void SetLookSensitivity(float sensitivity);

    void LockMouse(bool locked);
    bool IsMouseLocked() const;

    void ProcessMouseMove(int dx, int dy);
    void Update(float deltaTime, const bool keys[256]);

    DirectX::XMMATRIX GetViewMatrix() const;

private:
    void UpdateVectors();

    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_lookDir;
    DirectX::XMFLOAT3 m_right;
    DirectX::XMFLOAT3 m_up;
    float m_yaw;
    float m_pitch;
    float m_moveSpeed;
    float m_lookSensitivity;
    bool m_isMouseLocked;
};
