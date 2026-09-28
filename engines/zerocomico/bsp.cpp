/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: 2D BSP/pathfinding reader
 */

#include "zerocomico/bsp.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/str.h"

#include <cstdio>

namespace ZeroComico {

class LineReader {
public:
	LineReader(Common::SeekableReadStream &stream) : _stream(stream) {}

	Common::String next() {
		while (!_stream.eos()) {
			Common::String s = _stream.readLine();
			while (!s.empty() && (s.lastChar() == '\r' || s.lastChar() == '\n' || s.lastChar() == ' ' || s.lastChar() == '\t'))
				s.deleteLastChar();
			uint first = 0;
			while (first < s.size() && (s[first] == ' ' || s[first] == '\t'))
				++first;
			if (first)
				s = Common::String(s.c_str() + first);
			if (!s.empty())
				return s;
		}
		return Common::String();
	}

	bool expect(const char *word) { return next() == word; }
	int integer(bool &ok) {
		Common::String s = next();
		int v = 0;
		if (sscanf(s.c_str(), "%d", &v) != 1)
			ok = false;
		return v;
	}

private:
	Common::SeekableReadStream &_stream;
};

static bool parseVec2(const Common::String &s, Vec2 &v) {
	return sscanf(s.c_str(), "%f %f", &v.x, &v.y) == 2;
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
	if (sscanf(line.c_str(), "%d %d %d", &node.edge, &node.leaf, &node.unused) != 3) {
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
		points.push_back(v);
	}

	if (!ok || !r.expect("bsp_edges"))
		return false;
	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		BspEdge e;
		const Common::String s = r.next();
		if (sscanf(s.c_str(), "%d %d %d %d", &e.p0, &e.p1, &e.front, &e.back) != 4)
			ok = false;
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
		Common::String arcs = r.next();
		const char *p = arcs.c_str();
		while (ok && *p) {
			int target = -1;
			float weight = 0.0f;
			int consumed = 0;
			if (sscanf(p, "%d%n", &target, &consumed) != 1) {
				ok = false;
				break;
			}
			p += consumed;
			while (*p == ' ' || *p == '\t')
				++p;
			if (target == -1)
				break;
			if (sscanf(p, "%f%n", &weight, &consumed) != 1) {
				ok = false;
				break;
			}
			p += consumed;
			while (*p == ' ' || *p == '\t')
				++p;
			NavArc arc;
			arc.target = target;
			arc.weight = weight;
			node.arcs.push_back(arc);
		}
		graph.push_back(node);
	}

	// Parse and validate the support block even though this first engine stage
	// does not retain it yet. This keeps the reader synchronized to EOF.
	if (!ok || !r.expect("support"))
		return false;
	count = r.integer(ok);
	for (int i = 0; ok && i < count; ++i) {
		int n = r.integer(ok);
		for (int j = 0; ok && j < n; ++j)
			(void)r.integer(ok);
		n = r.integer(ok);
		for (int j = 0; ok && j < n; ++j) {
			int index = 0;
			float weight = 0.0f;
			if (sscanf(r.next().c_str(), "%d %f", &index, &weight) != 2)
				ok = false;
		}
	}

	return ok && r.expect("pathfinding_end");
}

} // namespace ZeroComico
