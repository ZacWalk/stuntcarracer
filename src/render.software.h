#pragma once

// Software 3D renderer — platform-independent public API
// Platform-specific implementation lives in SoftwareRenderer.cpp

#include <cstdint>
#include <cstdarg>


// 3D vector
struct Vec3
{
	float x, y, z;

	Vec3() : x(0), y(0), z(0)
	{
	}

	Vec3(const float _x, const float _y, const float _z) : x(_x), y(_y), z(_z)
	{
	}

	Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
	Vec3 operator*(const float s) const { return Vec3(x * s, y * s, z * s); }

	Vec3 operator/(const float s) const
	{
		const float inv = 1.0f / s;
		return Vec3(x * inv, y * inv, z * inv);
	}

	Vec3& operator+=(const Vec3& v)
	{
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}

	Vec3& operator-=(const Vec3& v)
	{
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}
};

inline Vec3 Vec3Cross(const Vec3& a, const Vec3& b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline float Vec3Dot(const Vec3& a, const Vec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float Vec3Length(const Vec3& v)
{
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 Vec3Normalize(const Vec3& v)
{
	const float len = Vec3Length(v);
	if (len < 1e-8f) return Vec3(0, 0, 0);
	return v / len;
}

// 4x4 row-major matrix
struct Mat4
{
	float m[4][4];

	Mat4() { memset(m, 0, sizeof(m)); }

	static Mat4 Identity()
	{
		Mat4 r;
		r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
		return r;
	}

	float& operator()(const int row, const int col) { return m[row][col]; }
	float operator()(const int row, const int col) const { return m[row][col]; }
};

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 Mat4RotationX(float angle);
Mat4 Mat4RotationY(float angle);
Mat4 Mat4RotationZ(float angle);
Mat4 Mat4Translation(float x, float y, float z);
Mat4 Mat4PerspectiveFovLH(float fovY, float aspect, float zn, float zf);
Mat4 Mat4LookAtLH(const Vec3& eye, const Vec3& at, const Vec3& up);

// Vertex with position, color, and texture coordinates
struct SWVertex
{
	Vec3 pos;
	uint32_t color;
	float tu, tv;
};

// Screen-space vertex after projection
struct TransformedVert
{
	float x, y, z, w; // screen x,y; z = depth [0,1]; w = 1/w
	uint32_t color;
	float tu, tv;
};

// Homogeneous clip-space vertex for near-plane clipping
struct ClipSpaceVert
{
	float x, y, z, w; // homogeneous clip space
	uint32_t color;
	float tu, tv;
};

// 2D RGBA texture loaded from BMP resource
struct SWTexture
{
	std::vector<uint32_t> pixels;
	int width = 0;
	int height = 0;
};


// Main rendering context — platform-specific backend (GDI on Windows, etc.)
class SoftwareRenderer
{
public:
	SoftwareRenderer();
	~SoftwareRenderer();

	SoftwareRenderer(const SoftwareRenderer&) = delete;
	SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
	SoftwareRenderer(SoftwareRenderer&&) = default;
	SoftwareRenderer& operator=(SoftwareRenderer&&) = default;

	// Platform-specific init/present — called only from platform layer
	bool Init(int width, int height);
	void Resize(int width, int height);

	void Clear(uint32_t color);
	void ClearDepth();

	void SetWorldMatrix(const Mat4& m);
	void SetViewMatrix(const Mat4& m);
	void SetProjectionMatrix(const Mat4& m);

	void SetDepthTestEnabled(const bool enabled) { m_depthTest = enabled; }
	void SetCullMode(const int mode) { m_cullMode = mode; }

	void SetLightingEnabled(const bool enabled) { m_lightingEnabled = enabled; }
	void SetLightDirection(const Vec3& dir) { m_lightDir = Vec3Normalize(dir); }
	void SetAmbientIntensity(const float a) { m_ambientIntensity = a; }
	void SetDiffuseIntensity(const float d) { m_diffuseIntensity = d; }

	void DrawTriangleList(const SWVertex* verts, int startVertex, int numTriangles, const SWTexture* tex);
	void DrawIndexedTriangleList(const SWVertex* verts, const uint32_t* indices, int startIndex, int numTriangles,
	                             const SWTexture* tex);

	void DrawScreenTriangleFan(const Point2D* pts, int numPoints, uint32_t color);
	void FillRect(int x1, int y1, int x2, int y2, uint32_t color);

	void DrawGameText(int x, int y, std::wstring_view text, uint32_t color);
	void DrawTextLargeCentered(int y, std::wstring_view text, uint32_t color);

	int GetWidth() const { return m_width; }
	int GetHeight() const { return m_height; }
	const uint32_t* const GetPixels() const { return m_pixels.data(); }

	// Cull modes
	static constexpr int CULL_NONE = 0;
	static constexpr int CULL_CW = 1;
	static constexpr int CULL_CCW = 2;

private:
	void CreateBackbuffer(int width, int height);
	void DestroyBackbuffer();

	TransformedVert TransformVertex(const SWVertex& v);
	ClipSpaceVert TransformToClipSpace(const SWVertex& v);
	TransformedVert PerspectiveDivide(const ClipSpaceVert& cv);
	int ClipTriangleNearPlane(const ClipSpaceVert in[3], ClipSpaceVert out[6]);
	void ProcessAndRasterizeTriangle(const ClipSpaceVert& cv0, const ClipSpaceVert& cv1, const ClipSpaceVert& cv2,
	                                 const SWTexture* tex);
	void ApplyFaceLighting(const Vec3& p0, const Vec3& p1, const Vec3& p2,
	                       ClipSpaceVert& cv0, ClipSpaceVert& cv1, ClipSpaceVert& cv2);
	void RasterizeTriangle(const TransformedVert& v0, const TransformedVert& v1, const TransformedVert& v2,
	                       const SWTexture* tex);

	float EdgeFunction(const float ax, const float ay, const float bx, const float by, const float cx, const float cy)
	{
		return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	}

	// Platform-independent state
	std::vector<uint32_t> m_pixels;
	std::vector<float> m_depthBuffer;
	int m_width = 0;
	int m_height;

	Mat4 m_world;
	Mat4 m_view;
	Mat4 m_proj;
	Mat4 m_worldViewProj;
	bool m_matricesDirty;

	bool m_depthTest = true;
	int m_cullMode = CULL_NONE;

	bool m_lightingEnabled;
	Vec3 m_lightDir;
	float m_ambientIntensity;
	float m_diffuseIntensity;

	void UpdateCombinedMatrix();
};
