#include "doomtype.h"
#include "nodebuild.h"
#include "r_main.h"

static inline void Warn (const char *format, ...)
{
}

static const angle_t ANGLE_EPSILON = 5000;

#if 0
#define D(x) x
#else
#define D(x) do{}while(0)
#endif

double FNodeBuilder::AddIntersection (const node_t &node, int vertex)
{
	static const FEventInfo defaultInfo =
	{
		-1, -1
	};

	// Calculate signed distance of intersection vertex from start of splitter.
	// Only ordering is important, so we don't need a sqrt.
	FPrivVert *v = &Vertices[vertex];
	double dx = double(v->x - node.x);
	double dy = double(v->y - node.y);
	double dist = dx*dx + dy*dy;

	if (node.dx != 0)
	{
		if ((node.dx > 0 && dx < 0.0) || (node.dx < 0 && dx > 0.0))
		{
			dist = -dist;
		}
	}
	else
	{
		if ((node.dy > 0 && dy < 0.0) || (node.dy < 0 && dy > 0.0))
		{
			dist = -dist;
		}
	}

	FEvent *event = Events.FindEvent (dist);
	if (event == NULL)
	{
		event = Events.GetNewNode ();
		event->Distance = dist;
		event->Info = defaultInfo;
		event->Info.Vertex = vertex;
		Events.Insert (event);
	}
	return dist;
}

// If there are any segs on the splitter that span more than two events, they
// must be split. Alien Vendetta is one example wad that is quite bad about
// having overlapping lines. If we skip this step, these segs will still be
// split later, but minisegs will erroneously be added for them, and partner
// seg information will be messed up in the generated tree.
void FNodeBuilder::FixSplitSharers (const node_t &node)
{
	for (size_t i = 0; i < SplitSharers.Size(); ++i)
	{
		WORD seg = SplitSharers[i].Seg;
		int v2 = Segs[seg].v2;
		FEvent *event = Events.FindEvent (SplitSharers[i].Distance);
		FEvent *next;

		if (event == NULL)
		{ // Should not happen
			continue;
		}

		if (SplitSharers[i].Forward)
		{
			event = Events.GetSuccessor (event);
			if (event == NULL)
			{
				continue;
			}
			next = Events.GetSuccessor (event);
		}
		else
		{
			event = Events.GetPredecessor (event);
			if (event == NULL)
			{
				continue;
			}
			next = Events.GetPredecessor (event);
		}

		while (event != NULL && next != NULL && event->Info.Vertex != v2)
		{
			D(Printf("Forced split of seg %d(%d->%d) at %d(%d,%d)\n", seg,
				Segs[seg].v1, Segs[seg].v2,
				event->Info.Vertex,
				Vertices[event->Info.Vertex].x>>16,
				Vertices[event->Info.Vertex].y>>16));

			WORD newseg = SplitSeg (seg, event->Info.Vertex, 1);

			Segs[newseg].next = Segs[seg].next;
			Segs[seg].next = newseg;

			WORD partner = Segs[seg].partner;
			if (partner != NO_INDEX)
			{
				int endpartner = SplitSeg (partner, event->Info.Vertex, 1);

				Segs[endpartner].next = Segs[partner].next;
				Segs[partner].next = endpartner;

				Segs[seg].partner = endpartner;
				Segs[endpartner].partner = seg;
			}

			seg = newseg;
			if (SplitSharers[i].Forward)
			{
				event = next;
				next = Events.GetSuccessor (next);
			}
			else
			{
				event = next;
				next = Events.GetPredecessor (next);
			}
		}
	}
}

void FNodeBuilder::AddMinisegs (const node_t &node, int splitseg, WORD &fset, WORD &bset)
{
	FEvent *event = Events.GetMinimum (), *prev = NULL;

	while (event != NULL)
	{
		if (prev != NULL)
		{
			int fseg1, bseg1, fseg2, bseg2;
			WORD fnseg, bnseg;

			// Minisegs should only be added when they can create valid loops on both the front and
			// back of the splitter. This means some subsectors could be unclosed if their sectors
			// are unclosed, but at least we won't be needlessly creating subsectors in void space.
			// Unclosed subsectors can be closed trivially once the BSP tree is complete.

			if ((fseg1 = CheckLoopStart (node.dx, node.dy, prev->Info.Vertex, event->Info.Vertex)) >= 0 &&
				(bseg1 = CheckLoopStart (-node.dx, -node.dy, event->Info.Vertex, prev->Info.Vertex)) >= 0 &&
				(fseg2 = CheckLoopEnd (node.dx, node.dy, prev->Info.Vertex, event->Info.Vertex)) >= 0 &&
				(bseg2 = CheckLoopEnd (-node.dx, -node.dy, event->Info.Vertex, prev->Info.Vertex)) >= 0)
			{
				// Add miniseg on the front side
				fnseg = AddMiniseg (prev->Info.Vertex, event->Info.Vertex, NO_INDEX, fseg1, splitseg);
				Segs[fnseg].next = fset;
				fset = fnseg;

				// Add miniseg on the back side
				bnseg = AddMiniseg (event->Info.Vertex, prev->Info.Vertex, fnseg, bseg1, splitseg);
				Segs[bnseg].next = bset;
				bset = bnseg;

				sector_t *fsector, *bsector;

				fsector = Segs[fseg1].frontsector;
				bsector = Segs[bseg1].frontsector;

				Segs[fnseg].frontsector = fsector;
				Segs[fnseg].backsector = bsector;
				Segs[bnseg].frontsector = bsector;
				Segs[bnseg].backsector = fsector;

				// Only print the warning if this might be bad.
				if (fsector != bsector &&
					fsector != Segs[fseg1].backsector &&
					bsector != Segs[bseg1].backsector)
				{
					Warn ("Sectors %d at (%d,%d) and %d at (%d,%d) don't match.\n",
						Segs[fseg1].frontsector,
						Vertices[prev->Info.Vertex].x>>FRACBITS, Vertices[prev->Info.Vertex].y>>FRACBITS,
						Segs[bseg1].frontsector,
						Vertices[event->Info.Vertex].x>>FRACBITS, Vertices[event->Info.Vertex].y>>FRACBITS
						);
				}

				D(Printf ("**Minisegs** %d/%d added %d(%d,%d)->%d(%d,%d)\n", fnseg, bnseg,
					prev->Info.Vertex,
					Vertices[prev->Info.Vertex].x>>16, Vertices[prev->Info.Vertex].y>>16,
					event->Info.Vertex,
					Vertices[event->Info.Vertex].x>>16, Vertices[event->Info.Vertex].y>>16));
			}
		}
		prev = event;
		event = Events.GetSuccessor (event);
	}
}

WORD FNodeBuilder::AddMiniseg (int v1, int v2, WORD partner, int seg1, int splitseg)
{
	int nseg;
	FPrivSeg *seg = &Segs[seg1];
	FPrivSeg newseg;

	newseg.sidedef = NO_INDEX;
	newseg.linedef = -1;
	newseg.loopnum = 0;
	newseg.next = NO_INDEX;
	newseg.planefront = true;

	if (splitseg >= 0)
	{
		newseg.planenum = Segs[splitseg].planenum;
	}
	else
	{
		newseg.planenum = -1;
	}

	newseg.v1 = v1;
	newseg.v2 = v2;
	newseg.nextforvert = Vertices[v1].segs;
	newseg.nextforvert2 = Vertices[v2].segs2;
	newseg.next = seg->next;
	if (partner != NO_INDEX)
	{
		newseg.partner = partner;
	}
	else
	{
		newseg.partner = NO_INDEX;
	}
	nseg = (int)Segs.Push (newseg);
	if (newseg.partner != NO_INDEX)
	{
		Segs[partner].partner = nseg;
	}
	Vertices[v1].segs = nseg;
	Vertices[v2].segs2 = nseg;
	//Printf ("Between %d and %d::::\n", seg1, seg2);
	return nseg;
}

int FNodeBuilder::CheckLoopStart (fixed_t dx, fixed_t dy, int vertex, int vertex2)
{
	FPrivVert *v = &Vertices[vertex];
	angle_t splitAngle = PointToAngle (dx, dy);
	int segnum;
	angle_t bestang;
	int bestseg;

	// Find the seg ending at this vertex that forms the smallest angle
	// to the splitter.
	segnum = v->segs2;
	bestang = ANGLE_MAX;
	bestseg = -1;
	while (segnum != NO_INDEX)
	{
		FPrivSeg *seg = &Segs[segnum];
		angle_t segAngle = PointToAngle (Vertices[seg->v1].x - v->x, Vertices[seg->v1].y - v->y);
		angle_t diff = splitAngle - segAngle;

		if (diff < ANGLE_EPSILON &&
			PointOnSide (Vertices[seg->v1].x, Vertices[seg->v1].y, v->x, v->y, dx, dy) == 0)
		{
			// If a seg lies right on the splitter, don't count it
		}
		else
		{
			if (diff <= bestang)
			{
				bestang = diff;
				bestseg = segnum;
			}
		}
		segnum = seg->nextforvert2;
	}
	if (bestseg < 0)
	{
		return -1;
	}
	// Now make sure there are no segs starting at this vertex that form
	// an even smaller angle to the splitter.
	segnum = v->segs;
	while (segnum != NO_INDEX)
	{
		FPrivSeg *seg = &Segs[segnum];
		if (seg->v2 == vertex2)
		{
			return -1;
		}
		angle_t segAngle = PointToAngle (Vertices[seg->v2].x - v->x, Vertices[seg->v2].y - v->y);
		angle_t diff = splitAngle - segAngle;
		if (diff < bestang && seg->partner != bestseg)
		{
			return -1;
		}
		segnum = seg->nextforvert;
	}
	return bestseg;
}

int FNodeBuilder::CheckLoopEnd (fixed_t dx, fixed_t dy, int vertex1, int vertex)
{
	FPrivVert *v = &Vertices[vertex];
	angle_t splitAngle = PointToAngle (dx, dy) + ANGLE_180;
	int segnum;
	angle_t bestang;
	int bestseg;

	// Find the seg starting at this vertex that forms the smallest angle
	// to the splitter.
	segnum = v->segs;
	bestang = ANGLE_MAX;
	bestseg = -1;
	while (segnum != NO_INDEX)
	{
		FPrivSeg *seg = &Segs[segnum];
		angle_t segAngle = PointToAngle (Vertices[seg->v2].x - v->x, Vertices[seg->v2].y - v->y);
		angle_t diff = segAngle - splitAngle;

		if (diff < ANGLE_EPSILON &&
			PointOnSide (Vertices[seg->v1].x, Vertices[seg->v1].y, v->x, v->y, dx, dy) == 0)
		{
			// If a seg lies right on the splitter, don't count it
		}
		else
		{
			if (diff <= bestang)
			{
				bestang = diff;
				bestseg = segnum;
			}
		}
		segnum = seg->nextforvert;
	}
	if (bestseg < 0)
	{
		return -1;
	}
	// Now make sure there are no segs ending at this vertex that form
	// an even smaller angle to the splitter.
	segnum = v->segs2;
	while (segnum != NO_INDEX)
	{
		FPrivSeg *seg = &Segs[segnum];
		angle_t segAngle = PointToAngle (Vertices[seg->v1].x - v->x, Vertices[seg->v1].y - v->y);
		angle_t diff = segAngle - splitAngle;
		if (diff < bestang && seg->partner != bestseg)
		{
			return -1;
		}
		segnum = seg->nextforvert2;
	}
	return bestseg;
}