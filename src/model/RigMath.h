#pragma once
#include <array>
#include <cmath>

// Small matrix helpers shared by the parsers, viewport and exporter. Header-only.
//
// PoE2 stores bone bind matrices row-major with the translation in row 3 (v·M convention:
// world = local * parentWorld), and meshes in a Z-down frame. kNativeToYUp maps native (x,y,z) to
// glTF/OpenGL (x, -z, y): a proper rotation (determinant +1) so triangle winding is preserved.
namespace RigMath {

using Mat4 = std::array<float, 16>;   // row-major

inline Mat4 identity()
{
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

// Row-major multiply: (a*b)[r][c] = sum_k a[r][k]*b[k][c].
inline Mat4 mul(const Mat4& a, const Mat4& b)
{
    Mat4 o{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r*4+k] * b[k*4+c];
            o[r*4+c] = s;
        }
    return o;
}

// General 4x4 inverse (Gauss-Jordan). Returns identity on a singular matrix.
inline Mat4 inverse(const Mat4& m)
{
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) { a[r][c] = m[r*4+c]; a[r][c+4] = (r == c) ? 1.0 : 0.0; }
    }
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col+1; r < 4; ++r) if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) return identity();
        if (piv != col) for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[piv][c]);
        const double d = a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    Mat4 o{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) o[r*4+c] = float(a[r][c+4]);
    return o;
}

// Transform a point (row vector · matrix), row-major with translation in row 3.
inline void transformPoint(const Mat4& m, float x, float y, float z, float out[3])
{
    out[0] = x*m[0] + y*m[4] + z*m[8]  + m[12];
    out[1] = x*m[1] + y*m[5] + z*m[9]  + m[13];
    out[2] = x*m[2] + y*m[6] + z*m[10] + m[14];
}

inline void translationOf(const Mat4& m, float out[3]) { out[0] = m[12]; out[1] = m[13]; out[2] = m[14]; }

// Transform a direction (row vector · upper-3x3, no translation): for normals/tangents. Not
// normalised — the caller normalises after the weighted blend.
inline void transformDir(const Mat4& m, float x, float y, float z, float out[3])
{
    out[0] = x*m[0] + y*m[4] + z*m[8];
    out[1] = x*m[1] + y*m[5] + z*m[9];
    out[2] = x*m[2] + y*m[6] + z*m[10];
}

// Rotation from a unit quaternion (x,y,z,w) as a row-major v·M matrix — i.e. the transform that,
// applied as v·R, rotates the row vector v. (This is the transpose of the textbook column-major
// R·v form.) Consistent with composeTRS/decomposeTRS below; verified by AstSkeleton::selfTest.
inline Mat4 quatToMat(float x, float y, float z, float w)
{
    const float n = std::sqrt(x*x + y*y + z*z + w*w);
    if (n < 1e-12f) return identity();
    x /= n; y /= n; z /= n; w /= n;
    const float xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
    // Row-major storage of v·M (row r, col c at [r*4+c]); rotation lives in the upper 3x3.
    return {
        1-2*(yy+zz),  2*(xy+wz),    2*(xz-wy),    0,
        2*(xy-wz),    1-2*(xx+zz),  2*(yz+wx),    0,
        2*(xz+wy),    2*(yz-wx),    1-2*(xx+yy),  0,
        0,            0,            0,            1
    };
}

// Compose a local matrix from translation, quaternion (xyzw) and per-axis scale, in the row-major
// v·M convention: v' = v · (S · R) + T. So a point is scaled, rotated, then translated.
inline Mat4 composeTRS(const float t[3], const float q[4], const float s[3])
{
    Mat4 R = quatToMat(q[0], q[1], q[2], q[3]);
    Mat4 m{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m[r*4+c] = s[r] * R[r*4+c];   // scale rows (v·S then ·R = s applied per input axis row)
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
    return m;
}

// Decompose a row-major v·M local matrix into translation, quaternion (xyzw) and per-axis scale so
// that composeTRS(t,q,s) reproduces it (assuming no shear / negative determinant). Inverse of
// composeTRS; verified by AstSkeleton::selfTest.
inline void decomposeTRS(const Mat4& m, float t[3], float q[4], float s[3])
{
    t[0] = m[12]; t[1] = m[13]; t[2] = m[14];
    // Each of the first three rows is s[r] * (rotation row r); the row length is the scale.
    float rows[3][3];
    for (int r = 0; r < 3; ++r) {
        const float len = std::sqrt(m[r*4+0]*m[r*4+0] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2]);
        s[r] = len;
        const float inv = len > 1e-12f ? 1.0f/len : 0.0f;
        for (int c = 0; c < 3; ++c) rows[r][c] = m[r*4+c] * inv;
    }
    // rows[][] is now the row-major v·M rotation; recover the quaternion (Shepperd's method).
    const float m00=rows[0][0], m01=rows[0][1], m02=rows[0][2];
    const float m10=rows[1][0], m11=rows[1][1], m12=rows[1][2];
    const float m20=rows[2][0], m21=rows[2][1], m22=rows[2][2];
    const float tr = m00 + m11 + m22;
    float x,y,z,w;
    if (tr > 0) {
        float ss = std::sqrt(tr + 1.0f) * 2.0f;   // ss = 4w
        w = 0.25f*ss; x = (m12 - m21)/ss; y = (m20 - m02)/ss; z = (m01 - m10)/ss;
    } else if (m00 > m11 && m00 > m22) {
        float ss = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;  // 4x
        w = (m12 - m21)/ss; x = 0.25f*ss; y = (m01 + m10)/ss; z = (m20 + m02)/ss;
    } else if (m11 > m22) {
        float ss = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;  // 4y
        w = (m20 - m02)/ss; x = (m01 + m10)/ss; y = 0.25f*ss; z = (m12 + m21)/ss;
    } else {
        float ss = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;  // 4z
        w = (m01 - m10)/ss; x = (m20 + m02)/ss; y = (m12 + m21)/ss; z = 0.25f*ss;
    }
    const float qn = std::sqrt(x*x+y*y+z*z+w*w);
    if (qn > 1e-12f) { x/=qn; y/=qn; z/=qn; w/=qn; }
    q[0]=x; q[1]=y; q[2]=z; q[3]=w;
}

// Normalised linear quaternion interpolation (xyzw), shortest-arc. Good enough for viewport preview.
inline void quatNlerp(const float a[4], const float b[4], float u, float out[4])
{
    float dot = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
    const float sgn = dot < 0 ? -1.0f : 1.0f;   // shortest arc
    for (int i = 0; i < 4; ++i) out[i] = a[i]*(1-u) + sgn*b[i]*u;
    const float n = std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]+out[3]*out[3]);
    if (n > 1e-12f) for (int i = 0; i < 4; ++i) out[i] /= n; else { out[0]=out[1]=out[2]=0; out[3]=1; }
}

}  // namespace RigMath
