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
	parseTree(r, ok);
	if (!ok || !r.expect("bsp_end") || !r.expect("pathfinding") || !r.expect("graph"))
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
