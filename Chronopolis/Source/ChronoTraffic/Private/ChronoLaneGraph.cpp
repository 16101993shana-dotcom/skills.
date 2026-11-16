#include "ChronoLaneGraph.h"

#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoTrafficSettings.h"

namespace ChronoLaneGraphPrivate
{
	/** Fills Distances and returns the total length of a polyline. */
	double MeasurePolyline(const TArray<FVector>& Points, TArray<double>& OutDistances)
	{
		OutDistances.Reset(Points.Num());
		double Length = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Index > 0)
			{
				Length += FVector::Distance(Points[Index - 1], Points[Index]);
			}
			OutDistances.Add(Length);
		}
		return Length;
	}

	void SamplePolyline(const TArray<FVector>& Points, const TArray<double>& Distances, double Distance, FVector& OutPosition, FVector& OutDirection)
	{
		OutPosition = FVector::ZeroVector;
		OutDirection = FVector(1.0, 0.0, 0.0);
		if (Points.Num() == 0)
		{
			return;
		}
		if (Points.Num() == 1)
		{
			OutPosition = Points[0];
			return;
		}

		// Binary search for the piece that contains the distance.
		int32 Low = 0;
		int32 High = Points.Num() - 1;
		while (High - Low > 1)
		{
			const int32 Mid = (Low + High) / 2;
			if (Distances[Mid] <= Distance)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}

		const double Span = Distances[High] - Distances[Low];
		const double Alpha = Span > UE_DOUBLE_KINDA_SMALL_NUMBER ? FMath::Clamp((Distance - Distances[Low]) / Span, 0.0, 1.0) : 0.0;
		OutPosition = Points[Low] + (Points[High] - Points[Low]) * Alpha;

		const FVector Direction = (Points[High] - Points[Low]).GetSafeNormal();
		if (Direction.SizeSquared() > 0.5)
		{
			OutDirection = Direction;
		}
	}

	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	double DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double T = LengthSquared > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(Point, A + AB * T);
	}

	/** Distance between two segments in the ground plane (0 if they cross). */
	double DistanceSegmentToSegment2D(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		const double D1 = Cross2D(B - A, C - A);
		const double D2 = Cross2D(B - A, D - A);
		const double D3 = Cross2D(D - C, A - C);
		const double D4 = Cross2D(D - C, B - C);
		if (((D1 > 0.0 && D2 < 0.0) || (D1 < 0.0 && D2 > 0.0)) && ((D3 > 0.0 && D4 < 0.0) || (D3 < 0.0 && D4 > 0.0)))
		{
			return 0.0;
		}

		return FMath::Min(
			FMath::Min(DistancePointToSegment2D(A, C, D), DistancePointToSegment2D(B, C, D)),
			FMath::Min(DistancePointToSegment2D(C, A, B), DistancePointToSegment2D(D, A, B)));
	}

	double DistancePolylineToPolyline2D(const TArray<FVector>& First, const TArray<FVector>& Second)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 I = 1; I < First.Num(); ++I)
		{
			const FVector2D A(First[I - 1].X, First[I - 1].Y);
			const FVector2D B(First[I].X, First[I].Y);
			for (int32 J = 1; J < Second.Num(); ++J)
			{
				Best = FMath::Min(Best, DistanceSegmentToSegment2D(A, B, FVector2D(Second[J - 1].X, Second[J - 1].Y), FVector2D(Second[J].X, Second[J].Y)));
			}
		}
		return Best;
	}

	/** Entry of the routing queue: a lane and the time needed to reach its start. */
	struct FQueueEntry
	{
		double Cost = 0.0;
		int32 LaneId = INDEX_NONE;
	};

	/** Minimal binary heap, smallest cost first. Ties are broken by lane id, for determinism. */
	struct FRouteQueue
	{
		TArray<FQueueEntry> Entries;

		static bool Before(const FQueueEntry& A, const FQueueEntry& B)
		{
			return A.Cost != B.Cost ? A.Cost < B.Cost : A.LaneId < B.LaneId;
		}

		void Push(const FQueueEntry& Entry)
		{
			Entries.Add(Entry);
			int32 Index = Entries.Num() - 1;
			while (Index > 0)
			{
				const int32 Parent = (Index - 1) / 2;
				if (!Before(Entries[Index], Entries[Parent]))
				{
					break;
				}
				Swap(Entries[Index], Entries[Parent]);
				Index = Parent;
			}
		}

		FQueueEntry Pop()
		{
			const FQueueEntry Top = Entries[0];
			Entries[0] = Entries.Last();
			Entries.RemoveAt(Entries.Num() - 1);

			int32 Index = 0;
			while (true)
			{
				const int32 Left = 2 * Index + 1;
				const int32 Right = Left + 1;
				int32 Smallest = Index;
				if (Left < Entries.Num() && Before(Entries[Left], Entries[Smallest]))
				{
					Smallest = Left;
				}
				if (Right < Entries.Num() && Before(Entries[Right], Entries[Smallest]))
				{
					Smallest = Right;
				}
				if (Smallest == Index)
				{
					break;
				}
				Swap(Entries[Index], Entries[Smallest]);
				Index = Smallest;
			}
			return Top;
		}
	};
}

FChronoTrafficRules FChronoTrafficRules::FromSettings()
{
	const UChronoTrafficSettings* Settings = UChronoTrafficSettings::Get();

	FChronoTrafficRules Rules;
	Rules.bDriveOnRight = Settings->bDriveOnRight;
	Rules.SampleSpacingCm = Settings->SampleSpacingCm;
	Rules.TurnSpeedCmPerS = Settings->TurnSpeedCmPerS;
	Rules.ConflictClearanceCm = Settings->ConflictClearanceCm;
	Rules.VehicleLengthCm = Settings->VehicleLengthCm;
	Rules.VehicleMaxSpeedCmPerS = Settings->VehicleMaxSpeedCmPerS;
	Rules.MaxAccelerationCmPerS2 = Settings->MaxAccelerationCmPerS2;
	Rules.ComfortDecelerationCmPerS2 = Settings->ComfortDecelerationCmPerS2;
	Rules.MinGapCm = Settings->MinGapCm;
	Rules.TimeHeadwaySeconds = Settings->TimeHeadwaySeconds;
	Rules.StopMarginCm = Settings->StopMarginCm;
	Rules.ReserveDistanceCm = Settings->ReserveDistanceCm;
	Rules.StuckTimeoutSeconds = Settings->StuckTimeoutSeconds;
	Rules.MaxActiveVehicles = Settings->MaxActiveVehicles;
	return Rules;
}

void FChronoLaneGraph::Reset()
{
	Lanes.Reset();
	Connectors.Reset();
	LanesBySegment.Reset();
}

TConstArrayView<int32> FChronoLaneGraph::GetSegmentLanes(int32 SegmentId) const
{
	const TArray<int32>* Found = LanesBySegment.Find(SegmentId);
	return Found ? TConstArrayView<int32>(*Found) : TConstArrayView<int32>();
}

void FChronoLaneGraph::Build(const FChronoRoadGraph& Graph, const FChronoTrafficRules& Rules)
{
	using namespace ChronoLaneGraphPrivate;

	Reset();
	bDriveOnRight = Rules.bDriveOnRight;

	// The side vector of a road points to the right of its direction of
	// travel (seen from above in the engine's axes), so with right-hand
	// traffic the lanes that follow the segment are on its positive side.
	const double KeepSide = Rules.bDriveOnRight ? 1.0 : -1.0;

	// ---- Lanes, segment by segment, in id order so lane ids are deterministic ----
	TArray<int32> SegmentIds;
	Graph.GetSegments().GetKeys(SegmentIds);
	SegmentIds.Sort();

	TArray<FChronoRoadSample> FullSamples;
	TArray<FChronoRoadSample> Samples;

	for (const int32 SegmentId : SegmentIds)
	{
		const FChronoRoadSegment& Segment = Graph.GetSegments().FindChecked(SegmentId);
		if (Segment.GetUsableLength() <= 1.0)
		{
			continue;
		}

		ChronoRoadGeometry::BuildPolyline(Segment.Curve, Rules.SampleSpacingCm, FullSamples);
		ChronoRoadGeometry::ClipPolyline(FullSamples, Segment.TrimStart, Segment.Length - Segment.TrimEnd, Samples);
		if (Samples.Num() < 2)
		{
			continue;
		}

		const int32 LanesPerDirection = FMath::Max(Segment.Profile.LanesPerDirection, 1);
		const bool bOneWay = Segment.Profile.bOneWay;
		const double Carriageway = Segment.Profile.CarriagewayWidthCm;
		const double LaneWidth = Carriageway / (bOneWay ? LanesPerDirection : 2 * LanesPerDirection);

		for (int32 Direction = 0; Direction < (bOneWay ? 1 : 2); ++Direction)
		{
			const bool bForward = (Direction == 0);

			for (int32 LaneIndex = 0; LaneIndex < LanesPerDirection; ++LaneIndex)
			{
				// Lateral position along the side vector of the segment.
				double Lateral;
				if (bOneWay)
				{
					Lateral = (LaneIndex + 0.5) * LaneWidth - 0.5 * Carriageway;
				}
				else
				{
					Lateral = (LaneIndex + 0.5) * LaneWidth * (bForward ? KeepSide : -KeepSide);
				}

				FChronoLane Lane;
				Lane.Id = Lanes.Num();
				Lane.SegmentId = SegmentId;
				Lane.bForward = bForward;
				Lane.LaneIndex = LaneIndex;
				Lane.FromNodeId = bForward ? Segment.StartNodeId : Segment.EndNodeId;
				Lane.ToNodeId = bForward ? Segment.EndNodeId : Segment.StartNodeId;
				Lane.SpeedLimitCmPerS = Segment.Profile.SpeedLimitCmPerS;
				Lane.SegmentStartDistance = Samples[0].Distance;
				Lane.SegmentEndDistance = Samples.Last().Distance;

				Lane.Points.Reserve(Samples.Num());
				for (int32 Index = 0; Index < Samples.Num(); ++Index)
				{
					const FChronoRoadSample& Sample = Samples[bForward ? Index : Samples.Num() - 1 - Index];
					const FVector2D Side(-Sample.Tangent.Y, Sample.Tangent.X);
					const FVector2D Position = Sample.Position + Side * Lateral;
					Lane.Points.Add(FVector(Position.X, Position.Y, Graph.GetSegmentHeightAtDistance(Segment, Sample.Distance)));
				}
				Lane.Length = MeasurePolyline(Lane.Points, Lane.Distances);

				LanesBySegment.FindOrAdd(SegmentId).Add(Lane.Id);
				Lanes.Add(MoveTemp(Lane));
			}
		}
	}

	// ---- Connectors, node by node ----
	TMap<int32, TArray<int32>> Arriving;
	TMap<int32, TArray<int32>> Departing;
	for (const FChronoLane& Lane : Lanes)
	{
		Arriving.FindOrAdd(Lane.ToNodeId).Add(Lane.Id);
		Departing.FindOrAdd(Lane.FromNodeId).Add(Lane.Id);
	}

	TArray<int32> NodeIds;
	Graph.GetNodes().GetKeys(NodeIds);
	NodeIds.Sort();

	// Number of straight pieces of a connector.
	const int32 ConnectorPieces = 8;

	for (const int32 NodeId : NodeIds)
	{
		const FChronoRoadNode& Node = Graph.GetNodes().FindChecked(NodeId);
		const TArray<int32>* In = Arriving.Find(NodeId);
		const TArray<int32>* Out = Departing.Find(NodeId);
		if (!In || !Out)
		{
			continue;
		}

		const bool bDeadEnd = Node.SegmentIds.Num() == 1;
		const int32 FirstConnector = Connectors.Num();

		for (const int32 InLaneId : *In)
		{
			for (const int32 OutLaneId : *Out)
			{
				// Turning back onto the road one came from is only allowed at a dead end.
				if (Lanes[InLaneId].SegmentId == Lanes[OutLaneId].SegmentId && !bDeadEnd)
				{
					continue;
				}

				const FChronoLane& InLane = Lanes[InLaneId];
				const FChronoLane& OutLane = Lanes[OutLaneId];
				const FVector Start = InLane.Points.Last();
				const FVector End = OutLane.Points[0];
				const FVector StartDirection = (InLane.Points.Last() - InLane.Points[InLane.Points.Num() - 2]).GetSafeNormal();
				const FVector EndDirection = (OutLane.Points[1] - OutLane.Points[0]).GetSafeNormal();

				// Control point of the curve: where the line that continues the
				// arriving lane meets the line that precedes the departing one.
				// For lanes that are nearly parallel, the midpoint (a straight
				// crossing, or a turn on the spot at a dead end).
				FVector Control = (Start + End) * 0.5;
				const FVector2D S2(Start.X, Start.Y);
				const FVector2D E2(End.X, End.Y);
				const FVector2D SD(StartDirection.X, StartDirection.Y);
				const FVector2D ED(EndDirection.X, EndDirection.Y);
				const double Denominator = Cross2D(SD, ED);
				if (FMath::Abs(Denominator) > 0.2)
				{
					const double AlongStart = Cross2D(E2 - S2, ED) / Denominator;
					const double BeforeEnd = Cross2D(E2 - S2, SD) / Denominator;
					if (AlongStart > 0.0 && BeforeEnd < 0.0)
					{
						const FVector2D Meeting = S2 + SD * AlongStart;
						Control = FVector(Meeting.X, Meeting.Y, 0.5 * (Start.Z + End.Z));
					}
				}

				FChronoLaneConnector Connector;
				Connector.Id = Connectors.Num();
				Connector.NodeId = NodeId;
				Connector.FromLaneId = InLaneId;
				Connector.ToLaneId = OutLaneId;
				for (int32 Piece = 0; Piece <= ConnectorPieces; ++Piece)
				{
					const double T = static_cast<double>(Piece) / ConnectorPieces;
					const double U = 1.0 - T;
					Connector.Points.Add(Start * (U * U) + Control * (2.0 * U * T) + End * (T * T));
				}
				Connector.Length = MeasurePolyline(Connector.Points, Connector.Distances);

				Lanes[InLaneId].OutConnectors.Add(Connector.Id);
				Connectors.Add(MoveTemp(Connector));
			}
		}

		// Conflicts inside this junction: two paths from different lanes that
		// merge into the same lane or pass too close to each other.
		for (int32 A = FirstConnector; A < Connectors.Num(); ++A)
		{
			for (int32 B = A + 1; B < Connectors.Num(); ++B)
			{
				if (Connectors[A].FromLaneId == Connectors[B].FromLaneId)
				{
					// Vehicles of one lane arrive one behind the other.
					continue;
				}

				if (Connectors[A].ToLaneId == Connectors[B].ToLaneId
					|| DistancePolylineToPolyline2D(Connectors[A].Points, Connectors[B].Points) < Rules.ConflictClearanceCm)
				{
					Connectors[A].Conflicts.Add(B);
					Connectors[B].Conflicts.Add(A);
				}
			}
		}
	}
}

int32 FChronoLaneGraph::FindLaneOnSide(int32 SegmentId, int32 Side) const
{
	// With right-hand traffic the lanes that follow the segment run along
	// its positive side; with left-hand traffic, along its negative side.
	const bool bWantForward = (Side > 0) == bDriveOnRight;

	int32 Best = INDEX_NONE;
	int32 Fallback = INDEX_NONE;
	for (const int32 LaneId : GetSegmentLanes(SegmentId))
	{
		const FChronoLane& Lane = Lanes[LaneId];
		if (Lane.bForward == bWantForward)
		{
			// The outermost lane of that direction is the one along the kerb.
			if (Best == INDEX_NONE || Lane.LaneIndex > Lanes[Best].LaneIndex)
			{
				Best = LaneId;
			}
		}
		else if (Fallback == INDEX_NONE)
		{
			Fallback = LaneId;
		}
	}

	// A one-way road has no lane on one of its sides: use the lane it has.
	return Best != INDEX_NONE ? Best : Fallback;
}

double FChronoLaneGraph::SegmentToLaneDistance(int32 LaneId, double SegmentDistance) const
{
	if (!Lanes.IsValidIndex(LaneId))
	{
		return 0.0;
	}

	const FChronoLane& Lane = Lanes[LaneId];
	const double Span = Lane.SegmentEndDistance - Lane.SegmentStartDistance;
	if (Span <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return 0.0;
	}

	// A lane is a little longer or shorter than the centerline on a bend:
	// distances are mapped in proportion.
	const double Along = FMath::Clamp((SegmentDistance - Lane.SegmentStartDistance) / Span, 0.0, 1.0);
	return (Lane.bForward ? Along : 1.0 - Along) * Lane.Length;
}

void FChronoLaneGraph::SampleLane(int32 LaneId, double Distance, FVector& OutPosition, FVector& OutDirection) const
{
	if (Lanes.IsValidIndex(LaneId))
	{
		ChronoLaneGraphPrivate::SamplePolyline(Lanes[LaneId].Points, Lanes[LaneId].Distances, Distance, OutPosition, OutDirection);
	}
}

void FChronoLaneGraph::SampleConnector(int32 ConnectorId, double Distance, FVector& OutPosition, FVector& OutDirection) const
{
	if (Connectors.IsValidIndex(ConnectorId))
	{
		ChronoLaneGraphPrivate::SamplePolyline(Connectors[ConnectorId].Points, Connectors[ConnectorId].Distances, Distance, OutPosition, OutDirection);
	}
}

int32 FChronoLaneGraph::FindConnector(int32 FromLaneId, int32 ToLaneId) const
{
	if (Lanes.IsValidIndex(FromLaneId))
	{
		for (const int32 ConnectorId : Lanes[FromLaneId].OutConnectors)
		{
			if (Connectors[ConnectorId].ToLaneId == ToLaneId)
			{
				return ConnectorId;
			}
		}
	}
	return INDEX_NONE;
}

bool FChronoLaneGraph::FindRoute(int32 FromLaneId, double FromDistance, int32 ToSegmentId, double ToSegmentDistance, const FChronoTrafficRules& Rules, const TArray<float>* LaneSpeeds, TArray<int32>& OutLaneIds, double& OutTargetDistance) const
{
	using namespace ChronoLaneGraphPrivate;

	OutLaneIds.Reset();
	OutTargetDistance = 0.0;
	if (!Lanes.IsValidIndex(FromLaneId))
	{
		return false;
	}

	// Slowest speed used to turn a length into a time: a jammed lane is very
	// slow to cross, but never impossible.
	const float MinSpeed = 30.0f;
	auto LaneSpeed = [this, LaneSpeeds, MinSpeed](int32 LaneId)
	{
		const float Limit = Lanes[LaneId].SpeedLimitCmPerS;
		const float Current = (LaneSpeeds && LaneSpeeds->IsValidIndex(LaneId)) ? (*LaneSpeeds)[LaneId] : Limit;
		return static_cast<double>(FMath::Clamp(Current, MinSpeed, Limit));
	};
	const double TurnSpeed = FMath::Max(Rules.TurnSpeedCmPerS, MinSpeed);

	double BestTotal = TNumericLimits<double>::Max();
	int32 BestLane = INDEX_NONE;

	// The destination may be straight ahead on the lane the trip starts on.
	if (Lanes[FromLaneId].SegmentId == ToSegmentId)
	{
		const double Target = SegmentToLaneDistance(FromLaneId, ToSegmentDistance);
		if (Target >= FromDistance)
		{
			BestTotal = (Target - FromDistance) / LaneSpeed(FromLaneId);
			BestLane = FromLaneId;
		}
	}

	// Dijkstra over lanes. CostToStart is the time to reach the start of a lane.
	TArray<double> CostToStart;
	CostToStart.Init(TNumericLimits<double>::Max(), Lanes.Num());
	TArray<int32> Previous;
	Previous.Init(INDEX_NONE, Lanes.Num());

	FRouteQueue Queue;

	// Leaving the first lane: the rest of it, then one of its connectors.
	const double CostToEndOfFirst = FMath::Max(Lanes[FromLaneId].Length - FromDistance, 0.0) / LaneSpeed(FromLaneId);
	for (const int32 ConnectorId : Lanes[FromLaneId].OutConnectors)
	{
		const FChronoLaneConnector& Connector = Connectors[ConnectorId];
		const double Cost = CostToEndOfFirst + Connector.Length / TurnSpeed;
		if (Cost < CostToStart[Connector.ToLaneId])
		{
			CostToStart[Connector.ToLaneId] = Cost;
			Previous[Connector.ToLaneId] = FromLaneId;
			Queue.Push({ Cost, Connector.ToLaneId });
		}
	}

	bool bBestIsDirect = (BestLane == FromLaneId);

	while (Queue.Entries.Num() > 0)
	{
		const FQueueEntry Entry = Queue.Pop();
		if (Entry.Cost > CostToStart[Entry.LaneId] || Entry.Cost >= BestTotal)
		{
			// Outdated entry, or nothing cheaper can be found any more.
			if (Entry.Cost >= BestTotal)
			{
				break;
			}
			continue;
		}

		const FChronoLane& Lane = Lanes[Entry.LaneId];
		const double Speed = LaneSpeed(Entry.LaneId);

		if (Lane.SegmentId == ToSegmentId)
		{
			const double Total = Entry.Cost + SegmentToLaneDistance(Entry.LaneId, ToSegmentDistance) / Speed;
			if (Total < BestTotal)
			{
				BestTotal = Total;
				BestLane = Entry.LaneId;
				bBestIsDirect = false;
			}
		}

		const double CostToEnd = Entry.Cost + Lane.Length / Speed;
		for (const int32 ConnectorId : Lane.OutConnectors)
		{
			const FChronoLaneConnector& Connector = Connectors[ConnectorId];
			const double Cost = CostToEnd + Connector.Length / TurnSpeed;
			if (Cost < CostToStart[Connector.ToLaneId])
			{
				CostToStart[Connector.ToLaneId] = Cost;
				Previous[Connector.ToLaneId] = Entry.LaneId;
				Queue.Push({ Cost, Connector.ToLaneId });
			}
		}
	}

	if (BestLane == INDEX_NONE)
	{
		return false;
	}

	OutTargetDistance = SegmentToLaneDistance(BestLane, ToSegmentDistance);
	if (bBestIsDirect)
	{
		OutLaneIds.Add(FromLaneId);
		return true;
	}

	// Walk back from the last lane to the first. A route may pass over the
	// first lane again (around the block), so the walk stops on the count,
	// not on meeting the first lane.
	TArray<int32> Reversed;
	int32 LaneId = BestLane;
	for (int32 Guard = 0; Guard <= Lanes.Num() && LaneId != INDEX_NONE; ++Guard)
	{
		Reversed.Add(LaneId);
		const int32 Before = Previous[LaneId];
		if (Before == FromLaneId)
		{
			Reversed.Add(FromLaneId);
			break;
		}
		LaneId = Before;
	}

	for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index)
	{
		OutLaneIds.Add(Reversed[Index]);
	}
	return OutLaneIds.Num() >= 2 && OutLaneIds[0] == FromLaneId;
}
