/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: 2D BSP/pathfinding reader
 */

#include "zerocomico/bsp.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/str.h"
#include "common/tokenizer.h"

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
			Common::StringTokenizer tok(r.next());
			int index = 0;
			float weight = 0.0f;
			if (tok.empty() || !tokenInt(tok.nextToken(), index) ||
			    tok.empty() || !tokenFloat(tok.nextToken(), weight) || !tok.empty())
				ok = false;
		}
	}

	return ok && r.expect("pathfinding_end");
}

} // namespace ZeroComico
