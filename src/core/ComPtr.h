#pragma once

template<typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            if (p) p->Release();
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
    T*  get()   const { return p; }
    T** addr()        { return &p; }
    void release()    { if (p) { p->Release(); p = nullptr; } }
    T* operator->()   { return p; }
    explicit operator bool() const { return p != nullptr; }
};
