#pragma once

// libvu0's arithmetic in C++. Each function writes the same lanes as the library does and leaves
// the others as it leaves them. A matrix is four column vectors, so m[i] is column i and m * v sums
// m[i] * v[i].

namespace sce_core::vu0 {

using Vector = float[4];
using IVector = int[4];
using Matrix = float[4][4];

void CopyVector(Vector v0, Vector v1);
void CopyVectorXYZ(Vector v0, Vector v1);
void FTOI0Vector(IVector v0, Vector v1);
void FTOI4Vector(IVector v0, Vector v1);
void ITOF0Vector(Vector v0, IVector v1);
void ITOF4Vector(Vector v0, IVector v1);
void ScaleVector(Vector v0, Vector v1, float s);
void ScaleVectorXYZ(Vector v0, Vector v1, float s);
void AddVector(Vector v0, Vector v1, Vector v2);
void SubVector(Vector v0, Vector v1, Vector v2);
void MulVector(Vector v0, Vector v1, Vector v2);
void InterVector(Vector v0, Vector v1, Vector v2, float r);
void InterVectorXYZ(Vector v0, Vector v1, Vector v2, float r);
void DivVector(Vector v0, Vector v1, float q);
void DivVectorXYZ(Vector v0, Vector v1, float q);
float InnerProduct(Vector v0, Vector v1);
void OuterProduct(Vector v0, Vector v1, Vector v2);
void Normalize(Vector v0, Vector v1);
void ApplyMatrix(Vector v0, Matrix m, Vector v1);
void UnitMatrix(Matrix m);
void CopyMatrix(Matrix m0, Matrix m1);
void TransposeMatrix(Matrix m0, Matrix m1);
void MulMatrix(Matrix m0, Matrix m1, Matrix m2);
void InversMatrix(Matrix m0, Matrix m1);
void RotMatrixX(Matrix m0, Matrix m1, float rx);
void RotMatrixY(Matrix m0, Matrix m1, float ry);
void RotMatrixZ(Matrix m0, Matrix m1, float rz);
void RotMatrix(Matrix m0, Matrix m1, Vector rot);
void TransMatrix(Matrix m0, Matrix m1, Vector tv);
void CameraMatrix(Matrix m, Vector p, Vector zd, Vector yd);
void NormalLightMatrix(Matrix m, Vector l0, Vector l1, Vector l2);
void LightColorMatrix(Matrix m, Vector c0, Vector c1, Vector c2, Vector a);
void ViewScreenMatrix(Matrix m, float scrz, float ax, float ay, float cx, float cy, float zmin, float zmax, float nearz, float farz);
void DropShadowMatrix(Matrix m, Vector lp, float a, float b, float c, int mode);
int ClipAll(Vector minv, Vector maxv, Matrix ms, Vector *vm, int n);
void ClampVector(Vector v0, Vector v1, float min, float max);
int ClipScreen(Vector v0);
int ClipScreen3(Vector v0, Vector v1, Vector v2);
void RotTransPersN(IVector *v0, Matrix m0, Vector *v1, int n, int mode);
void RotTransPers(IVector v0, Matrix m0, Vector v1, int mode);

} // namespace sce_core::vu0
