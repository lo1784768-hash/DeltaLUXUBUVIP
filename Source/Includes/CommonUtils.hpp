#ifndef VINHTRAN_HPP_INCLUDED
#define VINHTRAN_HPP_INCLUDED

#include <string>
#include <cstdint>

// Estructura simple para reemplazar ImVec2
struct SimpleVec2 {
    float x, y;
    SimpleVec2() : x(0), y(0) {}
    SimpleVec2(float x, float y) : x(x), y(y) {}
};

// Estructura simple para reemplazar ImVec4
struct SimpleVec4 {
    float x, y, z, w;
    SimpleVec4() : x(0), y(0), z(0), w(0) {}
    SimpleVec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

// Estructura simple para color
struct SimpleColor {
    float r, g, b, a;
    SimpleColor() : r(0), g(0), b(0), a(0) {}
    SimpleColor(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
};

#define HOOKAF(ret, func, ...)      \
    ret (*old_##func)(__VA_ARGS__); \
    ret hook_##func(__VA_ARGS__)

#define const_ptr(object, offset) *reinterpret_cast<uintptr_t *>(object + offset)

#define Str(ret) std::to_string(ret).c_str()

#define const_ptr_set(type, object, offset) *reinterpret_cast<type *>(object + offset)

#define const_field(type, object, offset) *reinterpret_cast<type *>(reinterpret_cast<uintptr_t>(object) + offset)

#define const_field_set(ret, first, second, val) *reinterpret_cast<ret *>(reinterpret_cast<uintptr_t>(first) + second) = val

// Operadores para SimpleVec2
static inline SimpleVec2 operator*(const SimpleVec2 &lhs, const float rhs) { return SimpleVec2(lhs.x * rhs, lhs.y * rhs); }
static inline SimpleVec2 operator/(const SimpleVec2 &lhs, const float rhs) { return SimpleVec2(lhs.x / rhs, lhs.y / rhs); }
static inline SimpleVec2 operator+(const SimpleVec2 &lhs, const float rhs) { return SimpleVec2(lhs.x + rhs, lhs.y + rhs); }
static inline SimpleVec2 operator+(const SimpleVec2 &lhs, const SimpleVec2 &rhs) { return SimpleVec2(lhs.x + rhs.x, lhs.y + rhs.y); }
static inline SimpleVec2 operator-(const SimpleVec2 &lhs, const SimpleVec2 &rhs) { return SimpleVec2(lhs.x - rhs.x, lhs.y - rhs.y); }
static inline SimpleVec2 operator-(const SimpleVec2 &lhs, const float rhs) { return SimpleVec2(lhs.x - rhs, lhs.y - rhs); }
static inline SimpleVec2 operator*(const SimpleVec2 &lhs, const SimpleVec2 &rhs) { return SimpleVec2(lhs.x * rhs.x, lhs.y * rhs.y); }
static inline SimpleVec2 operator/(const SimpleVec2 &lhs, const SimpleVec2 &rhs) { return SimpleVec2(lhs.x / rhs.x, lhs.y / rhs.y); }

static inline SimpleVec2 &operator*=(SimpleVec2 &lhs, const float rhs)
{
    lhs.x *= rhs;
    lhs.y *= rhs;
    return lhs;
}

static inline SimpleVec2 &operator/=(SimpleVec2 &lhs, const float rhs)
{
    lhs.x /= rhs;
    lhs.y /= rhs;
    return lhs;
}

static inline SimpleVec2 &operator+=(SimpleVec2 &lhs, const SimpleVec2 &rhs)
{
    lhs.x += rhs.x;
    lhs.y += rhs.y;
    return lhs;
}

static inline SimpleVec2 &operator-=(SimpleVec2 &lhs, const SimpleVec2 &rhs)
{
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;
    return lhs;
}

inline SimpleVec2 flooring(SimpleVec2 vec)
{
    return {static_cast<float>(vec.x), (float)int(vec.y)};
}

inline SimpleVec2 flooring(float x, float y)
{
    return {(float)int(x), (float)int(y)};
}

inline SimpleVec2 flooring(int x, int y)
{
    return {(float)x, (float)y};
}

struct Vvector3
{
    float X;
    float Y;
    float Z;
    Vvector3() : X(0), Y(0), Z(0) {}
    Vvector3(float X1, float Y1, float Z1) : X(X1), Y(Y1), Z(Z1) {}
    Vvector3(const Vvector3 &v);
    ~Vvector3();
};

Vvector3::Vvector3(const Vvector3 &v) : X(v.X), Y(v.Y), Z(v.Z) {}
Vvector3::~Vvector3() {}

struct display
{
    SimpleVec2 wh;
    float width;
    float height;
} disp;

#endif // VINHTRAN_HPP_INCLUDED