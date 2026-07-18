// Software 3D renderer — platform-independent implementation

#include "platform.h"
#include "render.software.h"

// Strip alpha from ARGB color for DIB pixel format
static inline uint32_t ColorToDIB(const uint32_t argb)
{
	return argb & 0x00FFFFFF;
}

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b)
{
	Mat4 r;
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			r.m[i][j] = a.m[i][0] * b.m[0][j] +
				a.m[i][1] * b.m[1][j] +
				a.m[i][2] * b.m[2][j] +
				a.m[i][3] * b.m[3][j];
		}
	}
	return r;
}

Mat4 Mat4RotationX(const double angle)
{
	Mat4 r = Mat4::Identity();
	const double c = cos(angle), s = sin(angle);
	r.m[1][1] = c;
	r.m[1][2] = s;
	r.m[2][1] = -s;
	r.m[2][2] = c;
	return r;
}

Mat4 Mat4RotationY(const double angle)
{
	Mat4 r = Mat4::Identity();
	const double c = cos(angle), s = sin(angle);
	r.m[0][0] = c;
	r.m[0][2] = -s;
	r.m[2][0] = s;
	r.m[2][2] = c;
	return r;
}

Mat4 Mat4RotationZ(const double angle)
{
	Mat4 r = Mat4::Identity();
	const double c = cos(angle), s = sin(angle);
	r.m[0][0] = c;
	r.m[0][1] = s;
	r.m[1][0] = -s;
	r.m[1][1] = c;
	return r;
}

Mat4 Mat4Translation(const double x, const double y, const double z)
{
	Mat4 r = Mat4::Identity();
	r.m[3][0] = x;
	r.m[3][1] = y;
	r.m[3][2] = z;
	return r;
}

Mat4 Mat4PerspectiveFovLH(const double fovY, const double aspect, const double zn, const double zf)
{
	// Reversed-Z left-handed perspective.
	//
	// Maps view-space z = zn -> ndcZ = 1, view-space z = zf -> ndcZ = 0. Combined
	// with a depth buffer cleared to 0 and a `z > depthBuf` test, this distributes
	// double depth precision much more uniformly across the frustum than the standard
	// near=0/far=1 mapping. With huge near/far ratios (this game uses 0.5 / 131072),
	// the standard mapping crushes everything past view-z ~= 1000 into the last few
	// ULPs of double, which is what causes far track triangles to win the depth test
	// over closer ones at random.
	Mat4 r;
	const double yScale = 1.0 / tan(fovY / 2.0);
	const double xScale = yScale / aspect;
	r.m[0][0] = xScale;
	r.m[1][1] = yScale;
	r.m[2][2] = zn / (zn - zf); // negative
	r.m[2][3] = 1.0f;
	r.m[3][2] = zn * zf / (zf - zn); // positive
	return r;
}

Mat4 Mat4LookAtLH(const Vec3& eye, const Vec3& at, const Vec3& up)
{
	const Vec3 zaxis = Vec3Normalize(at - eye);
	const Vec3 xaxis = Vec3Normalize(Vec3Cross(up, zaxis));
	const Vec3 yaxis = Vec3Cross(zaxis, xaxis);

	Mat4 r;
	r.m[0][0] = xaxis.x;
	r.m[0][1] = yaxis.x;
	r.m[0][2] = zaxis.x;
	r.m[0][3] = 0;
	r.m[1][0] = xaxis.y;
	r.m[1][1] = yaxis.y;
	r.m[1][2] = zaxis.y;
	r.m[1][3] = 0;
	r.m[2][0] = xaxis.z;
	r.m[2][1] = yaxis.z;
	r.m[2][2] = zaxis.z;
	r.m[2][3] = 0;
	r.m[3][0] = -Vec3Dot(xaxis, eye);
	r.m[3][1] = -Vec3Dot(yaxis, eye);
	r.m[3][2] = -Vec3Dot(zaxis, eye);
	r.m[3][3] = 1.0f;
	return r;
}


SoftwareRenderer::SoftwareRenderer()
	: m_width(0), m_height(0),
	  m_matricesDirty(true),
	  m_lightDir(Vec3Normalize(Vec3(0.3f, 0.8f, 0.5f))),
	  m_ambientIntensity(0.45f), m_diffuseIntensity(0.55f),
	  m_nearClipW(0.01f)
{
	m_world = Mat4::Identity();
	m_view = Mat4::Identity();
	m_proj = Mat4::Identity();
	m_worldViewProj = Mat4::Identity();
}

SoftwareRenderer::~SoftwareRenderer()
{
}

bool SoftwareRenderer::Init(const int width, const int height)
{
	CreateBackbuffer(width, height);
	return true;
}

void SoftwareRenderer::Resize(const int width, const int height)
{
	CreateBackbuffer(width, height);
}

void SoftwareRenderer::CreateBackbuffer(const int width, const int height)
{
	m_width = width;
	m_height = height;
	m_pixels.resize(width * height);
	m_depthBuffer.resize(width * height);
}

void SoftwareRenderer::DestroyBackbuffer()
{
	m_width = 0;
	m_height = 0;
	m_pixels.clear();
	m_depthBuffer.clear();
}

void SoftwareRenderer::Clear(const uint32_t color)
{
	const uint32_t dibColor = ColorToDIB(color);
	std::fill(m_pixels.begin(), m_pixels.end(), dibColor);
}

void SoftwareRenderer::ClearDepth()
{
	// Reversed-Z: far plane = 0, near plane = 1. Clear to the far value.
	std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), 0.0f);
}

void SoftwareRenderer::SetWorldMatrix(const Mat4& m)
{
	m_world = m;
	m_matricesDirty = true;
}

void SoftwareRenderer::SetViewMatrix(const Mat4& m)
{
	m_view = m;
	m_matricesDirty = true;
}

void SoftwareRenderer::SetProjectionMatrix(const Mat4& m)
{
	m_proj = m;
	m_matricesDirty = true;

	// Recover the projection's near plane (zn) from the matrix so the near-plane
	// clipper matches the actual frustum. Works for both standard and reversed-Z
	// LH perspective matrices: solve view-z at ndcZ = 0 and at ndcZ = 1, take the
	// smaller one (that is the near plane in either convention).
	//   ndcZ = m22 + m32 / z   (since w = z for these projections)
	//   ndcZ = 0  =>  z = -m32 / m22
	//   ndcZ = 1  =>  z =  m32 / (1 - m22)
	// Clip-space w equals view-space z for this projection (m[2][3]=1, m[3][3]=0),
	// so we clip against w >= zn directly. Bias slightly above zn to avoid producing
	// vertices with ndcZ exactly at the near boundary / a divide that's numerically
	// fragile.
	const double m22 = m.m[2][2];
	const double m32 = m.m[3][2];
	if (fabs(m22) > 1e-12f && fabs(1.0 - m22) > 1e-12)
	{
		const double zAtNdc0 = -m32 / m22;
		const double zAtNdc1 = m32 / (1.0f - m22);
		const double zn = (zAtNdc0 < zAtNdc1) ? zAtNdc0 : zAtNdc1;
		m_nearClipW = zn > 0.0f ? zn * 1.001f : 0.01f;
	}
}

void SoftwareRenderer::UpdateCombinedMatrix()
{
	if (m_matricesDirty)
	{
		const Mat4 wv = Mat4Multiply(m_world, m_view);
		m_worldViewProj = Mat4Multiply(wv, m_proj);
		m_matricesDirty = false;
	}
}

static ClipSpaceVert LerpClipVert(const ClipSpaceVert& a, const ClipSpaceVert& b, const double t)
{
	ClipSpaceVert r;
	r.x = a.x + (b.x - a.x) * t;
	r.y = a.y + (b.y - a.y) * t;
	r.z = a.z + (b.z - a.z) * t;
	r.w = a.w + (b.w - a.w) * t;
	r.tu = a.tu + (b.tu - a.tu) * t;
	r.tv = a.tv + (b.tv - a.tv) * t;
	const int ra = a.color >> 16 & 0xFF, ga = a.color >> 8 & 0xFF, ba = a.color & 0xFF;
	const int rb = b.color >> 16 & 0xFF, gb = b.color >> 8 & 0xFF, bb = b.color & 0xFF;
	const int rc = ra + static_cast<int>((rb - ra) * t);
	const int gc = ga + static_cast<int>((gb - ga) * t);
	const int bc = ba + static_cast<int>((bb - ba) * t);
	r.color = XRGB(rc, gc, bc);
	return r;
}

ClipSpaceVert SoftwareRenderer::TransformToClipSpace(const SWVertex& v)
{
	UpdateCombinedMatrix();
	const double x = v.pos.x, y = v.pos.y, z = v.pos.z;
	ClipSpaceVert cv;
	cv.x = x * m_worldViewProj.m[0][0] + y * m_worldViewProj.m[1][0] + z * m_worldViewProj.m[2][0] + m_worldViewProj.m[
		3][0];
	cv.y = x * m_worldViewProj.m[0][1] + y * m_worldViewProj.m[1][1] + z * m_worldViewProj.m[2][1] + m_worldViewProj.m[
		3][1];
	cv.z = x * m_worldViewProj.m[0][2] + y * m_worldViewProj.m[1][2] + z * m_worldViewProj.m[2][2] + m_worldViewProj.m[
		3][2];
	cv.w = x * m_worldViewProj.m[0][3] + y * m_worldViewProj.m[1][3] + z * m_worldViewProj.m[2][3] + m_worldViewProj.m[
		3][3];
	cv.color = v.color;
	cv.tu = v.tu;
	cv.tv = v.tv;
	return cv;
}

TransformedVert SoftwareRenderer::PerspectiveDivide(const ClipSpaceVert& cv)
{
	TransformedVert tv;
	double w = cv.w;
	if (fabs(w) < 1e-10) w = 1e-10;
	const double invW = 1.0 / w;
	const double ndcX = cv.x * invW;
	const double ndcY = cv.y * invW;
	const double ndcZ = cv.z * invW;
	tv.x = (ndcX + 1.0f) * 0.5 * m_width;
	tv.y = (1.0 - ndcY) * 0.5 * m_height;
	tv.z = ndcZ;
	tv.w = invW;
	tv.color = cv.color;
	tv.tu = cv.tu;
	tv.tv = cv.tv;
	return tv;
}

int SoftwareRenderer::ClipTriangleNearPlane(const ClipSpaceVert in[3], ClipSpaceVert out[6])
{
	// Sutherland-Hodgman clip against w >= m_nearClipW (near plane).
	// m_nearClipW is derived from the projection matrix so that survivors of
	// this clip never produce ndcZ < 0 (which would otherwise win every depth
	// test and paint over closer geometry).
	const double nearW = m_nearClipW;
	ClipSpaceVert poly[6];
	int numVerts = 0;

	for (int i = 0; i < 3; i++)
	{
		const ClipSpaceVert& curr = in[i];
		const ClipSpaceVert& next = in[(i + 1) % 3];
		const bool currInside = curr.w >= nearW;
		const bool nextInside = next.w >= nearW;

		if (currInside)
		{
			poly[numVerts++] = curr;
			if (!nextInside)
			{
				const double t = (nearW - curr.w) / (next.w - curr.w);
				poly[numVerts++] = LerpClipVert(curr, next, t);
			}
		}
		else if (nextInside)
		{
			const double t = (nearW - curr.w) / (next.w - curr.w);
			poly[numVerts++] = LerpClipVert(curr, next, t);
		}
	}

	// Fan-triangulate the clipped polygon (at most 2 triangles from a quad)
	int numTris = 0;
	for (int i = 1; i < numVerts - 1; i++)
	{
		out[numTris * 3 + 0] = poly[0];
		out[numTris * 3 + 1] = poly[i];
		out[numTris * 3 + 2] = poly[i + 1];
		numTris++;
	}
	return numTris;
}

void SoftwareRenderer::ProcessAndRasterizeTriangle(const ClipSpaceVert& cv0, const ClipSpaceVert& cv1,
                                                   const ClipSpaceVert& cv2, const SWTexture* tex)
{
	const double nearW = m_nearClipW;

	// All vertices behind camera - skip entirely
	if (cv0.w < nearW && cv1.w < nearW && cv2.w < nearW) return;

	const bool needsClip = cv0.w < nearW || cv1.w < nearW || cv2.w < nearW;

	if (needsClip)
	{
		const ClipSpaceVert input[3] = {cv0, cv1, cv2};
		ClipSpaceVert clipped[6];
		const int numTris = ClipTriangleNearPlane(input, clipped);

		for (int t = 0; t < numTris; t++)
		{
			TransformedVert tv0 = PerspectiveDivide(clipped[t * 3 + 0]);
			TransformedVert tv1 = PerspectiveDivide(clipped[t * 3 + 1]);
			TransformedVert tv2 = PerspectiveDivide(clipped[t * 3 + 2]);

			// Reversed-Z: past-far means ndcZ < 0. (Past-near is impossible after w-clip.)
			if (tv0.z < 0 && tv1.z < 0 && tv2.z < 0) continue;

			// No back-face culling: the track is single-sided, so culling caused
			// the road to vanish whenever the eye crossed below the surface
			// (banked turns, hill crests, hard landings). The rasterizer handles
			// both windings correctly via signed area; back-facing triangles
			// shade as ambient-only (N.L clamped at 0 in ApplyFaceLighting),
			// which is acceptable for the rare cases this matters.
			RasterizeTriangle(tv0, tv1, tv2, tex);
		}
	}
	else
	{
		const TransformedVert tv0 = PerspectiveDivide(cv0);
		const TransformedVert tv1 = PerspectiveDivide(cv1);
		const TransformedVert tv2 = PerspectiveDivide(cv2);

		// Reversed-Z: past-far means ndcZ < 0.
		if (tv0.z < 0 && tv1.z < 0 && tv2.z < 0) return;

		RasterizeTriangle(tv0, tv1, tv2, tex);
	}
}

static inline uint32_t SampleTexture(const SWTexture& tex, double u, double v)
{
	if (tex.pixels.empty()) return 0xFFFFFF;
	u = u - floor(u);
	v = v - floor(v);
	int tx = static_cast<int>(u * tex.width) % tex.width;
	int ty = static_cast<int>(v * tex.height) % tex.height;
	if (tx < 0) tx += tex.width;
	if (ty < 0) ty += tex.height;
	return tex.pixels[ty * tex.width + tx];
}

void SoftwareRenderer::RasterizeTriangle(const TransformedVert& v0, const TransformedVert& v1,
                                         const TransformedVert& v2, const SWTexture* tex)
{
	// Bounding box clipped to screen
	double minX = (std::min)({v0.x, v1.x, v2.x});
	double maxX = (std::max)({v0.x, v1.x, v2.x});
	double minY = (std::min)({v0.y, v1.y, v2.y});
	double maxY = (std::max)({v0.y, v1.y, v2.y});

	int iMinX = (std::max)(0, static_cast<int>(floor(minX)));
	int iMaxX = (std::min)(m_width - 1, static_cast<int>(ceil(maxX)));
	int iMinY = (std::max)(0, static_cast<int>(floor(minY)));
	int iMaxY = (std::min)(m_height - 1, static_cast<int>(ceil(maxY)));

	if (iMinX > iMaxX || iMinY > iMaxY) return;

	double area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
	if (fabs(area) < 1e-4) return;
	double invArea = 1.0 / area;

	// Edge function per-pixel increments (additions replace per-pixel multiplications)
	// w0 = EdgeFunction(v1, v2, P)  ->  dw0/dx = v1.y - v2.y,  dw0/dy = v2.x - v1.x
	double stepX0 = v1.y - v2.y, stepY0 = v2.x - v1.x;
	double stepX1 = v2.y - v0.y, stepY1 = v0.x - v2.x;
	double stepX2 = v0.y - v1.y, stepY2 = v1.x - v0.x;

	// Normalize sign so inside test is always w >= 0
	const bool negArea = area < 0;
	if (negArea)
	{
		stepX0 = -stepX0;
		stepY0 = -stepY0;
		stepX1 = -stepX1;
		stepY1 = -stepY1;
		stepX2 = -stepX2;
		stepY2 = -stepY2;
		invArea = -invArea;
	}

	// Top-left fill rule bias: an edge is "top-left" when it's either:
	//   - top edge:   horizontal (dy == 0) and goes to the left (dx < 0)
	//   - left edge:  goes downward (dy > 0)
	// where (dx, dy) is the edge vector from start to end vertex (after sign-normalization
	// so winding is CCW). Edge i is opposite vertex i, going from v[(i+1)%3] to v[(i+2)%3].
	//
	// The bias must be large enough to survive floating-point rounding when added to the
	// initial row edge value (which can reach ~1e5-1e6 for full-screen triangles, where a
	// double ULP is ~0.06-0.25). A tiny bias like 1/65536 is silently rounded away, leaving
	// adjacent triangles to double-cover shared edges and producing depth-tie flicker.
	// 1/16 (sub-pixel-precision unit) is well above FP noise and well below any non-
	// degenerate per-pixel step, so it cleanly excludes shared edges without losing
	// genuine coverage.
	constexpr double FILL_RULE_BIAS = -1.0f / 16.0f;
	auto topLeftBias = [negArea](const double vax, const double vay, const double vbx, const double vby) -> double
	{
		const double dx = negArea ? vax - vbx : vbx - vax;
		const double dy = negArea ? vay - vby : vby - vay;
		const bool topLeft = (dy == 0.0f && dx < 0.0f) || dy > 0.0f;
		return topLeft ? 0.0f : FILL_RULE_BIAS;
	};
	const double bias0 = topLeftBias(v1.x, v1.y, v2.x, v2.y);
	const double bias1 = topLeftBias(v2.x, v2.y, v0.x, v0.y);
	const double bias2 = topLeftBias(v0.x, v0.y, v1.x, v1.y);

	// Edge values at top-left pixel center
	const double pxStart = iMinX + 0.5f;
	const double pyStart = iMinY + 0.5f;
	double rowW0 = EdgeFunction(v1.x, v1.y, v2.x, v2.y, pxStart, pyStart);
	double rowW1 = EdgeFunction(v2.x, v2.y, v0.x, v0.y, pxStart, pyStart);
	double rowW2 = EdgeFunction(v0.x, v0.y, v1.x, v1.y, pxStart, pyStart);
	if (negArea)
	{
		rowW0 = -rowW0;
		rowW1 = -rowW1;
		rowW2 = -rowW2;
	}
	rowW0 += bias0;
	rowW1 += bias1;
	rowW2 += bias2;

	// Pre-compute per-pixel attribute increments (one-time cost per triangle)
	double zStepX = (stepX0 * v0.z + stepX1 * v1.z + stepX2 * v2.z) * invArea;
	double zStepY = (stepY0 * v0.z + stepY1 * v1.z + stepY2 * v2.z) * invArea;
	double bary0 = rowW0 * invArea, bary1 = rowW1 * invArea, bary2 = rowW2 * invArea;
	double zRow = bary0 * v0.z + bary1 * v1.z + bary2 * v2.z;

	// Hoist member fields into local __restrict pointers for the inner loop
	uint32_t* __restrict pixels = m_pixels.data();
	double* __restrict depthBuf = m_depthBuffer.data();
	const int scanWidth = m_width;

	// Branch on rendering mode OUTSIDE the scanline loops
	if (tex)
	{
		// Perspective-correct texturing: interpolate u/w, v/w, 1/w linearly in screen
		// space, then divide per-pixel. v0.w/v1.w/v2.w already hold 1/w from the
		// perspective divide.
		const double u0 = v0.tu * v0.w, u1 = v1.tu * v1.w, u2 = v2.tu * v2.w;
		const double vv0 = v0.tv * v0.w, vv1 = v1.tv * v1.w, vv2 = v2.tv * v2.w;
		const double iw0 = v0.w, iw1 = v1.w, iw2 = v2.w;

		const double uStepX = (stepX0 * u0 + stepX1 * u1 + stepX2 * u2) * invArea;
		const double uStepY = (stepY0 * u0 + stepY1 * u1 + stepY2 * u2) * invArea;
		const double vStepX = (stepX0 * vv0 + stepX1 * vv1 + stepX2 * vv2) * invArea;
		const double vStepY = (stepY0 * vv0 + stepY1 * vv1 + stepY2 * vv2) * invArea;
		const double iwStepX = (stepX0 * iw0 + stepX1 * iw1 + stepX2 * iw2) * invArea;
		const double iwStepY = (stepY0 * iw0 + stepY1 * iw1 + stepY2 * iw2) * invArea;
		double uRow = bary0 * u0 + bary1 * u1 + bary2 * u2;
		double vRow = bary0 * vv0 + bary1 * vv1 + bary2 * vv2;
		double iwRow = bary0 * iw0 + bary1 * iw1 + bary2 * iw2;

		for (int py = iMinY; py <= iMaxY; py++)
		{
			double w0 = rowW0, w1 = rowW1, w2 = rowW2;
			double z = zRow, u = uRow, v = vRow, iw = iwRow;
			const int rowOff = py * scanWidth;

			for (int px = iMinX; px <= iMaxX; px++)
			{
				if (w0 >= 0 && w1 >= 0 && w2 >= 0)
				{
					const int idx = rowOff + px;
					if (z > depthBuf[idx])
					{
						depthBuf[idx] = z;
						const double w = iw != 0.0f ? 1.0f / iw : 0.0f;
						pixels[idx] = ColorToDIB(SampleTexture(*tex, u * w, v * w));
					}
				}
				w0 += stepX0;
				w1 += stepX1;
				w2 += stepX2;
				z += zStepX;
				u += uStepX;
				v += vStepX;
				iw += iwStepX;
			}

			rowW0 += stepY0;
			rowW1 += stepY1;
			rowW2 += stepY2;
			zRow += zStepY;
			uRow += uStepY;
			vRow += vStepY;
			iwRow += iwStepY;
		}
	}
	else
	{
		int r0 = v0.color >> 16 & 0xFF, g0 = v0.color >> 8 & 0xFF, b0 = v0.color & 0xFF;
		int r1 = v1.color >> 16 & 0xFF, g1 = v1.color >> 8 & 0xFF, b1 = v1.color & 0xFF;
		int r2 = v2.color >> 16 & 0xFF, g2 = v2.color >> 8 & 0xFF, b2 = v2.color & 0xFF;

		double rStepX = (stepX0 * r0 + stepX1 * r1 + stepX2 * r2) * invArea;
		double gStepX = (stepX0 * g0 + stepX1 * g1 + stepX2 * g2) * invArea;
		double bStepX = (stepX0 * b0 + stepX1 * b1 + stepX2 * b2) * invArea;
		double rStepY = (stepY0 * r0 + stepY1 * r1 + stepY2 * r2) * invArea;
		double gStepY = (stepY0 * g0 + stepY1 * g1 + stepY2 * g2) * invArea;
		double bStepY = (stepY0 * b0 + stepY1 * b1 + stepY2 * b2) * invArea;
		double rRow = bary0 * r0 + bary1 * r1 + bary2 * r2;
		double gRow = bary0 * g0 + bary1 * g1 + bary2 * g2;
		double bRow = bary0 * b0 + bary1 * b1 + bary2 * b2;

		for (int py = iMinY; py <= iMaxY; py++)
		{
			double w0 = rowW0, w1 = rowW1, w2 = rowW2;
			double z = zRow, rf = rRow, gf = gRow, bf = bRow;
			const int rowOff = py * scanWidth;

			for (int px = iMinX; px <= iMaxX; px++)
			{
				if (w0 >= 0 && w1 >= 0 && w2 >= 0)
				{
					const int idx = rowOff + px;
					if (z > depthBuf[idx])
					{
						depthBuf[idx] = z;
						int ri = (std::min)(255, (std::max)(0, static_cast<int>(rf)));
						int gi = (std::min)(255, (std::max)(0, static_cast<int>(gf)));
						int bi = (std::min)(255, (std::max)(0, static_cast<int>(bf)));
						pixels[idx] = static_cast<uint32_t>(ri << 16 | gi << 8 | bi);
					}
				}
				w0 += stepX0;
				w1 += stepX1;
				w2 += stepX2;
				z += zStepX;
				rf += rStepX;
				gf += gStepX;
				bf += bStepX;
			}

			rowW0 += stepY0;
			rowW1 += stepY1;
			rowW2 += stepY2;
			zRow += zStepY;
			rRow += rStepY;
			gRow += gStepY;
			bRow += bStepY;
		}
	}
}

void SoftwareRenderer::ApplyFaceLighting(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                         ClipSpaceVert& cv0, ClipSpaceVert& cv1, ClipSpaceVert& cv2)
{
	// Compute face normal in object space
	const Vec3 edge1 = p1 - p0;
	const Vec3 edge2 = p2 - p0;
	Vec3 n = Vec3Cross(edge1, edge2);
	const double len = Vec3Length(n);
	if (len < 1e-8f) return; // degenerate triangle
	n = n / len;

	// Transform normal to world space (upper-left 3x3 of world matrix, rotation only)
	Vec3 wn;
	wn.x = n.x * m_world.m[0][0] + n.y * m_world.m[1][0] + n.z * m_world.m[2][0];
	wn.y = n.x * m_world.m[0][1] + n.y * m_world.m[1][1] + n.z * m_world.m[2][1];
	wn.z = n.x * m_world.m[0][2] + n.y * m_world.m[1][2] + n.z * m_world.m[2][2];
	wn = Vec3Normalize(wn);

	// N dot L
	double NdotL = Vec3Dot(wn, m_lightDir);
	if (NdotL < 0.0f) NdotL = 0.0f;
	double intensity = m_ambientIntensity + m_diffuseIntensity * NdotL;
	if (intensity > 1.0f) intensity = 1.0f;

	// Scale vertex colors (clamp to [0,255] to avoid bleeding into adjacent channels)
	auto scale = [intensity](const uint32_t c) -> uint32_t
	{
		int r = static_cast<int>((c >> 16 & 0xFF) * intensity);
		int g = static_cast<int>((c >> 8 & 0xFF) * intensity);
		int b = static_cast<int>((c & 0xFF) * intensity);
		if (r > 255) r = 255;
		if (r < 0) r = 0;
		if (g > 255) g = 255;
		if (g < 0) g = 0;
		if (b > 255) b = 255;
		if (b < 0) b = 0;
		return XRGB(r, g, b);
	};
	cv0.color = scale(cv0.color);
	cv1.color = scale(cv1.color);
	cv2.color = scale(cv2.color);
}

void SoftwareRenderer::DrawTriangleList(const SWVertex* verts, const int startVertex, const int numTriangles,
                                        const SWTexture* tex)
{
	for (int i = 0; i < numTriangles; i++)
	{
		const int base = startVertex + i * 3;
		ClipSpaceVert cv0 = TransformToClipSpace(verts[base + 0]);
		ClipSpaceVert cv1 = TransformToClipSpace(verts[base + 1]);
		ClipSpaceVert cv2 = TransformToClipSpace(verts[base + 2]);

		ApplyFaceLighting(verts[base + 0].pos, verts[base + 1].pos, verts[base + 2].pos, cv0, cv1, cv2);

		ProcessAndRasterizeTriangle(cv0, cv1, cv2, tex);
	}
}

void SoftwareRenderer::DrawIndexedTriangleList(const SWVertex* verts, const uint32_t* indices, const int startIndex,
                                               const int numTriangles, const SWTexture* tex)
{
	for (int i = 0; i < numTriangles; i++)
	{
		const int base = startIndex + i * 3;
		const SWVertex& sv0 = verts[indices[base + 0]];
		const SWVertex& sv1 = verts[indices[base + 1]];
		const SWVertex& sv2 = verts[indices[base + 2]];
		ClipSpaceVert cv0 = TransformToClipSpace(sv0);
		ClipSpaceVert cv1 = TransformToClipSpace(sv1);
		ClipSpaceVert cv2 = TransformToClipSpace(sv2);

		ApplyFaceLighting(sv0.pos, sv1.pos, sv2.pos, cv0, cv1, cv2);

		ProcessAndRasterizeTriangle(cv0, cv1, cv2, tex);
	}
}

void SoftwareRenderer::DrawScreenTriangleFan(const Point2D* pts, const int numPoints, const uint32_t color)
{
	if (numPoints < 3) return;

	const uint32_t dibColor = ColorToDIB(color);
	uint32_t* __restrict pixels = m_pixels.data();
	const int scanWidth = m_width;

	for (int i = 1; i < numPoints - 1; i++)
	{
		double x0 = pts[0].x, y0 = pts[0].y;
		double x1 = pts[i].x, y1 = pts[i].y;
		double x2 = pts[i + 1].x, y2 = pts[i + 1].y;

		const double fminX = (std::min)({x0, x1, x2});
		const double fmaxX = (std::max)({x0, x1, x2});
		const double fminY = (std::min)({y0, y1, y2});
		const double fmaxY = (std::max)({y0, y1, y2});

		const int iMinX = (std::max)(0, static_cast<int>(floor(fminX)));
		const int iMaxX = (std::min)(m_width - 1, static_cast<int>(ceil(fmaxX)));
		const int iMinY = (std::max)(0, static_cast<int>(floor(fminY)));
		const int iMaxY = (std::min)(m_height - 1, static_cast<int>(ceil(fmaxY)));

		const double area = EdgeFunction(x0, y0, x1, y1, x2, y2);
		if (fabs(area) < 1e-4f) continue;

		// Incremental edge function steps
		double stepX0 = y1 - y2, stepY0 = x2 - x1;
		double stepX1 = y2 - y0, stepY1 = x0 - x2;
		double stepX2 = y0 - y1, stepY2 = x1 - x0;

		const bool negArea = area < 0;
		if (negArea)
		{
			stepX0 = -stepX0;
			stepY0 = -stepY0;
			stepX1 = -stepX1;
			stepY1 = -stepY1;
			stepX2 = -stepX2;
			stepY2 = -stepY2;
		}

		// Top-left fill rule bias to prevent double-cover and shared-edge gaps. See the
		// matching comment in RasterizeTriangle for why 1/16 is the right magnitude here
		// rather than 1/65536 (which is silently rounded away in double arithmetic).
		constexpr double FILL_RULE_BIAS = -1.0f / 16.0f;
		auto topLeftBias = [negArea](const double vax, const double vay,
		                             const double vbx, const double vby) -> double
		{
			const double dx = negArea ? vax - vbx : vbx - vax;
			const double dy = negArea ? vay - vby : vby - vay;
			const bool topLeft = (dy == 0.0f && dx < 0.0f) || dy > 0.0f;
			return topLeft ? 0.0f : FILL_RULE_BIAS;
		};
		const double bias0 = topLeftBias(x1, y1, x2, y2);
		const double bias1 = topLeftBias(x2, y2, x0, y0);
		const double bias2 = topLeftBias(x0, y0, x1, y1);

		const double pxStart = iMinX + 0.5f, pyStart = iMinY + 0.5f;
		double rw0 = EdgeFunction(x1, y1, x2, y2, pxStart, pyStart);
		double rw1 = EdgeFunction(x2, y2, x0, y0, pxStart, pyStart);
		double rw2 = EdgeFunction(x0, y0, x1, y1, pxStart, pyStart);
		if (negArea)
		{
			rw0 = -rw0;
			rw1 = -rw1;
			rw2 = -rw2;
		}
		rw0 += bias0;
		rw1 += bias1;
		rw2 += bias2;

		for (int py = iMinY; py <= iMaxY; py++)
		{
			// Find contiguous inside span (triangle is convex) then fill with std::fill
			double w0 = rw0, w1 = rw1, w2 = rw2;
			int spanStart = -1, spanEnd = -1;

			for (int px = iMinX; px <= iMaxX; px++)
			{
				if (w0 >= 0 && w1 >= 0 && w2 >= 0)
				{
					if (spanStart < 0) spanStart = px;
					spanEnd = px;
				}
				else if (spanStart >= 0)
				{
					break; // Exited the convex region
				}
				w0 += stepX0;
				w1 += stepX1;
				w2 += stepX2;
			}

			if (spanStart >= 0)
			{
				uint32_t* row = pixels + py * scanWidth;
				std::fill(row + spanStart, row + spanEnd + 1, dibColor);
			}

			rw0 += stepY0;
			rw1 += stepY1;
			rw2 += stepY2;
		}
	}
}

void SoftwareRenderer::FillRect(int x1, int y1, int x2, int y2, const uint32_t color)
{
	const uint32_t dibColor = ColorToDIB(color);

	x1 = (std::max)(0, (std::min)(x1, m_width - 1));
	x2 = (std::max)(0, (std::min)(x2, m_width - 1));
	y1 = (std::max)(0, (std::min)(y1, m_height - 1));
	y2 = (std::max)(0, (std::min)(y2, m_height - 1));

	for (int y = y1; y <= y2; y++)
	{
		const auto row = m_pixels.data() + y * m_width;
		std::fill(row + x1, row + x2 + 1, dibColor);
	}
}

// Constant: font8x8_basic
// Contains an 8x8 font map for unicode points U+0000 - U+007F (basic latin)
static uint8_t font8x8[128][8] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0000 (nul)
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0001
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0002
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0003
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0004
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0005
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0006
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0007
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0008
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0009
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000A
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000B
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000C
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000D
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000E
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000F
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0010
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0011
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0012
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0013
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0014
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0015
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0016
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0017
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0018
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0019
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001A
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001B
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001C
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001D
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001E
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001F
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
	{0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // U+0021 (!)
	{0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0022 (")
	{0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // U+0023 (#)
	{0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // U+0024 ($)
	{0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // U+0025 (%)
	{0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // U+0026 (&)
	{0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
	{0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // U+0028 (()
	{0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // U+0029 ())
	{0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // U+002A (*)
	{0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // U+002B (+)
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+002C (,)
	{0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // U+002D (-)
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+002E (.)
	{0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
	{0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // U+0030 (0)
	{0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // U+0031 (1)
	{0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // U+0032 (2)
	{0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // U+0033 (3)
	{0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // U+0034 (4)
	{0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // U+0035 (5)
	{0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // U+0036 (6)
	{0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // U+0037 (7)
	{0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+0038 (8)
	{0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // U+0039 (9)
	{0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+003A (:)
	{0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+003B (;)
	{0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // U+003C (<)
	{0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // U+003D (=)
	{0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // U+003E (>)
	{0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // U+003F (?)
	{0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // U+0040 (@)
	{0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // U+0041 (A)
	{0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // U+0042 (B)
	{0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // U+0043 (C)
	{0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // U+0044 (D)
	{0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // U+0045 (E)
	{0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // U+0046 (F)
	{0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // U+0047 (G)
	{0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // U+0048 (H)
	{0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0049 (I)
	{0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // U+004A (J)
	{0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // U+004B (K)
	{0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // U+004C (L)
	{0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // U+004D (M)
	{0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // U+004E (N)
	{0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // U+004F (O)
	{0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // U+0050 (P)
	{0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // U+0051 (Q)
	{0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // U+0052 (R)
	{0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // U+0053 (S)
	{0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0054 (T)
	{0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U+0055 (U)
	{0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0056 (V)
	{0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // U+0057 (W)
	{0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // U+0058 (X)
	{0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+0059 (Y)
	{0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // U+005A (Z)
	{0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // U+005B ([)
	{0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // U+005C (\)
	{0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // U+005D (])
	{0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // U+005E (^)
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // U+005F (_)
	{0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0060 (`)
	{0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // U+0061 (a)
	{0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // U+0062 (b)
	{0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // U+0063 (c)
	{0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // U+0064 (d)
	{0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // U+0065 (e)
	{0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // U+0066 (f)
	{0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0067 (g)
	{0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // U+0068 (h)
	{0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0069 (i)
	{0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // U+006A (j)
	{0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // U+006B (k)
	{0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+006C (l)
	{0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // U+006D (m)
	{0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // U+006E (n)
	{0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+006F (o)
	{0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+0070 (p)
	{0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // U+0071 (q)
	{0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // U+0072 (r)
	{0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // U+0073 (s)
	{0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // U+0074 (t)
	{0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // U+0075 (u)
	{0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0076 (v)
	{0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // U+0077 (w)
	{0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // U+0078 (x)
	{0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0079 (y)
	{0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // U+007A (z)
	{0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // U+007B ({)
	{0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+007C (|)
	{0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // U+007D (})
	{0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007E (~)
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} // U+007F
};

void SoftwareRenderer::DrawGameText(const int x, const int y, const std::wstring_view text, const uint32_t color)
{
	const uint32_t dibColor = ColorToDIB(color);
	uint32_t* __restrict pixels = m_pixels.data();
	const int w = m_width;
	const int h = m_height;
	int cx = x;

	for (const wchar_t ch : text)
	{
		const int ci = ch < 128 ? static_cast<int>(ch) : 0;
		const uint8_t* glyph = font8x8[ci];

		for (int row = 0; row < 8; row++)
		{
			const int py = y + row;
			if (py < 0 || py >= h) continue;
			const uint8_t bits = glyph[row];
			for (int col = 0; col < 8; col++)
			{
				if (bits & 1 << col)
				{
					const int px = cx + col;
					if (px >= 0 && px < w)
						pixels[py * w + px] = dibColor;
				}
			}
		}
		cx += 8;
	}
}

void SoftwareRenderer::DrawTextLargeCentered(const int y, const std::wstring_view text, const uint32_t color)
{
	constexpr int scale = 2;
	constexpr int charW = 8 * scale;
	const int textWidth = static_cast<int>(text.size()) * charW;
	const int x = (m_width - textWidth) / 2;

	const uint32_t dibColor = ColorToDIB(color);
	uint32_t* __restrict pixels = m_pixels.data();
	const int w = m_width;
	const int h = m_height;
	int cx = x;

	for (const wchar_t ch : text)
	{
		const int ci = ch < 128 ? static_cast<int>(ch) : 0;
		const uint8_t* glyph = font8x8[ci];

		for (int row = 0; row < 8; row++)
		{
			const uint8_t bits = glyph[row];
			for (int col = 0; col < 8; col++)
			{
				if (bits & 1 << col)
				{
					const int px0 = cx + col * scale;
					const int py0 = y + row * scale;
					for (int sy = 0; sy < scale; sy++)
					{
						const int py = py0 + sy;
						if (py < 0 || py >= h) continue;
						for (int sx = 0; sx < scale; sx++)
						{
							const int px = px0 + sx;
							if (px >= 0 && px < w)
								pixels[py * w + px] = dibColor;
						}
					}
				}
			}
		}
		cx += charW;
	}
}
