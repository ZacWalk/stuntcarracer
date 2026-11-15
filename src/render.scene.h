#pragma once

// The game's 3D scene, drawn as libdesktop 2D polygons by the painter's algorithm. Each face is lit, clipped and
// projected as the game draws it, and a frame's faces are sorted from far to near and drawn over one another:
// the 2D backdrop before them, the HUD after.

#include "game.h" // Point2D, XRGB

#include <cstdint>
#include <cstring>
#include <vector>

// 3D vector
struct Vec3
{
	double x, y, z;

	Vec3() : x(0), y(0), z(0) {}

	Vec3(const double _x, const double _y, const double _z) : x(_x), y(_y), z(_z) {}

	Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
	Vec3 operator*(const double s) const { return Vec3(x * s, y * s, z * s); }

	Vec3 operator/(const double s) const
	{
		const double inv = 1.0f / s;
		return Vec3(x * inv, y * inv, z * inv);
	}
};

inline Vec3 Vec3Cross(const Vec3& a, const Vec3& b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline double Vec3Dot(const Vec3& a, const Vec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline double Vec3Length(const Vec3& v)
{
	return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 Vec3Normalize(const Vec3& v)
{
	const double len = Vec3Length(v);
	if (len < 1e-8f) return Vec3(0, 0, 0);
	return v / len;
}

// 4x4 row-major matrix
struct Mat4
{
	double m[4][4];

	Mat4() { memset(m, 0, sizeof(m)); }

	static Mat4 Identity()
	{
		Mat4 r;
		r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
		return r;
	}
};

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 Mat4RotationX(double angle);
Mat4 Mat4RotationY(double angle);
Mat4 Mat4RotationZ(double angle);
Mat4 Mat4Translation(double x, double y, double z);
Mat4 Mat4PerspectiveFovLH(double fovY, double aspect, double zn, double zf);
Mat4 Mat4LookAtLH(const Vec3& eye, const Vec3& at, const Vec3& up);

// A vertex of the game's models: its position, colour, and road texture coordinates.
struct SceneVertex
{
	Vec3 pos;
	uint32_t color;
	double tu, tv;
};

// A road texture as the scene draws it: the runs of colour across its first row. The road takes tv = 0, so the
// other rows are never seen, and each run is drawn as a polygon of its own colour.
struct RoadTexture
{
	struct Band
	{
		double u0, u1;
		uint32_t color;
	};

	std::vector<Band> bands;

	// A row of AARRGGBB texels.
	static RoadTexture FromRow(const uint32_t* row, int width);
};

// How a group of faces takes its place among the others: a stretch of track by its farthest point, so a car on it
// comes after it; a car by its nearest.
enum class Placement
{
	Farthest,
	Nearest
};

class SceneRenderer
{
public:
	SceneRenderer();

	// The size, in pixels, of the frames made from now on.
	void Resize(int width, int height);
	int GetWidth() const { return m_width; }
	int GetHeight() const { return m_height; }

	// Starts a new frame, dropping the last.
	void BeginFrame();

	void SetWorldMatrix(const Mat4& m);
	void SetViewMatrix(const Mat4& m);
	void SetProjectionMatrix(const Mat4& m);

	// The faces drawn until EndGroup take one place in the painter's order, by placement, and are drawn in the
	// order they come - a stretch of track's walls before its road, a car's shadow before the car. A face drawn
	// outside a group takes a place of its own, by its farthest point. Returns the group, for OrderGroups.
	uint32_t BeginGroup(Placement placement);
	// A group that is the surface of a convex solid around centre - a stretch of track, open at its ends - placed by
	// its farthest point. Its faces turned from the eye come first and those turned to it last, each in the order
	// drawn: seen from above a road hides the walls hanging below it, seen from beside and below a wall hides the
	// road. A face is judged alone, so a road twisting between banks is judged triangle by triangle.
	uint32_t BeginSolid(const Vec3& centre);
	void EndGroup();
	// Draws first before second whatever their places say: of two stretches of track that meet, the one beyond
	// their joint from the eye comes first, though a turn may bring its farthest point nearer.
	void OrderGroups(uint32_t first, uint32_t second);

	void DrawTriangleList(const SceneVertex* verts, int startVertex, int numTriangles,
	                      const RoadTexture* tex = nullptr);
	void DrawIndexedTriangleList(const SceneVertex* verts, const uint32_t* indices, int startIndex, int numTriangles,
	                             const RoadTexture* tex = nullptr);
	// A convex part of the current group - a wheel, a car's body. Its faces turned from the eye are hidden by those
	// turned towards it, so are not drawn. A group's parts come after its other faces, each drawn whole, a part
	// behind another before it.
	void DrawConvexPart(const SceneVertex* verts, int startVertex, int numTriangles);

	// How far a point is from the eye, under the current world and view.
	double ViewDistance(const Vec3& p);
	// Whether the plane through a, b and c has the eye on one side and point on the other: whether the eye is
	// beyond the end of a stretch of track from its start. True when point lies in the plane.
	bool IsEyeAcross(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& point);

	// 2D, in the frame's pixels with inclusive corners, over everything drawn before.
	void DrawScreenTriangleFan(const Point2D* pts, int numPoints, uint32_t color);
	void FillRect(int x1, int y1, int x2, int y2, uint32_t color);
	void BlendRect(int x1, int y1, int x2, int y2, uint32_t color, uint8_t opacity);
	void DrawGameText(int x, int y, std::wstring_view text, uint32_t color);
	void DrawTextLargeCentered(int y, std::wstring_view text, uint32_t color);

	// Draws the frame with its top left at origin.
	void Draw(render::draw_context& dc, core::pointi origin);

	// A vertex in clip space, with its texture's u.
	struct ClipVertex
	{
		double x, y, z, w, u;
	};

private:
	// Where a face falls in the painter's order: by its group's place, then the group's faces in turn.
	struct FaceOrder
	{
		double groupKey;
		uint32_t group;
		double partKey;
	};

	// A face waiting for the frame's faces to be sorted.
	struct PendingFace
	{
		FaceOrder order;
		uint32_t sequence;
		uint32_t first;
		uint32_t count;
		uint32_t color;
	};

	// A convex part of the open group, placed among the group's other parts once the group is complete.
	struct Part
	{
		size_t firstFace = 0;
		size_t faceCount = 0;
		size_t firstVertex = 0;
		size_t vertexCount = 0;
		size_t firstPlane = 0;
		size_t planeCount = 0;
		double distance = 0.0;
	};

	// A part's face plane, its normal facing out of the part.
	struct Plane
	{
		Vec3 normal;
		double offset;
	};

	struct Command
	{
		bool polygon;
		uint32_t color;
		uint32_t first;
		uint32_t count;
		core::recti rect;
	};

	void UpdateCombinedMatrix();
	Vec3 ToView(const Vec3& p) const;
	ClipVertex ToClip(const SceneVertex& v) const;
	uint32_t LitColor(const Vec3& p0, const Vec3& p1, const Vec3& p2, uint32_t color) const;
	FaceOrder Place(const Vec3& e0, const Vec3& e1, const Vec3& e2);
	void AddTriangle(const SceneVertex& v0, const SceneVertex& v1, const SceneVertex& v2, const RoadTexture* tex,
	                 const FaceOrder& order);
	void AddFace(const ClipVertex* verts, int count, uint32_t color, const FaceOrder& order);
	void AddRect(int left, int top, int right, int bottom, uint32_t color);
	void DrawText(int x, int y, std::wstring_view text, uint32_t color, int scale);
	void OrderParts();
	void Flush();

	int m_width = 0;
	int m_height = 0;

	Mat4 m_world;
	Mat4 m_view;
	Mat4 m_proj;
	Mat4 m_worldView;
	Mat4 m_worldViewProj;
	bool m_matricesDirty = true;

	Vec3 m_lightDir;
	double m_ambientIntensity;
	double m_diffuseIntensity;

	// Near-plane clip distance in clip-space w, derived from the projection matrix in SetProjectionMatrix.
	double m_nearClipW = 0.01;

	std::vector<core::pointd> m_points;
	std::vector<PendingFace> m_pending;
	std::vector<Command> m_commands;
	uint32_t m_sequence = 0;
	uint32_t m_nextGroup = 0;

	bool m_inGroup = false;
	Placement m_placement = Placement::Farthest;
	uint32_t m_group = 0;
	size_t m_groupFirst = 0;
	double m_groupNear = 0.0;
	double m_groupFar = 0.0;
	// The open group is a solid's surface, around this centre in view space.
	bool m_solid = false;
	Vec3 m_solidCentre;
	std::vector<Part> m_parts;
	std::vector<Vec3> m_partVertices;
	std::vector<Plane> m_partPlanes;
	// Pairs of groups, the first drawn before the second.
	std::vector<std::pair<uint32_t, uint32_t>> m_groupOrder;

	std::vector<core::pointd> m_outline;
};
