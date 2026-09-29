/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: 2D BSP/pathfinding reader
 */

#include "zerocomico/bsp.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/str.h"
#include "common/tokenizer.h"

#include <cmath>

namespace ZeroComico {

class LineReader {
public:
	LineReader(Common::SeekableReadStream &stream) : _stream(stream) {}

	Common::String next() {
		while (!_stream.eos()) {
			Common::String s = _stream.readLine();
			s.trim();
			if (!s.empty())
				return s;
		}
		return Common::String();
	}

	bool expect(const char *word) { return next() == word; }

	int integer(bool &ok) {
		const Common::String s = next();
		char *end = nullptr;
		const long v = strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || (end && *end != '\0'))
			ok = false;
		return (int)v;
	}

private:
	Common::SeekableReadStream &_stream;
};

static bool tokenInt(const Common::String &s, int &value) {
	char *end = nullptr;
	const long v = strtol(s.c_str(), &end, 10);
	if (end == s.c_str() || (end && *end != '\0'))
		return false;
	value = (int)v;
	return true;
}

static bool tokenFloat(const Common::String &s, float &value) {
	char *end = nullptr;
	const double v = strtod(s.c_str(), &end);
	if (end == s.c_str() || (end && *end != '\0'))
		return false;
	value = (float)v;
	return true;
}

static bool parseVec2(const Common::String &s, Vec2 &v) {
	Common::StringTokenizer tok(s);
	if (tok.empty() || !tokenFloat(tok.nextToken(), v.x))
		return false;
	if (tok.empty() || !tokenFloat(tok.nextToken(), v.y))
		return false;
	return tok.empty();
}

static bool parseEdge(const Common::String &s, BspEdge &e) {
	Common::StringTokenizer tok(s);
	int *values[4] = { &e.p0, &e.p1, &e.front, &e.back };
	for (int i = 0; i < 4; ++i) {
		if (tok.empty() || !tokenInt(tok.nextToken(), *values[i]))
			return false;
	}
	return tok.empty();
}

static bool parseTreeNode(const Common::String &s, BspTreeNode &node) {
	Common::StringTokenizer tok(s);
	int *values[3] = { &node.edge, &node.leaf, &node.unused };
	for (int i = 0; i < 3; ++i) {
		if (tok.empty() || !tokenInt(tok.nextToken(), *values[i]))
			return false;
	}
	return tok.empty();
}

bool BspMap::load(const Common::Path &path) {
	Common::File file;
	if (!file.open(path))
		return false;
	return load(file);
}

int BspMap::parseTree(LineReader &reader, bool &ok) {
	const Common::String line = reader.next();
	if (line.empty()) {
		ok = false;
		return -1;
	}
	if (line == "-1")
		return -1;

	BspTreeNode node;
	node.left = node.right = -1;
	if (!parseTreeNode(line, node)) {
		ok = false;
		return -1;
	}

	const int index = tree.size();
	tree.push_back(node);
	tree[index].left = parseTree(reader, ok);
	if (!ok)
		return index;
	tree[index].right = parseTree(reader, ok);
	return index;
}

bool BspMap::load(Common::SeekableReadStream &stream) {
	polygons.clear();
	points.clear();
	edges.clear();
	cells.clear();
	tree.clear();
	graph.clear();
	support.clear();
	_treeRoot = -1;

	stream.seek(0);
	if (stream.size() == 0)
		return false;

	LineReader r(stream);
	bool ok = true;
	if (!r.expect("scene") || !r.expect("room") || !r.expect("poly"))
		return false;

	int count = r.integer(ok);
	Common::Array<Vec2> outer;
	for (int i = 0; ok && i < count; ++i) {
		Vec2 v;
		ok = parseVec2(r.next(), v);
		if (ok)
			outer.push_back(v);
	}
	if (!ok)
		return false;
	polygons.push_back(outer);

	const int holes = r.integer(ok);
	for (int h = 0; ok && h < holes; ++h) {
		if (!r.expect("poly"))
			return false;
		count = r.integer(ok);
		Common::Array<Vec2> poly;
		for (int i = 0; ok && i < count; ++i) {
			Vec2 v;
			ok = parseVec2(r.next(), v);
			if (ok)
				poly.push_back(v);
		}
		polygons.push_back(poly);
	}

	if (!ok || !r.expect("scene_end") || !r.expect("bsp") || !r.expect("bsp_points"))
		return false;

	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		Vec2 v;
		ok = parseVec2(r.next(), v);
		if (ok)
			points.push_back(v);
	}

	if (!ok || !r.expect("bsp_edges"))
		return false;
	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		BspEdge e;
		ok = parseEdge(r.next(), e);
		if (ok)
			edges.push_back(e);
	}

	if (!ok || !r.expect("bsp_polygons"))
		return false;
	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		BspCell cell;
		cell.index = r.integer(ok);
		const int edgeCount = r.integer(ok);
		for (int e = 0; ok && e < edgeCount; ++e)
			cell.edges.push_back(r.integer(ok));
		cells.push_back(cell);
	}

	if (!ok || !r.expect("bsp_tree"))
		return false;
	_treeRoot = parseTree(r, ok);
	if (!ok || _treeRoot < 0 ||
	    !r.expect("bsp_end") || !r.expect("pathfinding") || !r.expect("graph"))
		return false;

	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		NavNode node;
		ok = parseVec2(r.next(), node.pos);
		Common::StringTokenizer tok(r.next());
		while (ok && !tok.empty()) {
			int target = -1;
			if (!tokenInt(tok.nextToken(), target)) {
				ok = false;
				break;
			}
			if (target == -1)
				break;
			if (tok.empty()) {
				ok = false;
				break;
			}
			float weight = 0.0f;
			if (!tokenFloat(tok.nextToken(), weight)) {
				ok = false;
				break;
			}
			NavArc arc;
			arc.target = target;
			arc.weight = weight;
			node.arcs.push_back(arc);
		}
		graph.push_back(node);
	}

	// The retail support block is stored once per BSP convex cell. Preserve it
	// instead of merely consuming it: camera maps and later traversal logic need
	// the same cell-to-navigation relationships that the original runtime had.
	if (!ok || !r.expect("support"))
		return false;
	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		BspSupport entry;

		int n = r.integer(ok);
		for (int j = 0; ok && j < n; ++j) {
			const int index = r.integer(ok);
			if (index < 0 || (uint32)index >= graph.size())
				ok = false;
			else
				entry.insideNodes.push_back(index);
		}

		n = r.integer(ok);
		for (int j = 0; ok && j < n; ++j) {
			Common::StringTokenizer tok(r.next());
			int index = 0;
			float weight = 0.0f;
			if (tok.empty() || !tokenInt(tok.nextToken(), index) ||
			    tok.empty() || !tokenFloat(tok.nextToken(), weight) || !tok.empty() ||
			    index < 0 || (uint32)index >= graph.size()) {
				ok = false;
				continue;
			}

			NavArc weighted;
			weighted.target = index;
			weighted.weight = weight;
			entry.weightedNodes.push_back(weighted);
		}

		support.push_back(entry);
	}

	return ok && r.expect("pathfinding_end");
}

static bool pointInPolygon(const Common::Array<Vec2> &polygon, float x, float y) {
	if (polygon.size() < 3)
		return false;

	bool inside = false;
	for (uint32 i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
		const Vec2 &a = polygon[i];
		const Vec2 &b = polygon[j];

		// Treat points on an edge as inside so exact clicks on the retail floor
		// boundary are not rejected before graph snapping.
		const float edgeX = b.x - a.x;
		const float edgeY = b.y - a.y;
		const float pointX = x - a.x;
		const float pointY = y - a.y;
		const float cross = edgeX * pointY - edgeY * pointX;
		const float dot = pointX * edgeX + pointY * edgeY;
		const float edgeLength2 = edgeX * edgeX + edgeY * edgeY;
		if (std::fabs(cross) <= 0.001f && dot >= -0.001f && dot <= edgeLength2 + 0.001f)
			return true;

		const bool crosses = ((a.y > y) != (b.y > y)) &&
			(x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x);
		if (crosses)
			inside = !inside;
	}
	return inside;
}

bool BspMap::containsWalkablePoint(float x, float y) const {
	if (polygons.empty() || !pointInPolygon(polygons[0], x, y))
		return false;

	// polygons[0] is the room floor boundary; the following polygons are holes.
	for (uint32 i = 1; i < polygons.size(); ++i)
		if (pointInPolygon(polygons[i], x, y))
			return false;
	return true;
}

int BspMap::containingCell(float x, float y) const {
	int nodeIndex = _treeRoot;
	while (nodeIndex >= 0) {
		if ((uint32)nodeIndex >= tree.size())
			return -1;
		const BspTreeNode &node = tree[(uint32)nodeIndex];
		if (node.edge < 0 || (uint32)node.edge >= edges.size())
			return -1;

		const BspEdge &edge = edges[(uint32)node.edge];
		if (edge.p0 < 0 || edge.p1 < 0 ||
		    (uint32)edge.p0 >= points.size() || (uint32)edge.p1 >= points.size())
			return -1;

		const Vec2 &a = points[(uint32)edge.p0];
		const Vec2 &b = points[(uint32)edge.p1];
		const float cross = (b.x - a.x) * (y - a.y) -
		                    (b.y - a.y) * (x - a.x);

		// Across every non-empty retail BSP, the front side of the node edge is
		// the right branch. When the node owns a leaf, that front side is the
		// convex cell itself; the back side continues through the left branch.
		if (cross >= -1.0e-5f) {
			if (node.leaf >= 0)
				return node.leaf;
			nodeIndex = node.right;
		} else {
			nodeIndex = node.left;
		}
	}
	return -1;
}

static Vec2 closestPointOnSegment(const Vec2 &a, const Vec2 &b, float x, float y) {
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float length2 = dx * dx + dy * dy;
	if (length2 <= 1.0e-12f)
		return a;

	float t = ((x - a.x) * dx + (y - a.y) * dy) / length2;
	if (t < 0.0f)
		t = 0.0f;
	else if (t > 1.0f)
		t = 1.0f;

	Vec2 out = { a.x + dx * t, a.y + dy * t };
	return out;
}

bool BspMap::nearestWalkablePoint(float x, float y, Vec2 &result) const {
	if (polygons.empty())
		return false;

	if (containsWalkablePoint(x, y)) {
		result.x = x;
		result.y = y;
		return true;
	}

	bool insideHole = false;
	uint32 holeIndex = 0;
	for (uint32 i = 1; i < polygons.size(); ++i) {
		if (pointInPolygon(polygons[i], x, y)) {
			insideHole = true;
			holeIndex = i;
			break;
		}
	}

	const Common::Array<Vec2> &boundary =
		insideHole ? polygons[holeIndex] : polygons[0];
	if (boundary.size() < 2)
		return false;

	bool haveCandidate = false;
	float bestDistance2 = 0.0f;
	Vec2 best = { 0.0f, 0.0f };
	for (uint32 i = 0, j = boundary.size() - 1; i < boundary.size(); j = i++) {
		Vec2 candidate = closestPointOnSegment(boundary[j], boundary[i], x, y);

		// A point exactly on a hole edge is classified as inside the hole.
		// Extend the shortest interior-to-edge vector just beyond the boundary,
		// which mirrors the camera being pushed back into legal MapCam space.
		if (insideHole) {
			float dx = candidate.x - x;
			float dy = candidate.y - y;
			const float length2 = dx * dx + dy * dy;
			if (length2 > 1.0e-12f) {
				const float invLength = 1.0f / std::sqrt(length2);
				candidate.x += dx * invLength * 0.05f;
				candidate.y += dy * invLength * 0.05f;
			}
		}

		if (!containsWalkablePoint(candidate.x, candidate.y))
			continue;

		const float dx = candidate.x - x;
		const float dy = candidate.y - y;
		const float distance2 = dx * dx + dy * dy;
		if (!haveCandidate || distance2 < bestDistance2) {
			haveCandidate = true;
			bestDistance2 = distance2;
			best = candidate;
		}
	}

	if (haveCandidate) {
		result = best;
		return true;
	}

	// Degenerate/custom maps can defeat the polygon projection above. Fall back
	// to a navigation point only if that point is itself legal walkable space.
	for (uint32 i = 0; i < graph.size(); ++i) {
		if (!containsWalkablePoint(graph[i].pos.x, graph[i].pos.y))
			continue;
		const float dx = graph[i].pos.x - x;
		const float dy = graph[i].pos.y - y;
		const float distance2 = dx * dx + dy * dy;
		if (!haveCandidate || distance2 < bestDistance2) {
			haveCandidate = true;
			bestDistance2 = distance2;
			best = graph[i].pos;
		}
	}
	if (!haveCandidate)
		return false;

	result = best;
	return true;
}

static float cross2D(float ax, float ay, float bx, float by) {
	return ax * by - ay * bx;
}

static bool segmentIntersectionParameter(const Vec2 &from, const Vec2 &to,
                                         const Vec2 &a, const Vec2 &b,
                                         float &t) {
	const float rx = to.x - from.x;
	const float ry = to.y - from.y;
	const float sx = b.x - a.x;
	const float sy = b.y - a.y;
	const float denominator = cross2D(rx, ry, sx, sy);
	if (std::fabs(denominator) <= 1.0e-7f)
		return false;

	const float qpx = a.x - from.x;
	const float qpy = a.y - from.y;
	const float candidateT = cross2D(qpx, qpy, sx, sy) / denominator;
	const float u = cross2D(qpx, qpy, rx, ry) / denominator;
	if (candidateT < 0.0f || candidateT > 1.0f ||
	    u < -1.0e-5f || u > 1.0f + 1.0e-5f)
		return false;

	t = candidateT;
	return true;
}

bool BspMap::clipWalkableSegment(float fromX, float fromY,
                                 float toX, float toY,
                                 Vec2 &result) const {
	if (_treeRoot < 0 || edges.empty() || points.empty() ||
	    containingCell(fromX, fromY) < 0)
		return false;

	const Vec2 from = { fromX, fromY };
	const Vec2 to = { toX, toY };
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	const float length2 = dx * dx + dy * dy;
	if (length2 <= 1.0e-12f) {
		result = from;
		return true;
	}

	// The retail BSP partitions legal space into convex cells. Edges with a
	// valid cell only on the front side are the actual solid MapCam boundary;
	// front+back edges are portals between convex cells and must not shorten the
	// camera boom. Find the first solid edge that the focus->camera segment
	// truly exits through.
	bool clipped = false;
	float bestT = 1.0f;
	const float length = std::sqrt(length2);
	const float probeStep = length > 0.0001f ? 0.05f / length : 1.0e-4f;

	for (uint32 edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
		const BspEdge &edge = edges[edgeIndex];
		if (edge.front < 0 || edge.back >= 0 ||
		    edge.p0 < 0 || edge.p1 < 0 ||
		    (uint32)edge.p0 >= points.size() || (uint32)edge.p1 >= points.size())
			continue;

		float t = 0.0f;
		if (!segmentIntersectionParameter(from, to,
		                                  points[(uint32)edge.p0],
		                                  points[(uint32)edge.p1], t) ||
		    t <= 1.0e-5f || t >= bestT)
			continue;

		// Vertex/tangent contacts can touch a solid edge without leaving the
		// legal BSP. Probe a tiny fixed world distance past the hit and accept the
		// edge only when the BSP tree says the boom is now outside every cell.
		const float afterT = t + probeStep < 1.0f ? t + probeStep : 1.0f;
		if (afterT > t &&
		    containingCell(from.x + dx * afterT,
		                   from.y + dy * afterT) >= 0)
			continue;

		bestT = t;
		clipped = true;
	}

	result.x = from.x + dx * (clipped ? bestT : 1.0f);
	result.y = from.y + dy * (clipped ? bestT : 1.0f);
	return true;
}

int BspMap::nearestGraphNode(float x, float y) const {
	if (graph.empty())
		return -1;

	int best = -1;
	float bestDistance2 = 0.0f;
	for (uint32 i = 0; i < graph.size(); ++i) {
		const float dx = graph[i].pos.x - x;
		const float dy = graph[i].pos.y - y;
		const float distance2 = dx * dx + dy * dy;
		if (best < 0 || distance2 < bestDistance2) {
			best = (int)i;
			bestDistance2 = distance2;
		}
	}
	return best;
}

bool BspMap::shortestPath(int startNode, int endNode, Common::Array<int> &path) const {
	path.clear();
	if (startNode < 0 || endNode < 0 ||
	    (uint32)startNode >= graph.size() || (uint32)endNode >= graph.size())
		return false;

	if (startNode == endNode) {
		path.push_back(startNode);
		return true;
	}

	Common::Array<float> distance;
	Common::Array<int> previous;
	Common::Array<byte> visited;
	distance.resize(graph.size());
	previous.resize(graph.size());
	visited.resize(graph.size());

	for (uint32 i = 0; i < graph.size(); ++i) {
		distance[i] = 1.0e30f;
		previous[i] = -1;
		visited[i] = 0;
	}
	distance[(uint32)startNode] = 0.0f;

	for (uint32 step = 0; step < graph.size(); ++step) {
		int current = -1;
		float currentDistance = 1.0e30f;
		for (uint32 i = 0; i < graph.size(); ++i) {
			if (!visited[i] && distance[i] < currentDistance) {
				current = (int)i;
				currentDistance = distance[i];
			}
		}

		if (current < 0)
			break;
		if (current == endNode)
			break;
		visited[(uint32)current] = 1;

		const NavNode &node = graph[(uint32)current];
		for (uint32 i = 0; i < node.arcs.size(); ++i) {
			const NavArc &arc = node.arcs[i];
			if (arc.target < 0 || (uint32)arc.target >= graph.size() || arc.weight < 0.0f)
				continue;
			const float candidate = currentDistance + arc.weight;
			if (candidate < distance[(uint32)arc.target]) {
				distance[(uint32)arc.target] = candidate;
				previous[(uint32)arc.target] = current;
			}
		}
	}

	if (previous[(uint32)endNode] < 0)
		return false;

	Common::Array<int> reverse;
	for (int node = endNode; node >= 0; node = previous[(uint32)node]) {
		reverse.push_back(node);
		if (node == startNode)
			break;
	}
	if (reverse.empty() || reverse.back() != startNode)
		return false;

	for (int i = (int)reverse.size() - 1; i >= 0; --i)
		path.push_back(reverse[(uint32)i]);
	return true;
}

} // namespace ZeroComico
