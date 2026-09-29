/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: 2D BSP/pathfinding reader
 */

#ifndef ZEROCOMICO_BSP_H
#define ZEROCOMICO_BSP_H

#include "common/array.h"
#include "common/path.h"
#include "common/scummsys.h"

namespace Common {
class SeekableReadStream;
}

namespace ZeroComico {

struct Vec2 {
	float x;
	float y;
};

struct BspEdge {
	int p0;
	int p1;
	int front;
	int back;
};

struct BspCell {
	int index;
	Common::Array<int> edges;
};

struct BspTreeNode {
	int edge;
	int leaf;
	int unused;
	int left;
	int right;
};

struct NavArc {
	int target;
	float weight;
};

struct NavNode {
	Vec2 pos;
	Common::Array<NavArc> arcs;
};

struct BspSupport {
	Common::Array<int> insideNodes;
	Common::Array<NavArc> weightedNodes;
};

class BspMap {
public:
	bool load(const Common::Path &path);
	bool load(Common::SeekableReadStream &stream);

	Common::Array<Common::Array<Vec2> > polygons;
	Common::Array<Vec2> points;
	Common::Array<BspEdge> edges;
	Common::Array<BspCell> cells;
	Common::Array<BspTreeNode> tree;
	Common::Array<NavNode> graph;
	Common::Array<BspSupport> support;

	bool containsWalkablePoint(float x, float y) const;
	bool nearestWalkablePoint(float x, float y, Vec2 &result) const;
	bool clipWalkableSegment(float fromX, float fromY, float toX, float toY,
	                         Vec2 &result) const;
	int nearestGraphNode(float x, float y) const;
	bool shortestPath(int startNode, int endNode, Common::Array<int> &path) const;

private:
	int parseTree(class LineReader &reader, bool &ok);
};

} // namespace ZeroComico

#endif
