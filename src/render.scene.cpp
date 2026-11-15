// The painter's algorithm over libdesktop's 2D polygons: faces are lit, clipped and projected as the game draws
// them, then sorted from far to near and drawn over one another.

#include "render.scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b)
{
	Mat4 r;
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
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

namespace
{
	using ClipVertex = SceneRenderer::ClipVertex;

	// How far past the frame's edges, as a share of its half width and height, a face is drawn before it is cut:
	// far enough that no cut edge shows, near enough that a face reaching behind the eye keeps its precision.
	constexpr double kGuardBand = 1.25;

	// Faces of a group that are not part of a convex part come before all of its parts: a solid's faces turned
	// from the eye first, then those turned to it.
	constexpr double kBeforeParts = std::numeric_limits<double>::infinity();
	constexpr double kFacingFaces = std::numeric_limits<double>::max();

	// A vertex this close to a part's face plane counts as on its outer side, so parts that touch still separate.
	constexpr double kPlaneTolerance = 1e-6;

	// Every vertex of a clipped triangle: three, one more for each of the seven planes that can cut it.
	constexpr int kMaxClipVertices = 16;

	ClipVertex Lerp(const ClipVertex& a, const ClipVertex& b, const double t)
	{
		return {
			a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t, a.u + (b.u - a.u) * t,
		};
	}

	// Where the edge from a to b crosses a plane, a and b at signed distances da and db from it. The same end
	// starts the interpolation whichever way round the edge is met, so faces that share the edge are cut at the
	// very same point and meet with neither gap nor overlap.
	ClipVertex Crossing(ClipVertex a, ClipVertex b, double da, double db)
	{
		if (std::tie(b.x, b.y, b.z, b.w, b.u) < std::tie(a.x, a.y, a.z, a.w, a.u))
		{
			std::swap(a, b);
			std::swap(da, db);
		}

		return Lerp(a, b, da / (da - db));
	}

	// Sutherland-Hodgman: the part of a convex polygon where distance is not negative.
	template <typename Distance>
	int ClipPolygon(const ClipVertex* in, const int count, ClipVertex* out, const Distance& distance)
	{
		int n = 0;

		for (int i = 0; i < count; ++i)
		{
			const ClipVertex& a = in[i];
			const ClipVertex& b = in[i + 1 == count ? 0 : i + 1];
			const double da = distance(a);
			const double db = distance(b);

			if (da >= 0.0) out[n++] = a;
			if ((da >= 0.0) != (db >= 0.0)) out[n++] = Crossing(a, b, da, db);
		}

		return n;
	}

	core::color ToColor(const uint32_t argb)
	{
		return {
			static_cast<float>(argb >> 16 & 0xFF) / 255.0f,
			static_cast<float>(argb >> 8 & 0xFF) / 255.0f,
			static_cast<float>(argb & 0xFF) / 255.0f,
			static_cast<float>(argb >> 24 & 0xFF) / 255.0f,
		};
	}

	// The opaque colour the game means by an AARRGGBB value, whatever its alpha byte holds.
	uint32_t Opaque(const uint32_t argb)
	{
		return argb | 0xFF000000;
	}
}

RoadTexture RoadTexture::FromRow(const uint32_t* row, const int width)
{
	RoadTexture texture;

	for (int x = 0; x < width;)
	{
		const uint32_t colour = Opaque(row[x]);
		int end = x + 1;
		while (end < width && Opaque(row[end]) == colour) ++end;
		texture.bands.push_back({static_cast<double>(x) / width, static_cast<double>(end) / width, colour});
		x = end;
	}

	// A road coordinate a hair outside 0 to 1 still finds the colour at its edge.
	if (!texture.bands.empty())
	{
		texture.bands.front().u0 = -1.0;
		texture.bands.back().u1 = 2.0;
	}

	return texture;
}

SceneRenderer::SceneRenderer() :
	m_lightDir(Vec3Normalize(Vec3(0.3f, 0.8f, 0.5f))), m_ambientIntensity(0.45f), m_diffuseIntensity(0.55f)
{
	m_world = Mat4::Identity();
	m_view = Mat4::Identity();
	m_proj = Mat4::Identity();
	m_worldView = Mat4::Identity();
	m_worldViewProj = Mat4::Identity();
}

void SceneRenderer::Resize(const int width, const int height)
{
	m_width = width;
	m_height = height;
}

void SceneRenderer::BeginFrame()
{
	m_points.clear();
	m_pending.clear();
	m_commands.clear();
	m_parts.clear();
	m_partVertices.clear();
	m_partPlanes.clear();
	m_inGroup = false;
	m_sequence = 0;
	m_nextGroup = 0;
}

void SceneRenderer::SetWorldMatrix(const Mat4& m)
{
	m_world = m;
	m_matricesDirty = true;
}

void SceneRenderer::SetViewMatrix(const Mat4& m)
{
	m_view = m;
	m_matricesDirty = true;
}

void SceneRenderer::SetProjectionMatrix(const Mat4& m)
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

void SceneRenderer::UpdateCombinedMatrix()
{
	if (m_matricesDirty)
	{
		m_worldView = Mat4Multiply(m_world, m_view);
		m_worldViewProj = Mat4Multiply(m_worldView, m_proj);
		m_matricesDirty = false;
	}
}

Vec3 SceneRenderer::ToView(const Vec3& p) const
{
	const auto& m = m_worldView.m;
	return {
		p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
		p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
		p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2],
	};
}

SceneRenderer::ClipVertex SceneRenderer::ToClip(const SceneVertex& v) const
{
	const auto& m = m_worldViewProj.m;
	const double x = v.pos.x, y = v.pos.y, z = v.pos.z;
	return {
		x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0],
		x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1],
		x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2],
		x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3],
		v.tu,
	};
}

double SceneRenderer::ViewDistance(const Vec3& p)
{
	UpdateCombinedMatrix();
	return Vec3Length(ToView(p));
}

bool SceneRenderer::IsEyeAcross(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& point)
{
	UpdateCombinedMatrix();
	const Vec3 va = ToView(a);
	const Vec3 normal = Vec3Normalize(Vec3Cross(ToView(b) - va, ToView(c) - va));
	const double pointSide = Vec3Dot(normal, ToView(point) - va);
	if (std::abs(pointSide) < kPlaneTolerance) return true;

	// The eye is at the origin of view space.
	const double eyeSide = -Vec3Dot(normal, va);
	return (eyeSide > 0.0) != (pointSide > 0.0);
}

uint32_t SceneRenderer::LitColor(const Vec3& p0, const Vec3& p1, const Vec3& p2, const uint32_t color) const
{
	// Compute face normal in object space
	const Vec3 edge1 = p1 - p0;
	const Vec3 edge2 = p2 - p0;
	Vec3 n = Vec3Cross(edge1, edge2);
	const double len = Vec3Length(n);
	if (len < 1e-8f) return color; // degenerate triangle
	n = n / len;

	// Transform normal to world space (upper-left 3x3 of world matrix, rotation only)
	Vec3 wn;
	wn.x = n.x * m_world.m[0][0] + n.y * m_world.m[1][0] + n.z * m_world.m[2][0];
	wn.y = n.x * m_world.m[0][1] + n.y * m_world.m[1][1] + n.z * m_world.m[2][1];
	wn.z = n.x * m_world.m[0][2] + n.y * m_world.m[1][2] + n.z * m_world.m[2][2];
	wn = Vec3Normalize(wn);

	// N dot L. A face wound away from the light is lit by the ambient alone.
	double NdotL = Vec3Dot(wn, m_lightDir);
	if (NdotL < 0.0f) NdotL = 0.0f;
	double intensity = m_ambientIntensity + m_diffuseIntensity * NdotL;
	if (intensity > 1.0f) intensity = 1.0f;

	const auto scale = [intensity](const uint32_t c)
	{
		return static_cast<uint32_t>(std::clamp(static_cast<int>(c * intensity), 0, 255));
	};
	return XRGB(scale(color >> 16 & 0xFF), scale(color >> 8 & 0xFF), scale(color & 0xFF));
}

uint32_t SceneRenderer::BeginGroup(const Placement placement)
{
	EndGroup();
	m_inGroup = true;
	m_placement = placement;
	m_group = m_nextGroup++;
	m_groupFirst = m_pending.size();
	m_groupNear = std::numeric_limits<double>::infinity();
	m_groupFar = 0.0;
	m_solid = false;
	return m_group;
}

uint32_t SceneRenderer::BeginSolid(const Vec3& centre)
{
	const uint32_t group = BeginGroup(Placement::Farthest);
	UpdateCombinedMatrix();
	m_solid = true;
	m_solidCentre = ToView(centre);
	return group;
}

void SceneRenderer::OrderGroups(const uint32_t first, const uint32_t second)
{
	if (first != second) m_groupOrder.emplace_back(first, second);
}

void SceneRenderer::EndGroup()
{
	if (!m_inGroup) return;
	m_inGroup = false;

	OrderParts();

	const double key = m_placement == Placement::Nearest ? m_groupNear : m_groupFar;
	for (size_t i = m_groupFirst; i < m_pending.size(); ++i) m_pending[i].order.groupKey = key;

	m_parts.clear();
	m_partVertices.clear();
	m_partPlanes.clear();
}

SceneRenderer::FaceOrder SceneRenderer::Place(const Vec3& e0, const Vec3& e1, const Vec3& e2)
{
	const double d0 = Vec3Length(e0);
	const double d1 = Vec3Length(e1);
	const double d2 = Vec3Length(e2);

	if (m_inGroup)
	{
		m_groupNear = std::min({m_groupNear, d0, d1, d2});
		m_groupFar = std::max({m_groupFar, d0, d1, d2});

		double partKey = kBeforeParts;
		if (m_solid)
		{
			// Out of a convex solid is away from its centre, whichever way round the face is wound; the eye is at
			// the origin of view space.
			Vec3 normal = Vec3Cross(e1 - e0, e2 - e0);
			if (Vec3Dot(normal, (e0 + e1 + e2) / 3.0 - m_solidCentre) < 0.0) normal = normal * -1.0;
			if (Vec3Dot(normal, e0) < 0.0) partKey = kFacingFaces;
		}

		return {0.0, m_group, partKey};
	}

	return {std::max({d0, d1, d2}), m_nextGroup++, 0.0};
}

void SceneRenderer::DrawTriangleList(const SceneVertex* verts, const int startVertex, const int numTriangles,
                                     const RoadTexture* tex)
{
	UpdateCombinedMatrix();

	for (int i = 0; i < numTriangles; i++)
	{
		const SceneVertex& v0 = verts[startVertex + i * 3 + 0];
		const SceneVertex& v1 = verts[startVertex + i * 3 + 1];
		const SceneVertex& v2 = verts[startVertex + i * 3 + 2];
		AddTriangle(v0, v1, v2, tex, Place(ToView(v0.pos), ToView(v1.pos), ToView(v2.pos)));
	}
}

void SceneRenderer::DrawIndexedTriangleList(const SceneVertex* verts, const uint32_t* indices, const int startIndex,
                                            const int numTriangles, const RoadTexture* tex)
{
	UpdateCombinedMatrix();

	for (int i = 0; i < numTriangles; i++)
	{
		const SceneVertex& v0 = verts[indices[startIndex + i * 3 + 0]];
		const SceneVertex& v1 = verts[indices[startIndex + i * 3 + 1]];
		const SceneVertex& v2 = verts[indices[startIndex + i * 3 + 2]];
		AddTriangle(v0, v1, v2, tex, Place(ToView(v0.pos), ToView(v1.pos), ToView(v2.pos)));
	}
}

void SceneRenderer::DrawConvexPart(const SceneVertex* verts, const int startVertex, const int numTriangles)
{
	if (numTriangles <= 0) return;

	// A part drawn alone is a group of its own, placed as an object is.
	const bool alone = !m_inGroup;
	if (alone) BeginGroup(Placement::Nearest);

	UpdateCombinedMatrix();

	Part part;
	part.firstFace = m_pending.size();
	part.firstVertex = m_partVertices.size();
	part.vertexCount = static_cast<size_t>(numTriangles) * 3;
	part.firstPlane = m_partPlanes.size();

	// Every face of a convex part looks away from a point inside it, so the centre of its vertices tells each
	// face's outside from its inside whichever way round the face is wound.
	Vec3 centre;
	for (int i = 0; i < numTriangles * 3; ++i)
	{
		const Vec3 p = ToView(verts[startVertex + i].pos);
		m_partVertices.push_back(p);
		centre = centre + p;
	}
	centre = centre / static_cast<double>(part.vertexCount);
	part.distance = Vec3Length(centre);

	for (int i = 0; i < numTriangles; ++i)
	{
		const Vec3& e0 = m_partVertices[part.firstVertex + i * 3 + 0];
		const Vec3& e1 = m_partVertices[part.firstVertex + i * 3 + 1];
		const Vec3& e2 = m_partVertices[part.firstVertex + i * 3 + 2];
		const FaceOrder order = Place(e0, e1, e2);

		Vec3 normal = Vec3Cross(e1 - e0, e2 - e0);
		const double length = Vec3Length(normal);
		if (length < 1e-12) continue;

		normal = normal / length;
		if (Vec3Dot(normal, (e0 + e1 + e2) / 3.0 - centre) < 0.0) normal = normal * -1.0;
		m_partPlanes.push_back({normal, Vec3Dot(normal, e0)});

		// The eye is at the origin of view space: a face it is not in front of is turned away.
		if (Vec3Dot(normal, e0) >= 0.0) continue;

		const SceneVertex& v0 = verts[startVertex + i * 3 + 0];
		const SceneVertex& v1 = verts[startVertex + i * 3 + 1];
		const SceneVertex& v2 = verts[startVertex + i * 3 + 2];
		AddTriangle(v0, v1, v2, nullptr, order);
	}

	part.faceCount = m_pending.size() - part.firstFace;
	part.planeCount = m_partPlanes.size() - part.firstPlane;
	m_parts.push_back(part);

	if (alone) EndGroup();
}

void SceneRenderer::OrderParts()
{
	const size_t count = m_parts.size();
	if (count == 0) return;

	// A face plane of one part with all of the other part on its outer side separates them, and of two parts
	// so separated, the one on the far side of the plane from the eye is drawn first: a ray from the eye crosses
	// the plane once, so the part on the eye's side may hide the other but never the reverse.
	const auto separated = [this](const Part& a, const Part& b, bool& aFirst)
	{
		for (size_t i = 0; i < a.planeCount; ++i)
		{
			const Plane& plane = m_partPlanes[a.firstPlane + i];
			bool outside = true;

			for (size_t v = 0; v < b.vertexCount && outside; ++v)
			{
				outside = Vec3Dot(plane.normal, m_partVertices[b.firstVertex + v]) - plane.offset > -kPlaneTolerance;
			}

			if (outside)
			{
				// The eye, at the origin, is on b's side: a is behind the plane.
				aFirst = -plane.offset > 0.0;
				return true;
			}
		}

		return false;
	};

	// first[a * count + b]: part a is drawn before part b.
	std::vector<char> first(count * count, 0);

	for (size_t a = 0; a < count; ++a)
	{
		for (size_t b = a + 1; b < count; ++b)
		{
			bool aFirst = false;
			bool bFirst = false;

			if (!separated(m_parts[a], m_parts[b], aFirst))
			{
				// Parts no face separates are drawn the farther first.
				aFirst = separated(m_parts[b], m_parts[a], bFirst) ? !bFirst
				                                                   : m_parts[a].distance >= m_parts[b].distance;
			}

			first[aFirst ? a * count + b : b * count + a] = 1;
		}
	}

	// Each turn draws, of the parts that nothing still waiting must precede, the farthest; parts that each must
	// precede another all round are drawn the farthest first.
	std::vector<char> drawn(count, 0);

	for (size_t rank = 0; rank < count; ++rank)
	{
		size_t next = count;
		size_t farthest = count;

		for (size_t candidate = 0; candidate < count; ++candidate)
		{
			if (drawn[candidate]) continue;

			if (farthest == count || m_parts[candidate].distance > m_parts[farthest].distance) farthest = candidate;

			bool waiting = false;
			for (size_t other = 0; other < count && !waiting; ++other)
			{
				waiting = other != candidate && !drawn[other] && first[other * count + candidate];
			}

			if (!waiting && (next == count || m_parts[candidate].distance > m_parts[next].distance)) next = candidate;
		}

		if (next == count) next = farthest;
		drawn[next] = 1;

		const Part& part = m_parts[next];
		for (size_t i = part.firstFace; i < part.firstFace + part.faceCount; ++i)
			m_pending[i].order.partKey = -static_cast<double>(rank);
	}
}

void SceneRenderer::AddTriangle(const SceneVertex& v0, const SceneVertex& v1, const SceneVertex& v2,
                                const RoadTexture* tex, const FaceOrder& order)
{
	const ClipVertex triangle[3] = {ToClip(v0), ToClip(v1), ToClip(v2)};

	if (tex && !tex->bands.empty())
	{
		// Each run of the texture's colour is a polygon of its own, cut from the triangle where the run starts
		// and ends; a textured road is drawn unlit.
		for (const auto& band : tex->bands)
		{
			ClipVertex from[kMaxClipVertices];
			ClipVertex to[kMaxClipVertices];
			int n = ClipPolygon(triangle, 3, from, [&band](const ClipVertex& v) { return v.u - band.u0; });
			n = ClipPolygon(from, n, to, [&band](const ClipVertex& v) { return band.u1 - v.u; });
			if (n >= 3) AddFace(to, n, band.color, order);
		}
	}
	else
	{
		AddFace(triangle, 3, LitColor(v0.pos, v1.pos, v2.pos, v0.color), order);
	}
}

void SceneRenderer::AddFace(const ClipVertex* verts, const int count, const uint32_t color, const FaceOrder& order)
{
	ClipVertex a[kMaxClipVertices];
	ClipVertex b[kMaxClipVertices];

	// Sutherland-Hodgman against w >= m_nearClipW (near plane): what survives never projects behind the eye.
	int n = ClipPolygon(verts, count, a, [this](const ClipVertex& v) { return v.w - m_nearClipW; });
	if (n < 3) return;

	// Reversed-Z: past the far plane, ndc z is below 0.
	if (std::all_of(a, a + n, [](const ClipVertex& v) { return v.z < 0.0; })) return;

	n = ClipPolygon(a, n, b, [](const ClipVertex& v) { return kGuardBand * v.w - v.x; });
	n = ClipPolygon(b, n, a, [](const ClipVertex& v) { return kGuardBand * v.w + v.x; });
	n = ClipPolygon(a, n, b, [](const ClipVertex& v) { return kGuardBand * v.w - v.y; });
	n = ClipPolygon(b, n, a, [](const ClipVertex& v) { return kGuardBand * v.w + v.y; });
	if (n < 3) return;

	const auto firstPoint = static_cast<uint32_t>(m_points.size());

	for (int i = 0; i < n; ++i)
	{
		const double invW = 1.0 / a[i].w;
		m_points.emplace_back((a[i].x * invW + 1.0) * 0.5 * m_width, (1.0 - a[i].y * invW) * 0.5 * m_height);
	}

	m_pending.push_back({order, m_sequence++, firstPoint, static_cast<uint32_t>(n), Opaque(color)});
}

void SceneRenderer::Flush()
{
	EndGroup();

	if (m_pending.empty())
	{
		m_groupOrder.clear();
		return;
	}

	// A group's faces were drawn together, so they lie together in the order drawn.
	struct GroupFaces
	{
		double key;
		size_t first;
		size_t count;
		uint32_t waiting;
		bool drawn;
	};

	std::vector<GroupFaces> groups;
	const uint32_t firstGroup = m_pending.front().order.group;
	std::vector<int> local(m_pending.back().order.group - firstGroup + 1, -1);

	for (size_t i = 0; i < m_pending.size(); ++i)
	{
		const auto& order = m_pending[i].order;
		if (i == 0 || order.group != m_pending[i - 1].order.group)
		{
			local[order.group - firstGroup] = static_cast<int>(groups.size());
			groups.push_back({order.groupKey, i, 0, 0, false});
		}
		++groups.back().count;
	}

	// The groups ordered against one another, as their first and second.
	std::vector<std::pair<int, int>> edges;
	for (const auto& [first, second] : m_groupOrder)
	{
		if (first < firstGroup || second < firstGroup) continue;
		if (first - firstGroup >= local.size() || second - firstGroup >= local.size()) continue;
		const int from = local[first - firstGroup];
		const int to = local[second - firstGroup];
		if (from < 0 || to < 0) continue;
		edges.emplace_back(from, to);
		++groups[to].waiting;
	}
	std::sort(edges.begin(), edges.end());
	m_groupOrder.clear();

	// Each turn draws the farthest group that no group still to come must precede; groups that must each come
	// before another all round are drawn the farthest first. Groups at the same distance keep the order drawn.
	using Candidate = std::pair<double, int>;
	std::priority_queue<Candidate> ready;
	for (int g = 0; g < static_cast<int>(groups.size()); ++g)
	{
		if (groups[g].waiting == 0) ready.emplace(groups[g].key, -g);
	}

	for (size_t drawnCount = 0; drawnCount < groups.size();)
	{
		int g = -1;

		while (!ready.empty() && g < 0)
		{
			const int candidate = -ready.top().second;
			ready.pop();
			if (!groups[candidate].drawn) g = candidate;
		}

		if (g < 0)
		{
			for (int candidate = 0; candidate < static_cast<int>(groups.size()); ++candidate)
			{
				if (!groups[candidate].drawn && (g < 0 || groups[candidate].key > groups[g].key)) g = candidate;
			}
		}

		auto& group = groups[g];
		group.drawn = true;
		++drawnCount;

		const auto begin = m_pending.begin() + static_cast<std::ptrdiff_t>(group.first);
		const auto end = begin + static_cast<std::ptrdiff_t>(group.count);
		std::sort(begin, end, [](const PendingFace& a, const PendingFace& b)
		{
			if (a.order.partKey != b.order.partKey) return a.order.partKey > b.order.partKey;
			return a.sequence < b.sequence;
		});

		for (auto face = begin; face != end; ++face)
			m_commands.push_back({true, face->color, face->first, face->count, {}});

		const auto next = std::lower_bound(edges.begin(), edges.end(),
		                                   std::make_pair(g, std::numeric_limits<int>::min()));
		for (auto edge = next; edge != edges.end() && edge->first == g; ++edge)
		{
			auto& after = groups[edge->second];
			if (after.waiting > 0 && --after.waiting == 0 && !after.drawn) ready.emplace(after.key, -edge->second);
		}
	}

	m_pending.clear();
}

void SceneRenderer::AddRect(const int left, const int top, const int right, const int bottom, const uint32_t color)
{
	m_commands.push_back({false, color, 0, 0, core::recti(left, top, right, bottom)});
}

void SceneRenderer::DrawScreenTriangleFan(const Point2D* pts, const int numPoints, const uint32_t color)
{
	if (numPoints < 3) return;
	Flush();

	// Each triangle of the fan is drawn, so a polygon that is not convex covers what the fan always has.
	for (int i = 1; i < numPoints - 1; i++)
	{
		const auto firstPoint = static_cast<uint32_t>(m_points.size());
		m_points.emplace_back(pts[0].x, pts[0].y);
		m_points.emplace_back(pts[i].x, pts[i].y);
		m_points.emplace_back(pts[i + 1].x, pts[i + 1].y);
		m_commands.push_back({true, Opaque(color), firstPoint, 3, {}});
	}
}

void SceneRenderer::FillRect(int x1, int y1, int x2, int y2, const uint32_t color)
{
	Flush();

	x1 = (std::max)(0, (std::min)(x1, m_width - 1));
	x2 = (std::max)(0, (std::min)(x2, m_width - 1));
	y1 = (std::max)(0, (std::min)(y1, m_height - 1));
	y2 = (std::max)(0, (std::min)(y2, m_height - 1));

	if (x1 <= x2 && y1 <= y2) AddRect(x1, y1, x2 + 1, y2 + 1, Opaque(color));
}

void SceneRenderer::BlendRect(int x1, int y1, int x2, int y2, const uint32_t color, const uint8_t opacity)
{
	if (m_width <= 0 || m_height <= 0 || x1 > x2 || y1 > y2 || opacity == 0) return;

	Flush();

	x1 = (std::max)(0, (std::min)(x1, m_width - 1));
	x2 = (std::max)(0, (std::min)(x2, m_width - 1));
	y1 = (std::max)(0, (std::min)(y1, m_height - 1));
	y2 = (std::max)(0, (std::min)(y2, m_height - 1));

	AddRect(x1, y1, x2 + 1, y2 + 1, (color & 0x00FFFFFF) | static_cast<uint32_t>(opacity) << 24);
}

void SceneRenderer::Draw(render::draw_context& dc, const core::pointi origin)
{
	Flush();

	for (const auto& command : m_commands)
	{
		const auto colour = ToColor(command.color);

		if (command.polygon)
		{
			m_outline.assign(m_points.begin() + command.first, m_points.begin() + command.first + command.count);
			for (auto& p : m_outline) p = p.offset(origin.x, origin.y);
			dc.draw_polygon(m_outline, {.fill = colour, .smooth = false});
		}
		else
		{
			dc.draw_rect(command.rect.offset(origin), colour);
		}
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
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // U+007F
};

void SceneRenderer::DrawText(const int x, const int y, const std::wstring_view text, const uint32_t color,
                             const int scale)
{
	Flush();

	const uint32_t colour = Opaque(color);
	int cx = x;

	for (const wchar_t ch : text)
	{
		const int ci = ch < 128 ? static_cast<int>(ch) : 0;
		const uint8_t* glyph = font8x8[ci];

		// Each run of lit pixels along a glyph's row is one rectangle.
		for (int row = 0; row < 8; row++)
		{
			const uint8_t bits = glyph[row];
			for (int col = 0; col < 8;)
			{
				if (!(bits & 1 << col))
				{
					++col;
					continue;
				}

				int end = col + 1;
				while (end < 8 && bits & 1 << end) ++end;

				const int top = y + row * scale;
				AddRect(cx + col * scale, top, cx + end * scale, top + scale, colour);
				col = end;
			}
		}

		cx += 8 * scale;
	}
}

void SceneRenderer::DrawGameText(const int x, const int y, const std::wstring_view text, const uint32_t color)
{
	DrawText(x, y, text, color, 1);
}

void SceneRenderer::DrawTextLargeCentered(const int y, const std::wstring_view text, const uint32_t color)
{
	constexpr int scale = 2;
	const int textWidth = static_cast<int>(text.size()) * 8 * scale;
	DrawText((m_width - textWidth) / 2, y, text, color, scale);
}
